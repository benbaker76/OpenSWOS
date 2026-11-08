/*
 * match_chat.c -- chatting during an online match (include/match_chat.h). SDL builds.
 *
 * ON THE WIRE. GGPO-X sends a chat line as one UDP datagram, once: a lost one is gone. So each
 * line goes out three times (now, +250 ms, +750 ms) as "\x02" + its number in 4 hex digits + the
 * text, and the receiver shows a number once. A line without the prefix (another program on
 * GGPO-X) is shown as it is, every time it comes. Numbers start again at each match.
 *
 * WHAT IS SHOWN is what MatchChat_Clean leaves: the other player's text is theirs, not ours, so
 * it is cut to MATCHCHAT_TEXT_MAX, control characters go, and only characters the chat font has
 * are kept (others are '?').
 */
#ifdef SWOS_SDL_BUILD
#include "match_chat.h"
#include "chat_style.h"     /* SWOS_CHAT_DYNAMIC_FONT 0: no chat box (it is overlay-only) */
#include "chat_font.h"
#include "gfx.h"
#include "netplay_match.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void InputSDL_ClearKeyState(void);       /* input_sdl.c */
extern int32_t GameLoop_Active(void);           /* game_loop.c: a match is on screen */

/* SWOS_CHAT_DEMO=1: the chat in ANY match, offline too, with a few lines from a pretend other
 * player and what is typed shown, not sent -- to see it (and screenshot it) without a second
 * game. Never in a live match: there the real chat is. */
static int32_t s_demo = -1;
static uint32_t s_demoAt;
static int32_t s_demoNext;

static int32_t demo_on(void)
{
    if (s_demo < 0) {
        const char *v = getenv("SWOS_CHAT_DEMO");
        s_demo = v && v[0] && v[0] != '0';
    }
    return s_demo;
}

/* The chat is up: a live match, or the demo's match. */
static int32_t chat_live(void)
{
    return NetplayMatch_IsLive() || (demo_on() && GameLoop_Active());
}

enum {
    KEEP_LINES     = 64,        /* lines kept */
    SHOW_LINES     = 6,         /* rows of the box (wrapped lines count) */
    SHOW_MS        = 6000,      /* the box stays this long after the newest line */
    FADE_MS        = 1000,      /* then fades out over this */
    REPEATS        = 3,         /* each line sent this many times */
    PENDING_MAX    = 8,
    CHAT_NAME_MAX       = 24,
};
static const uint32_t kRepeatMs[REPEATS] = { 0, 250, 750 };
#define WIRE_MARK '\x02'

typedef enum { WHO_ME, WHO_THEM, WHO_NOTICE } Who;

typedef struct {
    int32_t who;
    char    text[MATCHCHAT_TEXT_MAX + 1];
} Line;

static struct {
    int32_t  active;                    /* a live match is on */
    int32_t  typing;                    /* the match's line is open (L, owned by the match) */
    int32_t  hidden;                    /* F3 */
    int32_t  hinted;                    /* the keys were shown (once a session) */
    char     me[CHAT_NAME_MAX + 1], them[CHAT_NAME_MAX + 1];
    Line     lines[KEEP_LINES];
    int32_t  count;
    uint32_t shownAt;                   /* SDL_GetTicks of the newest line */
    uint16_t nextSeq;                   /* what we send */
    uint32_t seen[8];                   /* their numbers seen, a bit each, over the last 256 */
    uint16_t seenHigh;                  /* the newest of them */
    int32_t  seenAny;
    struct { char wire[MATCHCHAT_TEXT_MAX + 8]; uint32_t at; int32_t sent; } pending[PENDING_MAX];
} C;

/* THE LINE BEING TYPED -- one, shared: the match's (TAB) or the lobby's (its chat field,
 * ChatLine_Open). While it is open every key and typed character is the line's. */
static struct {
    int32_t open;
    char    text[MATCHCHAT_TEXT_MAX + 1];
    int32_t len;
    void  (*onSend)(const char *utf8);
} L;
#define CI L.text
#define CI_LEN L.len

static char s_quick[MATCHCHAT_QUICK][MATCHCHAT_TEXT_MAX + 1];
static int32_t s_quickSet;

