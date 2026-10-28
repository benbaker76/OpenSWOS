/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */

/* ============================================================
 * SDL build: POSIX/stdio implementation of the same RobNorthenLoader
 * dispatch surface the Amiga code below provides. The rest of the C
 * code calls `RobNorthenLoader_C` and a few stubs (`KBHook`,
 * `FixSnoopBug`, etc.) — those keep their signatures so linking is
 * unchanged.
 * ============================================================ */
#ifdef SWOS_SDL_BUILD

#include "resload.h"
#include "jag_graphics.h"
#include "display.h"
#include "rnc_magic.h"
#include "swos_io.h"      /* swos_save_dir() — shared save-dir base */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>          /* _mkdir on MinGW/MSVC */
#define swos_mkdir(path)  _mkdir(path)
#else
#define swos_mkdir(path)  mkdir((path), 0755)
#endif

extern void RncDecompressV1(uint8_t *src, uint8_t *dst);
extern void RncDecompressV2(uint8_t *src, uint8_t *dst);

/* ---- Globals shared with the rest of the C code -------------------- */
uint32_t size;
uint32_t _rnl_d1_return;
uint8_t _keyexit = 0x46;

/* ---- Path resolver ------------------------------------------------- */
static const char *known_save_exts[] = {
    ".DIY", ".PRE", ".SEA", ".CAR", ".TAC", ".HIL", ".EDT",
    ".RPL", /* PC whole-match replay (not native to the Amiga) */
    ".SRP", /* OpenSWOS whole-match replay (src/replay_v2.c) */
    ".TEAM", /* a custom team for online play (src/custom_team.c) */
    NULL
};

/* By the whole extension, from the name's last dot: they are not all three letters. */
static int has_save_ext(const char *name)
{
    const char *tail = strrchr(name, '.');
    if (!tail) return 0;
    for (const char **e = known_save_exts; *e; ++e)
        if (strcasecmp(*e, tail) == 0) return 1;
    return 0;
}

static void resolve_path(const uint8_t *amiga_name, char *out, size_t out_sz)
{
    const char *src = (const char *)amiga_name;
    const char *colon = strchr(src, ':');
    if (colon) src = colon + 1;

    /* A bare save-extension name (FOO.HIL) maps into the save/ dir. A name that
     * already carries a directory (e.g. the arcade attract reels under
     * grafs_arcade/attract_mode/) is taken relative to bin/ exactly as written. */
    if (has_save_ext(src) && !strchr(src, '/')) {
        snprintf(out, out_sz, "%s/%s", swos_save_dir(), src);
    } else {
        /* Relative to the data root. Identity everywhere the game chdir()s to
         * its own directory; anchored at rom:/ on the Switch (swos_io.h). */
        char anchored[1024];
        snprintf(out, out_sz, "%s",
                 swos_data_path(src, anchored, (uint32_t)sizeof anchored));
    }
}

/* ---- Low-level file ops ------------------------------------------- */
/* Case-insensitive open of a single literal path (host filesystems may be
 * case-sensitive; the Amiga names are upper/mixed case). */
static FILE *try_open_ci(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f) return f;

    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) return NULL;
    *slash = 0;
    const char *base = slash + 1;

    DIR *d = opendir(dir);
    if (!d) return NULL;
    struct dirent *de;
    char alt[1024];
    while ((de = readdir(d)) != NULL) {
        if (strcasecmp(de->d_name, base) == 0) {
            snprintf(alt, sizeof(alt), "%s/%s", dir, de->d_name);
            closedir(d);
            return fopen(alt, mode);
        }
    }
    closedir(d);
    return NULL;
}

static FILE *open_case_insensitive(const char *path, const char *mode)
{
#ifdef AGA
    /* AGA asset redirect + fallback. Graphics banks split across two folders:
     *   grafs_aga/ — AGA-specific mixed-depth assets (4bpp sprite sheets,
     *                8bpp backgrounds/pitches) produced by DOS2AGA.
     *   grafs/     — shared 4bpp assets (the extended SWOS_Font charset) and
     *                OCS screens not yet reconverted (disk2/socpic/tactics/winscrn).
     * A grafs load (from either a "grafs/" literal or a GRAFS-macro'd
     * "grafs_aga/" path) prefers grafs_aga/, then falls back to grafs/ by
     * basename. So a *partial* grafs_aga set + the shared grafs assets both
     * resolve, and dropping an AGA version into grafs_aga later just takes over.
     * grafs_arcade/ etc. don't match the "grafs/" prefix (next char is '_').
     *
     * `path` arrives ALREADY ANCHORED (resolve_path -> swos_data_path): at
     * rom:/ on the Switch, and at the assets folder on the desktops when the
     * file is there. Testing the prefix at offset 0 matched nothing on the
     * Switch, the whole redirect no-opped, and the console loaded the 4bpp OCS
     * set for the entire game. Strip the root before testing, and re-anchor
     * each candidate through swos_data_path so each one is looked for in the
     * assets folder and beside the binary alike. */
    const char *rel = swos_data_relative(path);

    const char *base = NULL;
    if (strncmp(rel, "grafs_aga/", 10) == 0)  base = rel + 10;
    else if (strncmp(rel, "grafs/", 6) == 0)  base = rel + 6;
    if (base) {
        char cand[1024], anchored[1024];
        FILE *f;
        /* The Jaguar sprite tier goes first when it is on: it holds the gameplay
         * banks only (jag_graphics.c), so everything else falls through to the
         * AGA set exactly as before. */
        if (JagGfx_Enabled()) {
            snprintf(cand, sizeof(cand), "grafs_jag/%s", base);
            f = try_open_ci(swos_data_path(cand, anchored, sizeof(anchored)), mode);
            if (f) return f;
        }
        snprintf(cand, sizeof(cand), "grafs_aga/%s", base);
        f = try_open_ci(swos_data_path(cand, anchored, sizeof(anchored)), mode);
        if (f) return f;
        snprintf(cand, sizeof(cand), "grafs/%s", base);
        return try_open_ci(swos_data_path(cand, anchored, sizeof(anchored)), mode);
    }
#endif
    return try_open_ci(path, mode);
}

static uint32_t fs_get_size(const char *path)
{
    FILE *f = open_case_insensitive(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n > 0 ? (uint32_t)n : 0;
}

static uint32_t fs_load(const char *path, uint8_t *dest)
{
    FILE *f = open_case_insensitive(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0; }
    size_t got = fread(dest, 1, (size_t)n, f);
    fclose(f);
    return (uint32_t)got;
}

static uint32_t fs_save(const char *path, const uint8_t *src, uint32_t n)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    char *slash = strrchr(buf, '/');
    if (slash) {
        *slash = 0;
        swos_mkdir(buf);
    }

    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t put = fwrite(src, 1, n, f);
    fclose(f);
    return (uint32_t)put;
}

