/*
 * rollback_rig.c -- the rollback state test rig. docs/ROLLBACK.md.
 *
 * Proves that src/sim_state.c names every variable a match needs to be rolled
 * back, by rolling a real match back inside the replay rig's headless run. The
 * recording supplies the setup and the inputs; the game is compared with itself.
 *
 *   check     At frame F save the registry, play K frames, restore it and rewind the
 *             input to F, play the same K frames again. The registry must come out
 *             byte-identical. A failure names the registry entries that differ and the
 *             unregistered variables that changed in the window -- the suspects.
 *   oracle    The same, restoring ALL of the executable's writable data (.data+.bss)
 *             instead of the registry. It has to pass for anything else here to mean
 *             something: it shows that nothing the match reads lives outside that
 *             data -- no heap state, no second thread, no wall clock.
 *   discover  Per window, and for every symbol the window wrote: restore everything
 *             EXCEPT that symbol and play the window again. If the game state then
 *             differs, the symbol is needed. "Game state" is the registry plus the
 *             replay rig's ~2500 observable fields (sprites, both teams, stats, clock,
 *             camera), minus the symbol itself. Evidence per symbol, merged across
 *             recordings by scripts/rollback_report.py.
 *   scan      No rollback. Diff the writable data every frame and report every symbol
 *             the match writes: the set the registry is chosen from.
 *   trace     No rollback. Write a hash per frame of the registry and of the observable
 *             fields. scripts/rollback_test.sh diffs a plain run against a check run:
 *             rolling back must not change the match at ANY later frame, which catches
 *             state whose effect shows up long after a window.
 *   (--rollback-trace works with every mode.)
 *
 * The whole-memory tools (scan, oracle, discover, and naming variables in a check
 * report) read the executable's own symbol table and exist on Linux only; check and
 * trace run anywhere.
 *
 * Everything the rig keeps between frames is on the heap: oracle and discover restore
 * the executable's data wholesale, and must not rewind the rig along with the game.
 */
#ifdef SWOS_SDL_BUILD

#include "rollback_rig.h"
#include "sim_state.h"
#include "replay_test.h"
#include "state_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

/* Desktop-only fork/ELF harness: Android builds as a shared library and has
 * neither the executable's __data_start symbol nor this test process model. */
#if defined(__linux__) && !defined(__ANDROID__)
#include <elf.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
extern char __data_start[];     /* start of .data -- provided by the GNU linker */
extern char _end[];             /* end of .bss */
#endif

extern void    Platform_QuiesceTimerThread(void);
extern uint8_t _kRandomTable[];
extern uint8_t _seed, _randXorIndex, _randXorKey;
extern uint8_t _seed2, _randXorIndex2, _randXorKey2;
extern volatile uint16_t _currentGameTick;
extern volatile uint16_t _currentTick;
extern volatile uint16_t _playGame;

/* ---- options: fixed once parsed, so a whole-data restore cannot change them ---- */
static int         s_mode = ROLLBACK_OFF;
static uint32_t    s_window;              /* K; 0 = the mode's default */
static uint32_t    s_every;               /* N: frames from one window's start to the next */
static uint32_t    s_first;               /* no window before this frame */
static uint32_t    s_seed = 0x5EED5EEDu;
static const char *s_reportPath;
static const char *s_tracePath;
static uint32_t    s_restoreTiers = SIM_TIER_ALL;  /* what check/discover save and restore */
static int         s_portable;            /* restore through the resync encoding */
static int         s_wire;                /* restore through a state packet (state_wire.c) */
static const char *s_dumpPath;            /* --rollback-dump: the registry at one frame, per region */
static uint32_t    s_dumpFrame;
static const char *s_saveStatePath;       /* --rollback-save-state: resync files at these frames */
static uint32_t    s_saveFrames[16];
static int         s_saveFrameCount;
static const char *s_loadStatePath;       /* --rollback-load-state: start from a resync file */

#define MAX_REPORTS 8

/* Wall-clock readings that pace or freshen the PRESENTATION, never the match. A
 * re-simulation reads the clock at a different moment, so these always come out
 * different; they are left out of every comparison. Each was checked at its source,
 * and the oracle run is what shows they are the only such bytes. */
static const struct { const char *name, *file; } k_volatile[] = {
    { "s_scroll_frac_ms",      "camera.c" },        /* sub-pixel scroll: sample freshness */
    { "s_hud_stamp",           "platform_sdl.c" },  /* HUD layer: freshness */
    { "s_last_isr_ms",         "platform_sdl.c" },  /* ISR throttle (0 ms in turbo) */
    { "s_last_main_render_ms", "platform_sdl.c" },  /* timer-thread fallback render gate */
};

/* ======================================================================
 * Options
 * ====================================================================== */
static const char *after(const char *arg, const char *prefix)
{
    size_t n = strlen(prefix);
    return strncmp(arg, prefix, n) == 0 ? arg + n : NULL;
}

int Rollback_ParseArg(const char *arg)
{
    const char *v;
    if ((v = after(arg, "--rollback="))) {
        if      (!strcmp(v, "trace"))    s_mode = ROLLBACK_TRACE;
        else if (!strcmp(v, "scan"))     s_mode = ROLLBACK_SCAN;
        else if (!strcmp(v, "check"))    s_mode = ROLLBACK_CHECK;
        else if (!strcmp(v, "oracle"))   s_mode = ROLLBACK_ORACLE;
        else if (!strcmp(v, "discover")) s_mode = ROLLBACK_DISCOVER;
        else {
            fprintf(stderr, "--rollback: expected trace|scan|check|oracle|discover, got '%s'\n", v);
            return -1;
        }
        return 1;
    }
    if ((v = after(arg, "--rollback-window="))) {
        long n = strtol(v, NULL, 10);
        if (n < 1 || n > 1000) { fprintf(stderr, "--rollback-window: 1..1000\n"); return -1; }
        s_window = (uint32_t)n;
        return 1;
    }
    if ((v = after(arg, "--rollback-every="))) {
        long n = strtol(v, NULL, 10);
        if (n < 1) { fprintf(stderr, "--rollback-every: >= 1\n"); return -1; }
        s_every = (uint32_t)n;
        return 1;
    }
    if ((v = after(arg, "--rollback-first="))) {
        s_first = (uint32_t)strtoul(v, NULL, 10);
        return 1;
    }
    if ((v = after(arg, "--rollback-seed="))) {
        s_seed = (uint32_t)strtoul(v, NULL, 0);
        return 1;
    }
    if ((v = after(arg, "--rollback-report="))) { s_reportPath = v; return 1; }
    if ((v = after(arg, "--rollback-trace=")))  { s_tracePath = v; return 1; }
    if (!strcmp(arg, "--rollback-portable")) { s_portable = 1; return 1; }
    if (!strcmp(arg, "--rollback-wire"))     { s_wire = 1; return 1; }
    if ((v = after(arg, "--rollback-dump=")))       { s_dumpPath = v; return 1; }
    if ((v = after(arg, "--rollback-dump-frame="))) { s_dumpFrame = (uint32_t)strtoul(v, NULL, 10); return 1; }
    if ((v = after(arg, "--rollback-save-state="))) { s_saveStatePath = v; return 1; }
    if ((v = after(arg, "--rollback-load-state="))) { s_loadStatePath = v; return 1; }
    if ((v = after(arg, "--rollback-save-frames="))) {
        char *end;
        s_saveFrameCount = 0;
        while (*v) {
            unsigned long n = strtoul(v, &end, 10);
            if (end == v || s_saveFrameCount == (int)(sizeof s_saveFrames / sizeof s_saveFrames[0])) {
                fprintf(stderr, "--rollback-save-frames: up to 16 comma-separated frame numbers\n");
                return -1;
            }
            s_saveFrames[s_saveFrameCount++] = (uint32_t)n;
            v = *end == ',' ? end + 1 : end;
        }
        return 1;
    }
    if ((v = after(arg, "--rollback-restore="))) {
        if      (!strcmp(v, "all")) s_restoreTiers = SIM_TIER_ALL;
        else if (!strcmp(v, "sim")) s_restoreTiers = SIM_TIER_SIM;
        else {
            fprintf(stderr, "--rollback-restore: expected all|sim, got '%s'\n", v);
            return -1;
        }
        return 1;
    }
    if ((v = after(arg, "--rollback-disable="))) {
        /* comma-separated registry entry names, left out of every save, load and hash */
        char name[128];
        while (*v) {
            size_t n = strcspn(v, ",");
            if (n == 0 || n >= sizeof name) {
                fprintf(stderr, "--rollback-disable: bad entry name\n");
                return -1;
            }
            memcpy(name, v, n);
            name[n] = 0;
            if (!SimState_Disable(name)) {
                fprintf(stderr, "--rollback-disable: no registry entry named '%s'\n", name);
                return -1;
            }
            v += n + (v[n] == ',');
        }
        return 1;
    }
    return -1;
}

