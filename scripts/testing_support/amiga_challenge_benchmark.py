#!/usr/bin/env python3
"""Measure the real menu path into a bundled challenge (default FINISH THE JOB;
another by name: amiga_challenge_benchmark.py "DERBY MIRACLE")."""
import json
import time
import amiga_benchmark as setup
import amiga_session
from testing_support.adapter import SwosStateAdapter


def run(challenge="FINISH THE JOB"):
    a = SwosStateAdapter(proxy=str(setup.Path(__file__).with_name("amiga_proxy.py")))
    a._domain = {}
    a.sw._is_sdl = False
    h = a.sw.h
    report = {"boot_seconds": json.loads((setup.OUT / "owner.json").read_text())["cold_seconds"],
              "steps": []}
    start = time.monotonic()
    snap = a.observe(prime=False)
    if snap.screen != "SWOSMainMenuInit":
        raise RuntimeError(f"Expected main menu, got {snap.screen}")
    # By name: a list reopened after BACK keeps BACK selected, so "the selected
    # entry" only meant the first challenge on a fresh boot.
    for label in ("CHALLENGES", "CHALLENGE COLLECTION", challenge):
        rows = [r for r in snap.entries if r.enabled and (r.label or "").strip() == label]
        if len(rows) != 1:
            raise RuntimeError(f"Cannot resolve {label}: {snap.to_dict()}")
        row = rows[0]
        t0 = time.monotonic()
        before = snap.fingerprint
        a.activate(row.ordinal, timeout=8)
        snap = a.observe(prime=False)
        if not snap.complete or before == snap.fingerprint:
            raise RuntimeError(f"No transition: {row.label}")
        step = {"action": row.label, "seconds": round(time.monotonic()-t0, 3),
                "screen": snap.screen}
        report["steps"].append(step)
        print(json.dumps(step), flush=True)
    rows = [r for r in snap.entries if r.enabled and (r.label or "").strip() == "PLAY"]
    if len(rows) != 1:
        raise RuntimeError(f"PLAY missing: {snap.to_dict()}")
    report["menu_seconds"] = round(time.monotonic()-start, 3)
    t0 = time.monotonic()
    h.cmd("tbreak ChallengeProgress_PositionLoaded")
    a.activate(rows[0].ordinal, timeout=30)
    pc = h.cmd("info symbol $pc")
    if "ChallengeProgress_PositionLoaded" not in pc:
        raise RuntimeError(f"Challenge did not load: {pc}")
    h.cmd("tbreak GameLoop_Frame")
    h.cont(10)
    if "GameLoop_Frame" not in h.cmd("info symbol $pc"):
        raise RuntimeError("Loaded position did not reach match loop")
    report["load_seconds"] = round(time.monotonic()-t0, 3)
    report["post_boot_seconds"] = round(time.monotonic()-start, 3)
    report["total_seconds"] = round(report["boot_seconds"]+report["post_boot_seconds"], 3)
    report["teams"] = a.read_match_teams()
    report["status"] = "challenge_loaded_at_first_match_frame"
    # Let the game actually render and play: breakpoints off, background resume
    # (`amiga_session.py pause` can take it back).
    amiga_session.release()
    (setup.OUT / "challenge-benchmark.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2), flush=True)


if __name__ == "__main__":
    import sys
    run(*sys.argv[1:2])
