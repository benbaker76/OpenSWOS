/*
 * phone_fetch.c -- the phone's end of the Wi-Fi copy from OpenSWOS Setup (include/phone_copy.h,
 * docs/MOBILE.md "Copying over Wi-Fi"): find the setup, copy its assets folder, take the GAME
 * OPTIONS it sent.
 *
 * Built on every SDL target with sockets (not the web, not the Switch): the phones use it
 * (src/mobile.c), and a desktop build runs it with --phone-fetch=<link|find> to test it against
 * a setup on the same network.
 */
#include "phone_fetch.h"

#if defined(SWOS_SDL_BUILD) && !defined(SWOS_WASM_BUILD) && !defined(SWOS_SWITCH_BUILD)

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "game_options.h"
#include "phone_copy.h"
#include "swos_io.h"
#include "mods.h"           /* Mods_Root: the mods the setup sends go there (docs/MODS.md) */

/* ---- files --------------------------------------------------------------------------- */

static void make_dirs(const char *path)        /* every folder on the way to `path` (a file) */
{
    char tmp[1600];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
}

static void remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                    continue;
                char sub[1280];
                snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
                remove_tree(sub);
            }
            closedir(d);
        }
        rmdir(path);
    } else
        unlink(path);
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)(n > 0 ? n : 0) + 1);
    if (buf) {
        size_t got = fread(buf, 1, (size_t)(n > 0 ? n : 0), f);
        buf[got] = 0;
    }
    fclose(f);
    return buf;
}

/* ---- the network --------------------------------------------------------------------- */

static int connect_to(const char *host, int port, int timeoutMs)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1)
        return -1;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) {
        if (errno != EINPROGRESS) {
            close(s);
            return -1;
        }
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s, &w);
        struct timeval tv = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        int err = 0;
        socklen_t len = sizeof err;
        if (select(s + 1, NULL, &w, NULL, &tv) <= 0 || getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err) {
            close(s);
            return -1;
        }
    }
    fcntl(s, F_SETFL, flags);
    struct timeval rt = { 20, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &rt, sizeof rt);
    return s;
}

/* GET http://host:port<path>. The body to `f`, or (f NULL) into *mem (malloc'd, NUL-ended, at
 * most `cap` bytes). The status, or -1 when the setup could not be reached / the reply broke
 * off; *got = the body's bytes. `tick` is called as it comes (for the progress bar). */
static int http_get(const char *host, int port, const char *path, FILE *f, char **mem, size_t cap,
                    int64_t *got, void (*tick)(int64_t more))
{
    *got = 0;
    int s = connect_to(host, port, 4000);
    if (s < 0)
        return -1;
    char req[1600];
    int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s:%d\r\nUser-Agent: " PROJECT_NAME "\r\n\r\n", path, host, port);
    if (send(s, req, (size_t)n, 0) != n) {
        close(s);
        return -1;
    }
    char buf[32768];
    size_t have = 0;
    char *body = NULL;
    while (have < sizeof buf - 1) {                /* the header */
        ssize_t k = recv(s, buf + have, sizeof buf - 1 - have, 0);
        if (k <= 0)
            break;
        have += (size_t)k;
        buf[have] = 0;
        if ((body = strstr(buf, "\r\n\r\n")) != NULL)
            break;
    }
    int status = -1;
    if (!body || sscanf(buf, "HTTP/%*d.%*d %d", &status) != 1) {
        close(s);
        return -1;
    }
    body += 4;
    int64_t length = -1;
    const char *cl = strstr(buf, "Content-Length:");
    if (cl && cl < body)
        length = strtoll(cl + 15, NULL, 10);
    size_t memLen = 0;
    if (!f && mem) {
        *mem = malloc(cap + 1);
        if (!*mem) {
            close(s);
            return -1;
        }
    }
    int ok = 1;
    const char *chunk = body;
    size_t chunkLen = have - (size_t)(body - buf);
    for (;;) {
        if (chunkLen) {
            if (f) {
                if (fwrite(chunk, 1, chunkLen, f) != chunkLen) { ok = 0; break; }
            } else if (mem) {
                if (memLen + chunkLen > cap) { ok = 0; break; }
                memcpy(*mem + memLen, chunk, chunkLen);
                memLen += chunkLen;
            }
            *got += (int64_t)chunkLen;
            if (tick)
                tick((int64_t)chunkLen);
        }
        if (length >= 0 && *got >= length)
            break;
        ssize_t k = recv(s, buf, sizeof buf, 0);
        if (k <= 0) {
            if (length >= 0) ok = 0;               /* cut off */
            break;
        }
        chunk = buf;
        chunkLen = (size_t)k;
    }
    close(s);
    if (mem && *mem)
        (*mem)[memLen] = 0;
    if (!ok || (length >= 0 && *got != length)) {
        if (mem) { free(*mem); *mem = NULL; }
        return -1;
    }
    return status;
}