int Rollback_Enabled(void)
{
    return s_mode != ROLLBACK_OFF || s_tracePath != NULL || s_dumpPath != NULL ||
           s_saveStatePath != NULL || s_loadStatePath != NULL;
}

int Rollback_WatchesFrames(void)
{
    return s_tracePath != NULL || s_dumpPath != NULL || s_saveStatePath != NULL;
}

int Rollback_Intervenes(void)
{
    return s_mode == ROLLBACK_SCAN || s_mode == ROLLBACK_CHECK || s_mode == ROLLBACK_ORACLE ||
           s_mode == ROLLBACK_DISCOVER || s_loadStatePath != NULL;
}

/* ---- resync files: what a peer joining or recovering mid-match would receive ----
 * The replay driver's input cursor, then two state packets (state_wire.c) for the same
 * frame -- SIM alone, and SIM+VIEW -- each a delta against the KICK-OFF state. Every
 * process running the match computes that state for itself, so a fresh process at
 * frame 0 holds the base already: exactly a peer that joins mid-match. Loading one and
 * playing on must reproduce the run that saved it frame for frame: any match state the
 * registry lacks has had the whole match so far to become different. */
typedef struct {
    char              magic[8];           /* "SWOSRS02" */
    ReplayDriverState driver;
    uint32_t          simPacketBytes, allPacketBytes;
} ResyncFileHeader;

typedef struct Rig Rig;
static void save_state_file(Rig *r, uint32_t frame);
static uint32_t load_state_file(Rig *r);


/* --rollback-dump: every registry region in the portable encoding, one line each
 * ("<name> <sim|view> <hex>"). Two runs loaded at different addresses must dump
 * identically; a region that does not is holding a pointer its map does not name. */
static void dump_registry(uint32_t frame)
{
    uint32_t len = SimState_PortableSize(SIM_TIER_ALL), off = 0;
    int unidentified;
    uint8_t *buf = (uint8_t *)malloc(len ? len : 1);
    FILE *f = fopen(s_dumpPath, "w");
    if (!buf || !f) {
        fprintf(stderr, "[rollback] cannot write %s\n", s_dumpPath);
        free(buf);
        if (f) fclose(f);
        return;
    }
    unidentified = SimState_SavePortable(buf, SIM_TIER_ALL);
    if (unidentified)
        fprintf(stderr, "[rollback] frame %u: %d pointer(s) in the match state have no identity\n",
                frame, unidentified);
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *r = &g_simStateRegions[i];
        const uint32_t size = SimState_RegionPortableSize(i);
        if (!SimState_RegionActive(i, SIM_TIER_ALL)) continue;
        fprintf(f, "%s %s ", r->name, (r->flags & SIM_VIEW) ? "view" : "sim");
        for (uint32_t k = 0; k < size; k++)
            fprintf(f, "%02x", buf[off + k]);
        fputc('\n', f);
        off += size;
    }
    fclose(f);
    free(buf);
    fprintf(stderr, "[rollback] dumped the registry at frame %u to %s\n", frame, s_dumpPath);
}

static int windowed(void)
{
    return s_mode == ROLLBACK_CHECK || s_mode == ROLLBACK_ORACLE || s_mode == ROLLBACK_DISCOVER;
}

/* ======================================================================
 * The executable's writable data, and the symbols in it
 * ====================================================================== */
typedef struct {
    uintptr_t   addr;
    uint32_t    size;
    const char *name;
    const char *file;       /* source file, for local (static) symbols */
} RbSym;

#define SF_VOLATILE 0x01    /* wall-clock reading: never compared */
#define SF_DRIVER   0x02    /* the replay/rollback rigs' own state: never compared */

typedef struct {
    uint8_t  *base;
    size_t    len;
    RbSym    *syms;         /* sorted by address */
    uint8_t  *symFlags;     /* SF_* per symbol */
    uint32_t  nsyms;
    uint8_t  *elf;          /* the executable image; symbol names point into it */
    uint8_t  *regBits;      /* one bit per byte: in the sim_state registry */
    uint8_t  *gameBits;     /* one bit per byte: registry or replay-observable */
} RbSegment;

typedef struct Rig {
    int       haveSegment;
    RbSegment seg;
    int       started;

    /* scan */
    uint8_t  *prev;
    uint8_t  *changedBits;
    uint32_t *symFrames, *symFirst, *symLast;    /* symLast is frame + 1, 0 = never */
    uint32_t  unnamedFrames, lastUnnamedFrame, framesScanned;

    /* windows: check / oracle / discover */
    int       phase;
    uint32_t  winStart, nextCheck;
    ReplayDriverState drv;
    uint32_t  simLen;
    uint8_t  *simA, *simB, *simC;
    uint8_t  *segA, *segB, *segC;
    uint32_t *countBC, *countAB;                 /* scratch, per symbol */
    uint32_t *symDiverged, *symUnrestored;       /* per symbol: windows it showed up in */
    uint32_t  checks, failures;

    /* discover */
    uint32_t *cand;
    uint32_t  ncand, candNext;
    uint32_t *dTested, *dGame, *dAny, *dExGame, *dExAny, *dFirstGame;  /* dEx*: symbol + 1 */
    uint32_t *dCrash;
    uint32_t  tests;
    int       inChild, childFd;                  /* in a forked test (fork_test) */
    uint32_t  testSym;

    /* trace */
    uint64_t *traceSim, *traceView, *traceObs;
    uint32_t  unidentifiedFrames;

    /* resync files */
    int       stateLoaded;
    uint8_t   stateSaved[16];                   /* which --rollback-save-frames are written */
    uint8_t  *kickoffSim, *kickoffAll;          /* portable state at the first frame: the resync base */

    /* --rollback-wire: each window's restore comes from a state packet */
    uint8_t  *wirePacket, *wireBase;
    uint32_t  wireCap, wireLen, wireBaseFrame;
    int       haveWireBase;
    uint32_t  wirePackets, wireFirstBytes, wireDeltaPackets, wireDeltaMin, wireDeltaMax;
    uint64_t  wireDeltaBytes;
    uint32_t  traceCap, traceLen;
} Rig;

enum { PH_IDLE, PH_RUN1, PH_RUN2, PH_TEST };

static Rig *R;

#define BIT_GET(b, i)  ((b)[(i) >> 3] & (1u << ((i) & 7)))
#define BIT_SET(b, i)  ((b)[(i) >> 3] |= (uint8_t)(1u << ((i) & 7)))

#if defined(__linux__) && !defined(__ANDROID__)
static int sym_order(const void *a, const void *b)
{
    const RbSym *x = (const RbSym *)a, *y = (const RbSym *)b;
    if (x->addr != y->addr) return x->addr < y->addr ? -1 : 1;
    return x->size > y->size ? -1 : (x->size < y->size ? 1 : 0);
}

