#!/usr/bin/env python3
"""Decode a .CHL challenge file's header and setup block (docs/CHALLENGE.md).

    python3 scripts/chl_dump.py <file.CHL> [...]        one report per file
    python3 scripts/chl_dump.py --diff <a.CHL> <b.CHL>  field-level comparison

The state blob (SimState_SavePortable) is NOT decoded here -- its layout is the
build's state registry, not a stable format; this script covers the parts that
are: the 96-byte header and the setup block (src/challenge.c's table). It is the
inspection half of the challenge generator's A/B check: generate a position,
dump it beside a live capture of the same position, and every header/setup
field but frame/seed/hash/build should match.
"""
import struct
import sys

# s_scalars in src/challenge.c, in table order. Width only matters in memory;
# the file widens everything to u16.
SCALARS = [
    "_pitchTypeOrSeason", "_friendlyPitchType", "_friendlySeason", "_pitchType",
    "_gameLength", "_autoReplays", "_autoSaveHighlights",
    "_minSubstitutes", "_maxSubstitutes", "_gameMaxSubstitutes",
    "_gameType", "_trainingGame", "_isCareer", "_twoPlayers", "_allPlayerTeamsEqual",
    "_pl1Coach", "_pl2Coach",
    "_topTeamPlayerNo", "_topTeamCoachNo", "_bottomTeamPlayerNo", "_bottomTeamCoachNo",
    "_pl1Tactics", "_pl2Tactics",
    "_team1SelectedPlayerIndex", "_team2SelectedPlayerIndex",
    "_randomSeed", "_currentSeasonShowing", "_newRandomSeedCopy", "_viewRandomPhase",
    "_extraTimeState", "_penaltiesState", "_secondLeg", "_playing2ndGame",
    "_team1GoalsFirstLeg", "_team2GoalsFirstLeg",
]

RULES = {0x01: "DOS_GAMEPLAY", 0x02: "DOS_VIEW", 0x04: "NO_CARDS"}

TEAM_BYTES = 684
TACTIC_BYTES = 370
NUM_TACTICS = 6


def cstr(b):
    return b.split(b"\0")[0].decode("latin1")


def parse(path):
    d = open(path, "rb").read()
    if len(d) < 96 or d[0:4] != b"SWCH":
        raise ValueError(f"{path}: not a challenge file (no SWCH header)")
    out = {"path": path, "size": len(d)}
    (out["version"], out["flags"]) = struct.unpack(">HH", d[4:8])
    (out["layout_hash"], out["state_hash"]) = struct.unpack(">QQ", d[8:24])
    (out["frame"], out["setup_len"], out["state_len"]) = struct.unpack(">III", d[24:36])
    (out["sim_width"], out["pitch"]) = struct.unpack(">HH", d[36:40])
    out["rules"], out["human_side"] = d[40], d[41]
    (out["minute"], out["goals1"], out["goals2"]) = struct.unpack(">HHH", d[42:48])
    out["team1"], out["team2"] = cstr(d[48:64]), cstr(d[64:80])
    out["build"] = cstr(d[80:96])

    off = 96
    (sver, cnt) = struct.unpack(">HH", d[off:off + 4])
    out["setup_version"], out["scalar_count"] = sver, cnt
    vals = struct.unpack(f">{cnt}H", d[off + 4:off + 4 + cnt * 2])
    out["scalars"] = {
        (SCALARS[i] if i < len(SCALARS) else f"scalar[{i}]"): v for i, v in enumerate(vals)
    }
    toff = off + 4 + cnt * 2
    out["setup_teams"] = []
    for s in range(2):
        t = d[toff + s * TEAM_BYTES: toff + (s + 1) * TEAM_BYTES]
        out["setup_teams"].append({
            "country": t[0], "ordinal": t[1], "controls": t[4],
            "name": cstr(bytes(t[5:22])),
        })
    out["tactics_off"] = toff + 2 * TEAM_BYTES
    out["state_off"] = 96 + out["setup_len"]
    out["raw"] = d
    return out


def report(c):
    print(f"{c['path']}: {c['size']} bytes")
    print(f"  version {c['version']}  layout {c['layout_hash']:016x}  "
          f"state hash {c['state_hash']:016x}")
    print(f"  build '{c['build']}'  frame {c['frame']}  sim width {c['sim_width']}  "
          f"pitch {c['pitch']}")
    rules = " ".join(n for b, n in RULES.items() if c["rules"] & b) or "plain"
    print(f"  rules {c['rules']:#04x} ({rules})  human side {c['human_side']}")
    print(f"  {c['team1']} {c['goals1']}-{c['goals2']} {c['team2']}  minute {c['minute']}")
    print(f"  setup {c['setup_len']} bytes ({c['scalar_count']} scalars), "
          f"state {c['state_len']} bytes at +{c['state_off']}")
    for i, t in enumerate(c["setup_teams"]):
        print(f"  team{i + 1}: '{t['name']}'  file {t['country']} ordinal {t['ordinal']}  "
              f"controls {t['controls']}")
    for name, v in c["scalars"].items():
        print(f"    {name:28s} = {v} ({v:#06x})")


def diff(a, b):
    def line(what, va, vb):
        print(f"  {what:32s} {va!s:>20s}  |  {vb!s:<20s}")

    n = 0
    for key in ("size", "version", "layout_hash", "state_hash", "frame", "setup_len",
                "state_len", "sim_width", "pitch", "rules", "human_side", "minute",
                "goals1", "goals2", "team1", "team2", "build"):
        va, vb = a[key], b[key]
        if va != vb:
            if isinstance(va, int) and va > 0xFFFF:
                va, vb = f"{va:016x}", f"{vb:016x}"
            line(key, va, vb)
            n += 1
    for name in a["scalars"]:
        va, vb = a["scalars"][name], b["scalars"].get(name)
        if va != vb:
            line(name, va, vb)
            n += 1
    for i in range(2):
        for f in ("country", "ordinal", "controls", "name"):
            va, vb = a["setup_teams"][i][f], b["setup_teams"][i][f]
            if va != vb:
                line(f"team{i + 1}.{f}", va, vb)
                n += 1
    ta, tb = a["raw"][a["tactics_off"]:a["tactics_off"] + NUM_TACTICS * TACTIC_BYTES], \
             b["raw"][b["tactics_off"]:b["tactics_off"] + NUM_TACTICS * TACTIC_BYTES]
    if ta != tb:
        line("user tactics", "(differ)", "(differ)")
        n += 1
    sa = a["raw"][a["state_off"]:]
    sb = b["raw"][b["state_off"]:]
    if sa != sb:
        first = next((i for i, (x, y) in enumerate(zip(sa, sb)) if x != y), min(len(sa), len(sb)))
        line("state blob", f"differs from +{first}", f"of {len(sa)}/{len(sb)} bytes")
        n += 1
    print(f"  -- {n} difference{'s' if n != 1 else ''}")
    return n


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    if argv[0] == "--diff":
        if len(argv) != 3:
            print("--diff takes exactly two files", file=sys.stderr)
            return 2
        a, b = parse(argv[1]), parse(argv[2])
        print(f"{a['path']}  |  {b['path']}")
        diff(a, b)
        return 0
    for p in argv:
        report(parse(p))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