/* ---- the link, the question ---------------------------------------------------------- */

int PhoneFetch_ParseLink(const char *link, PhoneFetchSetup *out)
{
    memset(out, 0, sizeof *out);
    /* <scheme>://get?h=..&k=..  or  https://<site>/get/#h=..&k=.. (the QR code's: an App Link) */
    static const char site[] = "https://" PROJECT_SITE "/get";
    const char *p = NULL;
    const char *s = strstr(link, "://get?");
    if (s && (size_t)(s - link) == strlen(PROJECT_URL_SCHEME) && !strncmp(link, PROJECT_URL_SCHEME, (size_t)(s - link)))
        p = s + 7;
    else if (!strncmp(link, site, sizeof site - 1) && (p = strchr(link, '#')) != NULL)
        p++;
    if (!p)
        return 0;
    const char *h = strstr(p, "h="), *k = strstr(p, "k=");
    if (!h || !k)
        return 0;
    h += 2;
    k += 2;
    size_t i = 0;
    while (*h && *h != ':' && *h != '&' && i < sizeof out->host - 1)
        out->host[i++] = *h++;
    out->host[i] = 0;
    if (*h != ':')
        return 0;
    out->port = atoi(h + 1);
    i = 0;
    while (*k && *k != '&' && i < sizeof out->key - 1)
        out->key[i++] = *k++;
    out->key[i] = 0;
    for (const char *c = out->key; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')))
            return 0;
    struct in_addr a;
    snprintf(out->name, sizeof out->name, "%s", out->host);
    return out->port > 0 && out->port < 65536 && out->key[0] && inet_pton(AF_INET, out->host, &a) == 1;
}

int PhoneFetch_Find(PhoneFetchSetup *out, int waitMs)
{
    memset(out, 0, sizeof *out);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return 0;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    struct sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(PHONE_COPY_PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    int found = 0;
    for (int t = 0; t < waitMs && !found; t += 500) {
        if (t % 1000 == 0)
            sendto(s, PHONE_COPY_ASK, strlen(PHONE_COPY_ASK), 0, (struct sockaddr *)&to, sizeof to);
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        struct timeval tv = { 0, 500000 };
        if (select(s + 1, &rd, NULL, NULL, &tv) <= 0)
            continue;
        char buf[512];
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(s, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &fl);
        if (n <= 0)
            continue;
        buf[n] = 0;
        const size_t hl = strlen(PHONE_COPY_HERE);
        if (strncmp(buf, PHONE_COPY_HERE, hl) != 0 || buf[hl] != ' ')
            continue;
        char key[64] = "";
        int port = 0, used = 0;
        if (sscanf(buf + hl + 1, "%d %63s %n", &port, key, &used) < 2)
            continue;
        inet_ntop(AF_INET, &from.sin_addr, out->host, sizeof out->host);
        out->port = port;
        snprintf(out->key, sizeof out->key, "%s", key);
        snprintf(out->name, sizeof out->name, "%s", used ? buf + hl + 1 + used : out->host);
        found = out->port > 0 && out->key[0];
    }
    close(s);
    return found;
}

/* ---- SWOS.ini: the GAME OPTIONS keys only ------------------------------------------- */

/* [section] key's value in `text`, into `out`; 0 when it has none. */
static int ini_value(const char *text, const char *section, const char *key, char *out, size_t cap)
{
    char sec[64] = "";
    const size_t kl = strlen(key);
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t'))
            len--;
        const char *l = line;
        while (len && (*l == ' ' || *l == '\t')) { l++; len--; }
        if (len > 1 && *l == '[') {
            const char *close = memchr(l, ']', len);
            size_t sl = close ? (size_t)(close - l - 1) : 0;
            if (sl >= sizeof sec) sl = sizeof sec - 1;
            memcpy(sec, l + 1, sl);
            sec[sl] = 0;
        } else if (!strcmp(sec, section) && len > kl && !strncmp(l, key, kl)) {
            const char *v = l + kl;
            while (v < l + len && (*v == ' ' || *v == '\t')) v++;
            if (v < l + len && *v == '=') {
                v++;
                while (v < l + len && (*v == ' ' || *v == '\t')) v++;
                size_t vl = (size_t)(l + len - v);
                const char *cut = v;
                while (cut < v + vl && *cut != ';' && *cut != ' ' && *cut != '\t') cut++;
                vl = (size_t)(cut - v);
                if (vl >= cap) vl = cap - 1;
                memcpy(out, v, vl);
                out[vl] = 0;
                return 1;
            }
        }
        line = end ? end + 1 : NULL;
    }
    return 0;
}

/* `text` with [section] key=value set: its line replaced, else added at the end of its section,
 * else the section added. A new malloc'd string; `text` is freed. */
static char *ini_set(char *text, const char *section, const char *key, const char *value)
{
    const size_t tl = strlen(text), kl = strlen(key);
    char *out = malloc(tl + strlen(section) + kl + strlen(value) + 16);
    if (!out)
        return text;
    char sec[64] = "";
    int inSec = 0, done = 0;
    size_t o = 0, insertAt = (size_t)-1;
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) + 1 : strlen(line);
        const char *l = line;
        while (l < line + len && (*l == ' ' || *l == '\t')) l++;
        if (*l == '[') {
            if (inSec && !done && insertAt == (size_t)-1) insertAt = o;
            const char *close = strchr(l, ']');
            size_t sl = close ? (size_t)(close - l - 1) : 0;
            if (sl >= sizeof sec) sl = sizeof sec - 1;
            memcpy(sec, l + 1, sl);
            sec[sl] = 0;
            inSec = !strcmp(sec, section);
        } else if (inSec && !done && !strncmp(l, key, kl)) {
            const char *v = l + kl;
            while (*v == ' ' || *v == '\t') v++;
            if (*v == '=') {
                o += (size_t)sprintf(out + o, "%s=%s\n", key, value);
                done = 1;
                line += len;
                continue;
            }
        }
        memcpy(out + o, line, len);
        o += len;
        line += len;
    }
    out[o] = 0;
    if (!done) {
        if (inSec && insertAt == (size_t)-1)
            insertAt = o;
        if (insertAt != (size_t)-1) {             /* the end of its section (before blank lines) */
            while (insertAt > 1 && out[insertAt - 1] == '\n' && (out[insertAt - 2] == '\n' || out[insertAt - 2] == '\r'))
                insertAt--;
            if (insertAt && out[insertAt - 1] != '\n') {   /* a file without its last newline */
                memmove(out + insertAt + 1, out + insertAt, o - insertAt + 1);
                out[insertAt++] = '\n';
                o++;
            }
            char add[256];
            int al = snprintf(add, sizeof add, "%s=%s\n", key, value);
            memmove(out + insertAt + (size_t)al, out + insertAt, o - insertAt + 1);
            memcpy(out + insertAt, add, (size_t)al);
        } else
            sprintf(out + o, "%s[%s]\n%s=%s\n", o && out[o - 1] != '\n' ? "\n\n" : o ? "\n" : "", section, key, value);
    }
    free(text);
    return out;
}