static int load_segment(RbSegment *seg)
{
    seg->base = (uint8_t *)__data_start;
    seg->len = (size_t)((uint8_t *)_end - (uint8_t *)__data_start);

    FILE *f = fopen("/proc/self/exe", "rb");
    if (!f) { fprintf(stderr, "[rollback] cannot open /proc/self/exe\n"); return 0; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    seg->elf = (uint8_t *)malloc((size_t)size);
    if (!seg->elf || fread(seg->elf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        fprintf(stderr, "[rollback] cannot read /proc/self/exe\n");
        return 0;
    }
    fclose(f);

    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)seg->elf;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "[rollback] the executable is not a 64-bit ELF\n");
        return 0;
    }
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(seg->elf + eh->e_shoff);
    const Elf64_Shdr *symtab = NULL, *strtab = NULL;
    for (uint32_t i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type == SHT_SYMTAB) {
            symtab = &sh[i];
            strtab = &sh[sh[i].sh_link];
        }
    }
    if (!symtab) {
        fprintf(stderr, "[rollback] the executable has no symbol table (stripped?)\n");
        return 0;
    }

    const Elf64_Sym *st = (const Elf64_Sym *)(seg->elf + symtab->sh_offset);
    uint32_t n = (uint32_t)(symtab->sh_size / sizeof *st);
    const char *names = (const char *)(seg->elf + strtab->sh_offset);
    uintptr_t lo = (uintptr_t)seg->base, hi = lo + seg->len;

    /* A position-independent executable is loaded at a random base: the load bias
     * is where __data_start really is, minus where the symbol table says it is. */
    uintptr_t bias = 0;
    int haveBias = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (strcmp(names + st[i].st_name, "__data_start") == 0) {
            bias = (uintptr_t)__data_start - (uintptr_t)st[i].st_value;
            haveBias = 1;
            break;
        }
    }
    if (!haveBias) {
        fprintf(stderr, "[rollback] no __data_start in the symbol table\n");
        return 0;
    }

    seg->syms = (RbSym *)malloc(n * sizeof *seg->syms);
    if (!seg->syms) return 0;
    const char *file = "";
    for (uint32_t i = 0; i < n; i++) {
        int type = ELF64_ST_TYPE(st[i].st_info);
        if (type == STT_FILE) { file = names + st[i].st_name; continue; }
        if (type != STT_OBJECT || st[i].st_size == 0) continue;
        uintptr_t addr = bias + (uintptr_t)st[i].st_value;
        if (addr < lo || addr + st[i].st_size > hi) continue;
        RbSym *s = &seg->syms[seg->nsyms++];
        s->addr = addr;
        s->size = (uint32_t)st[i].st_size;
        s->name = names + st[i].st_name;
        s->file = ELF64_ST_BIND(st[i].st_info) == STB_LOCAL ? file : "";
    }
    qsort(seg->syms, seg->nsyms, sizeof *seg->syms, sym_order);

    seg->symFlags = (uint8_t *)calloc(seg->nsyms ? seg->nsyms : 1, 1);
    if (!seg->symFlags) return 0;
    for (uint32_t i = 0; i < seg->nsyms; i++) {
        const RbSym *s = &seg->syms[i];
        if (!strcmp(s->file, "replay_test.c") || !strcmp(s->file, "rollback_rig.c") ||
            !strcmp(s->name, "g_rtFrameNo"))
            seg->symFlags[i] |= SF_DRIVER;
        for (size_t k = 0; k < sizeof k_volatile / sizeof k_volatile[0]; k++)
            if (!strcmp(s->name, k_volatile[k].name) && !strcmp(s->file, k_volatile[k].file))
                seg->symFlags[i] |= SF_VOLATILE;
    }
    return 1;
}
#endif /* __linux__ */

/* The smallest symbol containing address p, or NULL. */
static const RbSym *sym_at(const RbSegment *seg, uintptr_t p)
{
    uint32_t lo = 0, hi = seg->nsyms;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (seg->syms[mid].addr <= p) lo = mid + 1; else hi = mid;
    }
    const RbSym *best = NULL;
    for (int32_t i = (int32_t)lo - 1, k = 0; i >= 0 && k < 256; i--, k++) {
        const RbSym *s = &seg->syms[i];
        if (p < s->addr + s->size && (!best || s->size < best->size))
            best = s;
    }
    return best;
}

static uint32_t sym_index(const RbSegment *seg, const RbSym *s)
{
    return (uint32_t)(s - seg->syms);
}

static int never_compared(const RbSegment *seg, const RbSym *s)
{
    return (seg->symFlags[sym_index(seg, s)] & (SF_VOLATILE | SF_DRIVER)) != 0;
}

/* 0 = no byte of the symbol is registered, 1 = some, 2 = all. */
static int sym_registered(const RbSegment *seg, const RbSym *s)
{
    size_t off = s->addr - (uintptr_t)seg->base, covered = 0;
    for (size_t i = 0; i < s->size; i++)
        if (BIT_GET(seg->regBits, off + i)) covered++;
    return covered == 0 ? 0 : (covered == s->size ? 2 : 1);
}

/* Per-symbol count of bytes that differ between two copies of the segment. Symbols
 * that are never compared are counted but not included in the total. */
static size_t diff_by_symbol(const RbSegment *seg, const uint8_t *x, const uint8_t *y,
                             uint32_t *perSym, size_t *unnamed)
{
    size_t total = 0, i = 0, len = seg->len;
    memset(perSym, 0, seg->nsyms * sizeof *perSym);
    *unnamed = 0;
    while (i < len) {
        if ((i & 4095) == 0 && i + 4096 <= len && memcmp(x + i, y + i, 4096) == 0) {
            i += 4096;
            continue;
        }
        if (x[i] == y[i]) { i++; continue; }
        const RbSym *s = sym_at(seg, (uintptr_t)seg->base + i);
        if (!s) { (*unnamed)++; total++; i++; continue; }
        size_t end = (size_t)(s->addr + s->size - (uintptr_t)seg->base);
        int counts = !never_compared(seg, s);
        for (; i < end && i < len; i++) {
            if (x[i] != y[i]) {
                perSym[sym_index(seg, s)]++;
                if (counts) total++;
            }
        }
    }
    return total;
}

/* How the live data differs from `ref`, ignoring `exclude` and never-compared symbols. */
typedef struct {
    size_t  bytes, gameBytes;
    int32_t firstSym, firstGameSym;     /* symbol index, -1 none, -2 unnamed */
} CmpResult;

static void compare_live(const RbSegment *seg, const uint8_t *ref, const RbSym *exclude,
                         CmpResult *out)
{
    const uint8_t *live = seg->base;
    size_t i = 0, len = seg->len;
    out->bytes = out->gameBytes = 0;
    out->firstSym = out->firstGameSym = -1;
    while (i < len) {
        if ((i & 4095) == 0 && i + 4096 <= len && memcmp(live + i, ref + i, 4096) == 0) {
            i += 4096;
            continue;
        }
        if (live[i] == ref[i]) { i++; continue; }
        const RbSym *s = sym_at(seg, (uintptr_t)seg->base + i);
        size_t end = s ? (size_t)(s->addr + s->size - (uintptr_t)seg->base) : i + 1;
        if (s && (s == exclude || never_compared(seg, s))) {
            i = end;
            continue;
        }
        int32_t id = s ? (int32_t)sym_index(seg, s) : -2;
        for (; i < end && i < len; i++) {
            if (live[i] == ref[i]) continue;
            out->bytes++;
            if (out->firstSym == -1) out->firstSym = id;
            if (BIT_GET(seg->gameBits, i)) {
                out->gameBytes++;
                if (out->firstGameSym == -1) out->firstGameSym = id;
            }
        }
    }
}

static const char *sym_label(const RbSegment *seg, int32_t id)
{
    return id >= 0 ? seg->syms[id].name : (id == -2 ? "(unnamed bytes)" : "-");
}