static const char *const kDefaultQuick[MATCHCHAT_QUICK] = {
    "Good luck, have fun!", "Nice goal!", "Great save!", "Unlucky!", "Sorry!",
    "Well played!", "Lag...", "One moment please", "Rematch?", "GG",
};

const char *MatchChat_DefaultQuick(int32_t i)
{
    return i >= 0 && i < MATCHCHAT_QUICK ? kDefaultQuick[i] : "";
}

void MatchChat_SetQuick(int32_t i, const char *text)
{
    if (!s_quickSet) {
        for (int32_t k = 0; k < MATCHCHAT_QUICK; k++)
            MatchChat_Clean(kDefaultQuick[k], s_quick[k], sizeof s_quick[k]);
        s_quickSet = 1;
    }
    if (i >= 0 && i < MATCHCHAT_QUICK)
        MatchChat_Clean(text ? text : "", s_quick[i], sizeof s_quick[i]);
}

const char *MatchChat_Quick(int32_t i)
{
    if (!s_quickSet)
        MatchChat_SetQuick(-1, NULL);
    return i >= 0 && i < MATCHCHAT_QUICK ? s_quick[i] : "";
}

static int32_t put_utf8(char *out, uint32_t cp)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int32_t MatchChat_Clean(const char *in, char *out, int32_t outSize)
{
    int32_t n = 0, max = outSize - 1, len;
    if (max > MATCHCHAT_TEXT_MAX)
        max = MATCHCHAT_TEXT_MAX;
    if (!out || outSize <= 0)
        return 0;
    len = in ? (int32_t)strnlen(in, 4 * MATCHCHAT_TEXT_MAX) : 0;
    for (int32_t i = 0; i < len;) {
        int32_t used;
        uint32_t cp = ChatFont_NextChar(in + i, len - i, &used);
        char buf[4];
        i += used;
        if (cp == '\t')
            cp = ' ';
        if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0) || cp == 0xAD)
            continue;                                   /* control characters, soft hyphen */
        if (!ChatFont_Has(cp))
            cp = '?';
        if (cp == ' ' && (n == 0 || out[n - 1] == ' '))
            continue;                                   /* no leading or double spaces */
        const int32_t k = put_utf8(buf, cp);
        if (n + k > max)
            break;
        memcpy(out + n, buf, (size_t)k);
        n += k;
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
    return n;
}

static void add_line(int32_t who, const char *text)
{
    if (C.count == KEEP_LINES) {
        memmove(&C.lines[0], &C.lines[1], (KEEP_LINES - 1) * sizeof C.lines[0]);
        C.count--;
    }
    Line *l = &C.lines[C.count++];
    l->who = who;
    snprintf(l->text, sizeof l->text, "%s", text);
    C.shownAt = SDL_GetTicks();
    if (who != WHO_NOTICE)
        fprintf(stderr, "[chat] %s: %s\n", who == WHO_ME ? "me" : "them", text);
}

static void line_close(void);

void MatchChat_Begin(const char *myName, const char *theirName)
{
    line_close();                       /* (a lobby line still open when the match kicked off) */
    const int32_t hidden = C.hidden, hinted = C.hinted;
    memset(&C, 0, sizeof C);
    C.hidden = hidden;
    C.hinted = hinted;
    MatchChat_Clean(myName && *myName ? myName : "P1", C.me, sizeof C.me);
    MatchChat_Clean(theirName && *theirName ? theirName : "P2", C.them, sizeof C.them);
    C.active = 1;
    if (!C.hinted) {
#ifndef SWOS_MOBILE_BUILD
        add_line(WHO_NOTICE, "TAB: chat    F3: hide the chat    1-0: quick messages");
#endif
        C.hinted = 1;
    }
}

static void line_close(void)
{
    if (!L.open)
        return;
    if (SDL_HasScreenKeyboardSupport())
        SDL_StopTextInput();            /* (the phone's keyboard goes away) */
    L.open = 0;
    L.text[0] = '\0';
    L.len = 0;
    L.onSend = NULL;
    if (C.typing) {
        C.typing = 0;
        C.shownAt = SDL_GetTicks();     /* (the match's box fades from here) */
    }
}

static void line_open(void (*onSend)(const char *utf8))
{
    line_close();
    L.open = 1;
    L.text[0] = '\0';
    L.len = 0;
    L.onSend = onSend;
    InputSDL_ClearKeyState();           /* what the keyboard held, released: it types now */
    SDL_StartTextInput();
}