static uint32_t fs_delete(const char *path)
{
    return (remove(path) == 0) ? 1 : 0;
}

static uint32_t fs_listdir(const char *dirpath, uint8_t *out, uint32_t out_sz)
{
    DIR *d = opendir(dirpath);
    if (!d) {
        fprintf(stderr, "[fs_listdir] opendir(%s) failed: errno=%d (%s)\n",
                dirpath, errno, strerror(errno));
        return 0;
    }

    struct dirent *de;
    uint8_t *p = out;
    uint8_t *end = out + out_sz - 2;
    uint32_t n = 0;

    while ((errno = 0, de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        size_t len = strlen(de->d_name);
        if (p + len + 1 >= end) break;
        memcpy(p, de->d_name, len + 1);
        p += len + 1;
        ++n;
    }
    *p = 0;
    closedir(d);
    return n;
}

/* ---- RobNorthenLoader command handlers ---------------------------- */
static uint8_t s_listdir_buf[2048];

static uint32_t rob_load(uint8_t *name, uint8_t *dest)
{
    char path[512];
    resolve_path(name, path, sizeof(path));
    uint32_t got = fs_load(path, dest);
    size = got;
    _rnl_d1_return = got;
    return got ? 0 : 1;
}


#ifdef SWOS_WASM_BUILD
#include <emscripten.h>
/* Flush the /save mount to IndexedDB.
 *
 * wasm/pre.js mounts IDBFS at /save and calls FS.syncfs(true, ...) on startup,
 * so saves are READ back across page loads. But IDBFS is a write-back mirror:
 * everything the game writes stays in memory until someone calls
 * FS.syncfs(false, ...). Nothing did — only wasm/shell.html's import button —
 * so a career saved in the browser survived until the tab was reloaded and then
 * vanished, while an imported one persisted. docs/WASM.md 2d has carried this as
 * the outstanding follow-up, and wasm/pre.js has said so in its header since it
 * was written.
 *
 * Asynchronous on purpose: the game does not wait, the browser flushes while
 * play continues, and the next load picks it up through the existing
 * syncfs(true). Saves are user-initiated and rare, so one flush per write needs
 * no debouncing. */
static void wasm_persist_saves(void)
{
    EM_ASM({
        if (typeof FS !== 'undefined' && FS.syncfs) {
            FS.syncfs(false, function (err) {
                if (err) console.warn('SWOS: IDBFS save failed:', err);
            });
        }
    });
}
#else
#define wasm_persist_saves() ((void)0)
#endif

static uint32_t rob_save(uint8_t *name, uint8_t *src, uint32_t fileSize)
{
    char path[512];
    resolve_path(name, path, sizeof(path));
    uint32_t put = fs_save(path, src, fileSize);
    size = put;
    _rnl_d1_return = put;
    if (put) {
        wasm_persist_saves();
        /* The Switch's twin of that: a write to save:/ is not durable until it
         * is committed, and SDL will not do it for you. THIS is the game's save
         * path -- careers, tactics, edited teams, options all arrive here -- so
         * without it a Switch player's season reads back fine all session and is
         * gone at the next launch (docs/SWITCH.md, swos_io.h). */
        swos_save_commit();
    }
    return put ? 0 : 1;
}

static uint32_t rob_delete(uint8_t *name)
{
    char path[512];
    resolve_path(name, path, sizeof(path));
    fs_delete(path);
    wasm_persist_saves();     /* a deletion has to stick too */
    swos_save_commit();       /* ... on both of the targets that need telling */
    _rnl_d1_return = 0;
    return 0;
}

static uint32_t rob_get_file_size(uint8_t *name)
{
    char path[512];
    resolve_path(name, path, sizeof(path));
    uint32_t n = fs_get_size(path);
    size = n;
    _rnl_d1_return = n;
    return n;
}

static uint32_t rob_list_directory(uint8_t *name, uint8_t *callerBuf)
{
    (void)name;
    uint32_t count = fs_listdir(swos_save_dir(), s_listdir_buf, sizeof(s_listdir_buf));
    if (count == 0) {
        _rnl_d1_return = 0;
        size = 0;
        return 0;
    }

    uint8_t *src = s_listdir_buf;
    uint8_t *dst = callerBuf;
    for (uint32_t i = 0; i < count; ++i) {
        size_t len = strlen((char *)src);
        memset(dst, 0, 0x20);
        dst[0] = 0;
        if (len > 0x1B) len = 0x1B;
        dst[1] = (uint8_t)len;
        memcpy(dst + 2, src, len);
        dst[2 + len] = 0;
        src += strlen((char *)src) + 1;
        dst += 0x20;
    }

    *(uint16_t *)&size = (uint16_t)count;
    _rnl_d1_return = count;
    return 0;
}

uint32_t RobNorthenLoader_C(uint32_t cmd, uint32_t arg1, uint8_t *name, uint8_t *buf)
{
    switch (cmd) {
    case 0: return rob_load(name, buf);
    case 1: return rob_save(name, buf, arg1);
    case 2: return rob_delete(name);
    case 3: return rob_list_directory(name, buf);
    case 4: _rnl_d1_return = 0; return 0;
    case 5: return rob_get_file_size(name);
    case 6: return 0;
    default: return 0;
    }
}

/* ---- Hardware-side helpers (no-ops on SDL) ------------------------ */
void CloseDosLibrary(void) {}
void FixSnoopBug(void)     {}
void FixMemoryRoutine(void) {}
uint8_t KBHook(void) { return 0; }

#else  /* !SWOS_SDL_BUILD — Amiga path below */

/*
 * resload.c — C port of Resload.s
 *
 * AmigaOS library calls use inline asm with register-specified variables.
 * Data (S_DosBase, _S_VBR, etc.) stays in Resload.s (Phase 1).
 */
#include "resload.h"
#include "display.h"

/* ---- Hardware register addresses (resload-specific, not in display.h) ---- */
#define ADKCON_ADDR    0x00DFF09Eu
#define ADKCONR_ADDR   0x00DFF010u
#define INTENAR_ADDR   0x00DFF01Cu
#define COP2LC_ADDR    0x00DFF084u
#define BPLCON0_OFF    0x100u
#define CIAA_SDR_ADDR  0x00BFEC01u

/* ---- Interrupt vector offsets ---- */
#define LEVEL_1_VEC    0x64u
#define LEVEL_2_VEC    0x68u
#define LEVEL_3_VEC    0x6Cu
#define LEVEL_4_VEC    0x70u
#define LEVEL_5_VEC    0x74u

/* ---- AmigaOS constants ---- */
#define MODE_OLDFILE   1005u
#define MODE_NEWFILE   1006u
#define MEMF_PUBLIC    1u
#define MEMF_FAST      4u
#define DOS_FIB        2u
#define ACCESS_READ    (-2)

/* ---- GfxBase struct offsets ---- */
#define GFX_ACTIVIEW   0x22u
#define GFX_COPINIT    0x26u
#define GFX_LOFLIST    0x32u

/* ---- Exec struct offsets ---- */
#define EXEC_ATTNFLAGS 296u

/* ---- External asm data (Resload.s) ---- */
extern uint32_t S_DosBase;
extern uint32_t S_GfxBase;
extern uint16_t S_FromWB;
extern uint32_t S_WBMsg;
extern uint32_t _S_VBR;
extern uint32_t S_SavedView;
extern uint16_t S_SavedDmaCon;
extern uint16_t S_SavedIntEna;
extern uint16_t S_SavedAdkCon;
extern uint32_t S_SavedSP;
extern uint32_t S_SavedInt1;
extern uint32_t S_SavedInt2;
extern uint32_t S_SavedInt3;
extern uint32_t S_SavedInt4;
extern uint32_t S_SavedInt5;
extern uint32_t size;       /* file size global used by rob_* functions */
extern uint16_t _quitToDos;
extern uint8_t _keyexit;   /* DEL key code = 0x46 */

/* ---- External asm functions ---- */
extern void _S_GetVBR(void);          /* asm: MOVEC VBR,D0; RTE */
extern void Int2KeyboardHandler(void); /* game keyboard interrupt handler */

/* ---- External C functions ---- */
extern void RncDecompressV1(uint8_t *src, uint8_t *dst);
extern void RncDecompressV2(uint8_t *src, uint8_t *dst);

/* ---- Local data ---- */
static uint8_t data_path_buf[256];
static const uint8_t progdir_prefix[] = "PROGDIR:";
/* save_path_buf: "save/" prefix (5 bytes) + filename (up to 14 bytes).
 * fix_filename() copies the filename starting at offset 5 and returns
 * the buffer start, producing "save/FILENAME.EXT".
 * Original asm had these as two adjacent labels but in the wrong order
 * (_save_filename before _save_filepath), which was a latent bug.  */
static uint8_t save_path_buf[5 + 14] = { 's','a','v','e','/' };
static const uint8_t savedir[] = "save";
#ifdef AGA
/* AGA: "grafs/FILE" loads get redirected to "grafs_aga/FILE" (mixed-depth
 * AGA assets — 8bpp backgrounds/pitches, 4bpp sprite sheets, extended charset).
 * Symmetric with the SDL resload redirect (open_case_insensitive). 10-byte
 * "grafs_aga/" prefix + up to 20 for the filename. */
static uint8_t grafs_aga_buf[10 + 20] = { 'g','r','a','f','s','_','a','g','a','/' };
#endif
static uint8_t listdir_buf[2048 + 4];

static const uint32_t ext_table[] = {
    0x2E444959u, /* ".DIY" */
    0x2E505245u, /* ".PRE" */
    0x2E534541u, /* ".SEA" */
    0x2E434152u, /* ".CAR" */
    0x2E544143u, /* ".TAC" */
    0x2E48494Cu, /* ".HIL" */
    0x2E454454u, /* ".EDT" */
    0x2E52504Cu, /* ".RPL" (PC whole-match replay) */
    0x2E43484Cu, /* ".CHL" (a saved position, src/challenge.c) */
    0x2E505247u, /* ".PRG" (persistent challenge collection progress) */
    0            /* terminator */
};

/* RobNorthenLoader d1 return value (read by asm bridge) */
uint32_t _rnl_d1_return;

/* ================================================================
 * AmigaOS inline asm call helpers
 *
 * IMPORTANT: AmigaOS library calls trash d0, d1, a0, a1.
 * Any input register that is also trashed MUST use "+r" (read-write)
 * constraint, NOT "r" (read-only). This tells GCC the register is
 * modified, preventing it from keeping other values in that register
 * across the asm call. A trashed register must NOT also appear in
 * the clobber list (GCC forbids overlap between operands and clobbers).
 * ================================================================ */

static inline void *exec_base(void)
{
    return *(void **)4;
}

/* --- Exec library calls --- */

/* OldOpenLibrary: a1=name, d0=version → d0=base */
static inline void *os_OldOpenLibrary(const char *name, uint32_t version)
{
    register void *_a6 __asm("a6") = exec_base();
    register const char *_a1 __asm("a1") = name;
    register uint32_t _d0 __asm("d0") = version;
    __asm volatile (
        "jsr -408(%%a6)"
        : "+r"(_d0), "+r"(_a1)
        : "r"(_a6)
        : "d1", "a0", "cc", "memory"
    );
    return (void *)_d0;
}

/* FindTask: a1=name (NULL=self) → d0=task */
static inline void *os_FindTask(void *name)
{
    register void *_a6 __asm("a6") = exec_base();
    register void *_a1 __asm("a1") = name;
    register void *_d0 __asm("d0");
    __asm volatile (
        "jsr -294(%%a6)"
        : "=r"(_d0), "+r"(_a1)
        : "r"(_a6)
        : "d1", "a0", "cc", "memory"
    );
    return _d0;
}

/* WaitPort: a0=port */
static inline void os_WaitPort(void *port)
{
    register void *_a6 __asm("a6") = exec_base();
    register void *_a0 __asm("a0") = port;
    __asm volatile (
        "jsr -384(%%a6)"
        : "+r"(_a0)
        : "r"(_a6)
        : "d0", "d1", "a1", "cc", "memory"
    );
}

/* GetMsg: a0=port → d0=msg */
static inline void *os_GetMsg(void *port)
{
    register void *_a6 __asm("a6") = exec_base();
    register void *_a0 __asm("a0") = port;
    register void *_d0 __asm("d0");
    __asm volatile (
        "jsr -372(%%a6)"
        : "=r"(_d0), "+r"(_a0)
        : "r"(_a6)
        : "d1", "a1", "cc", "memory"
    );
    return _d0;
}

/* Supervisor: a5=func → d0=result (a5 is callee-saved, not trashed) */
static inline uint32_t os_Supervisor(void (*func)(void))
{
    register void *_a6 __asm("a6") = exec_base();
    register void (*_a5)(void) __asm("a5") = func;
    register uint32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -30(%%a6)"
        : "=r"(_d0)
        : "r"(_a6), "r"(_a5)
        : "d1", "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* ReplyMsg: a1=msg */
static inline void os_ReplyMsg(void *msg)
{
    register void *_a6 __asm("a6") = exec_base();
    register void *_a1 __asm("a1") = msg;
    __asm volatile (
        "jsr -378(%%a6)"
        : "+r"(_a1)
        : "r"(_a6)
        : "d0", "d1", "a0", "cc", "memory"
    );
}

/* CloseLibrary: a1=lib */
static inline void os_CloseLibrary(void *lib)
{
    register void *_a6 __asm("a6") = exec_base();
    register void *_a1 __asm("a1") = lib;
    __asm volatile (
        "jsr -414(%%a6)"
        : "+r"(_a1)
        : "r"(_a6)
        : "d0", "d1", "a0", "cc", "memory"
    );
}

/* AllocMem: d0=size, d1=flags → d0=addr */
static inline void *os_AllocMem(uint32_t sz, uint32_t flags)
{
    register void *_a6 __asm("a6") = exec_base();
    register uint32_t _d0 __asm("d0") = sz;
    register uint32_t _d1 __asm("d1") = flags;
    __asm volatile (
        "jsr -198(%%a6)"
        : "+r"(_d0), "+r"(_d1)
        : "r"(_a6)
        : "a0", "a1", "cc", "memory"
    );
    return (void *)_d0;
}

/* FreeMem: a1=addr, d0=size */
static inline void os_FreeMem(void *addr, uint32_t sz)
{
    register void *_a6 __asm("a6") = exec_base();
    register void *_a1 __asm("a1") = addr;
    register uint32_t _d0 __asm("d0") = sz;
    __asm volatile (
        "jsr -210(%%a6)"
        : "+r"(_d0), "+r"(_a1)
        : "r"(_a6)
        : "d1", "a0", "cc", "memory"
    );
}

/* --- DOS library calls (a6=dosBase) --- */

/* Open: d1=name, d2=mode → d0=handle */
static inline uint32_t os_DosOpen(void *dosBase, const uint8_t *name, uint32_t mode)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = (uint32_t)name;
    register uint32_t _d2 __asm("d2") = mode;
    register uint32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -30(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6), "r"(_d2)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* Close: d1=handle */
static inline void os_DosClose(void *dosBase, uint32_t handle)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = handle;
    __asm volatile (
        "jsr -36(%%a6)"
        : "+r"(_d1)
        : "r"(_a6)
        : "d0", "a0", "a1", "cc", "memory"
    );
}

/* Read: d1=handle, d2=buf, d3=len → d0=bytesRead */
static inline int32_t os_DosRead(void *dosBase, uint32_t handle, void *buf, uint32_t len)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = handle;
    register uint32_t _d2 __asm("d2") = (uint32_t)buf;
    register uint32_t _d3 __asm("d3") = len;
    register int32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -42(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6), "r"(_d2), "r"(_d3)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* Write: d1=handle, d2=buf, d3=len → d0=bytesWritten */
static inline int32_t os_DosWrite(void *dosBase, uint32_t handle, const void *buf, uint32_t len)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = handle;
    register uint32_t _d2 __asm("d2") = (uint32_t)buf;
    register uint32_t _d3 __asm("d3") = len;
    register int32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -48(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6), "r"(_d2), "r"(_d3)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* Seek: d1=handle, d2=offset, d3=mode → d0=oldPos */
static inline int32_t os_DosSeek(void *dosBase, uint32_t handle, int32_t offset, int32_t mode)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = handle;
    register int32_t _d2 __asm("d2") = offset;
    register int32_t _d3 __asm("d3") = mode;
    register int32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -66(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6), "r"(_d2), "r"(_d3)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* DeleteFile: d1=name → d0=success */
static inline uint32_t os_DosDeleteFile(void *dosBase, const uint8_t *name)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = (uint32_t)name;
    register uint32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -72(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* Lock: d1=name, d2=mode → d0=lock */
static inline uint32_t os_DosLock(void *dosBase, const uint8_t *name, int32_t mode)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = (uint32_t)name;
    register int32_t _d2 __asm("d2") = mode;
    register uint32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -84(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6), "r"(_d2)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* UnLock: d1=lock */
static inline void os_DosUnLock(void *dosBase, uint32_t lock)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = lock;
    __asm volatile (
        "jsr -90(%%a6)"
        : "+r"(_d1)
        : "r"(_a6)
        : "d0", "a0", "a1", "cc", "memory"
    );
}

/* Examine: d1=lock, d2=fib → d0=success */
static inline uint32_t os_DosExamine(void *dosBase, uint32_t lock, void *fib)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = lock;
    register uint32_t _d2 __asm("d2") = (uint32_t)fib;
    register uint32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -102(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6), "r"(_d2)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* ExNext: d1=lock, d2=fib → d0=success */
static inline uint32_t os_DosExNext(void *dosBase, uint32_t lock, void *fib)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = lock;
    register uint32_t _d2 __asm("d2") = (uint32_t)fib;
    register uint32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -108(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6), "r"(_d2)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* IoErr: → d0=error */
static inline uint32_t os_DosIoErr(void *dosBase)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d0 __asm("d0");
    __asm volatile (
        "jsr -132(%%a6)"
        : "=r"(_d0)
        : "r"(_a6)
        : "d1", "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* AllocDosObject: d1=type, d2=tags → d0=obj */
static inline void *os_DosAllocDosObject(void *dosBase, uint32_t type, void *tags)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = type;
    register uint32_t _d2 __asm("d2") = (uint32_t)tags;
    register void *_d0 __asm("d0");
    __asm volatile (
        "jsr -228(%%a6)"
        : "=r"(_d0), "+r"(_d1)
        : "r"(_a6), "r"(_d2)
        : "a0", "a1", "cc", "memory"
    );
    return _d0;
}

/* FreeDosObject: d1=type, d2=obj */
static inline void os_DosFreeDosObject(void *dosBase, uint32_t type, void *obj)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = type;
    register uint32_t _d2 __asm("d2") = (uint32_t)obj;
    __asm volatile (
        "jsr -234(%%a6)"
        : "+r"(_d1)
        : "r"(_a6), "r"(_d2)
        : "d0", "a0", "a1", "cc", "memory"
    );
}