/* Registry entries that differ between two SimState_Save buffers. */
static void report_registry_diff(const uint8_t *a, const uint8_t *b, int32_t limit)
{
    uint32_t off = 0;
    int32_t shown = 0;
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *r = &g_simStateRegions[i];
        uint32_t n = 0, first = 0;
        const uint32_t size = s_portable ? SimState_RegionPortableSize(i) : r->size;
        if (!SimState_RegionActive(i, s_restoreTiers)) continue;
        for (uint32_t k = 0; k < size; k++)
            if (a[off + k] != b[off + k]) { if (!n) first = k; n++; }
        if (n && shown++ < limit)
            fprintf(stderr, "[rollback]     %-36s %u byte(s) differ, first at +%u\n",
                    r->name, n, first);
        off += size;
    }
    if (shown > limit)
        fprintf(stderr, "[rollback]     ... and %d more registry entries\n", shown - limit);
}

typedef struct { uint32_t index, count; } SymCount;

static int by_count_desc(const void *a, const void *b)
{
    const SymCount *x = (const SymCount *)a, *y = (const SymCount *)b;
    return x->count < y->count ? 1 : (x->count > y->count ? -1 : 0);
}

/* The symbols with a non-zero count, largest first; never-compared ones left out. */
static void print_symbols(const RbSegment *seg, const uint32_t *count, int unregisteredOnly,
                          const char *heading, int limit)
{
    SymCount *list = (SymCount *)malloc(seg->nsyms * sizeof *list);
    uint32_t n = 0;
    if (!list) return;
    for (uint32_t i = 0; i < seg->nsyms; i++) {
        const RbSym *s = &seg->syms[i];
        if (!count[i] || never_compared(seg, s)) continue;
        if (unregisteredOnly && sym_registered(seg, s) == 2) continue;
        list[n].index = i;
        list[n].count = count[i];
        n++;
    }
    qsort(list, n, sizeof *list, by_count_desc);
    fprintf(stderr, "[rollback]   %s: %u\n", heading, n);
    for (uint32_t k = 0; k < n && (int)k < limit; k++) {
        const RbSym *s = &seg->syms[list[k].index];
        fprintf(stderr, "[rollback]     %-36s %7u of %-7u %s%s\n",
                s->name, list[k].count, s->size, s->file[0] ? s->file : "",
                sym_registered(seg, s) == 1 ? " (partly registered)" : "");
    }
    if (n > (uint32_t)limit)
        fprintf(stderr, "[rollback]     ... and %u more\n", n - (uint32_t)limit);
    free(list);
}

/* ======================================================================
 * Masks
 * ====================================================================== */
static void mark_bits(const RbSegment *seg, uint8_t *bits, void *addr, uint32_t size)
{
    uintptr_t a = (uintptr_t)addr;
    if (a < (uintptr_t)seg->base || a + size > (uintptr_t)seg->base + seg->len)
        return;
    size_t off = a - (uintptr_t)seg->base;
    for (uint32_t k = 0; k < size; k++)
        BIT_SET(bits, off + k);
}

static void mark_observable(void *addr, uint32_t size, void *ctx)
{
    RbSegment *seg = (RbSegment *)ctx;
    mark_bits(seg, seg->gameBits, addr, size);
}

static void mark_registry(RbSegment *seg)
{
    int bad = 0;
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const SimStateRegion *r = &g_simStateRegions[i];
        uintptr_t a = (uintptr_t)r->addr;
        if (a < (uintptr_t)seg->base || a + r->size > (uintptr_t)seg->base + seg->len) {
            fprintf(stderr, "[rollback] registry entry %s lies outside the executable's data\n",
                    r->name);
            bad = 1;
            continue;
        }
        /* An entry must cover exactly its variable -- a size taken from a wrong extern
         * would silently save too little or someone else's bytes -- or, for a
         * SIM_PART entry, lie inside it. */
        const RbSym *s = sym_at(seg, a);
        if (!s) {
            fprintf(stderr, "[rollback] registry entry %s: no symbol at its address\n", r->name);
            bad = 1;
        } else if ((r->flags & SIM_PART) ? a + r->size > s->addr + s->size
                                         : (s->addr != a || s->size != r->size)) {
            fprintf(stderr, "[rollback] registry entry %s covers %u byte(s) at +%u of symbol "
                            "%s (%u bytes)\n",
                    r->name, r->size, (unsigned)(a - s->addr), s->name, s->size);
            bad = 1;
        }
        if (SimState_RegionActive(i, s_restoreTiers))
            mark_bits(seg, seg->regBits, r->addr, r->size);
        if (SimState_RegionActive(i, SIM_TIER_SIM))
            mark_bits(seg, seg->gameBits, r->addr, r->size);
    }
    if (bad) {
        fprintf(stderr, "[rollback] src/sim_state.c does not match the executable\n");
        exit(1);
    }
    ReplayTest_ForEachObservable(mark_observable, seg);
}

/* ======================================================================
 * Modes
 * ====================================================================== */
static void pin_rng(void)
{
    /* Real generators, pinned start: the rig must exercise the RNG state, so it is
     * seeded rather than frozen to one value the way the replay comparison needs. */
    _seed          = (uint8_t)(s_seed);
    _randXorIndex  = (uint8_t)(s_seed >> 8);
    _randXorKey    = _kRandomTable[_randXorIndex];
    _seed2         = (uint8_t)(s_seed >> 16);
    _randXorIndex2 = (uint8_t)(s_seed >> 24);
    _randXorKey2   = _kRandomTable[_randXorIndex2];
}

static void scan_frame(Rig *r, uint32_t frame)
{
    RbSegment *seg = &r->seg;
    const uint8_t *live = seg->base;
    size_t i = 0, len = seg->len;
    r->framesScanned++;
    while (i < len) {
        if ((i & 4095) == 0 && i + 4096 <= len && memcmp(live + i, r->prev + i, 4096) == 0) {
            i += 4096;
            continue;
        }
        if (live[i] == r->prev[i]) { i++; continue; }
        const RbSym *s = sym_at(seg, (uintptr_t)seg->base + i);
        size_t end = i + 1;
        if (s) {
            uint32_t k = sym_index(seg, s);
            if (r->symLast[k] != frame + 1) {
                if (r->symFrames[k]++ == 0)
                    r->symFirst[k] = frame;
                r->symLast[k] = frame + 1;
            }
            end = (size_t)(s->addr + s->size - (uintptr_t)seg->base);
        } else if (r->lastUnnamedFrame != frame + 1) {
            r->unnamedFrames++;
            r->lastUnnamedFrame = frame + 1;
        }
        for (; i < end && i < len; i++) {
            if (live[i] != r->prev[i]) {
                BIT_SET(r->changedBits, i);
                r->prev[i] = live[i];
            }
        }
    }
}

/* ---- check / oracle ---- */
static void evaluate_window(Rig *r)
{
    int registryDiverged = memcmp(r->simB, r->simC, r->simLen) != 0;
    size_t segDiff = 0, unnamed = 0, unnamedAB = 0;
    r->checks++;

    if (r->haveSegment) {
        RbSegment *seg = &r->seg;
        segDiff = diff_by_symbol(seg, r->segB, r->segC, r->countBC, &unnamed);
        diff_by_symbol(seg, r->segA, r->segB, r->countAB, &unnamedAB);
        for (uint32_t i = 0; i < seg->nsyms; i++) {
            if (r->countBC[i]) r->symDiverged[i]++;
            if (r->countAB[i] && sym_registered(seg, &seg->syms[i]) != 2) r->symUnrestored[i]++;
        }
    }

    int failed = s_mode == ROLLBACK_ORACLE ? (segDiff > 0 || registryDiverged) : registryDiverged;
    if (!failed)
        return;
    if (r->failures++ >= MAX_REPORTS)
        return;

    fprintf(stderr, "[rollback] FAIL: frames %u..%u re-simulated differently after a %s restore\n",
            r->winStart, r->winStart + s_window - 1,
            s_mode == ROLLBACK_ORACLE ? "whole-data" : "registry");
    if (registryDiverged) {
        fprintf(stderr, "[rollback]   registry entries that came out different:\n");
        report_registry_diff(r->simB, r->simC, 24);
    }
    if (r->haveSegment) {
        print_symbols(&r->seg, r->countBC, s_mode == ROLLBACK_CHECK,
                      s_mode == ROLLBACK_CHECK ? "unregistered symbols that came out different"
                                               : "symbols that came out different", 24);
        if (unnamed)
            fprintf(stderr, "[rollback]   %zu unnamed byte(s) came out different\n", unnamed);
        if (s_mode == ROLLBACK_CHECK)
            print_symbols(&r->seg, r->countAB, 1,
                          "unregistered symbols that changed during the window (the suspects)", 32);
    }
}