static void close_input(void)          /* the match's line */
{
    if (C.typing)
        line_close();
}

void MatchChat_End(void)
{
    close_input();
    C.active = 0;
}

int32_t MatchChat_Active(void) { return SWOS_CHAT_DYNAMIC_FONT && C.active && chat_live(); }
int32_t MatchChat_Typing(void) { return C.typing; }

static void send_line(const char *raw)
{
    char text[MATCHCHAT_TEXT_MAX + 1];
    if (!MatchChat_Clean(raw, text, sizeof text))
        return;
    add_line(WHO_ME, text);
    for (int32_t p = 0; p < PENDING_MAX; p++) {
        if (C.pending[p].sent < REPEATS && C.pending[p].wire[0])
            continue;
        snprintf(C.pending[p].wire, sizeof C.pending[p].wire, "%c%04X%s", WIRE_MARK,
                 (unsigned)C.nextSeq, text);
        C.pending[p].at = SDL_GetTicks();
        C.pending[p].sent = 0;
        break;
    }
    C.nextSeq++;
    MatchChat_Tick();
}

void MatchChat_Tick(void)
{
    if (!C.active)
        return;
    const uint32_t now = SDL_GetTicks();
    for (int32_t p = 0; p < PENDING_MAX; p++) {
        if (!C.pending[p].wire[0] || C.pending[p].sent >= REPEATS)
            continue;
        if (now - C.pending[p].at < kRepeatMs[C.pending[p].sent])
            continue;
        if (!demo_on() || NetplayMatch_IsLive())
            NetplayMatch_SendChat(C.pending[p].wire);
        C.pending[p].sent++;
    }
}

/* Their line number: seen before? Remembers it if not. A bit per number for the 256 up to the
 * newest; older than that is dropped (the repeats are all within a second). */
static int32_t seen_before(uint16_t seq)
{
#define SEEN_BIT(q) (C.seen[((q) & 0xFF) >> 5] & (1u << ((q) & 31)))
#define SEEN_SET(q) (C.seen[((q) & 0xFF) >> 5] |= 1u << ((q) & 31))
#define SEEN_CLR(q) (C.seen[((q) & 0xFF) >> 5] &= ~(1u << ((q) & 31)))
    if (!C.seenAny) {
        C.seenAny = 1;
        memset(C.seen, 0, sizeof C.seen);
        C.seenHigh = seq;
        SEEN_SET(seq);
        return 0;
    }
    const int16_t d = (int16_t)(uint16_t)(seq - C.seenHigh);
    if (d > 0) {                                    /* newer: the numbers between are unseen */
        for (int32_t k = 1; k <= d && k <= 256; k++)
            SEEN_CLR((uint16_t)(C.seenHigh + k));
        C.seenHigh = seq;
        SEEN_SET(seq);
        return 0;
    }
    if (-d >= 256 || SEEN_BIT(seq))
        return 1;
    SEEN_SET(seq);
    return 0;
#undef SEEN_BIT
#undef SEEN_SET
#undef SEEN_CLR
}

static int32_t hex4(const char *s, uint16_t *v)
{
    uint32_t x = 0;
    for (int32_t i = 0; i < 4; i++) {
        const char c = s[i];
        x <<= 4;
        if (c >= '0' && c <= '9') x |= (uint32_t)(c - '0');
        else if (c >= 'A' && c <= 'F') x |= (uint32_t)(c - 'A' + 10);
        else return 0;
    }
    *v = (uint16_t)x;
    return 1;
}

void MatchChat_Received(const char *message)
{
    char text[MATCHCHAT_TEXT_MAX + 1];
    uint16_t seq;
    if (!C.active || !message)
        return;
    if (message[0] == WIRE_MARK && hex4(message + 1, &seq)) {
        if (seen_before(seq))
            return;
        message += 5;
    }
    if (MatchChat_Clean(message, text, sizeof text))
        add_line(WHO_THEM, text);
}

/* ---- keys ---- */

static void open_input(void)            /* the match's line */
{
    line_open(send_line);
    C.typing = 1;
}

void ChatLine_Open(void (*onSend)(const char *utf8))
{
    C.typing = 0;
    line_open(onSend);
}