/* PutStr: d1=string */
static inline void os_DosPutStr(void *dosBase, const char *str)
{
    register void *_a6 __asm("a6") = dosBase;
    register uint32_t _d1 __asm("d1") = (uint32_t)str;
    __asm volatile (
        "jsr -948(%%a6)"
        : "+r"(_d1)
        : "r"(_a6)
        : "d0", "a0", "a1", "cc", "memory"
    );
}

/* --- Graphics library calls (a6=gfxBase) --- */

/* LoadView: a1=view */
static inline void os_GfxLoadView(void *gfxBase, void *view)
{
    register void *_a6 __asm("a6") = gfxBase;
    register void *_a1 __asm("a1") = view;
    __asm volatile (
        "jsr -222(%%a6)"
        : "+r"(_a1)
        : "r"(_a6)
        : "d0", "d1", "a0", "cc", "memory"
    );
}

/* WaitTOF */
static inline void os_GfxWaitTOF(void *gfxBase)
{
    register void *_a6 __asm("a6") = gfxBase;
    __asm volatile (
        "jsr -270(%%a6)"
        :
        : "r"(_a6)
        : "d0", "d1", "a0", "a1", "cc", "memory"
    );
}


/* ================================================================
 * Internal helpers
 * ================================================================ */

static void print_file_error(const uint8_t *filename)
{
    void *dosBase = (void *)S_DosBase;
    if (!dosBase) return;
    os_DosPutStr(dosBase, "Error - Could not open file: ");
    os_DosPutStr(dosBase, (const char *)filename);
    os_DosPutStr(dosBase, "\n");
}

