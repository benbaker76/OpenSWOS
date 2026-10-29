#!/usr/bin/env python3
"""Time main -> Friendly -> Exit on the owned, already-booted Amiga session."""
import json
import os
from pathlib import Path
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts"))
OUT = REPO / "build/amiga-ui"
os.environ["SWOS_GDB_SOCKET"] = "/tmp/swos-amiga-ui.sock"
os.environ["SWOS_TEST_SCRIPT"] = str(OUT / "observe.gdb")
import subprocess
os.environ["SWOS_TEST_SCRIPT_GDB"] = subprocess.check_output(
    ["cygpath", "-am", str(OUT / "observe.gdb")], text=True).strip()
from testing_support.adapter import SwosStateAdapter


def run():
    adapter = SwosStateAdapter(proxy=str(Path(__file__).with_name("amiga_proxy.py")))
    adapter._domain = {}  # This benchmark only needs menu state, no season globals.
    adapter.sw._is_sdl = False
    reports = []
    try:
        for batch in (False, True):
            os.environ["SWOS_TEST_BATCH"] = str(int(batch))
            for iteration in range(3):
                t0, req0 = time.monotonic(), adapter.requests
                before = adapter.observe(prime=False)
                if not before.complete or before.screen != "SWOSMainMenuInit":
                    raise RuntimeError(f"Expected main menu: {before.to_dict()}")
                stages = []
                for label in ("FRIENDLY", "EXIT"):
                    rows = [r for r in before.entries if r.enabled and (r.label or "").strip() == label]
                    if len(rows) != 1:
                        raise RuntimeError(f"No unique {label}: {before.to_dict()}")
                    start = time.monotonic()
                    adapter.activate(rows[0].ordinal, timeout=5)
                    acted = time.monotonic()
                    after = adapter.observe(prime=False)
                    if not after.complete or after.screen == before.screen:
                        raise RuntimeError(f"No transition after {label}: {after.to_dict()}")
                    stages.append({"action": label, "act_s": round(acted-start, 3),
                                   "observe_s": round(time.monotonic()-acted, 3),
                                   "screen": after.screen})
                    before = after
                if before.screen != "SWOSMainMenuInit":
                    raise RuntimeError("Route did not return to main menu")
                report = {"batch": batch, "iteration": iteration+1,
                          "seconds": round(time.monotonic()-t0, 3),
                          "requests": adapter.requests-req0, "stages": stages}
                reports.append(report)
                print(json.dumps(report), flush=True)
    finally:
        (OUT / "benchmark.json").write_text(json.dumps(reports, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(run())
