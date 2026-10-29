#!/usr/bin/env python3
"""Drive the owned Amiga session through menus by button LABEL.

    python3 scripts/testing_support/amiga_goto.py challenges
    python3 scripts/testing_support/amiga_goto.py FRIENDLY EXIT --release

Each label is resolved on the screen that is open at that step (exactly one
visible, enabled row, surrounding spaces ignored), activated, and the screen
must change. A failure names the labels that were there. Starts from wherever
the stopped session is; a preset route checks it starts on the main menu.
The session stays stopped at the menu anchor (`--release` hands it over
running; `amiga_session.py pause` takes it back).
"""
import argparse
import json
import sys
import time

import amiga_benchmark as setup          # selects socket/scripts before the adapter
from testing_support.adapter import SwosStateAdapter
import amiga_session

ROUTES = {
    # main menu -> challenges hub -> the collection's list
    "challenges": ["CHALLENGES", "CHALLENGE COLLECTION"],
    "saved-positions": ["CHALLENGES", "SAVED POSITIONS"],
}


def labels(snap):
    return [(r.label or "").strip() for r in snap.entries
            if r.enabled and (r.label or "").strip()]


def goto(steps, require_main=False):
    a = SwosStateAdapter(proxy=str(setup.Path(setup.__file__).with_name("amiga_proxy.py")))
    a._domain = {}
    a.sw._is_sdl = False
    snap = a.observe(prime=False)
    if not snap.complete:
        raise RuntimeError(f"Session not stopped at a menu: {snap.errors} "
                           "(amiga_session.py status / pause)")
    if require_main and snap.screen != "SWOSMainMenuInit":
        raise RuntimeError(f"Route starts on the main menu, session is on {snap.screen}")
    report = [{"screen": snap.screen}]
    for label in steps:
        rows = [r for r in snap.entries if r.enabled and (r.label or "").strip() == label]
        if len(rows) != 1:
            raise RuntimeError(f"{label!r}: {len(rows)} matches on {snap.screen}; "
                               f"buttons: {labels(snap)}")
        before, t0 = snap.fingerprint, time.monotonic()
        a.activate(rows[0].ordinal, timeout=8)
        snap = a.observe(prime=False)
        if not snap.complete or snap.fingerprint == before:
            raise RuntimeError(f"No transition after {label!r}")
        step = {"action": label, "seconds": round(time.monotonic() - t0, 3),
                "screen": snap.screen, "buttons": labels(snap)}
        report.append(step)
        print(json.dumps(step), flush=True)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("steps", nargs="+", help=f"a preset ({', '.join(ROUTES)}) or button labels")
    p.add_argument("--release", action="store_true", help="hand the game over running afterwards")
    args = p.parse_args()
    preset = len(args.steps) == 1 and args.steps[0] in ROUTES
    steps = ROUTES[args.steps[0]] if preset else args.steps
    try:
        goto(steps, require_main=preset)
    except RuntimeError as e:
        print(f"FAILED: {e}", file=sys.stderr)
        return 1
    if args.release:
        print("[amiga] " + amiga_session.release())
    return 0


if __name__ == "__main__":
    sys.exit(main())