/* resload_MakeDataPath: prepend "PROGDIR:" if name has no colon */
static uint8_t *make_data_path(uint8_t *name)
{
    uint8_t *p;
    const uint8_t *src;
    uint8_t *dst;

    if (*name == 0) return name;

    /* Scan for colon */
    for (p = name; *p; p++) {
        if (*p == ':') return name; /* absolute/device path */
    }

    /* Build "PROGDIR:" + name into data_path_buf */
    dst = data_path_buf;
    for (src = progdir_prefix; *src; )
        *dst++ = *src++;
    for (src = name; ; ) {
        *dst = *src++;
        if (*dst == 0) break;
        dst++;
    }
    return data_path_buf;
}

/* Swap interrupt vector 2 to OS (restore saved) */
static void swap_int2_to_os(void)
{
    volatile uint32_t *vec = (volatile uint32_t *)(_S_VBR + LEVEL_2_VEC);
    *vec = S_SavedInt2;
}

/* Swap interrupt vector 2 back to game handler */
static void swap_int2_to_game(void)
{
    volatile uint32_t *vec = (volatile uint32_t *)(_S_VBR + LEVEL_2_VEC);
    *vec = (uint32_t)Int2KeyboardHandler;
}

/* Wait for blitter idle */
static inline __attribute__((always_inline)) void wait_blitter(void)
{
    (void)(_custom->dmaconr & (1u << 14));
    while (BLIT_BUSY())
        ;
}