/* ---- discover ---- */
typedef struct {
    int32_t  matchEnded;
    uint32_t bytes, gameBytes;
    int32_t  firstSym, firstGameSym;
} TestResult;

#define WHOLE_DATA 0xFFFFFFFFu

#if defined(__linux__) && !defined(__ANDROID__)
enum { TEST_OK, TEST_CHILD, TEST_DIED, TEST_HUNG };

/* Each leave-one-out pass runs in a forked copy of the process. Leaving a variable
 * out can put the game in a state it never reaches on its own -- leaving out
 * _gameState crashed UpdatePlayers -- and a child that crashes or hangs is simply the
 * strongest possible "needed". The parent never plays a test frame, so it carries on
 * from the window's real end state. In the child this returns TEST_CHILD, and the
 * child plays the window from the restored state and reports from PH_TEST. */
static int fork_test(Rig *r, uint32_t sym, TestResult *res)
{
    RbSegment *seg = &r->seg;
    int fds[2];
    fflush(stdout);
    fflush(stderr);
    if (pipe(fds) != 0) { perror("[rollback] pipe"); exit(1); }
    pid_t pid = fork();
    if (pid < 0) { perror("[rollback] fork"); exit(1); }
    if (pid == 0) {
        close(fds[0]);
        r->inChild = 1;
        r->childFd = fds[1];
        r->testSym = sym;
        memcpy(seg->base, r->segA, seg->len);
        if (sym != WHOLE_DATA) {
            const RbSym *s = &seg->syms[sym];
            size_t off = s->addr - (uintptr_t)seg->base;
            memcpy(seg->base + off, r->segB + off, s->size);   /* left where the window left it */
        }
        ReplayTest_SetDriverState(&r->drv);
        r->phase = PH_TEST;
        return TEST_CHILD;
    }
    close(fds[1]);
    struct pollfd p = { fds[0], POLLIN, 0 };
    int got = 0, timedOut = 0, pr = poll(&p, 1, 60000);
    if (pr > 0)
        got = read(fds[0], res, sizeof *res) == (ssize_t)sizeof *res;
    else
        timedOut = 1;
    if (!got)
        kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    close(fds[0]);
    return got ? TEST_OK : (timedOut ? TEST_HUNG : TEST_DIED);
}

static void run_tests(Rig *r)
{
    RbSegment *seg = &r->seg;
    uint32_t game = 0, died = 0;
    for (uint32_t t = 0; t <= r->ncand; t++) {
        uint32_t sym = t < r->ncand ? r->cand[t] : WHOLE_DATA;
        TestResult res;
        memset(&res, 0, sizeof res);
        int outcome = fork_test(r, sym, &res);
        if (outcome == TEST_CHILD)
            return;

        if (sym == WHOLE_DATA) {
            /* Everything restored: the window must reproduce exactly (the oracle, once
             * per window). If it does not, nothing else this window said counts. */
            if (outcome != TEST_OK || res.matchEnded || res.bytes) {
                r->failures++;
                fprintf(stderr, "[rollback] DISCOVER INVALID: frames %u..%u re-simulated "
                                "differently after a whole-data restore (%s, %u bytes, first in %s)\n",
                        r->winStart, r->winStart + s_window - 1,
                        outcome == TEST_OK ? "ran" : (outcome == TEST_HUNG ? "hung" : "died"),
                        res.bytes, sym_label(seg, res.firstSym));
            }
            break;
        }

        r->tests++;
        r->dTested[sym]++;
        if (outcome != TEST_OK || res.matchEnded || res.gameBytes) {
            game++;
            r->dGame[sym]++;
            if (!r->dFirstGame[sym]) r->dFirstGame[sym] = r->winStart + 1;
            if (!r->dExGame[sym] && res.firstGameSym >= 0) r->dExGame[sym] = (uint32_t)res.firstGameSym + 1;
        }
        if (outcome != TEST_OK) {
            died++;
            r->dCrash[sym]++;
        }
        if (outcome != TEST_OK || res.matchEnded || res.bytes) {
            r->dAny[sym]++;
            if (!r->dExAny[sym] && res.firstSym >= 0) r->dExAny[sym] = (uint32_t)res.firstSym + 1;
        }
    }
    fprintf(stderr, "[rollback] window at frame %u: %u symbols written; leaving out %u changed "
                    "the game (%u of them crashed or hung the match)\n",
            r->winStart, r->ncand, game, died);
}
#else
static void run_tests(Rig *r) { (void)r; }
#endif

static void save_state_file(Rig *r, uint32_t frame)
{
    char path[1024];
    ResyncFileHeader h;
    const uint32_t capSim = StateWire_MaxBytes(SIM_TIER_SIM), capAll = StateWire_MaxBytes(SIM_TIER_ALL);
    uint8_t *simPacket = (uint8_t *)malloc(capSim), *allPacket = (uint8_t *)malloc(capAll);
    int32_t simBytes, allBytes;
    FILE *f;

    if (!simPacket || !allPacket) {
        fprintf(stderr, "[rollback] out of memory for a resync file\n");
        exit(1);
    }
    simBytes = StateWire_Encode(simPacket, capSim, SIM_TIER_SIM, frame, r->kickoffSim, 0);
    allBytes = StateWire_Encode(allPacket, capAll, SIM_TIER_ALL, frame, r->kickoffAll, 0);
    if (simBytes <= 0 || allBytes <= 0) {
        fprintf(stderr, "[rollback] cannot pack the state of frame %u: %s\n", frame,
                StateWire_ErrorName(simBytes <= 0 ? simBytes : allBytes));
        exit(1);
    }
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "SWOSRS02", 8);
    ReplayTest_GetDriverState(&h.driver);
    h.simPacketBytes = (uint32_t)simBytes;
    h.allPacketBytes = (uint32_t)allBytes;
    snprintf(path, sizeof path, "%s.%u", s_saveStatePath, frame);
    f = fopen(path, "wb");
    if (!f || fwrite(&h, sizeof h, 1, f) != 1 ||
        fwrite(simPacket, 1, h.simPacketBytes, f) != h.simPacketBytes ||
        fwrite(allPacket, 1, h.allPacketBytes, f) != h.allPacketBytes) {
        fprintf(stderr, "[rollback] cannot write %s\n", path);
        exit(1);
    }
    fclose(f);
    free(simPacket);
    free(allPacket);
    fprintf(stderr, "[rollback] saved resync state at frame %u to %s: SIM %d bytes, SIM+VIEW %d bytes "
                    "(deltas against kick-off; raw %u and %u)\n", frame, path, simBytes, allBytes,
            SimState_PortableSize(SIM_TIER_SIM), SimState_PortableSize(SIM_TIER_ALL));
}