/* The phone's GAME OPTIONS from the setup into the player's SWOS.ini: only the keys the table
 * names for a phone, and only values it knows. How many were set. */
static int take_options(const char *sent, const char *iniPath)
{
    char *text = read_text(iniPath);
    if (!text && !(text = calloc(1, 1)))
        return 0;
    int n = 0;
    for (int i = 0; i < GAME_OPTION_COUNT(kGameOptions); i++) {
        const GameOption *o = &kGameOptions[i];
        char v[64];
        if (!(o->where & GAME_OPTION_PHONE) || !ini_value(sent, o->section, o->key, v, sizeof v))
            continue;
        for (int k = 0; k < o->count; k++)
            if (!strcmp(v, o->values[k].value)) {
                text = ini_set(text, o->section, o->key, v);
                fprintf(stderr, "[phone copy] SWOS.ini: [%s] %s=%s\n", o->section, o->key, v);
                n++;
                break;
            }
    }
    /* The mods that are on, on the computer (docs/MODS.md): [mods] as the setup sent it. */
    static const char *const kModKeys[] = { "enabled", "disabled" };
    for (int i = 0; i < 2; i++) {
        char v[256];
        if (ini_value(sent, "mods", kModKeys[i], v, sizeof v)) {
            text = ini_set(text, "mods", kModKeys[i], v);
            fprintf(stderr, "[phone copy] SWOS.ini: [mods] %s=%s\n", kModKeys[i], v);
            n++;
        }
    }
    if (n) {
        make_dirs(iniPath);
        FILE *f = fopen(iniPath, "wb");
        if (f) {
            fwrite(text, 1, strlen(text), f);
            fclose(f);
        } else
            n = 0;
    }
    free(text);
    return n;
}