void ChatLine_Close(void) { line_close(); }
int32_t ChatLine_IsOpen(void) { return L.open; }
const char *ChatLine_Text(void) { return L.text; }

void MatchChat_ToggleInput(void)
{
    if (!MatchChat_Active())
        return;
    if (C.typing)
        close_input();
    else
        open_input();
}

static void input_space(void)
{
    if (CI_LEN > 0 && CI_LEN < MATCHCHAT_TEXT_MAX && CI[CI_LEN - 1] != ' ') {
        CI[CI_LEN++] = ' ';
        CI[CI_LEN] = '\0';
    }
}

/* Typed text: a key's character, or several at once (a paste, an input method). Cleaned as a
 * line is, except that its spaces at either end are kept -- the line is still being typed. */
static void input_append(const char *utf8)
{
    char clean[MATCHCHAT_TEXT_MAX + 1];
    const size_t len = strlen(utf8);
    if (len && (utf8[0] == ' ' || utf8[0] == '\t'))
        input_space();
    const int32_t k = MatchChat_Clean(utf8, clean, sizeof clean);
    if (k > 0 && CI_LEN + k <= MATCHCHAT_TEXT_MAX) {
        memcpy(CI + CI_LEN, clean, (size_t)k);
        CI_LEN += k;
        CI[CI_LEN] = '\0';
    }
    if (k > 0 && len > 1 && (utf8[len - 1] == ' ' || utf8[len - 1] == '\t'))
        input_space();
}

static void input_backspace(void)
{
    while (CI_LEN > 0) {
        const uint8_t c = (uint8_t)CI[--CI_LEN];
        if ((c & 0xC0) != 0x80)
            break;                                  /* the first byte of the character */
    }
    CI[CI_LEN] = '\0';
}

int32_t MatchChat_Event(const SDL_Event *e)
{
#if !SWOS_CHAT_DYNAMIC_FONT
    (void)e;
    return 0;                           /* (no box to type into: chat_style.h) */
#endif
    if (C.typing && (!C.active || !chat_live()))
        close_input();                  /* (the match ended under the line) */
    if (L.open) {
        switch (e->type) {
        case SDL_TEXTINPUT:
            input_append(e->text.text);
            return 1;
        case SDL_TEXTEDITING:
            return 1;
        case SDL_KEYUP:
            return 1;
        case SDL_KEYDOWN: {
            const SDL_Keycode k = e->key.keysym.sym;
            if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && (e->key.keysym.mod & KMOD_ALT))
                return 0;                           /* Alt+Enter: fullscreen, as ever */
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                if (e->key.repeat == 0) {
                    void (*send)(const char *) = L.onSend;
                    char text[MATCHCHAT_TEXT_MAX + 1];
                    snprintf(text, sizeof text, "%s", L.text);
                    line_close();
                    if (text[0] && send)
                        send(text);
                }
            } else if (k == SDLK_ESCAPE || k == SDLK_AC_BACK || (k == SDLK_TAB && e->key.repeat == 0)) {
                line_close();
            } else if (k == SDLK_BACKSPACE) {
                input_backspace();
            }
            return 1;
        }
        default:
            return 0;
        }
    }
    if (!C.active || !chat_live())
        return 0;
    if (e->type != SDL_KEYDOWN || e->key.repeat != 0)
        return 0;
    const SDL_Keycode k = e->key.keysym.sym;
    const uint16_t mods = e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT | KMOD_GUI);
    if (k == SDLK_TAB && !mods) {
        open_input();
        return 1;
    }
    if (k == SDLK_F3 && !mods) {
        C.hidden = !C.hidden;
        if (!C.hidden)
            C.shownAt = SDL_GetTicks();
        return 1;
    }
    if (!mods && ((k >= SDLK_1 && k <= SDLK_9) || k == SDLK_0)) {
        const char *q = MatchChat_Quick(k == SDLK_0 ? 9 : (int32_t)(k - SDLK_1));
        if (q[0]) {
            send_line(q);
            return 1;
        }
    }
    return 0;
}

/* ---- drawing ---- */