/* Returns the frame the loaded state belongs to; exits on a file or packet it refuses. */
static uint32_t load_state_file(Rig *r)
{
    ResyncFileHeader h;
    uint8_t *packets = NULL;
    const int withView = (s_restoreTiers & SIM_TIER_VIEW) != 0;
    StateWireInfo info;
    int err;
    FILE *f = fopen(s_loadStatePath, "rb");

    if (!f || fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "SWOSRS02", 8) != 0 ||
        !(packets = (uint8_t *)malloc((size_t)h.simPacketBytes + h.allPacketBytes + 1)) ||
        fread(packets, 1, (size_t)h.simPacketBytes + h.allPacketBytes, f) !=
            (size_t)h.simPacketBytes + h.allPacketBytes) {
        fprintf(stderr, "[rollback] %s is not a resync file\n", s_loadStatePath);
        exit(1);
    }
    fclose(f);
    err = withView
        ? StateWire_Decode(packets + h.simPacketBytes, h.allPacketBytes, r->kickoffAll, &info)
        : StateWire_Decode(packets, h.simPacketBytes, r->kickoffSim, &info);
    free(packets);
    if (err != STATE_WIRE_OK) {
        fprintf(stderr, "[rollback] %s: the state packet was refused: %s\n", s_loadStatePath,
                StateWire_ErrorName(err));
        exit(1);
    }
    ReplayTest_SetDriverState(&h.driver);
    fprintf(stderr, "[rollback] loaded resync state of frame %u from %s (%s, %u bytes)\n", info.frame,
            s_loadStatePath, withView ? "sim + view" : "sim only",
            withView ? h.allPacketBytes : h.simPacketBytes);
    return info.frame;
}

/* --rollback-wire: the window-start state leaves as a packet (a delta against the
 * previous window start), and the restore at the window's end comes back from it. */
static void pack_window_start(Rig *r, uint32_t frame)
{
    int32_t n = StateWire_Encode(r->wirePacket, r->wireCap, s_restoreTiers, frame,
                                 r->haveWireBase ? r->wireBase : NULL, r->wireBaseFrame);
    if (n <= 0) {
        fprintf(stderr, "[rollback] cannot pack the state of frame %u: %s\n", frame,
                StateWire_ErrorName(n));
        exit(2);
    }
    r->wireLen = (uint32_t)n;
    r->wirePackets++;
    if (!r->haveWireBase) {
        r->wireFirstBytes = (uint32_t)n;
    } else {
        if (!r->wireDeltaPackets || (uint32_t)n < r->wireDeltaMin) r->wireDeltaMin = (uint32_t)n;
        if ((uint32_t)n > r->wireDeltaMax) r->wireDeltaMax = (uint32_t)n;
        r->wireDeltaPackets++;
        r->wireDeltaBytes += (uint32_t)n;
    }
}

static void unpack_window_start(Rig *r)
{
    StateWireInfo info;
    int err = StateWire_Decode(r->wirePacket, r->wireLen, r->haveWireBase ? r->wireBase : NULL, &info);
    if (err != STATE_WIRE_OK || info.frame != r->winStart) {
        fprintf(stderr, "[rollback] FAIL: the state packet of frame %u was refused: %s\n",
                r->winStart, err != STATE_WIRE_OK ? StateWire_ErrorName(err) : "wrong frame");
        fflush(stderr);
        exit(2);
    }
    /* What was just loaded is what both ends of a session would now hold: the base of the
     * next packet. */
    if (SimState_SavePortable(r->wireBase, s_restoreTiers) != 0) {
        fprintf(stderr, "[rollback] FAIL: the state of frame %u holds a pointer with no identity\n",
                r->winStart);
        exit(2);
    }
    r->haveWireBase = 1;
    r->wireBaseFrame = r->winStart;
}

static void save_registry(uint8_t *dst)
{
    if (!s_portable) {
        SimState_Save(dst, s_restoreTiers);
    } else if (SimState_SavePortable(dst, s_restoreTiers) != 0) {
        fprintf(stderr, "[rollback] FAIL: the match state holds a pointer with no identity\n");
        exit(2);
    }
}

static void load_registry(const uint8_t *src)
{
    if (!s_portable) {
        SimState_Load(src, s_restoreTiers);
    } else if (SimState_LoadPortable(src, s_restoreTiers) != 0) {
        fprintf(stderr, "[rollback] FAIL: a portable state did not resolve in this build\n");
        exit(2);
    }
}

static void window_boundary(Rig *r, uint32_t frame)
{
    RbSegment *seg = &r->seg;
    for (;;) {
        switch (r->phase) {
        case PH_IDLE:
            if (frame < r->nextCheck)
                return;
            r->winStart = frame;
            save_registry(r->simA);
            if (s_wire && s_mode == ROLLBACK_CHECK)
                pack_window_start(r, frame);
            if (r->haveSegment) memcpy(r->segA, seg->base, seg->len);
            ReplayTest_GetDriverState(&r->drv);
            r->phase = PH_RUN1;
            return;

        case PH_RUN1:
            if (frame < r->winStart + s_window)
                return;
            save_registry(r->simB);
            if (r->haveSegment) memcpy(r->segB, seg->base, seg->len);
            if (s_mode == ROLLBACK_DISCOVER) {
                size_t unnamed;
                diff_by_symbol(seg, r->segA, r->segB, r->countAB, &unnamed);
                r->ncand = 0;
                for (uint32_t i = 0; i < seg->nsyms; i++)
                    if (r->countAB[i] && !never_compared(seg, &seg->syms[i]))
                        r->cand[r->ncand++] = i;
                r->checks++;
                run_tests(r);
                if (r->inChild)
                    return;                 /* a forked test: play the window */
                r->phase = PH_IDLE;
                r->nextCheck = r->winStart + s_every;
                continue;
            }
            if (s_mode == ROLLBACK_ORACLE)
                memcpy(seg->base, r->segA, seg->len);   /* the replay rig's cursor included */
            else if (s_wire)
                unpack_window_start(r);
            else
                load_registry(r->simA);
            ReplayTest_SetDriverState(&r->drv);
            r->phase = PH_RUN2;
            return;

        case PH_RUN2:
            if (frame < r->winStart + s_window)
                return;
            save_registry(r->simC);
            if (r->haveSegment) memcpy(r->segC, seg->base, seg->len);
            evaluate_window(r);
            r->phase = PH_IDLE;
            r->nextCheck = r->winStart + s_every;
            continue;               /* a new window may start at this very frame */

        case PH_TEST: {
#if defined(__linux__) && !defined(__ANDROID__)
            /* In a forked test (fork_test): play the window, report, and vanish. */
            TestResult res;
            memset(&res, 0, sizeof res);
            res.matchEnded = _playGame == 0;        /* before GameLoop acts on it */
            if (!res.matchEnded && frame < r->winStart + s_window)
                return;
            if (!res.matchEnded) {
                CmpResult c;
                compare_live(seg, r->segB,
                             r->testSym == WHOLE_DATA ? NULL : &seg->syms[r->testSym], &c);
                res.bytes = (uint32_t)c.bytes;
                res.gameBytes = (uint32_t)c.gameBytes;
                res.firstSym = c.firstSym;
                res.firstGameSym = c.firstGameSym;
            }
            if (write(r->childFd, &res, sizeof res) != (ssize_t)sizeof res)
                _exit(1);
            _exit(0);
#else
            return;
#endif
        }
        }
    }
}

static void trace_frame(Rig *r, uint32_t frame)
{
    if (frame >= r->traceCap) {
        uint32_t cap = r->traceCap ? r->traceCap * 2 : 65536;
        while (cap <= frame) cap *= 2;
        r->traceSim = (uint64_t *)realloc(r->traceSim, cap * sizeof(uint64_t));
        r->traceView = (uint64_t *)realloc(r->traceView, cap * sizeof(uint64_t));
        r->traceObs = (uint64_t *)realloc(r->traceObs, cap * sizeof(uint64_t));
        if (!r->traceSim || !r->traceView || !r->traceObs) {
            fprintf(stderr, "[rollback] out of memory for the trace\n");
            exit(1);
        }
        r->traceCap = cap;
    }
    uint32_t unidentified;
    r->traceSim[frame] = SimState_Hash(SIM_TIER_SIM);
    unidentified = SimState_HashUnidentified();
    r->traceView[frame] = SimState_Hash(SIM_TIER_VIEW);
    unidentified += SimState_HashUnidentified();
    if (unidentified && r->unidentifiedFrames++ == 0)
        fprintf(stderr, "[rollback] FAIL: frame %u: %u pointer(s) in the match state have no "
                        "identity -- the checksum cannot be compared between builds\n",
                frame, unidentified);
    r->traceObs[frame] = ReplayTest_ObservableHash();
    if (frame + 1 > r->traceLen)
        r->traceLen = frame + 1;
}

