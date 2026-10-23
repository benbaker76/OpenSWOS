#!/usr/bin/env python3
"""SWOS-specific PLUGIN for the gdb test harness.

This is the game-knowledge layer on top of the generic harness_lib.GdbHarness
(itself on top of gdb_proxy.py). Everything SWOS-specific lives here: the Menu
struct accessors, the menu-input injection point + direction encoding, the
framebuffer/palette geometry, and career save loading. Lifting the harness to
another game means swapping THIS file, not the framework. See docs/GDB_HARNESS.md.

Driving works on BOTH targets (SDL native + Amiga FS-UAE) because the menu
system is shared C and the FS-UAE stub is now write-capable (see
docs/GDB_HARNESS.md "FS-UAE write support"). The injection point is a single line in
the shared MenuProc, so one mechanism drives both.
"""
import os
import re
import subprocess
import time
from harness_lib import GdbHarness, _write_png

# Menu input-injection point: the no-op MenuInputInjectPoint() in MenuProc(),
# called _right after MenuCheckControls() (which sets the frame's input globals)
# and BEFORE the d7/onSelect dispatch and the nav read. Setting the input globals
# here replicates real input for the whole frame: _fire feeds the onSelect d7
# bitmask; _menuControlsDirection feeds the nav.
#
# This is a SYMBOL, not a line number, on purpose: a hardcoded `menu_system.c:NNN`
# silently broke whenever code above MenuProc moved (e.g. the i18n render hook
# pushed MenuCheckControls past the old inject line, so the injected _fire was
# overwritten and every career season-transition "stuck"). gdb resolves the
# symbol wherever it lands.
INJECT_LINE = "MenuInputInjectPoint"

# _menuControlsDirection encoding: (dir & 6) >> 1 indexes _nextEntryDirectionOffsets.
# Verified against the live table [2,1,3,0] + neighbor links.
DIR = {"_up": 0, "_right": 2, "_down": 4, "_left": 6}
DIR_GLOBAL = {"_up": "_up", "_right": "_right", "_down": "_down", "_left": "_left"}

MENU = "((Menu *)_currentMenu)->selectedEntry"
# Scratch global to stage a filename string (visible on both builds).
FILENAME_SCRATCH = "_menuSongData"

# Framebuffer geometry. SDL is 8bpp chunky (stride == width). The Amiga build
# is interleaved bitplanes (src/include/display.h): each scanline holds
# NUM_PLANES planes of PLANE_STRIDE bytes; pixel (x,y) bit p lives at
# base + y*PLANE_STRIDE*NUM_PLANES + p*PLANE_STRIDE + x/8, bit 7-(x%8).
SCREEN_W, SCREEN_H, SCREEN_STRIDE = 336, 272, 336
AMIGA_PLANE_STRIDE = 44


