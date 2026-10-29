#!/usr/bin/env python3
"""SwosStateAdapter — live state observation over the existing gdb harness.

The adapter owns everything SWOS- and debugger-specific about OBSERVING:
one batched script per snapshot (snapshot.py builds and parses it), a
one-time per-session probe of optional domain symbols, request counting and
timing. It builds on SwosHarness (scripts/swos_plugin.py), so priming /
cold-boot / Amiga session recovery are the plugin's proven paths, and one
mechanism serves both targets.

Performance contract (docs/TESTING_FLOWS_DESIGN.md): an ordinary observation
is ONE `source` round trip (plus, once per session, the domain probe and any
priming). Request counts and elapsed time ride on every Snapshot in
`.stats` so a regression to per-entry reads is visible immediately.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from testing_support.snapshot import (DOMAIN_CANDIDATES, Snapshot,
                                      build_observe_script, make_nonce,
                                      parse_observe_output)

# Where the observe script is written (this tool's filesystem) and how the
# gdb PROCESS names the same file. They differ when the daemon runs a NATIVE
# Windows gdb from Cygwin (the Amiga session): the tool writes
# /cygdrive/c/...­, gdb reads C:/... . Same file, two spellings.
SCRIPT_PATH = os.environ.get("SWOS_TEST_SCRIPT", "/tmp/swos_observe.gdb")
SCRIPT_PATH_GDB = os.environ.get("SWOS_TEST_SCRIPT_GDB", SCRIPT_PATH)


class SwosStateAdapter:
    def __init__(self, plugin=None, proxy=None, timeout=8):
        if plugin is None:
            from swos_plugin import SwosHarness
            plugin = SwosHarness(proxy=proxy, timeout=timeout) if proxy \
                else SwosHarness(timeout=timeout)
        self.sw = plugin
        # Direct-socket transport (SWOS_TEST_FAST=0 restores the subprocess
        # client): a Cygwin subprocess spawn costs ~0.1-0.2 s PER gdb command
        # and dominated every run.
        if os.environ.get("SWOS_TEST_FAST", "1") != "0":
            from testing_support.fastproxy import FastGdb
            self.sw.h = FastGdb(proxy=self.sw.h.proxy, timeout=timeout)
        self._seq = 0
        self._domain = None          # probed subset of DOMAIN_CANDIDATES
        self._requests = 0
        self.in_match = False
        self.last_career_team = None
        self.verified_match = None
        self.verified_season = None
        self._wrap_counting()

    # ---- request counting ---------------------------------------------------
    def _wrap_counting(self):
        h = self.sw.h
        orig_cmd, orig_simple = h.cmd, h._simple

        def cmd(command, timeout=None):
            self._requests += 1
            return orig_cmd(command, timeout)

        def simple(*args):
            self._requests += 1
            return orig_simple(*args)

        h.cmd, h._simple = cmd, simple

    # ---- one-time symbol probe ----------------------------------------------
    def probe_domain(self):
        """Which optional domain globals exist in THIS build. Probed once per
        session: a missing symbol inside the sourced observe script would
        abort it mid-way, so absent ones are left out of the script."""
        if self._domain is None:
            self._domain = {}
            for name, (sym, expr, fmt) in DOMAIN_CANDIDATES.items():
                r = self.sw.h.cmd(f"print &{sym}")
                if "No symbol" not in r and "0x" in r:
                    self._domain[name] = (expr, fmt)
        return self._domain

    # ---- observation ---------------------------------------------------------
    def observe(self, prime=True) -> Snapshot:
        """One structured snapshot of the stopped target. With prime=True the
        target is brought to the menu inject anchor first (cold-booting if it
        was never run — the plugin's proven path); with prime=False a
        not-stopped target yields an explicit unknown-phase snapshot instead
        of touching execution."""
        t0 = time.monotonic()
        req0 = self._requests
        self._seq += 1

        if prime:
            self.sw.ensure_stopped()
        elif not self.sw.h.is_stopped():
            snap = parse_observe_output("", "x", seq=self._seq)
            snap.errors = ["target not stopped (prime disabled)"]
            snap.stats = {"requests": self._requests - req0,
                          "elapsed_s": round(time.monotonic() - t0, 3)}
            return snap
        t_prime = time.monotonic()

        nonce = make_nonce()
        script = build_observe_script(nonce, self.probe_domain())
        with open(SCRIPT_PATH, "w") as f:
            f.write(script)
        out = self.sw.h.cmd(f"source {SCRIPT_PATH_GDB}", timeout=30)
        snap = parse_observe_output(out, nonce, seq=self._seq)
        # A Cygwin/SDL boot can report an early SPURIOUS stop inside the C
        # runtime (cleanly stopped, but not at the anchor, no menu yet).
        # Re-anchor: continue to the inject breakpoint and look again.
        reanchor = 3
        while (prime and snap.screen is None and reanchor > 0
               and not snap.errors):
            reanchor -= 1
            self.sw.h.cmd("continue", timeout=3)
            self.sw.h.waitstop(120)
            out = self.sw.h.cmd(f"source {SCRIPT_PATH_GDB}", timeout=30)
            snap = parse_observe_output(out, nonce, seq=self._seq)
        snap.stats = {
            "requests": self._requests - req0,
            "elapsed_s": round(time.monotonic() - t0, 3),
            "prime_s": round(t_prime - t0, 3),
            "observe_s": round(time.monotonic() - t_prime, 3),
        }
        return snap

    # ---- acting (one bounded pulse each; the EXECUTOR owns retries) ----------
    # Selection is a MEMORY WRITE, never an inferior call: SetCurrentEntry()
    # is exactly `menu->selectedEntry = entry` (src/menu_system.c:261), and
    # the FS-UAE stub executes writes but not gdb inferior calls — a call
    # silently fired the OLD selection on the Amiga.
    _SELECT = ("set var ((Menu *)_currentMenu)->selectedEntry = "
               "(MenuEntry *)((char *)_currentMenu + "
               "(sizeof(void *) == 8 ? 38 : 22) + "
               "{ordinal} * (sizeof(void *) == 8 ? 76 : 56))")

    def select_entry(self, ordinal):
        self.sw.h.cmd(self._SELECT.format(ordinal=ordinal))

    def activate(self, ordinal, timeout=20):
        """Select `ordinal` by memory write, then one fire pulse."""
        h = self.sw.h
        if os.environ.get("SWOS_TEST_BATCH", "1") == "1":
            # A sourced script runs through continue, stops at the existing
            # anchor, then releases the pulse. One host request, same game path.
            path = SCRIPT_PATH + ".action"
            commands = [self._SELECT.format(ordinal=ordinal)]
            for name, value in (("_fire", 1), ("_shortFire", 1)):
                commands.append(f"set var *(unsigned short *)&{name} = {value}")
            for name in ("_up", "_down", "_left", "_right"):
                commands.append(f"set var *(unsigned char *)&{name} = 0")
            commands += ["continue", "set var *(unsigned short *)&_fire = 0",
                         "set var *(unsigned short *)&_shortFire = 0",
                         'printf "ACTION_DONE\\n"']
            with open(path, "w") as stream:
                stream.write("\n".join(commands) + "\n")
            out = h.cmd(f"source {SCRIPT_PATH_GDB}.action", timeout=timeout)
            if "ACTION_DONE" not in out:
                raise RuntimeError(f"Input batch did not complete: {out[-1000:]}")
            return
        self.select_entry(ordinal)
        h.set_var("_fire", 1)
        h.set_var("_shortFire", 1)
        for v in ("_up", "_down", "_left", "_right"):
            h.set_var(v, 0)
        h.cont(timeout)
        h.set_var("_fire", 0)
        h.set_var("_shortFire", 0)

    def pulse_short_fire(self, timeout=20):
        """One _shortFire frame (modal Continue buttons, memo dismissal)."""
        h = self.sw.h
        h.set_var("_shortFire", 1)
        h.cont(timeout)
        h.set_var("_shortFire", 0)

    def pulse_raw_fire(self, timeout=20):
        """One RAW joystick fire frame (_pl1Fire) — the pre-match squad
        screen's PlayMatchSelected gates on this, not the menu _shortFire."""
        h = self.sw.h
        h.set_var("_pl1Fire", 1)
        h.cont(timeout)
        h.set_var("_pl1Fire", 0)

    # ---- domain verification reads -------------------------------------------
    @staticmethod
    def _gdb_char_array(raw):
        """gdb prints an int8_t[] as '"ARSENAL\\000\\000...", <repeats>' —
        take the quoted part up to the first embedded NUL."""
        import re
        m = re.search(r'"((?:[^"\\]|\\.)*)"', raw or "")
        return m.group(1).split("\\000")[0] if m else ""

    def read_career_team(self) -> dict:
        """The ACTUAL career club identity from _careerTeam (TeamFileEx)."""
        h = self.sw.h
        base = "((TeamFileEx *)&_careerTeam)->team"
        return {
            "teamName": self._gdb_char_array(h.value(f"{base}.teamName")),
            "countryNumber": h.int_value(f"{base}.countryNumber"),
            "globalTeamNumber": h.int_value(f"{base}.globalTeamNumber"),
        }

    def read_selected_teams(self, limit=24) -> list:
        """The selected-teams records (name, control, country) — what team
        selection ACTUALLY wrote, for effect-aware control setting. ONE
        batched round trip: a season pre-selects a whole league (10+ teams).
        (_selectedTeams is the backing array; _selTeamsPtr is a C macro over
        it that gdb cannot see.)"""
        from testing_support.snapshot import make_nonce
        nonce = make_nonce()
        m = f"@{nonce}@"
        script = (
            "set $tn = (int)_numSelectedTeams\n"
            f"if ($tn > {limit})\n"
            f"  set $tn = {limit}\n"
            "end\n"
            "set $ti = 0\n"
            "while $ti < $tn\n"
            "  set $tt = &((TeamFileEx *)_selectedTeams)[$ti].team\n"
            f'  printf "{m}TM|%d|%d|", $tt->teamControls, $tt->countryNumber\n'
            '  printf "%s", (char *)$tt->teamName\n'
            f'  printf "{m}EOL\\n"\n'
            "  set $ti = $ti + 1\n"
            "end\n"
            f'printf "{m}END\\n"\n')
        with open(SCRIPT_PATH, "w") as f:
            f.write(script)
        out = self.sw.h.cmd(f"source {SCRIPT_PATH_GDB}", timeout=30)
        import re
        rows = []
        for g in re.finditer(
                re.escape(m) + r"TM\|(\d+)\|(\d+)\|(.*?)" + re.escape(m) + "EOL",
                out, re.DOTALL):
            rows.append({"teamName": g.group(3),
                         "teamControls": int(g.group(1)),
                         "countryNumber": int(g.group(2))})
        return rows

    def identity(self) -> dict:
        """Build identity for the report (Build_Id/Build_Platform)."""
        import re
        h = self.sw.h
        out = {}
        for k, fn in (("build_id", "Build_Id"), ("platform", "Build_Platform")):
            r = h.value(f"(const char *) {fn}()")
            m = re.search(r'"((?:[^"\\]|\\.)*)"', r or "")
            out[k] = m.group(1) if m else None
        out["target"] = "sdl" if self.sw.is_sdl() else "amiga"
        return out

    # ---- match entry (the GameLoop_Frame boundary) -----------------------------
    def enter_match(self, ordinal, timeout=120):
        """Fire the squad screen's PLAY/COACH MATCH entry — the menu fire AND
        the raw _pl1Fire in the SAME frame (PlayMatchSelected gates on the
        raw stick) — with the GameLoop_Frame breakpoint installed FIRST.
        Returns the match team list at the first-frame stop, or None if the
        boundary was never reached. The target is left stopped there."""
        h = self.sw.h
        h.break_at("GameLoop_Frame")
        self.select_entry(ordinal)
        for v in ("_fire", "_shortFire", "_pl1Fire"):
            h.set_var(v, 1)
        h.cont(timeout)
        for v in ("_fire", "_shortFire", "_pl1Fire"):
            h.set_var(v, 0)
        for _ in range(3):
            pc = h.value("$pc") or ""
            if "GameLoop_Frame" in pc:
                self.in_match = True
                return self.read_match_teams()
            h.waitstop(timeout)
        return None

    def read_match_teams(self) -> list:
        """Identity + ownership of both match sides at a match-loop stop.
        owner = playerCoachNumber or playerNumber (MatchStep_TeamOwner);
        0 means the computer plays that side."""
        h = self.sw.h
        out = []
        for side, pos in (("_topTeamData", "top"), ("_bottomTeamData", "bottom")):
            name = self._gdb_char_array(
                h.value(f"((TeamGame *){side}.inGameTeamPtr)->teamName"))
            pcn = h.int_value(f"{side}.playerCoachNumber") or 0
            pn = h.int_value(f"{side}.playerNumber") or 0
            out.append({"side": pos, "teamName": name,
                        "owner": pcn if pcn else pn})
        return out

    # ---- lifecycle -----------------------------------------------------------
    def reboot(self):
        """EXPLICIT fresh boot to the main menu (never done implicitly — a
        session holding user progress is never silently rebooted). Returns
        the first snapshot of the fresh session."""
        self.sw.boot()
        return self.observe()

    def resume(self, timeout=20):
        """Explicitly release a stopped target for interactive use."""
        return self.sw.h.cont(timeout)

    @property
    def requests(self):
        return self._requests