static void *alloc_or_die(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fprintf(stderr, "[rollback] out of memory (%zu bytes)\n", n);
        exit(1);
    }
    return p;
}

static const char *const k_modeName[] = { "off", "trace", "scan", "check", "oracle", "discover" };

void Rollback_MatchStart(void)
{
    if (s_mode == ROLLBACK_OFF)
        s_mode = ROLLBACK_TRACE;            /* --rollback-trace on its own */
    if (!s_window) s_window = s_mode == ROLLBACK_DISCOVER ? 32 : 8;
    if (!s_every)  s_every = s_mode == ROLLBACK_DISCOVER ? 1024 : 64;

    R = (Rig *)alloc_or_die(sizeof *R);

    /* Every region's portable layout must describe it exactly, on every build. */
    for (int32_t i = 0; i < g_simStateRegionCount; i++) {
        const char *why = SimState_LayoutError(i);
        if (why) {
            fprintf(stderr, "[rollback] registry entry %s: %s\n", g_simStateRegions[i].name, why);
            exit(1);
        }
    }

    /* Only the game loop may write game memory from here on (docs/ROLLBACK.md). */
    Platform_QuiesceTimerThread();
    _currentTick = 0;
    _currentGameTick = 0;
    pin_rng();

#if defined(__linux__) && !defined(__ANDROID__)
    R->haveSegment = load_segment(&R->seg);
#endif
    if (!R->haveSegment && s_mode != ROLLBACK_CHECK && s_mode != ROLLBACK_TRACE) {
        fprintf(stderr, "[rollback] --rollback=%s needs the executable's symbol table "
                        "(Linux, unstripped)\n", k_modeName[s_mode]);
        exit(1);
    }

    RbSegment *seg = &R->seg;
    uint32_t nsyms = seg->nsyms;
    if (R->haveSegment) {
        seg->regBits = (uint8_t *)alloc_or_die(seg->len / 8 + 1);
        seg->gameBits = (uint8_t *)alloc_or_die(seg->len / 8 + 1);
        mark_registry(seg);
    }

    R->simLen = s_portable ? SimState_PortableSize(s_restoreTiers) : SimState_Size(s_restoreTiers);
    fprintf(stderr, "[rollback] mode %s, seed 0x%08X; registry: %d entries, sim %u + view %u "
                    "bytes (portable %u + %u); restoring %s",
            k_modeName[s_mode], s_seed, g_simStateRegionCount, SimState_Size(SIM_TIER_SIM),
            SimState_Size(SIM_TIER_VIEW), SimState_PortableSize(SIM_TIER_SIM),
            SimState_PortableSize(SIM_TIER_VIEW), s_restoreTiers == SIM_TIER_ALL ? "all" : "sim only");
    if (s_portable)
        fprintf(stderr, " (portable encoding)");
    if (R->haveSegment)
        fprintf(stderr, "; writable data %zu bytes, %u symbols", seg->len, nsyms);
    fputc('\n', stderr);

    if (s_mode == ROLLBACK_SCAN) {
        R->prev = (uint8_t *)alloc_or_die(seg->len);
        R->changedBits = (uint8_t *)alloc_or_die(seg->len / 8 + 1);
        R->symFrames = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->symFirst = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->symLast = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
    }
    if (windowed()) {
        if (s_every < s_window) {
            fprintf(stderr, "[rollback] --rollback-every (%u) must be >= --rollback-window (%u)\n",
                    s_every, s_window);
            exit(1);
        }
        fprintf(stderr, "[rollback] window %u frames, one every %u frames from frame %u\n",
                s_window, s_every, s_first);
        R->nextCheck = s_first;
        R->simA = (uint8_t *)alloc_or_die(R->simLen);
        if (s_wire) {
            R->wireCap = StateWire_MaxBytes(s_restoreTiers);
            R->wirePacket = (uint8_t *)alloc_or_die(R->wireCap);
            R->wireBase = (uint8_t *)alloc_or_die(SimState_PortableSize(s_restoreTiers));
        }
        R->simB = (uint8_t *)alloc_or_die(R->simLen);
        R->simC = (uint8_t *)alloc_or_die(R->simLen);
        if (R->haveSegment) {
            R->segA = (uint8_t *)alloc_or_die(seg->len);
            R->segB = (uint8_t *)alloc_or_die(seg->len);
            R->segC = (uint8_t *)alloc_or_die(seg->len);
            R->countBC = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
            R->countAB = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
            R->symDiverged = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
            R->symUnrestored = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        }
    }
    if (s_mode == ROLLBACK_DISCOVER) {
        R->cand = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->dTested = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->dGame = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->dAny = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->dExGame = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->dExAny = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->dFirstGame = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
        R->dCrash = (uint32_t *)alloc_or_die(nsyms * sizeof(uint32_t));
    }
    fflush(stderr);
}

void Rollback_FrameBoundary(uint32_t frame)
{
    if (!R)
        return;
    if ((s_saveStatePath || s_loadStatePath) && !R->kickoffSim) {
        /* The resync base: this process's own state at its first frame. */
        R->kickoffSim = (uint8_t *)alloc_or_die(SimState_PortableSize(SIM_TIER_SIM));
        R->kickoffAll = (uint8_t *)alloc_or_die(SimState_PortableSize(SIM_TIER_ALL));
        SimState_SavePortable(R->kickoffSim, SIM_TIER_SIM);
        SimState_SavePortable(R->kickoffAll, SIM_TIER_ALL);
    }
    if (s_loadStatePath && !R->stateLoaded) {
        R->stateLoaded = 1;
        frame = load_state_file(R);
    }
    if (!R->started) {
        R->started = 1;
        if (s_mode == ROLLBACK_SCAN)
            memcpy(R->prev, R->seg.base, R->seg.len);
    } else if (s_mode == ROLLBACK_SCAN) {
        scan_frame(R, frame);
    }

    if (windowed()) {
        window_boundary(R, frame);
        ReplayDriverState now;
        ReplayTest_GetDriverState(&now);
        frame = now.frame;                  /* after a rewind, the frame about to run */
    }
    if (s_tracePath)
        trace_frame(R, frame);
    if (s_dumpPath && frame == s_dumpFrame)
        dump_registry(frame);
    for (int i = 0; s_saveStatePath && i < s_saveFrameCount; i++)
        if (s_saveFrames[i] == frame && !R->stateSaved[i]) {
            R->stateSaved[i] = 1;             /* once, even if a window replays the frame */
            save_state_file(R, frame);
        }
}

/* ======================================================================
 * Reports
 * ====================================================================== */
static void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static void json_symbol_head(FILE *f, const RbSegment *seg, uint32_t i, int *first)
{
    const RbSym *s = &seg->syms[i];
    fprintf(f, "%s    {\"name\": ", *first ? "" : ",\n");
    json_string(f, s->name);
    fprintf(f, ", \"file\": ");
    json_string(f, s->file);
    fprintf(f, ", \"size\": %u, \"registered\": %d, \"volatile\": %d, \"driver\": %d",
            s->size, sym_registered(seg, s), (seg->symFlags[i] & SF_VOLATILE) != 0,
            (seg->symFlags[i] & SF_DRIVER) != 0);
    *first = 0;
}

static FILE *open_report(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) fprintf(stderr, "[rollback] cannot write %s\n", path);
    return f;
}