class SwosHarness:
    def __init__(self, proxy=None, timeout=8):
        self.h = GdbHarness(proxy, timeout) if proxy else GdbHarness(timeout=timeout)
        self._is_sdl = None

    # ---- target detection --------------------------------------------------
    def is_sdl(self):
        """SDL build exposes the SDL palette symbol; the Amiga build does not.
        The result is CACHED only on a CONFIDENT answer. A timed-out/empty
        response (possible when the daemon is under load) is retried, never
        cached — caching a false 'Amiga' here misroutes screencap() to the
        Amiga bitplane decoder (the 'dump _gamePalette' wrong-path bug)."""
        if self._is_sdl is None:
            for _ in range(3):
                r = self.h.cmd("print &s_palette", timeout=15)
                if "No symbol" in r:
                    self._is_sdl = False
                    break
                if "0x" in r:
                    self._is_sdl = True
                    break
                # ambiguous (timeout/partial) — retry rather than cache wrong
            if self._is_sdl is None:
                self._is_sdl = True   # this plugin's primary target is SDL
        return self._is_sdl

    # ---- being at the injection point -------------------------------------
    # ---- Amiga one-shot-port auto-recovery -------------------------------
    def amiga_up(self, fresh=False):
        """Bring up / recover the Amiga FS-UAE+gdb session via amiga_gdb.sh
        (no-op on SDL). `up` is idempotent: it REUSES a live healthy session
        instantly and does a full FS-UAE+daemon RESTART only if the one-shot
        debugger port was burned / the session died. fresh=True forces a clean
        restart. Returns True if the session came up ('OK')."""
        if self.is_sdl():
            return True
        sh = os.path.join(os.path.dirname(self.h.proxy), "amiga_gdb.sh")
        args = ["bash", sh, "up"] + (["--fresh"] if fresh else [])
        r = subprocess.run(args, capture_output=True, text=True, timeout=300)
        return "OK" in (r.stdout + r.stderr)

    def _amiga_session_dead(self):
        """True iff the Amiga daemon/target is DEFINITIVELY dead — safe to re-up:
        no socket, no ping, or gdb reports the remote target gone. A RUNNING
        target (HEALTH 'running (busy)') and a STOPPED one are both LIVE -> False,
        so a drive is never re-booted out from under itself on a false positive."""
        if not os.path.exists("/tmp/swos_gdb.sock"):
            return True
        if "pong" not in (self.h._simple("ping") or ""):
            return True
        h = self.h.health() or ""
        return any(s in h for s in ("not being run", "Remote connection closed",
                                    "Connection refused"))

    def _inferior_running(self):
        """True iff a live target process exists (has been `run`). A FRESH SDL
        daemon has LOADED the ELF but not started it, so `continue` silently
        no-ops — the recurring cold-start failure. Empirically grounded:
        not-booted `info inferiors` shows '<null>' (no process); booted shows
        'process NNNN'. See docs/GDB_HARNESS.md."""
        return "process " in self.h.cmd("info inferiors")

    def prime(self, timeout=120):
        """Ensure the target is STOPPED at the inject anchor (MenuInputInjectPoint),
        cold-BOOTING it first if it has never been `run`. Safe to call on a fresh
        daemon: every entry point (where/nav/_fire/load/…) routes here, so all of
        them are cold-start-safe — no separate boot() call required."""
        # Amiga: auto-recover a DEAD session (FS-UAE crash / one-shot port burned)
        # before driving, instead of hard-failing the run. Conservative — only
        # fires when the daemon is unreachable or gdb says the remote is gone,
        # never for a merely-running target. amiga_gdb.sh up reuses a healthy
        # session instantly, so this is ~free when the session is live.
        if not self.is_sdl() and self._amiga_session_dead():
            print("[harness] Amiga session dead/burned — recovering via "
                  "amiga_gdb.sh up", flush=True)
            self.amiga_up()
        # SDL cold-start: a fresh daemon loaded the ELF but never ran it, so
        # `continue` would no-op forever. boot() `run`s it and leaves it stopped
        # at the inject anchor on the main menu — exactly prime()'s post-state.
        if self.is_sdl() and not self._inferior_running():
            self.boot(timeout=max(timeout, 180))
            return
        # Dedup by the ACTUAL anchor symbol (INJECT_LINE == MenuInputInjectPoint),
        # which is what `info breakpoints` prints — not the old fragile line
        # number, which never matched and re-added a breakpoint every call.
        if INJECT_LINE not in self.h.cmd("info breakpoints"):
            self.h.break_at(INJECT_LINE)
        self.h.cmd("continue", timeout=3)
        self.h.waitstop(timeout)

    def ensure_stopped(self):
        if not self.h.is_stopped():
            self.prime()

    # ---- menu state (read-only) -------------------------------------------
    def ordinal(self):
        return self.h.int_value(f"{MENU}->ordinal")

    def num_entries(self):
        return self.h.int_value("((Menu *)_currentMenu)->numEntries")

    def action(self):
        return self.h.symbol(f"{MENU}->onSelect")

    def entry_action(self, i):
        return self.h.symbol(f"((MenuEntry *)CalcMenuEntryAddress({i}))->onSelect")

    def where(self):
        return {"ordinal": self.ordinal(), "action": self.action(),
                "num_entries": self.num_entries()}

    def enum(self):
        n = self.num_entries() or 0
        return [(i, self.entry_action(i)) for i in range(n)]

    # ---- input injection (drives SDL + Amiga) -----------------------------
    def nav(self, direction):
        """One directional step. direction in _up/_down/_left/_right."""
        self.prime()
        self.h.set_var("_menuControlsDirection", DIR[direction])
        self.h.set_var(DIR_GLOBAL[direction], 1)
        self.h.set_var("_fire", 0)
        self.h.cont(20)            # consume this frame; lands back on inject bp
        return self.where()

    def _fire(self):
        """Press _fire on the current entry (activates its onSelect). Sets BOTH
        _fire (d7 bit 0) and _shortFire (bit 5): most menu *selections* (CONTINUE
        buttons, etc.) gate on _shortFire via the entry's _controlMask, not _fire.

        The press is a SINGLE-FRAME PULSE: _fire/_shortFire are cleared after the
        frame. A *held* _fire mis-drives a nested modal entered by the press —
        e.g. firing CAREER opens _careerMenu (a ShowMenu do/while loop), and a
        still-set _fire makes _careerMenu immediately re-_fire/exit its default
        entry on the next frame (the _careerMenu 'bounce/hang' bug)."""
        self.prime()
        self.h.set_var("_fire", 1)
        self.h.set_var("_shortFire", 1)
        self.h.cont(20)
        self.h.set_var("_fire", 0)
        self.h.set_var("_shortFire", 0)
        return self.where()

    def combo(self, controls):
        """Hold several controls for one frame (e.g. ['_fire','_left'] -> OTHER
        nationality). controls = subset of _fire/_up/_down/_left/_right."""
        self.prime()
        if "_fire" in controls:
            self.h.set_var("_fire", 1)
            self.h.set_var("_shortFire", 1)
        for d in ("_up", "_down", "_left", "_right"):
            if d in controls:
                self.h.set_var(DIR_GLOBAL[d], 1)
                self.h.set_var("_menuControlsDirection", DIR[d])
        self.h.cont(20)
        return self.where()

    # ---- direct selection (no path-finding; uses a memory write) ----------
    def goto(self, symbol):
        """Move selection to the entry whose onSelect == symbol (no _fire)."""
        self.ensure_stopped()
        n = self.num_entries() or 0
        target = next((i for i in range(n) if self.entry_action(i) == symbol), None)
        if target is None:
            return None
        self.h.cmd(f"call (void)SetCurrentEntry({target})")
        return target if self.action() == symbol else None

    def select(self, symbol, tries=12):
        """goto(symbol) then _fire, RETRYING until the menu actually transitions
        (its onInit changes). The single-frame _fire() pulse is occasionally
        dropped — the input isn't present at the dispatch frame, so a one-shot
        _fire silently no-ops and the caller hangs. The season-loop drivers are
        robust precisely because they re-assert the press every iteration; this
        brings menu *selection* to the same robustness. Re-goto each try because
        a redraw can reset the selection. Returns True once onInit changes, False
        if it never transitions (entry missing / wrong screen)."""
        before = self._current_init()
        for _ in range(tries):
            if self.goto(symbol) is None:
                return False
            self._fire()
            if self._current_init() != before:
                return True
        return False

    # ---- label-driven selection (for dynamic lists) -----------------------
    # Some screens reuse ONE onSelect across many entries whose meaning is the
    # displayed STRING, not the callback — the career team picker (continents,
    # then countries, then divisions, then teams all share SelectTeamsCommon /
    # SelectLeaguesCommon / ChooseTeamsFriendlyCommon2). goto(symbol) can't
    # disambiguate those, so navigate by the entry's TEXT instead. A string
    # entry (content == ENTRY_STRING == 2) keeps its text pointer in `fg`.
    def entry_text(self, ord):
        """The visible label of entry `ord`, or None if it's invisible / not a
        string entry."""
        e = f"((MenuEntry*)CalcMenuEntryAddress({ord}))"
        if self.h.int_value(f"{e}->isInvisible"):
            return None
        if self.h.int_value(f"{e}->content") != 2:   # ENTRY_STRING
            return None
        m = re.search(r'"((?:[^"\\]|\\.)*)"', self.h.cmd(f"x/s (char*)({e}->fg)"))
        return m.group(1) if m else None

    def scan_text(self):
        """[(ordinal, label)] for every VISIBLE string entry of the current
        menu — the reliable way to 'see' a dynamic list without a screenshot.

        Batched: one gdb `while`-loop over all entries in a SINGLE round-trip,
        not ~3 per entry. A 91-entry team-select screen went from >80 s (which
        blew driver timeouts) to <1 s. Selects visible entries whose `fg` is a
        readable pointer (the displayed string); the content==2 gate used by
        entry_text() wrongly dropped the preset-chooser continent rows."""
        n = self.num_entries() or 0
        if not n:
            return []
        script = (
            "set $i = 0\n"
            # numEntries is a COUNT (DrawMenu iterates i < count,
            # src/menu_system.c); `<=` read one MenuEntry past the end.
            f"while $i < {n}\n"
            "  set $e = (MenuEntry*)CalcMenuEntryAddress($i)\n"
            "  if ($e->isInvisible == 0 && (unsigned long)$e->fg > 0x10000)\n"
            '    printf "SCAN|%d|%s|\\n", $i, (char*)$e->fg\n'
            "  end\n"
            "  set $i = $i + 1\n"
            "end\n"
        )
        path = "/tmp/harness_scan.gdb"
        with open(path, "w") as f:
            f.write(script)
        r = self.h.cmd(f"source {path}", timeout=30)
        out = []
        for m in re.finditer(r"SCAN\|(\d+)\|(.*?)\|", r):
            out.append((int(m.group(1)), m.group(2)))
        return out

    def find_text(self, label):
        """Ordinal of the visible entry whose label == `label` (case-insensitive,
        also matches on a leading-substring so 'NOTT' finds "NOTT'M FOREST"), or
        None."""
        want = label.upper()
        cands = self.scan_text()
        for i, t in cands:
            if t.upper() == want:
                return i
        for i, t in cands:
            if t.upper().startswith(want):
                return i
        return None

    def fire_ordinal(self, ordinal, timeout=20):
        """Select `ordinal` by memory write, then a single-frame _fire pulse.
        Use a long timeout on the press that triggers InitCareer (~30-60s)."""
        self.h.cmd(f"call (void)SetCurrentEntry({ordinal})")
        self.h.set_var("_fire", 1)
        self.h.set_var("_shortFire", 1)
        for v in ("_up", "_down", "_left", "_right"):
            self.h.set_var(v, 0)
        self.h.cont(timeout)
        self.h.set_var("_fire", 0)
        self.h.set_var("_shortFire", 0)

    def fire_text(self, label, timeout=20):
        """find_text(label) then fire_ordinal. Returns the ordinal, or None."""
        ordn = self.find_text(label)
        if ordn is None:
            return None
        self.fire_ordinal(ordn, timeout)
        return ordn

    # ---- screen capture (orientation only) --------------------------------
    def is_aga(self):
        """AGA build = 8 planes / 256 colours; OCS = 4 / 16. bitPlane4Base is
        an AGA-only symbol (#ifdef AGA in display.c)."""
        r = self.h.cmd("print &bitPlane4Base")
        return ("No symbol" not in r and "0x" in r)

    def screencap(self, out_png):
        if self.is_sdl():
            # The SDL render width is ADAPTIVE (widescreen): the frontbuffer
            # stride == the live _screen_width, not the 336 native. Reading it at
            # a fixed 336 skews every row diagonally. Query the real geometry
            # (fall back to the native constants if the symbols aren't present).
            w = self.h.int_value("_screen_width") or SCREEN_W
            hgt = self.h.int_value("_screen_height") or SCREEN_H
            return self.h.screencap(out_png, "_frontbufferBaseAltPtr",
                                    w, hgt, w,
                                    palette_expr="&s_palette[0]", pal_fmt="rgba")
        return self._screencap_amiga(out_png)

    def screencap_oriented(self, out_png):
        """Screenshot with the selection cursor pinned BRIGHT and blink-text
        visible, so the image reliably shows which entry is selected (the cursor
        normally pulses _kColorTableShine={2,2,1,1,7,7,1,1} via _menuCursorFrame and
        is faint on some frames). _menuCursorFrame=8 -> after the frame's ++ -> 9
        -> index (9&14)>>1=4 -> colour 7 (brightest). One input-free frame is
        consumed to re-render with the pinned values. Use this to ORIENT — see
        docs/GDB_HARNESS.md 'Navigating by screenshot'."""
        self.prime()
        self.h.set_var("_menuCursorFrame", 8)
        self.h.set_var("_menuTextBlinkCounter", 0)
        for v in ("_fire", "_shortFire", "_up", "_down", "_left", "_right"):
            self.h.set_var(v, 0)
        self.h.cont(20)            # re-render one frame with cursor pinned bright
        return self.screencap(out_png)

    def _screencap_amiga(self, out_png):
        """Decode the Amiga interleaved-bitplane display -> PNG. Handles AGA
        (8 planes/256 col) and OCS (4 planes/16 col). Menu use case: camera
        offsets are 0, so the displayed base is the flag-selected frontbuffer."""
        planes = 8 if self.is_aga() else 4
        ncolors = 256 if planes == 8 else 16
        w, h, ps = SCREEN_W, SCREEN_H, AMIGA_PLANE_STRIDE
        row_stride = ps * planes
        # Displayed base (matches UpdatePlanePtrsAndBlitParams selection).
        if self.h.int_value("_usePlaneOffsetFlag"):
            base = "_frontbufferMaskBuffer"
        elif self.h.int_value("_useAltBaseFlag"):
            base = "_frontbufferBaseAltPtr"
        else:
            base = "_frontbufferBasePtr"
        fb = self.h.dump_mem(base, row_stride * h, "/tmp/harness_amiga_fb.bin")
        # _gamePalette: big-endian uint16, 12-bit 0x0RGB -> 8-bit RGB (nibble*0x11)
        praw = self.h.dump_mem("&_gamePalette[0]", ncolors * 2,
                               "/tmp/harness_amiga_pal.bin")
        pal = []
        for i in range(ncolors):
            c = (praw[i * 2] << 8) | praw[i * 2 + 1]
            pal.append((((c >> 8) & 0xF) * 0x11, ((c >> 4) & 0xF) * 0x11,
                        (c & 0xF) * 0x11))
        rows = []
        for y in range(h):
            rb = y * row_stride
            row = bytearray()
            for x in range(w):
                col, bit = x >> 3, 7 - (x & 7)
                idx = 0
                for p in range(planes):
                    idx |= ((fb[rb + p * ps + col] >> bit) & 1) << p
                row += bytes(pal[idx])
            rows.append(row)
        _write_png(out_png, w, h, rows)
        nz = sum(1 for b in fb if b)
        return (f"wrote {out_png} {w}x{h} (Amiga {planes}-plane/{ncolors}col, "
                f"base={base}, {nz} non-zero bytes)")

    # ---- cold boot + fresh-career start (the NORMAL flow) -----------------
    def boot(self, timeout=180):
        """Cold-boot the inferior to the MAIN MENU. Assumes the daemon is _up in
        SDL mode with a HEADLESS env (SDL_VIDEODRIVER=dummy /
        SDL_AUDIODRIVER=dummy) + the season-loop flags
        (SWOS_TEST_NO_INJURIES_CARDS=1, SWOS_SAVE_FORMAT_JSON_VERIFY=0) — see
        docs/GDB_HARNESS.md 'Reliability'. Sets the inject breakpoint, (re)starts
        the inferior with `run`, and waits for the first MenuProc stop (the
        main menu). `set confirm off` + `kill` make a re-boot in the SAME daemon
        non-interactive (no 'restart from beginning?' prompt to hang on).
        Returns the active menu's onInit ('SWOSMainMenuInit' on success)."""
        self.h.cmd("set confirm off")
        self.h.cmd("kill")                     # no-op if nothing is running
        # TURBO: make the inferior CPU-bound rather than paced to a 50 Hz wall
        # clock. Set on the inferior's environment (inherited at `run`) so EVERY
        # harness boot gets it without the caller remembering an env var on the
        # daemon launch. This is the root-cause fix for cont-times ballooning to
        # 10-120 s under WSL2/gdb timer-thread starvation (see platform_sdl.c
        # s_harness_turbo); it also slashes the ~1-2 min loader-intro cold boot.
        self.h.cmd("set environment SWOS_HARNESS_TURBO 1")
        if INJECT_LINE not in self.h.cmd("info breakpoints"):
            self.h.break_at(INJECT_LINE)
        self.h.cmd("run", timeout=4)           # boot is slow; catch the stop below
        self.h.waitstop(timeout)
        return self._current_init()

    # Default fresh-career drill-_down through the team picker, BY LABEL:
    #   continent -> country -> league division -> club.
    FRESH_CAREER_PATH = ("EUROPE", "ENGLAND", "PREMIER LEAGUE", "ARSENAL")

    def start_fresh_career(self, path=None, init_timeout=120):
        """Start a brand-new career through the NORMAL menu flow (NO
        LoadLiverpool). Call boot() first; this must run at the MAIN MENU. It
        drives the real path verified 2026-06-19:
            CAREER -> CHOOSE TEAM AND START CAREER
              -> <continent> -> <country> -> <division> -> <club>   (path, by label)
              -> InitCareer(teamId) -> dismiss chairman memo(s) -> contest hub.
        The auto-generated manager name + default nationality are kept (a fresh
        career needs no text typed). Returns the hub onInit ('ContestMenuInit')
        on success, else None (printing the labels it actually saw).

        Why label-driven and not goto(onSelect): the picker reuses ONE callback
        across every continent/country/division/club; only the displayed STRING
        identifies a row. Matching by label (case-insensitive, leading-substring
        fallback) also survives data edits that renumber ordinals — exactly what
        we want when testing the data-jailbreak changes. See docs/GDB_HARNESS.md
        'Starting a fresh career'."""
        path = path or self.FRESH_CAREER_PATH
        self.prime()
        if self._current_init() != "SWOSMainMenuInit":
            print(f"start_fresh_career: not at main menu (at {self._current_init()})")
            return None
        if not self.select("CareerMenu"):
            print("start_fresh_career: CAREER entry not found")
            return None
        # CHOOSE TEAM AND START CAREER opens the team picker (SelectTeamsInit).
        # Retry until the menu transitions — the _fire pulse is occasionally
        # dropped (see select()); a one-shot _fire silently no-ops and hangs.
        before = self._current_init()
        opened = False
        for _ in range(12):
            if self.goto("ChooseTeamAndStartCareer") is None:
                print("start_fresh_career: CHOOSE TEAM entry not found")
                return None
            self._fire()
            if self._current_init() != before:
                opened = True
                break
        if not opened:
            print(f"start_fresh_career: team picker did not open (stuck at {before})")
            return None
        # Drill the picker by label, RETRYING each hop until the visible list (or
        # the menu init) actually changes — same dropped-pulse robustness. The
        # LAST hop fires the club, which runs InitCareer (slow — init_timeout).
        for n, label in enumerate(path):
            last = (n == len(path) - 1)
            before_list = [t for _, t in self.scan_text()]
            before_init = self._current_init()
            advanced = False
            for _ in range(12):
                ordn = self.find_text(label)
                if ordn is None:
                    if (self._current_init() != before_init or
                            [t for _, t in self.scan_text()] != before_list):
                        advanced = True   # already advanced (label consumed)
                        break
                    print(f"start_fresh_career: '{label}' not found; visible: "
                          f"{[t for _, t in self.scan_text()]}")
                    return None
                self.fire_ordinal(ordn, timeout=init_timeout if last else 20)
                if (self._current_init() != before_init or
                        [t for _, t in self.scan_text()] != before_list):
                    advanced = True
                    break
            if not advanced:
                print(f"start_fresh_career: '{label}' press did not advance (stuck)")
                return None
        # After the club press, InitCareer runs then stops at the chairman
        # memo's inject bp; ensure stopped, then dismiss the welcome memo(s).
        if not self.h.is_stopped():
            self.h.waitstop(init_timeout)
        for _ in range(6):
            if self._current_init() != self.MEMO_INIT:
                break
            self.h.set_var("_shortFire", 1)
            self.h.cont(20)
            self.h.set_var("_shortFire", 0)
        return self._current_init()

    # ---- season-loop driver -----------------------------------------------
    # Per-screen INPUT MODEL (reverse-engineered 2026-06-17; see docs/GDB_HARNESS.md
    # "Season loop"). The career advances through THREE kinds of screen, each
    # taking a DIFFERENT input. Using the wrong one is exactly what made the
    # driver appear to "wedge" — it was NEVER gdb / multithread instability:
    #
    #  1. Season TRANSITION (_seasonUpdateInProgress != 0): the cup/league result
    #     displays are AUTO-ADVANCE menus — DIYCupInit pre-selects entry-66
    #     (_controlMask 0xFFFF; see Career_AutoAdvanceCupMatches). Setting
    #     s_seasonSkipLatched=1 drains the WHOLE chain at once; the latch persists
    #     until _seasonUpdateInProgress drops to 0. DO NOT move the selection
    #     (that clobbers entry-66 and the modal sticks — the original bug). The
    #     ONLY screen in the chain that needs a keypress is the chairman memo
    #     (ChairmanMemosInit, a plain modal): _shortFire on its Continue button.
    #     The transition ends at the season increment (InitializeNewSeason).
    #
    #     The OLD driver free-ran straight to the increment line — which hangs
    #     forever at the chairman memo (a modal waiting for input). That hang,
    #     mistaken for a gdb/thread wedge, is the whole reason this rewrite
    #     exists. Skip-latch + a conditional memo breakpoint = no free-run into
    #     an input-waiting screen, no wedge, threads untouched.
    #
    #  2. In-season menus (memo / contest hub / match-day list): _shortFire.
    #
    #  3. The pre-match SQUAD screen (PlayMatchMenuInit): PlayMatchSelected
    #     gates on the RAW joystick _fire _pl1Fire (NOT the menu _shortFire). Set
    #     _pl1Fire=1 to start/sim the fixture (CPU-vs-CPU -> instant result).
    SEASON_INCR_LINE = "career_init.c:974"
    MEMO_INIT = "ChairmanMemosInit"
    SQUAD_INIT = "PlayMatchMenuInit"
    HUB_INIT = "CareerOverInit"         # season-end hub (CONTINUE CAREER)
    HUB_CONTINUE = "ContinueCareer"     # the CONTINUE CAREER entry's onSelect

    def _current_init(self):
        """onInit symbol of the active menu (identifies the screen). ONE read,
        O(1) — reliable, unlike _currentMenuPtr (which shifts in sub-menus)."""
        return self.h.symbol("((Menu *)_currentMenu)->onInit")

    def at_contest_menu(self):
        """True at the season-end hub (onInit == CareerOverInit)."""
        return self._current_init() == self.HUB_INIT

    # ---- interrupt fallback (only for an UNEXPECTED input-waiter) ----------
    def _gdb_pid(self):
        r = subprocess.run(["pgrep", "-f", "gdb .*bin/SWOS"],
                           capture_output=True, text=True)
        return r.stdout.split()[0] if r.stdout.strip() else None

    def _interrupt(self):
        """Stop a running inferior after a cont timed out (it hit a screen the
        driver did not anticipate). kill -INT on the gdb process is the only
        method that works through the daemon pipe — stdin Ctrl-C does not (no
        tty). A SAFETY NET for discovery; the proven transition path never needs
        it. Returns True if the inferior is stopped afterward."""
        pid = self._gdb_pid()
        if pid:
            subprocess.run(["kill", "-INT", pid])
            time.sleep(1.5)
        return self.h.is_stopped()

    # ---- the season transition (skip-latch; NO free-run into a modal) ------
    def drive_transition(self, max_stops=80, timeout=None):
        """Drive ONE season transition to the next season's increment. Assumes
        CONTINUE CAREER was just fired at the hub (or _seasonUpdateInProgress is
        already set). Returns the new _currentSeasonShowing, or None if it stuck
        on an unanticipated input-waiter (call stack printed to stdout).

        Mechanism (see the class comment): s_seasonSkipLatched drains the
        cup/league auto-advance displays; a CONDITIONAL breakpoint catches only
        chairman-memo frames (_shortFire dismisses them); a code breakpoint at the
        season increment ends the drive. No blind free-run, so no wedge — this is
        the proven-reliable core (season 3->4 verified, health OK throughout,
        threads untouched)."""
        h = self.h
        # Mode-aware per-continue timeout: a cycle-exact 68020 `continue` through
        # the season-transition screens runs far longer than a native SDL frame,
        # so the old SDL-tuned 14s falsely declared "stuck" while the Amiga was
        # still progressing (2026-06-28). Pick by target; callers can override.
        if timeout is None:
            timeout = 14 if self.is_sdl() else 90
        start = h.int_value("_currentSeasonShowing")
        h.set_var("s_seasonSkipLatched", 1)
        h.delete_breakpoints()
        h.break_at(self.SEASON_INCR_LINE)
        h.cmd(f'break {INJECT_LINE} '
              f'if ((Menu*)_currentMenu)->onInit == {self.MEMO_INIT}')
        for _ in range(max_stops):
            h.set_var("s_seasonSkipLatched", 1)    # keep latched across screens
            h.cont(timeout)
            if not h.is_stopped():                  # unanticipated waiter
                self._interrupt()
                print("drive_transition: stuck (unknown waiter)\n" + h.cmd("bt"))
                return None
            s = h.int_value("_currentSeasonShowing")
            if s is not None and s != start:
                return s                            # hit the season increment
            if self._current_init() == self.MEMO_INIT:
                h.set_var("_shortFire", 1)           # dismiss the chairman memo
        return None

    # ---- the in-season fixtures (auto-sim via teamControls=1) --------------
    def drive_inseason(self, max_steps=2000, timeout=None, stuck_limit=40,
                       sim_mode=True):
        """Drive the current season to the season-end hub (CareerOverInit).
        Returns True if the hub was reached.

        SIM MODE (default): set _careerTeam.team.teamControls = TEAM_COMPUTER(1)
        so the player's OWN fixtures auto-SIMULATE on _shortFire like the AI
        fixtures (the game computes a result; no squad, no real-time in-game
        match). A human-controlled club team (teamControls 2/3) instead PLAYS
        each fixture in the real-time in-game loop (>2 min each, never returning
        to the inject breakpoint until the clock runs out) — impractical to drive
        unattended. Sim mode is re-asserted every step.

        STUCK DETECTION keys on (matchesPlayedCount, currentRoundIndex,
        _currentSeasonShowing, _seasonFinished), NOT the screen — the league
        match-day keeps onInit=DIYLeagueTournamentInit + ordinal=3 constant
        across fixtures, so the old (screen, ordinal) key false-stopped while
        matches were in fact playing. matchesPlayedCount (DIYCompetitionEx 0x1BE)
        is a clean named field and a VALID progress signal on a CLEAN run.

        ⚠️ TEST FROM A COLD BOOT — DO NOT load_career() IN PLACE first.
        matchesPlayedCount is RUNTIME state of the active DIY competition and is
        NOT serialized in the career save, so load_career() does not reset it.
        Reloading a save into an inferior that already drove a career leaves the
        active competition with STALE values (e.g. 1614 from a prior drive) → the
        league screen runs on inconsistent state and the driver wanders. Always
        cold-boot + start_fresh_career() to exercise this.

        STATUS (2026-06-19): a clean sim-mode drive was PROGRESSING (mp climbing,
        a 4-division season is many fixtures) but was killed at ~30 min before it
        finished — full ENGLISH-LEAGUE completion to CareerOverInit is UNVERIFIED
        (not proven to reach the hub, but NOT shown blocked; the earlier 'wanders'
        finding was the reload-in-place confound above). The sim lever and the
        season TRANSITION path (drive_transition) work."""
        h = self.h
        # Mode-aware per-continue timeout (see drive_transition): FS-UAE matches
        # auto-sim far slower than native SDL, so use a longer wait on Amiga.
        if timeout is None:
            timeout = 25 if self.is_sdl() else 90
        h.delete_breakpoints()
        h.break_at(INJECT_LINE)

        def progress():
            # structural signal — does NOT include the (wandering) screen
            return (h.int_value("_diyCompetitionStart.matchesPlayedCount"),
                    h.int_value("_diyCompetitionStart.base.currentRoundIndex"),
                    h.int_value("_currentSeasonShowing"),
                    h.int_value("_seasonFinished"))

        last_prog, same = None, 0
        for _ in range(max_steps):
            if sim_mode:
                h.set_var("_careerTeam.team.teamControls", 1)  # auto-sim my fixtures
            h.cont(timeout)
            if not h.is_stopped():                  # a match/menu over-ran timeout
                self._interrupt()
                return False
            init = self._current_init()
            if init == self.HUB_INIT:
                return True
            prog = progress()
            same = same + 1 if prog == last_prog else 0
            last_prog = prog
            if same >= stuck_limit:
                print(f"drive_inseason: no structural progress for {stuck_limit} "
                      f"steps at {init} prog={prog} (see KNOWN LIMITATION)")
                return False
            if init == self.SQUAD_INIT:
                h.set_var("_pl1Fire", 1)             # fallback when sim_mode=False
            h.set_var("_shortFire", 1)              # advance menu / state machine
        return self.at_contest_menu()

    def advance_one_season(self):
        """From a season-end hub: _fire CONTINUE -> drive the transition to the
        season increment -> drive the new season's fixtures to the next hub.
        Returns the new _currentSeasonShowing (at the increment), or None."""
        if not self.at_contest_menu():
            return None
        # _fire CONTINUE CAREER (selections gate on _shortFire, bit 5)
        self.goto(self.HUB_CONTINUE)
        self.h.set_var("_shortFire", 1)
        s = self.drive_transition()
        if s is None:
            return None
        self.drive_inseason()
        return s

    def run_seasons(self, n):
        """Advance _up to n full seasons. Returns the list of season numbers
        reached (None marks a season that stuck). Drives to a season-end hub
        first."""
        self.prime()
        if not self.at_contest_menu():
            self.drive_inseason()
        out = []
        for _ in range(n):
            s = self.advance_one_season()
            out.append(s)
            if s is None or s == 19:   # 19 = retirement
                break
        return out

    # ---- career save loading (game's own native load; no UI nav needed) ---
    @staticmethod
    def _save_basename(filename):
        """The game's resolve_save_path() (swos_io.c) strips a 'drive:' prefix
        then prepends swos_save_dir() + '/'. So a caller passing 'save/X.CAR'
        (or the save dir + 'X.CAR') gets a doubled path -> fopen fails -> the load
        silently no-ops (rc 1; long masked by warm in-memory career state). Pass
        the bare basename; strip a leading 'save/' if a caller still includes it.

        ⚠️ SAVE DIR MOVED (2026-07-04): swos_save_dir() is now the per-user OS dir
        (~/.swos/save on Linux/WSL, Documents/SWOS/save on Windows, Application
        Support/SWOS on macOS) — NOT bin/save. So load_career('MILAN.CAR') resolves
        to ~/.swos/save/MILAN.CAR and returns rc 1 (silent) if the file isn't there.
        Copy test careers into the per-user dir first:
            cp bin/save/*.CAR ~/.swos/save/
        Verify the game's resolved dir with `cmd 'print (char*)swos_save_dir()'`.
        See docs/SAVE.md."""
        f = filename.replace("\\", "/")
        if f.lower().startswith("save/"):
            f = f[5:]
        return f

    # NOTE on timeouts: the inferior CALL of SaveJson_Load/WriteCareer parses/
    # emits JSON via mjson. On SDL that's a few seconds; on the EMULATED Amiga
    # (FS-UAE, cycle-exact 68020) it's 1-3 MINUTES. The daemon cmd returns as
    # soon as the call completes, so a high ceiling is safe on both — but it must
    # exceed the Amiga parse time or the proxy disconnects mid-call and wedges
    # the gdb client (looks like corruption; it's a timeout artifact). 300s.
    CALL_TIMEOUT = 300

    def _stage_filename(self, filename):
        """Stage a save basename into the scratch global as ONE bulk write
        (`set {char[N]}scratch = "..."`) — robust on the Amiga stub, where the
        per-byte write_bytes path (N separate M packets) is slow/flaky. Returns
        the staged basename."""
        name = self._save_basename(filename)
        n = len(name) + 1  # + NUL
        self.h.cmd(f'set {{char[{n}]}}{FILENAME_SCRATCH} = "{name}"')
        return name

    def load_career(self, filename):
        """Stage the basename into a scratch global and call the game's loader.
        Returns 0 on success (game convention: non-zero == failure; rc 1 usually
        means the file wasn't found in the per-user save dir — see _save_basename,
        `cp bin/save/*.CAR ~/.swos/save/`). NOTE: this only loads DATA; to enter
        the career UI from a cold boot call enter_career() afterwards (the game's
        LoadCareerFile path runs StartContest, which load_career does not)."""
        self.ensure_stopped()
        self._stage_filename(filename)
        r = self.h.cmd(
            f"call (int)SaveJson_LoadCareer((unsigned char *){FILENAME_SCRATCH}, 0, 0)",
            timeout=self.CALL_TIMEOUT)
        m = re.search(r"\$\d+\s*=\s*(-?\d+)", r)
        return int(m.group(1)) if m else None

    def save_career(self, filename, timeout=None):
        """Write the live career to a .CAR via the game's SaveJson_WriteCareer.
        Pass a basename ('X.CAR'); resolve_save_path prepends 'save/'. Returns
        the writer's rc (0 == success)."""
        self.ensure_stopped()
        self._stage_filename(filename)
        r = self.h.cmd(
            f"call (int)SaveJson_WriteCareer((unsigned char *){FILENAME_SCRATCH})",
            timeout=timeout or self.CALL_TIMEOUT)
        m = re.search(r"\$\d+\s*=\s*(-?\d+)", r)
        return int(m.group(1)) if m else None

    def enter_career(self):
        """Enter the loaded career's UI from a cold boot. The game's load path
        runs StartContest() -> ShowMenu(_contestMenu) (modal). Calling it with
        the inject breakpoint set makes the inferior call break INSIDE the now-
        active contest menu (the correct nested context the whole career runs
        in). Returns the active menu's onInit symbol afterwards."""
        if INJECT_LINE not in self.h.cmd("info breakpoints"):
            self.h.break_at(INJECT_LINE)
        self.h.cmd("call (void)StartContest()", timeout=40)
        return self._current_init()