/* How many bytes of `s` fit in `width` pixels, cut at a space when it can be. */
static int32_t fit(const char *s, int32_t len, float width)
{
    int32_t lastSpace = -1, i = 0;
    while (i < len) {
        int32_t used;
        ChatFont_NextChar(s + i, len - i, &used);
        if (ChatFont_Measure(s, i + used) > width)
            break;
        if (s[i] == ' ')
            lastSpace = i;
        i += used;
    }
    if (i >= len)
        return len;
    if (lastSpace > 0)
        return lastSpace;
    if (i == 0) {                                    /* not even one character: take it anyway */
        int32_t used;
        ChatFont_NextChar(s, len, &used);
        return used;
    }
    return i;
}

typedef struct { const Line *line; int32_t first; int32_t from, to; } Row;

static void demo_tick(void)
{
    static const char *const kLines[] = {
        "gl hf!", "Nice pass... but that keeper \xE2\x80\x93 wow", "Ça va? Ready for the second half?",
        "Unlucky! That was so close", "Ha, the ref didn't see that one \xF0\x9F\x98\x80",
    };
    if (!demo_on() || NetplayMatch_IsLive())
        return;
    if (!GameLoop_Active()) {
        if (C.active)
            MatchChat_End();
        s_demoNext = 0;
        return;
    }
    if (!C.active) {
        MatchChat_Begin("You", "Rival");
        s_demoAt = SDL_GetTicks();
        s_demoNext = 0;
    }
    const uint32_t now = SDL_GetTicks();
    if (s_demoNext < (int32_t)(sizeof kLines / sizeof kLines[0]) && now - s_demoAt >= 2500u) {
        MatchChat_Received(kLines[s_demoNext++]);
        s_demoAt = now;
    }
}

