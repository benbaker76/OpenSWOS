#!/usr/bin/env python3
"""JSON -> C emitter for SWOS competition tables — the inverse of export_competitions.py.

Regenerates the C struct definitions in src/competition_data.c FROM
bin/data_json/competitions/*.json, so the JSON becomes the source of truth and the Amiga
build can bake it (no runtime JSON parse on 68k). The split fields (substitutes/points,
legs/extra_time/penalties) are recombined back into their packed bytes.

Self-verifies via the export round-trip: emit -> splice into competition_data.c -> re-export
-> diff the JSON. 0 diff == the generated C is byte-faithful to the originals.

This increment covers CompetitionCup + CompetitionTournament (eurocups, world cup,
qualifiers). Leagues/playoffs/qualifying/country-tables are the next increments.
"""
import json, os, re, sys, copy, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_competitions as exp

REPO = exp.REPO
COMP_DIR = exp.OUT_DIR


def _load(name):
    return json.load(open(os.path.join(COMP_DIR, name)))


def _subs_byte(d, lo_key):
    return ((int(d.get("substitutes", 0)) & 0xF) << 4) | (int(d.get(lo_key, 0)) & 0xF)


def _rrf_bytes(rrf):
    out = []
    for r in rrf:
        b = (((r.get("legs", 0) & 3) << 6) | ((r.get("extra_time", 0) & 3) << 4)
             | ((r.get("penalties", 0) & 3) << 2) | (r.get("low", 0) & 3))
        out.append(b - 256 if b > 127 else b)        # emit as signed int8 (matches the .c)
    return out


def _comp9(c):
    return "{ %d, %d, %d, %d, %d, %d, %d, %d, %d }" % (
        c["comp_id"], c["type"], c["team_number"], c["begin_month"], c["end_month"],
        c["name_table_offset"], c["reserved"], c["team_list_offset"], c["seed_reorder_flag"])


def _names(names):
    return ", ".join((n if n else "0") for n in names)   # JSON null -> NULL pointer


def _seed_list(seeds):
    if not seeds:
        return "{ }"
    rows = ["        { %s, %d%s }," % (s["country"], s["ordinal"],
                                      " /* %s */" % s["team"] if s.get("team") else "")
            for s in seeds]
    return "{\n" + "\n".join(rows) + "\n    }"


def emit_tournament(d):
    rounds = ", ".join("{ %d, %d, %d }" % (r["team_count"], r["group_count"], r["team_group_count"])
                       for r in d["rounds"])
    return ("CompetitionTournament %s = {\n"
            "    { %s, %d, %d, %d, %d, %d, %d,\n"
            "      { %s }, %d },\n"
            "    { %s },\n"
            "    { %s },\n"
            "    %s\n};") % (
        d["id"], _comp9(d["comp"]), d["seeding_type"], d["menu_state"], d["play_each_team"],
        d["away_goals_mode"], _subs_byte(d, "points_for_win"), d["round_string_base"],
        rounds, d.get("header_reserved", 0),
        ", ".join(str(x) for x in _rrf_bytes(d["round_rule_flags"])),
        _names(d["name"]), _seed_list(d["team_seed_list"]))


def emit_cup(d):
    return ("CompetitionCup %s = {\n"
            "    { %s, %d, %d, %d, %d, %d },\n"
            "    { %s },\n"
            "    { %s },\n"
            "    %s\n};") % (
        d["id"], _comp9(d["comp"]), d["num_rounds_override"], d["teams"], d["away_goals_mode"],
        _subs_byte(d, "points_for_win"), d["round_string_base"],
        ", ".join(str(x) for x in _rrf_bytes(d["round_rule_flags"])),
        _names(d["name"]), _seed_list(d["team_seed_list"]))


def emit_league(d):
    divs = ", ".join("{ %d, %d, %d, %d, %d, %d }" % (
        dv["teams"], dv["promoted"], dv["promoted_po"], dv["relegated"],
        dv["relegated_po"], dv["qualifier_link_offset"]) for dv in d["divisions"])
    parts = [
        "{ %s, %d, %d, %d, %d }" % (_comp9(d["comp"]), d["divisions_count"], d["rounds"],
                                    d["points_for_win"], _subs_byte(d, "max_subs")),
        "{ %s }" % divs,
        "%d" % d["reserved1"],
        "{ %s }" % _names(d["name"]),
    ]
    if "playoff_links" in d:
        parts.append("{ %s }" % ", ".join("(char *)&%s" % x for x in d["playoff_links"]))
    if "promotion_link_data" in d:
        parts.append("{ %s }" % ", ".join(str(x) for x in d["promotion_link_data"]))
    return "CompetitionLeague %s = {\n    %s\n};" % (d["id"], ",\n    ".join(parts))


