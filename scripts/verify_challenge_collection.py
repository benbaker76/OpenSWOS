"""Verify bundled collection headers/setup against the retained request specs.
This is file verification, not a substitute for playing each challenge.
"""
import argparse
import json
from pathlib import Path
from chl_dump import parse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--target", choices=("sdl", "amiga", "both"), default="both")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
teams = [("AC MILAN", "JUVENTUS"), ("ITALY", "MALTA"),
         ("LIVERPOOL", "EVERTON"), ("BARCELONA", "REAL MADRID"),
         ("JUVENTUS", "AC MILAN"), ("PARTIZAN B'GRADE", "RED STAR B'GRADE")]
for target in (("sdl", "amiga") if args.target == "both" else (args.target,)):
    layouts = set()
    for index in range(1, 7):
        spec = json.loads((root / f"docs/challenge_specs/collection/{target}_0{index}.json").read_text())
        prefix = "CCS" if target == "sdl" else "CCA"
        data = parse(root / f"bin/data/{prefix}0{index}.DAT")
        assert (data["team1"], data["team2"]) == teams[index - 1], data["path"]
        assert [data["goals1"], data["goals2"]] == spec["score"], data["path"]
        assert data["minute"] == spec["minute"], data["path"]
        assert data["human_side"] == spec["human"] == 1, data["path"]
        assert data["rules"] == 0, data["path"]
        # A level score goes to extra time and penalties unless the spec opts out.
        assert data["scalars"]["_extraTimeState"] == int(spec.get("extra_time", True)), data["path"]
        assert data["scalars"]["_penaltiesState"] == int(spec.get("penalties", True)), data["path"]
        assert data["size"] == 96 + data["setup_len"] + data["state_len"], data["path"]
        assert [t["controls"] for t in data["setup_teams"]] == [2, 1], data["path"]
        layouts.add(data["layout_hash"])
        print(f"{target} {index}: {data['team1']} {data['goals1']}-{data['goals2']} "
              f"{data['team2']}, minute {data['minute']}, human 1: PASS")
    assert len(layouts) == 1, f"{target}: mixed registry layouts"