/* ---- the copy ------------------------------------------------------------------------ */

static void (*s_progress)(int permille);
static int64_t s_done, s_total;
static int s_lastPermille = -1;

static void tick(int64_t more)
{
    s_done += more;
    int pm = s_total ? (int)(s_done * 1000 / s_total) : 0;
    if (pm > 1000) pm = 1000;
    if (s_progress && pm != s_lastPermille) {
        s_lastPermille = pm;
        s_progress(pm);
    }
}

/* A path from the list: relative, no "..", no backslashes, nothing odd. */
static int path_ok(const char *p)
{
    if (!*p || *p == '/' || strstr(p, "..") || strchr(p, '\\') || strchr(p, ':'))
        return 0;
    for (; *p; p++)
        if ((unsigned char)*p < 32)
            return 0;
    return 1;
}

static void encode_path(const char *p, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *p && o + 4 < cap; p++) {
        const unsigned char c = (unsigned char)*p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("/-_.~", c))
            out[o++] = (char)c;
        else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}

/* A world package the phone had and the setup did not send (data_json/worlds/<n>: the player
 * copied it over by hand, or the setup's computer never had it) is kept, not lost with the old
 * folder. */
static void keep_worlds(const char *oldDir, const char *newDir)
{
    char from[1600], to[1600];
    snprintf(from, sizeof from, "%s/data_json/worlds", oldDir);
    DIR *d = opendir(from);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        struct stat st;
        snprintf(to, sizeof to, "%s/data_json/worlds/%s", newDir, e->d_name);
        if (stat(to, &st) == 0)
            continue;                              /* the setup sent its own */
        char src[1600];
        snprintf(src, sizeof src, "%s/%s", from, e->d_name);
        make_dirs(to);
        if (rename(src, to) == 0)
            fprintf(stderr, "[phone copy] kept the phone's world %s\n", e->d_name);
    }
    closedir(d);
}

void PhoneFetch_Tidy(const char *assetsDir)
{
    char p[1280];
    snprintf(p, sizeof p, "%s.part", assetsDir);
    remove_tree(p);
    snprintf(p, sizeof p, "%s.old", assetsDir);
    struct stat st;
    if (stat(assetsDir, &st) != 0 && stat(p, &st) == 0)
        rename(p, assetsDir);                      /* a copy cut off between its two renames */
    else
        remove_tree(p);
}