/* Disable the game's VBlank ISR, wait for any in-progress ISR to complete,
 * then allow safe file I/O. Re-enable after I/O is done.
 *
 * Sequence:
 * 1. Disable VERTB (bit 5) in INTENA — prevents NEW VBlank interrupts
 * 2. Wait for blitter idle — if ISR was mid-blit, this waits for it to finish
 * 3. Brief delay — let ISR complete any non-blitter work
 * 4. Final blitter wait — in case ISR had a second blit pending
 *
 * After this, no ISR can fire and any previous ISR has completed. */
static void disable_vblank_and_wait(void)
{
    /* Disable VBlank interrupt (clear bit 5 — writing without SET bit clears) */
    _custom->intena = (1u << 5);

    /* Wait for blitter to finish — ISR may have started a blit */
    wait_blitter();

    /* Brief delay to let any in-flight ISR finish non-blitter work */
    (void)(*(volatile uint16_t *)0xDFF006u);
    (void)(*(volatile uint16_t *)0xDFF006u);

    /* Final blitter wait */
    wait_blitter();
}

static void enable_vblank(void)
{
    /* Re-enable VBlank interrupt (SET bit + bit 5) */
    _custom->intena = 0x8000u | (1u << 5);
}


/* ================================================================
 * SafeStart
 * ================================================================ */
__attribute__((used, externally_visible))
void SafeStart(void)
{
    /* Open graphics.library (optional) */
    S_GfxBase = (uint32_t)os_OldOpenLibrary("graphics.library", 0);

    S_FromWB = 0;

    /* Find our process */
    void *task = os_FindTask((void *)0);
    if (task) {
        /* Check CLI vs Workbench: pr_CLI at offset 172 */
        uint32_t cli = *(uint32_t *)((uint8_t *)task + 172);
        if (cli == 0) {
            /* Workbench launch */
            S_FromWB = 1;
            void *msgPort = (void *)((uint8_t *)task + 92); /* pr_MsgPort */
            os_WaitPort(msgPort);
            S_WBMsg = (uint32_t)os_GetMsg(msgPort);
        }
    }

    _S_VBR = 0;

    /* Check for 68010+ (AttnFlags bit 0) */
    uint8_t *execPtr = (uint8_t *)exec_base();
    if (execPtr[EXEC_ATTNFLAGS + 1] & 1) {
        /* 68010+ : get VBR via Supervisor mode */
        _S_VBR = os_Supervisor(_S_GetVBR);
    }

    /* Save original interrupt vectors */
    volatile uint32_t *vbr = (volatile uint32_t *)_S_VBR;
    S_SavedInt1 = vbr[LEVEL_1_VEC / 4];
    S_SavedInt2 = vbr[LEVEL_2_VEC / 4];
    S_SavedInt3 = vbr[LEVEL_3_VEC / 4];
    S_SavedInt4 = vbr[LEVEL_4_VEC / 4];
    S_SavedInt5 = vbr[LEVEL_5_VEC / 4];
}


/* ================================================================
 * DetachOSDisplay
 * ================================================================ */
__attribute__((used, externally_visible))
void DetachOSDisplay(void)
{
    if (S_FromWB == 0) return;

    void *gfxBase = (void *)S_GfxBase;
    if (!gfxBase) return;

    /* Save current View from gfxbase */
    S_SavedView = *(uint32_t *)((uint8_t *)gfxBase + GFX_ACTIVIEW);

    /* LoadView(NULL) — detach WB display */
    os_GfxLoadView(gfxBase, (void *)0);
    os_GfxWaitTOF(gfxBase);
    os_GfxWaitTOF(gfxBase);

    /* Save DMACONR/INTENAR/ADKCONR with SET bit for later restore */
    S_SavedDmaCon = _custom->dmaconr | 0x8000u;
    S_SavedIntEna = _custom->intenar | 0x8000u;
    S_SavedAdkCon = _custom->adkconr | 0x8000u;

    /* Wait for blitter idle */
    wait_blitter();

    /* Disable all DMA and interrupts */
    _custom->dmacon = 0x01FFu;
    _custom->intena = 0x3FFFu;
}


/* ================================================================
 * LoadDosLibrary
 * ================================================================ */
__attribute__((used, externally_visible))
void LoadDosLibrary(void)
{
    if (S_DosBase != 0) return; /* already loaded */
    S_DosBase = (uint32_t)os_OldOpenLibrary("dos.library", 39);
}


/* ================================================================
 * CloseDosLibrary
 * ================================================================ */
__attribute__((used, externally_visible))
void CloseDosLibrary(void)
{
    if (S_DosBase == 0) return;
    os_CloseLibrary((void *)S_DosBase);
}