static void write_scan_report(Rig *r, const char *path)
{
    RbSegment *seg = &r->seg;
    FILE *f = open_report(path);
    if (!f) return;
    fprintf(f, "{\n  \"mode\": \"scan\",\n  \"frames\": %u,\n  \"segment_bytes\": %zu,\n"
               "  \"unnamed_frames\": %u,\n  \"symbols\": [\n",
            r->framesScanned, seg->len, r->unnamedFrames);
    int first = 1;
    for (uint32_t i = 0; i < seg->nsyms; i++) {
        if (!r->symFrames[i]) continue;
        const RbSym *s = &seg->syms[i];
        size_t off = s->addr - (uintptr_t)seg->base, changed = 0;
        for (uint32_t k = 0; k < s->size; k++)
            if (BIT_GET(r->changedBits, off + k)) changed++;
        json_symbol_head(f, seg, i, &first);
        fprintf(f, ", \"bytes_changed\": %zu, \"frames_changed\": %u, \"first\": %u, \"last\": %u}",
                changed, r->symFrames[i], r->symFirst[i], r->symLast[i] - 1);
    }
    fprintf(f, "\n  ]\n}\n");
    fclose(f);
    fprintf(stderr, "[rollback] wrote %s\n", path);
}

static void write_check_report(Rig *r, const char *path)
{
    RbSegment *seg = &r->seg;
    FILE *f = open_report(path);
    if (!f) return;
    fprintf(f, "{\n  \"mode\": \"%s\",\n  \"window\": %u,\n  \"every\": %u,\n  \"checks\": %u,\n"
               "  \"failures\": %u,\n  \"registry_bytes\": %u,\n  \"symbols\": [\n",
            k_modeName[s_mode], s_window, s_every, r->checks, r->failures, r->simLen);
    int first = 1;
    for (uint32_t i = 0; r->haveSegment && i < seg->nsyms; i++) {
        if (!r->symDiverged[i] && !r->symUnrestored[i]) continue;
        json_symbol_head(f, seg, i, &first);
        fprintf(f, ", \"diverged_windows\": %u, \"changed_unrestored_windows\": %u}",
                r->symDiverged[i], r->symUnrestored[i]);
    }
    fprintf(f, "\n  ]\n}\n");
    fclose(f);
    fprintf(stderr, "[rollback] wrote %s\n", path);
}

static void write_discover_report(Rig *r, const char *path)
{
    RbSegment *seg = &r->seg;
    FILE *f = open_report(path);
    if (!f) return;
    fprintf(f, "{\n  \"mode\": \"discover\",\n  \"window\": %u,\n  \"every\": %u,\n"
               "  \"windows\": %u,\n  \"tests\": %u,\n  \"invalid_windows\": %u,\n"
               "  \"registry_bytes\": %u,\n  \"symbols\": [\n",
            s_window, s_every, r->checks, r->tests, r->failures, r->simLen);
    int first = 1;
    for (uint32_t i = 0; i < seg->nsyms; i++) {
        if (!r->dTested[i]) continue;
        json_symbol_head(f, seg, i, &first);
        fprintf(f, ", \"tested\": %u, \"affects_game\": %u, \"affects_any\": %u, \"crashed\": %u, "
                   "\"first_game_window\": %d, \"example_game\": ",
                r->dTested[i], r->dGame[i], r->dAny[i], r->dCrash[i], (int)r->dFirstGame[i] - 1);
        json_string(f, r->dExGame[i] ? seg->syms[r->dExGame[i] - 1].name : "");
        fprintf(f, ", \"example_any\": ");
        json_string(f, r->dExAny[i] ? seg->syms[r->dExAny[i] - 1].name : "");
        fputc('}', f);
    }
    fprintf(f, "\n  ]\n}\n");
    fclose(f);
    fprintf(stderr, "[rollback] wrote %s\n", path);
}

int Rollback_Finish(void)
{
    int code = 0;
    if (!R)
        return 0;

    if (s_tracePath) {
        FILE *f = fopen(s_tracePath, "w");
        if (f) {
            for (uint32_t i = 0; i < R->traceLen; i++)
                fprintf(f, "%u %016llx %016llx %016llx\n", i, (unsigned long long)R->traceSim[i],
                        (unsigned long long)R->traceView[i], (unsigned long long)R->traceObs[i]);
            fclose(f);
            fprintf(stderr, "[rollback] wrote %s (%u frames)\n", s_tracePath, R->traceLen);
        } else {
            fprintf(stderr, "[rollback] cannot write %s\n", s_tracePath);
            code = 1;
        }
    }
    if (R->unidentifiedFrames) {
        fprintf(stderr, "[rollback] FAIL: %u frame(s) held a pointer with no portable identity\n",
                R->unidentifiedFrames);
        code = 2;
    }
    if (s_wire && R->wirePackets)
        fprintf(stderr, "[rollback] wire: %u state packets; the first, standing alone, "
                        "%u bytes; %u deltas against the previous window start, %u..%u bytes, "
                        "%u on average (raw %u)\n",
                R->wirePackets, R->wireFirstBytes, R->wireDeltaPackets, R->wireDeltaMin, R->wireDeltaMax,
                R->wireDeltaPackets ? (uint32_t)(R->wireDeltaBytes / R->wireDeltaPackets) : 0, R->simLen);
    if (windowed() && R->phase != PH_IDLE)
        fprintf(stderr, "[rollback] the run ended inside the window starting at frame %u "
                        "(not counted)\n", R->winStart);

    switch (s_mode) {
    case ROLLBACK_SCAN: {
        uint32_t touched = 0, registered = 0;
        for (uint32_t i = 0; i < R->seg.nsyms; i++) {
            if (!R->symFrames[i]) continue;
            touched++;
            if (sym_registered(&R->seg, &R->seg.syms[i]) == 2) registered++;
        }
        fprintf(stderr, "[rollback] scan: %u frames, %u symbols written (%u of them registered), "
                        "unnamed bytes written in %u frames\n",
                R->framesScanned, touched, registered, R->unnamedFrames);
        if (s_reportPath)
            write_scan_report(R, s_reportPath);
        break;
    }
    case ROLLBACK_CHECK:
    case ROLLBACK_ORACLE:
        if (R->haveSegment && R->checks) {
            print_symbols(&R->seg, R->symDiverged, s_mode == ROLLBACK_CHECK,
                          "symbols that came out different, windows each", 40);
            if (s_mode == ROLLBACK_CHECK)
                print_symbols(&R->seg, R->symUnrestored, 1,
                              "unregistered symbols that changed in a window, windows each", 40);
        }
        fprintf(stderr, "[rollback] %s: %u windows of %u frames, %u failed -> %s\n",
                k_modeName[s_mode], R->checks, s_window, R->failures,
                R->checks == 0 ? "NOTHING TESTED" : (R->failures ? "FAIL" : "PASS"));
        if (s_reportPath)
            write_check_report(R, s_reportPath);
        if (R->checks == 0) code = 1;
        else if (R->failures) code = 2;
        break;
    case ROLLBACK_DISCOVER: {
        uint32_t game = 0, any = 0, tested = 0, unregisteredGame = 0;
        for (uint32_t i = 0; i < R->seg.nsyms; i++) {
            if (!R->dTested[i]) continue;
            tested++;
            if (R->dGame[i]) {
                game++;
                if (sym_registered(&R->seg, &R->seg.syms[i]) != 2) unregisteredGame++;
            } else if (R->dAny[i]) {
                any++;
            }
        }
        fprintf(stderr, "[rollback] discover: %u windows, %u leave-one-out tests over %u symbols: "
                        "%u change the game (%u not registered), %u change only other state, "
                        "%u change nothing; %u invalid window(s)\n",
                R->checks, R->tests, tested, game, unregisteredGame, any, tested - game - any,
                R->failures);
        if (s_reportPath)
            write_discover_report(R, s_reportPath);
        if (R->checks == 0) code = 1;
        else if (R->failures) code = 2;
        break;
    }
    default:
        break;
    }
    fflush(stderr);
    return code;
}

#endif /* SWOS_SDL_BUILD */