def emit_playoffs(d):
    return "Playoffs %s = { %s, %d, %d, %d, %d, %d, { %s }, { %s }, { 0 } };" % (
        d["id"], _comp9(d["comp"]), d["num_rounds_override"], d["teams"], d["away_goals_mode"],
        _subs_byte(d, "max_subs"), d["round_string_base"],
        ", ".join(str(x) for x in _rrf_bytes(d["round_rule_flags"])), _names(d["name"]))


def emit_qualifying(d):
    cc = ", ".join(("(Competition *)&%s" % x) if isinstance(x, str) else ("(Competition *)%d" % x)
                   for x in d["comp_cup"])
    countries = ", ".join(str(x) for x in d["countries"])
    return "QualifyingGroup %s = {\n    { %s },\n    { %s }\n};" % (d["id"], cc, countries)


def emit_leagues(d):
    parts = ["(CompetitionLeague *)&%s, { %s }" % (lg["league"], ", ".join(str(x) for x in lg["link_params"]))
             for lg in d["leagues"]]
    tail = []
    if "playoffs" in d:
        tail.append("&%s" % d["playoffs"] if d["playoffs"] else "0")
    if "next_group" in d:
        tail.append("(Leagues *)&%s" % d["next_group"] if d["next_group"] else "0")
    body = ",\n    ".join(parts)
    if tail:
        body += ",\n    " + ", ".join(tail)
    if "qualifier_index_pairs" in d:
        body += ",\n    { %s }" % ", ".join(str(x) for x in d["qualifier_index_pairs"])
    return "Leagues %s = {\n    %s\n};" % (d["id"], body)


def emit_career(rec):
    rows = []
    for c in rec["contests"]:
        comp, copy = c["competition"], c["competition_copy"]
        if not comp:                                  # sentinel terminator: InitCareer's
            # _careerContests copy loop stops on competition == 0xFFFFFFFF, NOT 0 — emitting
            # { 0, 0, 0 } here makes it walk off the end (SIGSEGV starting a career).
            rows.append("    { (Competition *)0xFFFFFFFFu, (Competition *)0, 0 },  /* sentinel */")
        else:
            rows.append("    { (Competition *)&%s, (Competition *)&%s, sizeof(%s) }," %
                        (comp, copy if copy else "0", comp))
    return "CompetitionCareer _careerContests[35] = {\n" + "\n".join(rows) + "\n};"


# Tables with a spare slot after their end: a world's league cup goes there (src/world_cups.c),
# and include/competition_data.h declares them that size.
WORLD_CUP_ROOM = {"_italyTable"}


def emit_country_table(c):
    """Competition *<table>[N] = { &league(s), -2, &cup(s), -1 }."""
    elems = ["(Competition *)&%s" % lg for lg in c["leagues"]]
    elems.append("(Competition *)-2")
    elems += ["(Competition *)&%s" % cp for cp in c.get("cups", []) or []]
    elems.append("(Competition *)-1")
    if c["table"] in WORLD_CUP_ROOM:
        elems.append("(Competition *)-1")
    return "Competition *%s[%d] = { %s };" % (c["table"], len(elems), ", ".join(elems))


def emit_comp_index(countries):
    """void *_competitionsTable[256] = { [n] = table, ... }. Array tables (normal countries)
    decay to a pointer; QualifyingGroup 'tables' (customs/regions/quals, no leagues) need &."""
    lines = []
    for c in sorted(countries, key=lambda c: c["number"]):
        if not c.get("table"):
            continue
        amp = "" if c.get("leagues") else "&"
        lines.append("    [%d] = %s%s," % (c["number"], amp, c["table"]))
    return "void *_competitionsTable[256] = {\n" + "\n".join(lines) + "\n};"


def splice_country(src_text, countries):
    edits = 0
    for c in countries:
        if not c.get("leagues"):       # special QualifyingGroup tables: handled by emit_qualifying
            continue
        new = emit_country_table(c)
        pat = re.compile(r'Competition\s*\*\s*' + re.escape(c["table"]) + r'\s*\[\d+\]\s*=\s*\{[^}]*\};')
        src_text, k = pat.subn(lambda m: new, src_text)
        if not k:
            print("  WARN: country table %s not found" % c["table"])
        edits += k
    src_text, ki = re.subn(r'void\s*\*\s*_competitionsTable\s*\[\d+\]\s*=\s*\{[^}]*\};',
                           lambda m: emit_comp_index(countries), src_text)
    return src_text, edits + ki