/* ================================================================
 * AllocMemAny — C-callable wrapper for Exec AllocMem(MEMF_PUBLIC)
 * ================================================================ */
__attribute__((used, externally_visible))
uint8_t *AllocMemAny(uint32_t sz)
{
    return (uint8_t *)os_AllocMem(sz, MEMF_PUBLIC);
}

/* ================================================================
 * FreeMemAny — C-callable wrapper for Exec FreeMem. Pair with
 * AllocMemAny: the caller must remember the size that was passed
 * to AllocMem (Exec doesn't track block sizes). swos_io.c stores
 * the size in a 4-byte header at the front of each block.
 * ================================================================ */
__attribute__((used, externally_visible))
void FreeMemAny(void *addr, uint32_t sz)
{
    os_FreeMem(addr, sz);
}


/* ================================================================
 * ExitToDos — restore hardware, close libs, reply WB msg
 * NOTE: SP restore is handled by startShim in asm.
 * ================================================================ */
__attribute__((used, externally_visible))
void ExitToDos(void)
{
    /* Restore interrupt vectors */
    volatile uint32_t *vbr = (volatile uint32_t *)_S_VBR;
    vbr[LEVEL_1_VEC / 4] = S_SavedInt1;
    vbr[LEVEL_2_VEC / 4] = S_SavedInt2;
    vbr[LEVEL_3_VEC / 4] = S_SavedInt3;
    vbr[LEVEL_4_VEC / 4] = S_SavedInt4;
    vbr[LEVEL_5_VEC / 4] = S_SavedInt5;

    /* Wait for blitter idle */
    wait_blitter();

    /* Restore copper list from gfx base */
    void *gfxBase = (void *)S_GfxBase;
    _custom->cop1lc = *(uint32_t *)((uint8_t *)gfxBase + GFX_COPINIT);
    _custom->cop2lc = *(uint32_t *)((uint8_t *)gfxBase + GFX_LOFLIST);
    _custom->copjmp1 = 0;

    /* Restore DMA / interrupts / ADKCON */
    _custom->dmacon = S_SavedDmaCon;
    _custom->intena = S_SavedIntEna;
    _custom->adkcon = S_SavedAdkCon;

    /* Restore original view via graphics.library */
    void *savedView = (void *)S_SavedView;
    if (savedView) {
        os_GfxLoadView(gfxBase, savedView);
        gfxBase = (void *)S_GfxBase; /* reload after call */
        os_GfxWaitTOF(gfxBase);
        os_GfxWaitTOF(gfxBase);
    }

    if (S_FromWB == 0) return;

    /* Reply WB message */
    if (S_WBMsg != 0) {
        os_ReplyMsg((void *)S_WBMsg);
    }

    /* Close graphics.library */
    if (S_GfxBase != 0) {
        os_CloseLibrary((void *)S_GfxBase);
    }
}


/* ================================================================
 * resload file I/O — internal C implementations
 * ================================================================ */

/* GetFileSize: open, seek to end, seek back, close */
static uint32_t resload_getfilesize(uint8_t *name)
{
    name = make_data_path(name);
    void *dosBase = (void *)S_DosBase;

    uint32_t handle = os_DosOpen(dosBase, name, MODE_OLDFILE);
    if (handle == 0) return 0;

    /* Seek to end */
    os_DosSeek(dosBase, handle, 0, 1);  /* OFFSET_END = 1 */
    /* Seek back to beginning — returns old pos = file size */
    int32_t fileSize = os_DosSeek(dosBase, handle, 0, -1); /* OFFSET_BEGINNING = -1 */

    os_DosClose(dosBase, handle);
    return (uint32_t)fileSize;
}

/* LoadFile: open, read, close */
static uint32_t resload_loadfile(uint8_t *name, uint8_t *dest, uint32_t *outError)
{
    name = make_data_path(name);
    void *dosBase = (void *)S_DosBase;
    uint32_t handle = os_DosOpen(dosBase, name, MODE_OLDFILE);
    if (handle == 0) {
        _custom->color[0] = 0x0F00u; /* red flash */
        print_file_error(name);
        if (outError) *outError = 1;
        return 0;
    }

    int32_t bytesRead = os_DosRead(dosBase, handle, dest, 0x7FFFFFFFu);
    os_DosClose(dosBase, handle);

    if (bytesRead <= 0) {
        _custom->color[0] = 0x0F00u;
        print_file_error(name);
        if (outError) *outError = 1;
        return 0;
    }

    if (outError) *outError = 0;
    return (uint32_t)bytesRead;
}

/* LoadFileDEC: load to temp buffer, RNC decompress or copy raw, free temp */
static uint32_t resload_loadfiledec(uint8_t *name, uint8_t *dest, uint32_t *outError)
{
    name = make_data_path(name);

    /* Get file size */
    uint32_t fileSize = resload_getfilesize(name);
    if (fileSize == 0) {
        if (outError) *outError = 1;
        return 0;
    }

    /* Allocate temporary FastMem buffer */
    uint8_t *tempBuf = (uint8_t *)os_AllocMem(fileSize, MEMF_FAST);
    if (!tempBuf) {
        if (outError) *outError = 1;
        return 0;
    }

    /* Open and read file into temp buffer */
    void *dosBase = (void *)S_DosBase;
    uint32_t handle = os_DosOpen(dosBase, name, MODE_OLDFILE);
    if (handle == 0) {
        os_FreeMem(tempBuf, fileSize);
        if (outError) *outError = 1;
        return 0;
    }

    int32_t bytesRead = os_DosRead(dosBase, handle, tempBuf, fileSize);
    os_DosClose(dosBase, handle);

    if ((uint32_t)bytesRead != fileSize) {
        os_FreeMem(tempBuf, fileSize);
        if (outError) *outError = 1;
        return 0;
    }

    /* Check for RNC magic and decompress. The RNC signature is the 4 bytes
     * 'R','N','C',version — compare per-byte so detection is endian-independent
     * with no magic-constant / header dependency (matches the byte-based check
     * in file_io.c). The unpacked size at +4 is a BE field, read host-native
     * here: correct on the BE Amiga, and this resload path is unused on the LE
     * SDL build (file_io.c is the live RNC path there). */
    int isRnc = (tempBuf[0] == 'R' && tempBuf[1] == 'N' && tempBuf[2] == 'C');
    uint32_t decompSize;

    if (isRnc && tempBuf[3] == 0x02) {
        decompSize = *(uint32_t *)(tempBuf + 4);
        RncDecompressV2(tempBuf, dest);
    } else if (isRnc && tempBuf[3] == 0x01) {
        decompSize = *(uint32_t *)(tempBuf + 4);
        RncDecompressV1(tempBuf, dest);
    } else {
        /* Raw copy */
        uint32_t i;
        for (i = 0; i < fileSize; i++)
            dest[i] = tempBuf[i];
        decompSize = fileSize;
    }

    os_FreeMem(tempBuf, fileSize);
    if (outError) *outError = 0;
    return decompSize;
}

