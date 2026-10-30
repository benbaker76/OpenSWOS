#!/usr/bin/env python3
"""check_data_versions.py -- did OpenSWOS Setup's output change without its data version?

ASSETS.ini (include/assets_ini.h) gives every part the setup makes a DATA VERSION, and a
player is asked to install again only when a part they have was made at a lower one. That
works only if the number goes up whenever the part's files change. This runs the setup on
the reference copies of the game, fingerprints each part's files, and compares them with
tools/swosdata/data_versions.txt:

  same version, same files      fine
  same version, files changed   FAIL: raise that part's version in include/assets_ini.h
  higher version                fine once recorded: rerun with --update

  scripts/check_data_versions.py                      # check
  scripts/check_data_versions.py --update             # record the fingerprints now
  scripts/check_data_versions.py --scan DIR [--swos2020 DIR] [--swosdata PATH]

The reference copies (the PC game, both Amiga disks, the Jaguar cartridge, SWOS 2020) are
the maintainer's own -- by default the folders on this machine (--scan, --swos2020). A part
with no source here is reported and skipped. Run it after any change to tools/swosdata or
third_party/swosgfx, before a release. Exit status: 0 fine, 1 a part changed unversioned.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RECORD = os.path.join(REPO, "tools", "swosdata", "data_versions.txt")
DEFAULT_SCAN = "/mnt/d/Projects/SWOS/OpenSWOS/Data"
DEFAULT_2020 = "/mnt/c/Games/SWOS 2020"
SWOSDATA = [os.path.expanduser("~/build/openswos-setup/bin/swosdata"),
            os.path.join(REPO, "tools", "swosdata", "build", "swosdata")]


def read_record():
    rec = {}
    if os.path.exists(RECORD):
        for line in open(RECORD, encoding="utf-8"):
            if line.strip() and not line.startswith("#"):
                part, ver, digest, files = line.split()
                rec[part] = (int(ver), digest, int(files))
    return rec


def write_record(parts):
    with open(RECORD, "w", encoding="utf-8", newline="\n") as f:
        f.write("# OpenSWOS Setup's output per part, from the maintainer's reference copies:\n"
                "# part  data version  sha1 of its files (path + content)  files.\n"
                "# Written by scripts/check_data_versions.py --update; it fails when a part's\n"
                "# files change and include/assets_ini.h does not raise its version.\n")
        for part in sorted(parts):
            ver, digest, files = parts[part]
            f.write(f"{part} {ver} {digest} {files}\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--update", action="store_true", help="record the fingerprints")
    ap.add_argument("--scan", default=DEFAULT_SCAN, help="where the reference PC game, disks, cartridge are")
    ap.add_argument("--swos2020", default=DEFAULT_2020, help="a SWOS 2020 install")
    ap.add_argument("--swosdata", help="the swosdata program to run")
    args = ap.parse_args()

    tool = args.swosdata or next((p for p in SWOSDATA if os.path.exists(p)), None)
    if not tool:
        sys.exit("check_data_versions: no swosdata built (tools/swosdata/build.sh), or --swosdata")
    with tempfile.TemporaryDirectory(prefix="swos-dv-") as out:
        cmd = [tool, "--scan", args.scan, "--res", os.path.join(REPO, "bin"), "--out", out]
        if args.swos2020 and os.path.isdir(args.swos2020):
            cmd[1:1] = ["--swos2020", args.swos2020]
        run = subprocess.run(cmd, capture_output=True, text=True)
        if run.returncode != 0:
            sys.exit("check_data_versions: the setup failed:\n" + run.stdout + run.stderr)
        listing = subprocess.run([tool, "--list-parts", out], capture_output=True, text=True,
                                 check=True).stdout
        files = {}
        for line in listing.splitlines():
            part, ver, rel = line.split("\t")
            files.setdefault(part, (int(ver), []))[1].append(rel)
        now = {}
        for part, (ver, rels) in files.items():
            h = hashlib.sha1()
            for rel in sorted(rels):
                h.update(rel.encode() + b"\0")
                h.update(hashlib.sha1(open(os.path.join(out, rel), "rb").read()).digest())
            now[part] = (ver, h.hexdigest(), len(rels))

    rec = read_record()
    bad = 0
    for part in sorted(set(rec) | set(now)):
        if part == "world":     # (a world's package is copied as it is: its files are the package's)
            continue
        if part not in now:
            print(f"  {part:12} not made here (no source on this machine): not checked")
            continue
        ver, digest, n = now[part]
        if part not in rec:
            print(f"  {part:12} v{ver}: not recorded yet{'' if args.update else ' (--update)'}")
            continue
        rver, rdigest, rn = rec[part]
        if ver == rver and digest == rdigest:
            print(f"  {part:12} v{ver}: unchanged")
        elif ver == rver:
            print(f"  {part:12} v{ver}: FILES CHANGED ({rn} -> {n} files) -- raise its data version "
                  f"in include/assets_ini.h, then --update")
            bad += 1
        elif ver > rver:
            print(f"  {part:12} v{rver} -> v{ver}: {'recorded' if args.update else 'raised; --update to record'}")
        else:
            print(f"  {part:12} v{ver} is BELOW the recorded v{rver}: a data version never goes down")
            bad += 1
    if args.update and not bad:
        merged = dict(rec)
        merged.update(now)
        write_record(merged)
        print(f"check_data_versions: recorded {len(now)} part(s) in {os.path.relpath(RECORD, REPO)}")
    elif args.update:
        print("check_data_versions: not recorded -- fix the versions first")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