def _def_spans(text, typename):
    """Map global name -> (start, end) char span of `typename name = { ... };`."""
    spans = {}
    for m in re.finditer(r'\b' + typename + r'\s+([A-Za-z_]\w*)\s*(?:\[\s*\d*\s*\])?\s*=\s*\{', text):
        i = text.index('{', m.start()); depth = 0; j = i
        while j < len(text):
            if text[j] == '{':
                depth += 1
            elif text[j] == '}':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        end = text.index(';', j) + 1
        spans[m.group(1)] = (m.start(), end)
    return spans


def splice(src_text, emitters):
    """Replace each `TYPE name = {...};` in src_text with its emitted version.
    emitters: list of (typename, json_records, emit_fn)."""
    edits = []
    for typename, records, fn in emitters:
        spans = _def_spans(src_text, typename)
        for rec in records:
            name = rec["id"]
            if name not in spans:
                print("  WARN: %s %s not found in competition_data.c" % (typename, name))
                continue
            edits.append((spans[name][0], spans[name][1], fn(rec)))
    for start, end, new in sorted(edits, key=lambda e: -e[0]):   # apply back-to-front
        src_text = src_text[:start] + new + src_text[end:]
    return src_text, len(edits)


def main():
    cups = _load("cups.json")["competitions"]
    tours = _load("tournaments.json")["competitions"]
    leagues = _load("leagues.json")["leagues"]
    playoffs = _load("playoffs.json")["playoffs"]
    qualifying = _load("qualifying.json")["groups"]
    league_blocks = _load("league_blocks.json")["groups"]
    career_rec = {"id": "_careerContests", "contests": _load("career.json")["contests"]}
    src_text = open(exp.SRC, encoding="latin1").read()

    spliced, n = splice(src_text, [
        ("CompetitionCup", cups, emit_cup),
        ("CompetitionTournament", tours, emit_tournament),
        ("CompetitionLeague", leagues, emit_league),
        ("Playoffs", playoffs, emit_playoffs),
        ("QualifyingGroup", qualifying, emit_qualifying),
        ("Leagues", league_blocks, emit_leagues),
        ("CompetitionCareer", [career_rec], emit_career),
    ])
    country_comps = _load("country_competitions.json")["countries"]
    spliced, nc = splice_country(spliced, country_comps)
    n += nc
    print("emitted/spliced %d definitions (incl. %d country tables + index)" % (n, nc))

    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
        open(out, "w", encoding="latin1").write(spliced)
        print("wrote regenerated C -> %s" % out)
        return 0                         # build/regen mode: skip the slow round-trip verify

    # ---- round-trip verify: re-export the spliced file and diff the JSON ----
    with tempfile.TemporaryDirectory() as td:
        gen_path = os.path.join(td, "competition_data.c")
        open(gen_path, "w", encoding="latin1").write(spliced)
        out_dir = os.path.join(td, "out")
        os.makedirs(out_dir)
        exp.SRC = gen_path
        exp.OUT_DIR = out_dir
        exp.main()
        bad = 0
        for fname in ("cups.json", "tournaments.json", "leagues.json", "playoffs.json",
                      "qualifying.json", "league_blocks.json", "career.json",
                      "country_competitions.json"):
            orig = _load.__wrapped__(fname) if hasattr(_load, "__wrapped__") else \
                json.load(open(os.path.join(COMP_DIR, fname)))
            regen = json.load(open(os.path.join(out_dir, fname)))
            if orig != regen:
                bad += 1
                print("  DIFF in %s" % fname)
                _diff(orig, regen)
        print("\nround-trip: %s" % ("FAITHFUL (0 diff)" if bad == 0 else "%d file(s) differ" % bad))
        return bad


def _diff(a, b, path=""):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in set(a) | set(b):
            if k not in a: print("    + %s/%s" % (path, k))
            elif k not in b: print("    - %s/%s" % (path, k))
            else: _diff(a[k], b[k], path + "/" + str(k))
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            print("    len %s: %d != %d" % (path, len(a), len(b)))
        for i in range(min(len(a), len(b))):
            _diff(a[i], b[i], path + "[%d]" % i)
    elif a != b:
        print("    %s: %r != %r" % (path, a, b))


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
