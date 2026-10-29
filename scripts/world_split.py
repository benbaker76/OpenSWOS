"""world_split.py -- a league division's mid-season split, compiled (docs/WORLD_2026_27.md, phase 8).

Ported from the SWOS 96/97 Mod's tools/split.py (schedule, _orient, matrix, entry) and the split part of
tools/mkseason.py (splits), which carry this notice:

  Copyright (c) 2026 Davide Lorigliola

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
  associated documentation files (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge, publish, distribute,
  sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or
  substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
  NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
  DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
  OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

The bytes are the Mod's (its SP_TAB entry, scripts/league_playoffs_check.sh compares them for every
split league): src/league_split.c walks them as its split.py's routines walk theirs. The
after-stage routines ('then') and the stage tables ('table', 'annual') -- the Apertura and
Clausura titles' -- are not compiled yet.
"""
import os
import re
import struct

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

POINTS = {"full": 0, "half_up": 1, "half_down": 2, "reset": 3, "h2h_half": 4, "half_down_h2h": 4}
ROUNDS = {"none": 0, "single": 1, "double": 2, "triple": 3, "quadruple": 4}
NMAX = 30
SR = 28                     # a stage's record
H2H_NMAX = 8


def days_before(n, games):
    """The days of `games` meetings: pairs per cycle / (n / 2) x meetings."""
    return n * (n - 1) // 2 // (n // 2) * games


