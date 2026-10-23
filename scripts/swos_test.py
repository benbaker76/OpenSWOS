#!/usr/bin/env python3
"""swos_test.py — flow-testing CLI (docs/TESTING_FLOWS_DESIGN.md).

Implemented: inspect (delivery step 1 — the structured snapshot).
plan / run arrive with later delivery steps and report themselves as
unsupported capabilities rather than pretending.

  python3 scripts/swos_test.py inspect [--json] [--no-prime] [--resume]

The target is left STOPPED at the observed boundary by default (the design's
rule): a halted game window looks hung but is merely paused under gdb.
--resume releases it after the observation.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from testing_support.snapshot import Snapshot  # noqa: E402

CTRL_BITS = [(0x1, "fire"), (0x20, "sfire"), (0x40, "L"), (0x80, "R"),
             (0x100, "U"), (0x200, "D")]


def _mask_str(mask):
    if mask == 0xFFFF:
        return "ALL"
    bits = [n for b, n in CTRL_BITS if mask & b]
    return "+".join(bits) if bits else f"{mask:#x}"


def print_snapshot(s: Snapshot):
    print(f"phase:       {s.phase}")
    print(f"screen:      {s.screen or '?'} ({s.screen_addr:#x})")
    sel = "-" if s.selected_ordinal is None else s.selected_ordinal
    print(f"selected:    {sel} ({s.selected_callback or 'no onSelect'})")
    print(f"entries:     {len(s.entries)} visible / {s.num_entries} total")
    if s.entries:
        print(f"  {'ord':>4} {'en':2} {'mask':>6} {'k':1} "
              f"{'callback':<28} label")
        for e in s.entries:
            label = (e.label or "").replace("\n", "\\n")
            print(f"  {e.ordinal:>4} {'y' if e.enabled else 'N':2} "
                  f"{_mask_str(e.control_mask):>6} {e.label_kind:1} "
                  f"{(e.callback or '-'):<28} {label}")
    if s.domain:
        print("domain:      " +
              " ".join(f"{k}={v}" for k, v in sorted(s.domain.items())))
    print(f"fingerprint: {s.fingerprint}  (seq {s.seq}, "
          f"complete={s.complete})")
    if s.stats:
        print("stats:       " +
              " ".join(f"{k}={v}" for k, v in sorted(s.stats.items())))
    for err in s.errors:
        print(f"error:       {err}")


def cmd_inspect(args):
    from testing_support.adapter import SwosStateAdapter
    adapter = SwosStateAdapter(proxy=args.proxy, timeout=args.timeout)
    snap = adapter.observe(prime=not args.no_prime)

    if args.json:
        print(json.dumps(snap.to_dict(), indent=2))
    else:
        print_snapshot(snap)

    if args.resume:
        adapter.resume()
        print("target resumed")
    elif snap.phase != "unknown" or snap.complete:
        print("TARGET STOPPED at the menu inject anchor — the game window is "
              "paused, not hung.")
        print("resume with: python3 scripts/swos_test.py inspect --no-prime "
              "--resume   (or the harness CLI's cont)")
    return 0 if not snap.errors else 1


def _load_request(args):
    with open(args.request, encoding="utf-8") as f:
        return json.load(f)


def cmd_plan(args):
    from testing_support.goals import GoalError
    from testing_support.runner import make_plan, _strip_fns
    try:
        plan = make_plan(_load_request(args),
                         catalog_paths=args.catalog or None)
    except GoalError as e:
        print(json.dumps(e.to_dict(), indent=2))
        return 2
    plan["steps"] = _strip_fns(plan["steps"])
    print(json.dumps(plan, indent=2))
    return 0


def cmd_run(args):
    from testing_support.adapter import SwosStateAdapter
    from testing_support.runner import run_request, save_report
    adapter = SwosStateAdapter(proxy=args.proxy, timeout=args.timeout)
    report = run_request(_load_request(args), adapter,
                         catalog_paths=args.catalog or None)
    if args.report:
        save_report(report, args.report)
        print(f"report written: {args.report}")
    print(json.dumps({k: report.get(k) for k in
                      ("status", "detail", "final_screen",
                       "verified_career_team", "verified_match",
                       "totals")}, indent=2))
    if report.get("target_stopped"):
        print("TARGET STOPPED at the final verified boundary — the game "
              "window is paused, not hung. Resume: swos_test.py inspect "
              "--no-prime --resume")
    return 0 if report.get("status") == "success" else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    i = sub.add_parser("inspect", help="Observe the stopped target; print a "
                                       "structured snapshot.")
    i.add_argument("--json", action="store_true", help="machine-readable output")
    i.add_argument("--no-prime", action="store_true",
                   help="never touch execution; fail if the target is not "
                        "already stopped")
    i.add_argument("--resume", action="store_true",
                   help="release the target after observing")
    i.add_argument("--proxy", default=None, help="gdb_proxy.py path override")
    i.add_argument("--timeout", type=int, default=8)
    i.set_defaults(fn=cmd_inspect)

    p = sub.add_parser("plan", help="Resolve + plan a request (no execution).")
    p.add_argument("--request", required=True, help="goal JSON file")
    p.add_argument("--catalog", action="append",
                   help="catalog root(s); default: assets folder, bin/data_json")
    p.set_defaults(fn=cmd_plan)

    r = sub.add_parser("run", help="Execute a request; write the report.")
    r.add_argument("--request", required=True)
    r.add_argument("--report", help="write the full JSON report here")
    r.add_argument("--catalog", action="append")
    r.add_argument("--proxy", default=None)
    r.add_argument("--timeout", type=int, default=8)
    r.set_defaults(fn=cmd_run)

    args = ap.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