/* Deletefile: dos_DeleteFile */
static uint32_t resload_deletefile(uint8_t *name, uint32_t *outError)
{
    name = make_data_path(name);
    void *dosBase = (void *)S_DosBase;

    uint32_t success = os_DosDeleteFile(dosBase, name);
    if (success == 0) {
        if (outError) *outError = 1;
        return 0;
    }
    if (outError) *outError = 0;
    return success;
}

/* SaveFile: create/truncate, write, close */
static uint32_t resload_savefile(uint32_t fileSize, uint8_t *name, uint8_t *src, uint32_t *outError)
{
    name = make_data_path(name);
    void *dosBase = (void *)S_DosBase;

    uint32_t handle = os_DosOpen(dosBase, name, MODE_NEWFILE);
    if (handle == 0) {
        if (outError) *outError = 1;
        return 0;
    }

    int32_t bytesWritten = os_DosWrite(dosBase, handle, src, fileSize);
    if ((uint32_t)bytesWritten != fileSize) {
        os_DosClose(dosBase, handle);
        if (outError) *outError = 1;
        return 0;
    }

    os_DosClose(dosBase, handle);
    if (outError) *outError = 0;
    return 1; /* success */
}

/* ListFiles: lock dir, examine/exnext loop, copy names to buffer */
static uint32_t resload_listfiles(uint32_t buffersize, uint8_t *dirname, uint8_t *buffer, uint32_t *outError)
{
    dirname = make_data_path(dirname);
    void *dosBase = (void *)S_DosBase;
    uint32_t count = 0;
    uint32_t dirLock = 0;
    uint8_t *fib = (uint8_t *)0;

    if ((int32_t)buffersize <= 0) {
        if (outError) *outError = 1;
        return 0;
    }

    /* Lock directory */
    dirLock = os_DosLock(dosBase, dirname, ACCESS_READ);
    if (dirLock == 0) {
        if (outError) *outError = os_DosIoErr(dosBase);
        return 0;
    }

    /* Allocate FileInfoBlock */
    fib = (uint8_t *)os_DosAllocDosObject(dosBase, DOS_FIB, (void *)0);
    if (!fib) {
        os_DosUnLock(dosBase, dirLock);
        if (outError) *outError = 1;
        return 0;
    }

    /* Prime with Examine */
    if (os_DosExamine(dosBase, dirLock, fib) == 0) {
        os_DosFreeDosObject(dosBase, DOS_FIB, fib);
        os_DosUnLock(dosBase, dirLock);
        if (outError) *outError = 1;
        return 0;
    }

    /* Iterate with ExNext */
    uint32_t remaining = buffersize;
    uint8_t *writePtr = buffer;
    count = 0;

    while (os_DosExNext(dosBase, dirLock, fib) != 0) {
        /* Skip directories: fib_DirEntryType at offset 4, >0 = directory */
        int32_t entryType = *(int32_t *)(fib + 4);
        if (entryType > 0) continue;

        /* Get filename from FIB (at offset 8) */
        volatile uint8_t *fname = (volatile uint8_t *)(fib + 8);
        uint32_t nameLen = 0;
        while (fname[nameLen]) nameLen++;

        /* Need nameLen + 1 bytes (name + null) */
        uint32_t needed = nameLen + 1;
        if (needed > remaining) break; /* no more room */

        /* Copy name + null */
        uint32_t i;
        for (i = 0; i < needed; i++)
            *writePtr++ = (uint8_t)fname[i];

        remaining -= needed;
        count++;
    }

    os_DosFreeDosObject(dosBase, DOS_FIB, fib);
    os_DosUnLock(dosBase, dirLock);
    if (outError) *outError = 0;
    return count;
}


/* Debug dump: write raw buffer to file */
__attribute__((used, externally_visible))
void DebugDumpToFile(uint8_t *filename, uint8_t *data, uint32_t size)
{
    disable_vblank_and_wait();
    swap_int2_to_os();
    resload_savefile(size, filename, data, (uint32_t *)0);
    swap_int2_to_game();
    enable_vblank();
}

/* ================================================================
 * File I/O wrappers (swap interrupt vectors around DOS calls)
 * ================================================================ */

static void file_GetFileSize(uint8_t *name)
{
    disable_vblank_and_wait();
    swap_int2_to_os();
    uint32_t sz = resload_getfilesize(name);
    swap_int2_to_game();
    enable_vblank();
    /* d0 = sz, d1 = error — not used by callers, size stored by caller */
    size = sz;
}

static void file_LoadFile(uint8_t *name, uint8_t *dest)
{
    disable_vblank_and_wait();
    swap_int2_to_os();
    uint32_t len = resload_loadfile(name, dest, (uint32_t *)0);
    swap_int2_to_game();
    enable_vblank();
    size = len;
}

static void file_SaveFile(uint32_t fileSize, uint8_t *name, uint8_t *src)
{
    disable_vblank_and_wait();
    swap_int2_to_os();
    resload_savefile(fileSize, name, src, (uint32_t *)0);
    swap_int2_to_game();
    enable_vblank();
    size = fileSize;
}

static void file_Deletefile(uint8_t *name)
{
    disable_vblank_and_wait();
    swap_int2_to_os();
    resload_deletefile(name, (uint32_t *)0);
    swap_int2_to_game();
    enable_vblank();
}

static void file_ListFiles(uint32_t buffersize, uint8_t *dirname, uint8_t *buffer)
{
    disable_vblank_and_wait();
    swap_int2_to_os();
    uint32_t count = resload_listfiles(buffersize, dirname, buffer, (uint32_t *)0);
    swap_int2_to_game();
    enable_vblank();
    /* Store count as word in high 16 bits of size (big-endian) — matching asm behavior */
    *(uint16_t *)&size = (uint16_t)count;
}


/* ================================================================
 * FixFilename — detect save file extensions, prepend "save/" path
 * ================================================================ */