int PhoneFetch_Copy(const PhoneFetchSetup *from, const char *assetsDir, const char *iniPath,
                    void (*progress)(int permille), char *err, int errCap)
{
    char path[1600], part[1280], old[1280];
    char *list = NULL;
    int64_t got;
    snprintf(err, (size_t)errCap, "%s", "");
    s_progress = progress;
    s_done = s_total = 0;
    s_lastPermille = -1;
    fprintf(stderr, "[phone copy] from %s (%s:%d) into %s\n", from->name, from->host, from->port, assetsDir);

    snprintf(path, sizeof path, "/%s/list", from->key);
    int st = http_get(from->host, from->port, path, NULL, &list, 4u << 20, &got, NULL);
    if (st != 200 || !list || strncmp(list, PHONE_COPY_LIST "\n", strlen(PHONE_COPY_LIST) + 1) != 0) {
        snprintf(err, (size_t)errCap, st < 0 ? "Could not reach " PROJECT_SETUP_NAME " on %s. Is it still open on its phone screen, "
                 "and this phone on the same Wi-Fi?" : PROJECT_SETUP_NAME " on %s did not send the game's data. "
                 "Scan its QR code again.", from->name);
        free(list);
        return 0;
    }
    /* The list: "<size> <path>" lines. Counted first, for the bar. */
    int files = 0;
    for (char *l = strchr(list, '\n') + 1; l && *l; l = strchr(l, '\n'), l = l ? l + 1 : NULL) {
        long long sz = 0;
        if (sscanf(l, "%lld ", &sz) == 1) { s_total += sz; files++; }
    }
    if (!files) {
        snprintf(err, (size_t)errCap, PROJECT_SETUP_NAME " on %s has no game data yet: install it there first.", from->name);
        free(list);
        return 0;
    }
    snprintf(part, sizeof part, "%s.part", assetsDir);
    snprintf(old, sizeof old, "%s.old", assetsDir);
    remove_tree(part);
    mkdir(part, 0755);
    /* The mods the setup sends (listed as mods/<slug>/...): into the player's mods folder, not
     * the assets -- staged beside it, put in place once everything came. */
    char modsDir[1100], modsPart[1200], modsOld[1200];
    if (Mods_Root()[0])
        snprintf(modsDir, sizeof modsDir, "%s", Mods_Root());
    else
        snprintf(modsDir, sizeof modsDir, "%s/mods", swos_user_dir());
    snprintf(modsPart, sizeof modsPart, "%s.part", modsDir);
    snprintf(modsOld, sizeof modsOld, "%s.old", modsDir);
    remove_tree(modsPart);
    int ok = 1, copied = 0, mods = 0;
    for (char *l = strchr(list, '\n') + 1; ok && l && *l;) {
        char *end = strchr(l, '\n');
        if (end) *end = 0;
        long long sz = -1;
        char *sp = strchr(l, ' ');
        if (sp && sscanf(l, "%lld", &sz) == 1 && sz >= 0) {
            const char *rel = sp + 1;
            if (!path_ok(rel)) {
                snprintf(err, (size_t)errCap, "The setup sent a file this phone will not take (%s).", rel);
                ok = 0;
                break;
            }
            char enc[1024], dest[1600];
            encode_path(rel, enc, sizeof enc);
            snprintf(path, sizeof path, "/%s/f/%s", from->key, enc);
            if (strncmp(rel, "mods/", 5) == 0) {
                snprintf(dest, sizeof dest, "%s/%s", modsPart, rel + 5);
                mods++;
            } else
                snprintf(dest, sizeof dest, "%s/%s", part, rel);
            make_dirs(dest);
            FILE *f = fopen(dest, "wb");
            if (!f) {
                snprintf(err, (size_t)errCap, "Could not write %s on this phone (%s).", rel, strerror(errno));
                ok = 0;
                break;
            }
            st = http_get(from->host, from->port, path, f, NULL, 0, &got, tick);
            if (fclose(f) != 0) st = -1;
            if (st != 200 || got != sz) {
                snprintf(err, (size_t)errCap, "The copy of %s broke off. Keep " PROJECT_SETUP_NAME " open and try again.", rel);
                ok = 0;
                break;
            }
            copied++;
        }
        l = end ? end + 1 : NULL;
    }
    free(list);
    if (!ok) {
        remove_tree(part);
        remove_tree(modsPart);
        fprintf(stderr, "[phone copy] failed: %s\n", err);
        return 0;
    }
    /* Every file came: in the assets folder's place. */
    remove_tree(old);
    struct stat sb;
    if (stat(assetsDir, &sb) == 0 && rename(assetsDir, old) != 0) {
        snprintf(err, (size_t)errCap, "Could not replace the game's data on this phone (%s).", strerror(errno));
        remove_tree(part);
        return 0;
    }
    if (rename(part, assetsDir) != 0) {
        snprintf(err, (size_t)errCap, "Could not put the game's data in place (%s).", strerror(errno));
        rename(old, assetsDir);
        return 0;
    }
    keep_worlds(old, assetsDir);
    remove_tree(old);
    if (mods) {             /* (a setup that sent no mods leaves the phone's as they are) */
        remove_tree(modsOld);
        if (stat(modsDir, &sb) == 0 && rename(modsDir, modsOld) != 0)
            fprintf(stderr, "[phone copy] could not replace the mods (%s)\n", strerror(errno));
        else if (rename(modsPart, modsDir) != 0)
            rename(modsOld, modsDir);
        remove_tree(modsOld);
        fprintf(stderr, "[phone copy] %d files of mods into %s\n", mods, modsDir);
    }
    fprintf(stderr, "[phone copy] %d files, %lld bytes\n", copied, (long long)s_total);

    /* The options chosen for the phone, if any. */
    char *ini = NULL;
    snprintf(path, sizeof path, "/%s/ini", from->key);
    if (iniPath && http_get(from->host, from->port, path, NULL, &ini, 256u << 10, &got, NULL) == 200 && ini)
        take_options(ini, iniPath);
    free(ini);
    return 1;
}