void MatchChat_Draw(void)
{
#if !SWOS_CHAT_DYNAMIC_FONT
    return;
#endif
    demo_tick();
    if (!C.active || !Gfx_InFrame())
        return;
    if (!chat_live()) {
        close_input();
        return;
    }
    const uint32_t now = SDL_GetTicks();
    float alpha = 1.0f;
    if (!C.typing) {
        if (C.hidden || C.count == 0)
            return;
        const uint32_t age = now - C.shownAt;
        if (age >= SHOW_MS + FADE_MS)
            return;
        if (age > SHOW_MS)
            alpha = 1.0f - (float)(age - SHOW_MS) / FADE_MS;
    }

    const GfxSpace space = Gfx_Space();
    const GfxBlend blend = Gfx_Blend();
    Gfx_SetSpace(GFX_SPACE_OUTPUT);
    Gfx_SetBlend(GFX_BLEND_ALPHA);
    int ow, oh;
    Gfx_SpaceSize(&ow, &oh);
    int32_t px = oh / 38;
    if (px < 12) px = 12;
    if (px > 48) px = 48;
    if (ChatFont_Prepare(px) != 0) {
        Gfx_SetBlend(blend);
        Gfx_SetSpace(space);
        return;
    }
    const float lh = (float)ChatFont_LineHeight();
    const float pad = SDL_floorf(px * 0.5f), margin = SDL_floorf(px * 0.6f);
    float boxW = SDL_floorf(ow * 0.42f);
    if (boxW < px * 14.0f) boxW = px * 14.0f;
    if (boxW > ow - 2 * margin) boxW = ow - 2 * margin;
    const float textW = boxW - 2 * pad;

    /* The newest SHOW_LINES rows, wrapped, from the bottom up. */
    Row rows[SHOW_LINES];
    int32_t nrows = 0;
    for (int32_t li = C.count - 1; li >= 0 && nrows < SHOW_LINES; li--) {
        const Line *l = &C.lines[li];
        char who[CHAT_NAME_MAX + 4];
        const float nameW = l->who == WHO_NOTICE ? 0.0f
            : ChatFont_Measure(who, snprintf(who, sizeof who, "%s: ", l->who == WHO_ME ? C.me : C.them));
        Row wrapped[8];
        int32_t nw = 0, at = 0;
        const int32_t len = (int32_t)strlen(l->text);
        while (at < len && nw < 8) {
            const float room = textW - (nw == 0 ? nameW : 0.0f);
            int32_t n = fit(l->text + at, len - at, room);
            wrapped[nw].line = l;
            wrapped[nw].first = nw == 0;
            wrapped[nw].from = at;
            wrapped[nw].to = at + n;
            nw++;
            at += n;
            while (at < len && l->text[at] == ' ')
                at++;
        }
        for (int32_t w = nw - 1; w >= 0 && nrows < SHOW_LINES; w--)
            rows[nrows++] = wrapped[w];
    }
    const int32_t inputRows = C.typing ? 1 : 0;
    const float boxH = (nrows + inputRows) * lh + 2 * pad;
    /* While typing on a phone its keyboard covers the bottom of the screen: the box goes to the top. */
    const float bx = margin, by = C.typing && SDL_HasScreenKeyboardSupport() ? margin : (float)oh - margin - boxH;

    const SDL_FRect box = { bx, by, boxW, boxH };
    Gfx_FillRect(&box, (SDL_Color){ 0, 0, 0, (Uint8)(150 * alpha) });

    const Uint8 a = (Uint8)(255 * alpha);
    const SDL_Color white = { 255, 255, 255, a }, meCol = { 120, 200, 255, a }, themCol = { 255, 214, 90, a },
                    noteCol = { 170, 170, 170, a }, shadow = { 0, 0, 0, (Uint8)(200 * alpha) };
    const float sh = px >= 24 ? 2.0f : 1.0f;
    float y = by + pad;
    for (int32_t r = nrows - 1; r >= 0; r--, y += lh) {
        const Row *row = &rows[r];
        float x = bx + pad;
        if (row->first && row->line->who != WHO_NOTICE) {
            char who[CHAT_NAME_MAX + 4];
            const int32_t n = snprintf(who, sizeof who, "%s: ", row->line->who == WHO_ME ? C.me : C.them);
            ChatFont_Draw(x + sh, y + sh, who, n, shadow);
            ChatFont_Draw(x, y, who, n, row->line->who == WHO_ME ? meCol : themCol);
            x += ChatFont_Measure(who, n);
        }
        const char *t = row->line->text + row->from;
        const int32_t n = row->to - row->from;
        ChatFont_Draw(x + sh, y + sh, t, n, shadow);
        ChatFont_Draw(x, y, t, n, row->line->who == WHO_NOTICE ? noteCol : white);
    }
    if (C.typing) {
        /* the input line: its end, when longer than the box */
        const char *prompt = "> ";
        const float promptW = ChatFont_Measure(prompt, -1);
        const char *t = CI;
        while (*t && ChatFont_Measure(t, -1) > textW - promptW - px * 0.5f) {
            int32_t used;
            ChatFont_NextChar(t, -1, &used);
            t += used;
        }
        float x = bx + pad;
        const SDL_FRect field = { bx + pad * 0.5f, y - 1.0f, boxW - pad, lh + 2.0f };
        Gfx_FillRect(&field, (SDL_Color){ 40, 40, 60, 200 });
        ChatFont_Draw(x, y, prompt, -1, meCol);
        x += promptW;
        ChatFont_Draw(x + sh, y + sh, t, -1, shadow);
        ChatFont_Draw(x, y, t, -1, white);
        if ((now / 500) & 1) {                   /* the caret, blinking */
            const SDL_FRect caret = { x + ChatFont_Measure(t, -1) + 1.0f, y + lh * 0.1f, px >= 24 ? 2.0f : 1.0f, lh * 0.8f };
            Gfx_FillRect(&caret, white);
        }
    }
    Gfx_SetBlend(blend);
    Gfx_SetSpace(space);
}

/* ---- --chat-selftest ---- */

static char s_selftestGot[MATCHCHAT_TEXT_MAX + 1];
static void chat_selftest_cb(const char *t)
{
    snprintf(s_selftestGot, sizeof s_selftestGot, "%s", t);
}