static uint8_t *fix_filename(uint8_t *name)
{
    uint8_t *a0 = name;

    /* Skip "DF0:" prefix if present */
    if (a0[3] == ':')
        a0 += 4;

#ifdef AGA
    /* Redirect "grafs/FILE" -> "grafs_aga/FILE" so the mixed-depth AGA assets
     * (8bpp menubg/backgrounds/pitches, 8bpp soccer_s, extended charset) are
     * loaded instead of the OCS 4bpp grafs/ versions. The && short-circuits
     * safely at the NUL of any shorter name. grafs_aga/ holds every asset the
     * game loads (charset included), so no fallback to grafs/ is needed here. */
    if (a0[0]=='g' && a0[1]=='r' && a0[2]=='a' && a0[3]=='f' &&
        a0[4]=='s' && a0[5]=='/') {
        uint8_t *src = a0 + 6;             /* after "grafs/"     */
        uint8_t *dst = grafs_aga_buf + 10; /* after "grafs_aga/" */
        uint8_t *cap = grafs_aga_buf + sizeof(grafs_aga_buf) - 1;
        while (dst < cap && (*dst = *src) != 0) { dst++; src++; }
        *dst = 0;
        return grafs_aga_buf;
    }
#endif

    /* Find end of string */
    uint8_t *end = a0;
    while (*end) end++;

    /* Need at least 4 chars for extension check */
    if (end - a0 < 4)
        return a0;

    /* Read last 4 chars as big-endian uint32_t */
    uint32_t ext = ((uint32_t)end[-4] << 24) | ((uint32_t)end[-3] << 16) |
                   ((uint32_t)end[-2] << 8) | end[-1];

    /* Check against extension table */
    const uint32_t *ep;
    for (ep = ext_table; *ep; ep++) {
        if (*ep == ext) {
            /* Save extension matched — copy filename after "save/" prefix */
            uint8_t *src = a0;
            uint8_t *dst = save_path_buf + 5; /* after "save/" */
            while ((*dst++ = *src++) != 0)
                ;
            return save_path_buf;
        }
    }

    return a0; /* no match, return (possibly DF0:-stripped) name */
}


/* ================================================================
 * rob_* command handlers (called by RobNorthenLoader dispatch)
 * ================================================================ */

static uint32_t rob_load(uint8_t *name, uint8_t *dest)
{
    name = fix_filename(name);
    file_LoadFile(name, dest);
    _rnl_d1_return = size;
    return 0;
}

static uint32_t rob_save(uint8_t *name, uint8_t *src, uint32_t fileSize)
{
    name = fix_filename(name);
    file_SaveFile(fileSize, name, src);
    _rnl_d1_return = size;
    return 0;
}

static uint32_t rob_delete(uint8_t *name)
{
    name = fix_filename(name);
    file_Deletefile(name);
    _rnl_d1_return = 0;
    return 0;
}

static uint32_t rob_format_disk(void)
{
    _rnl_d1_return = 0;
    return 0;
}

static uint32_t rob_get_file_size(uint8_t *name)
{
    name = fix_filename(name);
    file_GetFileSize(name);
    /* d0 = file size (non-zero = success), d1 = file size */
    _rnl_d1_return = size;
    return size; /* DO NOT clear d0 — AssetSys needs d0!=0 for success */
}

static uint32_t rob_list_directory(uint8_t *name, uint8_t *callerBuf)
{
    /* Skip drive prefix (colon) from name */
    uint32_t i = 0;
    while (name[i]) {
        if (name[i] == ':') {
            name += i + 1;
            break;
        }
        i++;
    }

    /* Always use "save" directory */
    uint8_t *dirname = (uint8_t *)savedir;

    /* List files into our scratch buffer */
    file_ListFiles(2048, dirname, listdir_buf);

    /* Get count from high word of size (big-endian) */
    uint16_t count = *(uint16_t *)&size;
    if (count == 0) {
        _rnl_d1_return = (uint32_t)count;
        return 0;
    }

    /* Copy names from listdir_buf to caller's buffer in rob format */
    uint8_t *src = listdir_buf;
    uint8_t *dst = callerBuf;
    uint16_t n;
    for (n = 0; n < count; n++) {
        /* Copy filename to entry at offset +2, measure length */
        uint32_t nameLen = 0;
        while (*src) {
            dst[2 + nameLen] = *src++;
            nameLen++;
        }
        dst[2 + nameLen] = 0; /* null terminate */
        src++; /* skip source null */

        /* Set name length at offset +1 */
        dst[1] = (uint8_t)nameLen;
        /* Set marker byte at offset +0 */
        dst[0] = 0xFD;
        /* Advance to next 32-byte entry */
        dst += 0x20;
    }

    /* Return: d0=0, d1=count (as word from size) */
    _rnl_d1_return = (uint32_t)count;
    return 0;
}


/* ================================================================
 * RobNorthenLoader — C dispatcher (called via asm bridge)
 * ================================================================ */
__attribute__((used, externally_visible))
uint32_t RobNorthenLoader_C(uint32_t cmd, uint32_t arg1, uint8_t *name, uint8_t *buf)
{
    switch (cmd) {
    case 0: return rob_load(name, buf);
    case 1: return rob_save(name, buf, arg1);
    case 2: return rob_delete(name);
    case 3: return rob_list_directory(name, buf);
    case 4: return rob_format_disk();
    case 5: return rob_get_file_size(name);
    case 6:
        /* Op 6 is the "use the address from Op 5" callback. It MUST NOT
         * clear `_rnl_d1_return` because the dispatcher
         * `CAssetSys_DispatchGetSize` reads that value back into
         * `*outFileSize` AFTER the Op-5/Op-6 pair returns. Clearing it
         * in Op 6 (as the default case did) makes every cache precache
         * call allocate a 0-byte node, which cascades into a corrupted
         * cache linked list whose `next` pointer walks through low
         * memory, returning tiny "addresses" like 0x0F. The cache-store
         * then writes payload bytes starting at 0x0F+48=0x3F, trampling
         * the m68k exception vector table — the next interrupt loads a
         * corrupted vector and jumps to whatever byte pattern appears
         * in the file data. (For olddisk1.raw that byte pattern is
         * `00 F7 7B 00` at file offset 0x20 → wild PC = 0x00F77B00.) */
        return 0;
    default:
        _rnl_d1_return = 0;
        return 0;
    }
}


/* ================================================================
 * Hardware helper functions
 * ================================================================ */

__attribute__((used, externally_visible))
void FixSnoopBug(void)
{
    /* BPLCON0 is at offset 0x100, not in struct Custom — use absolute address */
    *(volatile uint16_t *)(CUSTOM_BASE + BPLCON0_OFF) = 0x0200u;
}

__attribute__((used, externally_visible))
void FixMemoryRoutine(void)
{
    /* no-op */
}

/* KBHook: read keyboard scan code, check for quit key */
__attribute__((used, externally_visible))
uint8_t KBHook(void)
{
    uint8_t raw = *(volatile uint8_t *)CIAA_SDR_ADDR;
    uint8_t keycode = ~raw;

    /* ROR.B #1 equivalent */
    keycode = (keycode >> 1) | (keycode << 7);

    if (keycode == _keyexit) {
        _quitToDos = 1;
    }

    return raw;
}

#endif  /* !SWOS_SDL_BUILD */