def _round_robin(m):
    """Circle-method rounds of m clubs (local places 0..m-1; a bye for odd m)."""
    k = m + (m & 1)
    lst = list(range(k))
    out = []
    for r in range(k - 1):
        out.append([(lst[i], lst[k - 1 - i]) for i in range(k // 2) if max(lst[i], lst[k - 1 - i]) < m])
        lst = [lst[0]] + [lst[-1]] + lst[1:-1]
    return out


def _orient(m, rounds):
    """Home / away of a single round robin: every club ceil or floor of its games at home, the extra
    home games to the best placed, no club 3 times running at home or away when avoidable."""
    games = m - 1
    total = m * games // 2
    lo = games // 2
    extra = total - m * lo
    want = [lo + (1 if p < extra else 0) for p in range(m)]
    flat = [pr for rd in rounds for pr in rd]
    rnd = [r for r, rd in enumerate(rounds) for _ in rd]
    best, nodes = [None], [0]

    def dfs(i, home, last, run, cost, out):
        nodes[0] += 1
        if best[0] is not None and (cost >= best[0][0] or nodes[0] > 300000):
            return
        if i == len(flat):
            best[0] = (cost, list(out))
            return
        a, b = flat[i]
        left = {p: sum(1 for x in flat[i:] if p in x) for p in (a, b)}
        opts = []
        for h, v in ((a, b), (b, a)):
            if home[h] + 1 > want[h] or (left[v] - 1) < want[v] - home[v]:
                continue
            c = cost
            nl, nr = dict(last), dict(run)
            for t, side in ((h, 1), (v, 0)):
                nr[t] = nr.get(t, 0) + 1 if last.get(t) == side else 1
                nl[t] = side
                if nr[t] >= 3:
                    c += 1
            opts.append((c, sum(nr[t] for t in (h, v)), h, v, nl, nr))
        for c, _, h, v, nl, nr in sorted(opts, key=lambda o: o[:2] if m > 10 else 0):
            home[h] += 1
            out.append((h, v))
            dfs(i + 1, home, nl, nr, c, out)
            out.pop()
            home[h] -= 1
            if best[0] is not None and best[0][0] == 0:
                return

    dfs(0, [0] * m, {}, {}, 0, [])
    assert best[0] is not None, m
    res, k = [[] for _ in rounds], 0
    for r in rnd:
        res[r].append(best[0][1][k])
        k += 1
    return res


def schedule(groups, rounds):
    """The days after the split: [[(home place, away place, group), ...] per day], places 0-based."""
    days, start = [], 0
    for g, (m, r) in enumerate(zip(groups, rounds)):
        one = _orient(m, _round_robin(m)) if r else []
        back = [[(b, a) for a, b in rd] for rd in one]
        plan = [rd for k in range(r) for rd in (one if k % 2 == 0 else back)]
        for d, rd in enumerate(plan):
            while len(days) <= d:
                days.append([])
            days[d] += [(start + a, start + b, g) for a, b in rd]
        start += m
    return days


def phase1_hosts(table, n, games, cycles):
    """{(slot a, slot b): [meetings where a hosted b]} before the split: the pair table once a cycle,
    home and away swapped when the cycles left are odd."""
    hosts = {}
    for c in range(games):
        swap = (cycles - c) & 1
        for a, b in table:
            if swap:
                a, b = b, a
            hosts.setdefault((a, b), []).append(c)
    return hosts


def matrix(table, n, games, cycles, home):
    """n x n bytes: [a * n + b] = 1 when slot a is at home against b after the split, 0 when b is,
    2 the calendar's own (a balance tie)."""
    h = phase1_hosts(table, n, games, cycles)
    out = bytearray(n * n)
    for a in range(n):
        for b in range(n):
            if a == b:
                continue
            ha, hb = h.get((a, b), []), h.get((b, a), [])
            if home == "balance":
                out[a * n + b] = 1 if len(ha) < len(hb) else 0 if len(ha) > len(hb) else 2
            else:                                   # reverseK: the club away in the K-th meeting hosts
                k = int(home[7:]) - 1
                assert games > k, "%s needs %d meetings before the split" % (home, k + 1)
                out[a * n + b] = 1 if k in hb else 0
    return bytes(out)


MONTH_DAYS = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)


def week_of(date):
    """'dd/mm' (or 'dd/mm+1': the next calendar year) -> the career calendar's week byte: 8 a
    month from January of the season's first year (a year = 96)."""
    d, m = date.split("+")[0].split("/")
    y = int(date.split("+")[1]) if "+" in date else 0
    d, m = int(d), int(m)
    return 96 * y + 8 * (m - 1) + (d - 1) * 8 // MONTH_DAYS[m - 1]


def entry(world, file, div, n, spec, table):
    """One split table (the Mod's SP_TAB entry): its bytes, its days before and after. spec:
    games, stages [{after, groups, rounds, points, home, reset, keep, perm}] or a fixture stage
    {after, fixtures: [[(home row, away row), ...] per day], members, reset, wins}; weeks: a
    week byte per day (optional, all the days). The after-stage routines ('then') and the stage
    tables ('table', 'annual') are not played yet: their bytes are 0."""
    games = spec["games"]
    stages = spec.get("stages") or []
    assert n <= NMAX and len(stages) <= 8, (n, len(stages))
    d1 = days_before(n, games)
    cycles = games + (1 if stages else 0)
    days, recs = [], []
    for st in stages:
        extra = (st.get("members"), 0, 0, 0)
        if "fixtures" in st:                        # a fixture list by rows
            mem = st.get("members")
            assert st["after"] - d1 == len(days)
            assert mem is None or len(mem) == n, (len(mem), n)
            groups = [mem.count(g) for g in range(max(mem) + 1)] if mem else [n]
            assert len(groups) <= 4 and all(groups), groups
            for d in st["fixtures"]:
                assert len({x for pr in d for x in pr}) == 2 * len(d) and all(0 <= x < n for pr in d for x in pr), d
            flags = (1 if st.get("reset") else 0) | 4 | (8 if mem else 0) | (16 if st.get("wins") else 0)
            recs.append((len(days), groups, ["full"] * len(groups), flags, (b"", None)) + extra)
            days += [[(a, b, 0) for a, b in d] for d in st["fixtures"]]
            continue
        groups, rounds = st["groups"], st["rounds"]
        points = st.get("points") or ["full"] * len(groups)
        assert len(groups) <= 4 and sum(groups) == n, (n, groups)
        assert st["after"] - d1 == len(days)
        assert n <= H2H_NMAX or all(POINTS[x] != 4 for x in points), "head-to-head points: at most 8 clubs"
        assert not st.get("keep") or 3 * n <= 64, "'keep': at most 21 clubs"
        single = {g for g, r in enumerate(rounds) if r == 1}
        home = st.get("home", "seeded")
        mat = matrix(table, n, games, cycles, home) if home != "seeded" and single else b""
        sd = schedule(groups, rounds)
        perm = st.get("perm")                       # explicit places (a permutation of the frozen order)
        assert perm is None or sorted(perm) == list(range(n)), perm
        recs.append((len(days), groups, points, (1 if st.get("reset") else 0) | (2 if st.get("keep") else 0)
                     | (0x20 if perm else 0), (mat, perm)) + extra)
        days += [[(a, b, 1 if mat and g in single else 0) for a, b, g in d] for d in sd]
    d2 = len(days)
    counts = bytes(len(d) for d in days)
    firsts, pairs, k = b"", b"", 0
    for d in days:
        firsts += struct.pack("<H", k)
        for a, b, f in d:
            pairs += bytes((a, b, f))
            k += 1
    o_counts = 16 + SR * len(recs)
    o_firsts = o_counts + len(counts) + (len(counts) & 1)
    o_pairs = o_firsts + len(firsts)
    o = o_pairs + len(pairs)
    srec, mats = b"", b""
    weeks = bytes(spec["weeks"]) if spec.get("weeks") else b""
    assert not weeks or len(weeks) == d1 + d2, (len(weeks), d1 + d2)
    for k, (j0, groups, points, flags, (mat, perm), mem, cb, tb, ann) in enumerate(recs):
        g, start = b"", 0
        for i in range(4):
            if i < len(groups):
                g += bytes((start, groups[i], POINTS[points[i]]))
                start += groups[i]
            else:
                g += b"\0\0\0"
        mato = o + len(mats) if mat else 0
        mats += mat
        po = o + len(mats) if perm else 0
        mats += bytes(perm) if perm else b""
        srec += bytes((j0, len(groups), flags, 0)) + g + struct.pack("<HH", mato, po)
        mo = o + len(mats) if mem else 0
        mats += bytes(mem) if mem else b""
        wo = o + len(mats) if weeks and k == 0 else 0
        mats += weeks if weeks and k == 0 else b""
        srec += struct.pack("<HBBBBH", mo, cb, tb, ann, 0, wo)
    length = o + len(mats)
    length += -length & 3
    head = bytes((world, file, div, n, cycles, d1, d2, len(recs)))
    body = (head + struct.pack("<4H", o_counts, o_firsts, o_pairs, length) + srec + counts
            + b"\0" * (len(counts) & 1) + firsts + pairs + mats)
    return body + b"\0" * (length - len(body)), d1, d2


def division_spec(n, sp, games=None, rows=None):
    """A division's split as the Mod's mkseason.splits makes its spec: None if the division has
    neither a split nor meetings of its own. rows: the division's club keys in table-row (team
    file) order, for a fixture stage."""
    if not sp and games is None:
        return None
    spec = {"stages": []}
    stages = (sp.get("stages") or [sp]) if sp else []
    per = days_before(n, 1)
    day = None
    for i, st in enumerate(stages):
        if "fixtures" in st:                        # a fixture list by club
            if i == 0:
                assert st.get("after") == 0
                games, day = 0, 0
            assert st.get("after") == day
            fxd = [[(rows.index(a), rows.index(b)) for a, b in dd] for dd in st["fixtures"]]
            mem = None
            if st.get("zones"):
                mem = [next(g for g, z in enumerate(st["zones"]) if c in z) for c in rows]
            spec["stages"].append({"after": day, "fixtures": fxd, "members": mem, "reset": bool(st.get("reset")),
                                   "wins": st.get("tiebreak") == "wins",
                                   "groups": [len(z) for z in st.get("zones") or [rows]]})
            day += len(fxd)
            continue
        if i == 0:
            assert st["after"] % per == 0 and 1 <= st["after"] // per <= 4, (st["after"], per)
            games = st["after"] // per
            day = st["after"]
        assert st["after"] == day
        gs = st.get("groups") or []
        expl = st.get("places")
        if st.get("places_snake"):                  # (groups of equal size, places dealt 1 2 .. g g .. 1)
            order = [k for b in range(n // len(gs)) for k in (range(len(gs)) if b % 2 == 0
                                                            else reversed(range(len(gs))))]
            expl = [[p + 1 for p, k in enumerate(order) if k == x] for x in range(len(gs))]
        one = {"after": st["after"], "groups": [g["places"][1] - g["places"][0] + 1 for g in gs],
               "rounds": [ROUNDS[g.get("rounds", "single")] for g in gs],
               "points": [g.get("points", "full") for g in gs], "home": st.get("home", "seeded"),
               "reset": bool(st.get("reset")), "keep": st.get("keep")}
        if expl is not None:
            one["perm"] = [p - 1 for e in expl for p in e]
        spec["stages"].append(one)
        day += len(schedule(one["groups"], one["rounds"]))
    spec["games"] = games
    if sp and sp.get("weeks"):
        spec["weeks"] = [week_of(x) for x in sp["weeks"]]
    return spec


def pair_table(n):
    """The engine's pairs for n clubs as a world plays them: the original's (src/fixture_data.c) for an
    even n, src/fixture_rounds.c's rounds for an odd one."""
    if n & 1:
        k, lst, out = n + 1, list(range(n + 1)), []
        for r in range(k - 1):
            for i in range(k // 2):
                a, b = lst[i], lst[k - 1 - i]
                if (r & 1) if i == 0 else (i & 1):
                    a, b = b, a
                if a < n and b < n:
                    out.append((a, b))
            lst = [lst[0]] + [lst[-1]] + lst[1:-1]
        return out
    src = open(os.path.join(REPO, "src", "fixture_data.c"), encoding="utf-8").read()
    m = re.search(r"_fixtureSchedule%dTeams\[\d+\] = \{([^}]*)\}" % n, src)
    v = [int(x) for x in m.group(1).split(",")]
    return list(zip(v[0::2], v[1::2]))