int32_t MatchChat_SelfTest(void)
{
    int32_t fails = 0;
    char out[MATCHCHAT_TEXT_MAX + 1];
    static const struct { const char *in, *want; } k[] = {
        { "hello", "hello" },
        { "  two  spaces  ", "two spaces" },
        { "tab\there", "tab here" },
        { "bell\x07 gone", "bell gone" },
        { "caf\xC3\xA9 \xE2\x82\xAC", "caf\xC3\xA9 \xE2\x82\xAC" },
        { "emoji \xF0\x9F\x98\x80!", "emoji ?!" },
        { "bad \xFF byte", "bad ? byte" },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        MatchChat_Clean(k[i].in, out, sizeof out);
        if (strcmp(out, k[i].want) != 0) {
            fprintf(stderr, "[chat-selftest] FAIL: clean(\"%s\") = \"%s\"\n", k[i].in, out);
            fails++;
        }
    }
    {   /* a long line is cut on a character, never inside one */
        char in[400];
        for (int32_t i = 0; i < 199; i++) {
            in[i * 2] = (char)0xC3;
            in[i * 2 + 1] = (char)0xA9;
        }
        in[398] = '\0';
        const int32_t n = MatchChat_Clean(in, out, sizeof out);
        if (n != MATCHCHAT_TEXT_MAX || (uint8_t)out[n - 1] != 0xA9) {
            fprintf(stderr, "[chat-selftest] FAIL: a long line cut to %d bytes\n", n);
            fails++;
        }
    }
    {   /* repeats: each number once, in any order, and old ones dropped */
        memset(&C, 0, sizeof C);
        C.active = 1;
        static const struct { uint16_t seq; int32_t shown; } r[] = {
            { 0, 1 }, { 0, 0 }, { 1, 1 }, { 3, 1 }, { 2, 1 }, { 3, 0 }, { 2, 0 }, { 1, 0 },
            { 300, 1 }, { 3, 0 }, { 299, 1 }, { 299, 0 },
        };
        for (size_t i = 0; i < sizeof r / sizeof r[0]; i++) {
            char msg[32];
            const int32_t before = C.count;
            snprintf(msg, sizeof msg, "%c%04X line", WIRE_MARK, (unsigned)r[i].seq);
            MatchChat_Received(msg);
            if ((C.count != before) != r[i].shown) {
                fprintf(stderr, "[chat-selftest] FAIL: number %u, step %d: shown %d\n",
                        (unsigned)r[i].seq, (int)i, C.count != before);
                fails++;
            }
        }
        memset(&C, 0, sizeof C);
    }
    {   /* the shared line: typed text (in chunks, spaces kept), backspace over a 2-byte
         * character, ENTER sends it; ESC drops a line without sending */
        struct { int32_t kind; const char *text; SDL_Keycode key; } steps[] = {
            { SDL_TEXTINPUT, "Bonne", 0 }, { SDL_TEXTINPUT, " chance", 0 }, { SDL_TEXTINPUT, " \xC3\xA0", 0 },
            { SDL_KEYDOWN, NULL, SDLK_BACKSPACE }, { SDL_TEXTINPUT, "\xC3\xA9 !", 0 }, { SDL_KEYDOWN, NULL, SDLK_RETURN },
        };
        ChatLine_Open(chat_selftest_cb);
        for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
            SDL_Event e;
            memset(&e, 0, sizeof e);
            e.type = (Uint32)steps[i].kind;
            if (steps[i].text)
                snprintf(e.text.text, sizeof e.text.text, "%s", steps[i].text);
            else
                e.key.keysym.sym = steps[i].key;
            if (!MatchChat_Event(&e)) {
                fprintf(stderr, "[chat-selftest] FAIL: the open line did not take event %d\n", (int)i);
                fails++;
            }
        }
        if (strcmp(s_selftestGot, "Bonne chance \xC3\xA9 !") != 0 || ChatLine_IsOpen()) {
            fprintf(stderr, "[chat-selftest] FAIL: the line sent \"%s\" (open %d)\n", s_selftestGot, ChatLine_IsOpen());
            fails++;
        }
        s_selftestGot[0] = 0;
        ChatLine_Open(chat_selftest_cb);
        {
            SDL_Event e;
            memset(&e, 0, sizeof e);
            e.type = SDL_TEXTINPUT;
            snprintf(e.text.text, sizeof e.text.text, "never");
            MatchChat_Event(&e);
            e.type = SDL_KEYDOWN;
            e.key.keysym.sym = SDLK_ESCAPE;
            MatchChat_Event(&e);
        }
        if (s_selftestGot[0] || ChatLine_IsOpen()) {
            fprintf(stderr, "[chat-selftest] FAIL: ESC sent the line, or left it open\n");
            fails++;
        }
    }
    fails += ChatFont_SelfTest();
    fprintf(stderr, "[chat-selftest] match chat %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
#endif /* SWOS_SDL_BUILD */