static void cli_progress(int pm)
{
    if (pm % 100 == 0)
        fprintf(stderr, "[phone copy] %d%%\n", pm / 10);
}

int PhoneFetch_Cli(const char *arg, const char *iniPath)
{
    PhoneFetchSetup from;
    if (!strcmp(arg, "find") ? !PhoneFetch_Find(&from, 3000) : !PhoneFetch_ParseLink(arg, &from)) {
        fprintf(stderr, "[phone copy] %s\n", strcmp(arg, "find") ? "not a link" : "no setup answered");
        return 1;
    }
    char err[512];
    const int ok = PhoneFetch_Copy(&from, swos_assets_dir(), iniPath, cli_progress, err, sizeof err);
    fprintf(stderr, "[phone copy] %s\n", ok ? "done" : err);
    return ok ? 0 : 1;
}

#else  /* no sockets here */

int PhoneFetch_Cli(const char *arg, const char *iniPath) { (void)arg; (void)iniPath; return 1; }
void PhoneFetch_Tidy(const char *assetsDir) { (void)assetsDir; }

int PhoneFetch_ParseLink(const char *link, PhoneFetchSetup *out) { (void)link; (void)out; return 0; }
int PhoneFetch_Find(PhoneFetchSetup *out, int waitMs) { (void)out; (void)waitMs; return 0; }
int PhoneFetch_Copy(const PhoneFetchSetup *from, const char *assetsDir, const char *iniPath,
                    void (*progress)(int permille), char *err, int errCap)
{
    (void)from; (void)assetsDir; (void)iniPath; (void)progress; (void)err; (void)errCap;
    return 0;
}

#endif
