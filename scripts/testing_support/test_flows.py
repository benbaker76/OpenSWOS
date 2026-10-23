#!/usr/bin/env python3
"""Offline tests for the flow machinery — fake adapter + synthetic catalog,
no game, no debugger (docs/TESTING_FLOWS_DESIGN.md, delivery step 2):
ambiguous names, missing catalogs, stale paths, disabled actions, same
initializer with changed rows, unsafe retries, memo chains, defaults.

    python3 scripts/testing_support/test_flows.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from testing_support.catalog import Catalog, load_catalog, normalize
from testing_support.executor import Executor, StepFailure
from testing_support.goals import (GoalError, parse_goal, S_ASSERTION,
                                   S_CLARIFY, S_NOT_FOUND, S_PRECONDITION,
                                   S_SUCCESS, S_UNKNOWN, S_UNSUPPORTED)
from testing_support.planner import plan_goal
from testing_support.runner import run_request
from testing_support.snapshot import EntryRow, Snapshot, _fingerprint


# ---------------------------------------------------------------- fake world
def snap(screen, rows, sel=0, domain=None, seq=0):
    s = Snapshot(phase="menu", screen=screen, screen_addr=0x1000,
                 num_entries=len(rows), selected_ordinal=sel,
                 selected_callback=None, entries=[], domain=domain or {},
                 fingerprint="", seq=seq, complete=True)
    for i, r in enumerate(rows):
        s.entries.append(EntryRow(
            ordinal=i, enabled=r.get("enabled", True), content=2,
            control_mask=0x21, callback=r.get("cb"),
            callback_addr=0x2000 + i, label_kind="s",
            label=r.get("label")))
    s.fingerprint = _fingerprint(s)
    return s


class FakeAdapter:
    """Scripted world: screens + transition rules keyed by (screen, ordinal).
    `drops` simulates the dropped-pulse flakiness; every act is counted so a
    test can assert a press was NOT replayed."""

    def __init__(self, screens, transitions, start, career_team=None,
                 drops=None):
        self.screens = screens          # name -> list of row dicts (or callable)
        self.transitions = transitions  # (screen, ordinal) -> next screen
        self.state = start
        self.requests = 0
        self.presses = []
        self.drops = dict(drops or {})  # (screen, ordinal) -> n pulses to drop
        self.career_team = career_team or {}
        self.last_career_team = None
        self._seq = 0

    def observe(self, prime=True):
        self.requests += 1
        self._seq += 1
        rows = self.screens[self.state]
        screen_sym = rows["init"]
        return snap(screen_sym, rows["rows"], seq=self._seq)

    def activate(self, ordinal, timeout=20):
        self.requests += 1
        self.presses.append((self.state, ordinal))
        key = (self.state, ordinal)
        if self.drops.get(key, 0) > 0:
            self.drops[key] -= 1
            return                      # pulse dropped: nothing happens
        nxt = self.transitions.get(key)
        if nxt:
            self.state = nxt

    def pulse_short_fire(self, timeout=20):
        self.requests += 1
        self.presses.append((self.state, "shortfire"))
        nxt = self.transitions.get((self.state, "shortfire"))
        if nxt:
            self.state = nxt

    def pulse_raw_fire(self, timeout=20):
        self.pulse_short_fire(timeout)

    def read_career_team(self):
        self.requests += 1
        return dict(self.career_team)

    def identity(self):
        return {"build_id": "fake", "platform": "test", "target": "fake"}


def career_world(career_team=None, drops=None, division_label="PREMIER LEAGUE",
                 careers_disabled=False, division_rows=None):
    """The standard fake career world used across tests."""
    screens = {
        "main": {"init": "SWOSMainMenuInit", "rows": [
            {"label": "FRIENDLY", "cb": "FriendlyMenu"},
            {"label": "CAREER", "cb": "CareerMenu",
             "enabled": not careers_disabled}]},
        "career": {"init": "CareerMenuInit", "rows": [
            {"label": "CHOOSE TEAM AND START CAREER",
             "cb": "ChooseTeamAndStartCareer"},
            {"label": "EXIT", "cb": "SetExitMenuFlag"}]},
        "picker_cont": {"init": "SelectTeamsInit", "rows": [
            {"label": "EUROPE", "cb": "SelectTeamsCommon"},
            {"label": "AFRICA", "cb": "SelectTeamsCommon"}]},
        "picker_country": {"init": "SelectTeamsInit", "rows": [
            {"label": "ENGLAND", "cb": "SelectTeamsCommon"},
            {"label": "ITALY", "cb": "SelectTeamsCommon"}]},
        "picker_div": {"init": "SelectTeamsInit", "rows": division_rows or [
            {"label": division_label, "cb": "SelectLeaguesCommon"},
            {"label": "DIVISION ONE", "cb": "SelectLeaguesCommon"}]},
        "picker_club": {"init": "SelectTeamsInit", "rows": [
            {"label": "ARSENAL", "cb": "SelectTeamsCommon"},
            {"label": "CHELSEA", "cb": "SelectTeamsCommon"}]},
        "memo1": {"init": "ChairmanMemosInit", "rows": [
            {"label": "CONTINUE", "cb": "ContinueBtn"}]},
        "memo2": {"init": "ChairmanMemosInit", "rows": [
            {"label": "CONTINUE!", "cb": "ContinueBtn"}]},
        "hub": {"init": "ContestMenuInit", "rows": [
            {"label": "GO TO MATCH", "cb": "GoToMatch"}]},
    }
    transitions = {
        ("main", 1): "career",
        ("career", 0): "picker_cont",
        ("picker_cont", 0): "picker_country",
        ("picker_country", 0): "picker_div",
        ("picker_div", 0): "picker_club",
        ("picker_club", 0): "memo1",
        ("memo1", "shortfire"): "memo2",
        ("memo2", "shortfire"): "hub",
    }
    return FakeAdapter(screens, transitions, "main",
                       career_team=career_team or
                       {"teamName": "ARSENAL", "countryNumber": 8,
                        "globalTeamNumber": 1701},
                       drops=drops)


def synth_catalog():
    cat = Catalog(comp_root="<synthetic>", teams_root="<synthetic>")
    cat.countries = {8: "ENGLAND", 20: "ITALY", 41: "YUGOSLAVIA",
                     80: "EUROPE"}
    cat.continent_of = {8: 80, 20: 80, 41: 80}
    cat.country_leagues = {8: ["_englishLeague"], 41: ["_yugoslavianLeague"]}
    cat.league_divnames = {"_englishLeague":
                           ["PREMIER LEAGUE", "DIVISION ONE"],
                           "_yugoslavianLeague": [None]}
    cat._strings = {"_aPremierDivisio": "PREMIER DIVISION"}

    def club(name, country, ordinal, league=0):
        return {"uuid": f"uuid-{name}", "checksum": "x",
                "team": {"team_name": name, "country_number": country,
                         "team_ordinal": ordinal, "league": league}}
    cat.teams = {
        8: [club("ARSENAL", 8, 0), club("UNITED", 8, 1)],
        20: [club("UNITED", 20, 0)],          # ambiguous with England's
        41: [club("PARTIZAN B'GRADE", 41, 0)],
        80: [club("ITALY", 80, 0), club("ENGLAND", 80, 1)],  # nationals
    }
    cat.aliases = {normalize("PARTIZAN BELGRADE"):
                   {"team_name": "PARTIZAN B'GRADE", "country": "YUGOSLAVIA",
                    "kind": "club"}}
    cat.digest = "synthetic"
    return cat


ARSENAL_REQ = {"schema_version": 1, "goal": "career.active",
               "constraints": {"club": {"name": "Arsenal",
                                        "country": "England"},
                               "source": "new"}}


# ------------------------------------------------------------------- goals
class TestGoals(unittest.TestCase):
    def test_default_source_recorded(self):
        g = parse_goal({"schema_version": 1, "goal": "career.active",
                        "constraints": {"club": {"name": "Arsenal"}}})
        self.assertEqual(g.source, "new")
        self.assertIn("source", g.defaults_applied)

    def test_missing_club_clarifies(self):
        with self.assertRaises(GoalError) as cm:
            parse_goal({"schema_version": 1, "goal": "career.active",
                        "constraints": {}})
        self.assertEqual(cm.exception.status, S_CLARIFY)
        self.assertTrue(cm.exception.questions)

    def test_load_unsupported(self):
        with self.assertRaises(GoalError) as cm:
            parse_goal({"schema_version": 1, "goal": "career.active",
                        "constraints": {"club": {"name": "X"},
                                        "source": "load"}})
        self.assertEqual(cm.exception.status, S_UNSUPPORTED)

    def test_friendly_two_participants(self):
        with self.assertRaises(GoalError):
            parse_goal({"schema_version": 1, "goal": "match.active",
                        "constraints": {"competition": "friendly",
                                        "participants": [
                                            {"team": {"name": "ITALY"},
                                             "control": "player"}]}})


# ------------------------------------------------------------------ catalog
class TestCatalog(unittest.TestCase):
    def setUp(self):
        self.cat = synth_catalog()

    def test_exact_with_country(self):
        r = self.cat.resolve_team("arsenal", kind="club", country="England")
        self.assertEqual(r.name, "ARSENAL")
        self.assertEqual(r.continent_label, "EUROPE")
        self.assertEqual(r.division_label, "PREMIER LEAGUE")

    def test_ambiguous_without_country(self):
        with self.assertRaises(GoalError) as cm:
            self.cat.resolve_team("UNITED", kind="club")
        self.assertEqual(cm.exception.status, S_CLARIFY)
        self.assertTrue(any("ENGLAND" in q or "ITALY" in q
                            for q in cm.exception.questions))

    def test_country_disambiguates(self):
        r = self.cat.resolve_team("UNITED", kind="club", country="ITALY")
        self.assertEqual(r.country_number, 20)

    def test_not_found(self):
        with self.assertRaises(GoalError) as cm:
            self.cat.resolve_team("NO SUCH CLUB")
        self.assertEqual(cm.exception.status, S_NOT_FOUND)

    def test_alias_and_fallback_division_name(self):
        r = self.cat.resolve_team("Partizan Belgrade")
        self.assertEqual(r.name, "PARTIZAN B'GRADE")
        self.assertEqual(r.aliased_from, "Partizan Belgrade")
        # yugoslav league names are empty -> _divisionStringTable fallback
        self.assertEqual(r.division_label, "PREMIER DIVISION")

    def test_kind_filters_nationals(self):
        r = self.cat.resolve_team("ITALY", kind="national")
        self.assertEqual(r.kind, "national")
        self.assertEqual(r.country_number, 80)

    def test_missing_teams_catalog(self):
        cat = synth_catalog()
        cat.teams_root = None
        cat.missing = ["teams/ not found"]
        with self.assertRaises(GoalError) as cm:
            cat.resolve_team("ARSENAL")
        self.assertEqual(cm.exception.status, S_NOT_FOUND)


# ------------------------------------------------------------------- planner
class TestPlanner(unittest.TestCase):
    def test_same_plan_shape_for_any_club(self):
        cat = synth_catalog()
        p1 = plan_goal(parse_goal(ARSENAL_REQ), cat)
        req2 = {"schema_version": 1, "goal": "career.active",
                "constraints": {"club": {"name": "Partizan Belgrade"}}}
        p2 = plan_goal(parse_goal(req2), cat)
        self.assertEqual([s["name"] for s in p1["steps"]],
                         [s["name"] for s in p2["steps"]])
        self.assertNotEqual(p1["resolved"], p2["resolved"])

    def test_national_team_career_rejected(self):
        cat = synth_catalog()
        req = {"schema_version": 1, "goal": "career.active",
               "constraints": {"club": {"name": "ITALY",
                                        "kind": "national"}}}
        with self.assertRaises(GoalError) as cm:
            plan_goal(parse_goal(req), cat)
        self.assertEqual(cm.exception.status, S_UNSUPPORTED)


# ------------------------------------------------------- executor + end to end
class TestExecution(unittest.TestCase):
    def run_arsenal(self, adapter):
        return run_request(ARSENAL_REQ, adapter, catalog=synth_catalog())

    def test_happy_path(self):
        a = career_world()
        r = self.run_arsenal(a)
        self.assertEqual(r["status"], S_SUCCESS, r.get("detail"))
        self.assertEqual(r["final_screen"], "ContestMenuInit")
        self.assertEqual(r["verified_career_team"]["teamName"], "ARSENAL")
        # per-step before/after fingerprints + requests recorded
        for s in r["steps"]:
            self.assertIn("requests", s)
            self.assertIn("before_fingerprint", s)

    def test_same_init_changed_rows_is_progress(self):
        """Continent -> country keeps SelectTeamsInit; the fingerprint/rows
        change is what proves the hop."""
        a = career_world()
        r = self.run_arsenal(a)
        hops = [s for s in r["steps"] if s["name"] == "picker hop"]
        self.assertTrue(hops and all(s["status"] == "ok" for s in hops))
        self.assertTrue(all(s["before_screen"] == "SelectTeamsInit"
                            for s in hops))

    def test_dropped_pulses_retried(self):
        a = career_world(drops={("picker_cont", 0): 2})
        r = self.run_arsenal(a)
        self.assertEqual(r["status"], S_SUCCESS)
        hop = next(s for s in r["steps"] if s["name"] == "picker hop")
        self.assertEqual(hop["attempts"], 3)

    def test_disabled_action_fails_precondition(self):
        a = career_world(careers_disabled=True)
        r = self.run_arsenal(a)
        self.assertEqual(r["status"], S_PRECONDITION)
        self.assertIn("disabled", r["detail"])

    def test_stale_catalog_path_stops_with_choices(self):
        """Catalog predicts PREMIER LEAGUE; the live screen has different
        rows -> stop with the actual choices, never press the closest."""
        a = career_world(division_rows=[
            {"label": "SUPER LIGA", "cb": "SelectLeaguesCommon"}])
        r = self.run_arsenal(a)
        self.assertEqual(r["status"], S_PRECONDITION)
        ev = next(s for s in r["steps"]
                  if s["status"] == S_PRECONDITION)["evidence"]
        self.assertIn((0, "SUPER LIGA"), ev["visible"])

    def test_unsafe_retry_stops(self):
        """State changed but completion unmet -> unknown_outcome and NO
        second press of the same entry."""
        a = career_world()
        # firing ARSENAL lands on an unexpected screen (not memo/hub)
        a.transitions[("picker_club", 0)] = "career"
        r = self.run_arsenal(a)
        self.assertEqual(r["status"], S_UNKNOWN)
        club_presses = [p for p in a.presses if p == ("picker_club", 0)]
        self.assertEqual(len(club_presses), 1)

    def test_memo_chain_dismissed(self):
        a = career_world()
        r = self.run_arsenal(a)
        memo = next(s for s in r["steps"]
                    if s["name"] == "dismiss welcome memos")
        self.assertEqual(memo["status"], "ok")
        self.assertEqual(memo["attempts"], 2)      # two memos in the world

    def test_wrong_career_team_fails_assertion(self):
        a = career_world(career_team={"teamName": "CHELSEA",
                                      "countryNumber": 8})
        r = self.run_arsenal(a)
        self.assertEqual(r["status"], S_ASSERTION)

    def test_mixed_kind_friendly_unsupported_without_acting(self):
        a = career_world()
        r = run_request({"schema_version": 1, "goal": "match.active",
                         "constraints": {"competition": "friendly",
                                         "participants": [
                                             {"team": {"name": "ITALY",
                                                       "kind": "national"},
                                              "control": "player"},
                                             {"team": {"name": "ARSENAL",
                                                       "kind": "club"},
                                              "control": "cpu"}]}},
                        a, catalog=synth_catalog())
        self.assertEqual(r["status"], S_UNSUPPORTED)
        self.assertEqual(a.presses, [])


FRIENDLY_REQ = {"schema_version": 1, "goal": "match.active",
                "constraints": {"competition": "friendly", "participants": [
                    {"team": {"name": "ITALY", "kind": "national"},
                     "control": "player"},
                    {"team": {"name": "ENGLAND", "kind": "national"},
                     "control": "cpu"}]}}


class FakeFriendlyAdapter(FakeAdapter):
    """Career FakeAdapter + the friendly team list's control cycling and the
    match boundary."""

    def __init__(self, screens, transitions, start):
        super().__init__(screens, transitions, start)
        self.controls = {}            # team name -> control 0..3
        self.match_entered = False

    def activate(self, ordinal, timeout=20):
        rows = self.screens[self.state]["rows"]
        if (self.state == "teamlist" and ordinal < len(rows)
                and rows[ordinal].get("team")):
            name = rows[ordinal]["label"]
            self.requests += 1
            self.presses.append((self.state, ordinal))
            self.controls[name] = (self.controls.get(name, 0) + 1) % 4
            return
        super().activate(ordinal, timeout)

    def read_selected_teams(self):
        self.requests += 1
        return [{"teamName": n, "teamControls": c, "countryNumber": 80}
                for n, c in self.controls.items() if c]

    def enter_match(self, ordinal, timeout=120):
        self.match_entered = True
        return [{"side": "top", "teamName": n,
                 "owner": 1 if c == 2 else 0}
                for n, c in self.controls.items() if c]


def friendly_world():
    screens = {
        "main": {"init": "SWOSMainMenuInit", "rows": [
            {"label": "FRIENDLY", "cb": "FriendlyMenu"}]},
        "fmenu": {"init": "FriendlyMenuInit", "rows": [
            {"label": "OK", "cb": "FriendlyOK"}]},
        "fmenu_ok": {"init": "FriendlyMenuInit", "rows": [
            {"label": "CHOOSE TEAMS", "cb": "ChooseTeamsMenu"}]},
        "kind": {"init": "ChooseTeamsInit", "rows": [
            {"label": "NATIONAL TEAMS", "cb": "NationalTeams"}]},
        "picker": {"init": "SelectTeamsInit", "rows": [
            {"label": "EUROPE", "cb": "SelectTeamsCommon"}]},
        "teamlist": {"init": "ChooseTeamsFriendlyInitFinish", "rows": [
            {"label": "ITALY", "cb": "ChooseTeamsFriendlyCommon2",
             "team": True},
            {"label": "ENGLAND", "cb": "ChooseTeamsFriendlyCommon2",
             "team": True},
            {"label": "PLAY FRIENDLY",
             "cb": "ChooseTeamsPlayMatchSelected"}]},
        "squad": {"init": "PlayMatchMenuInit", "rows": [
            {"label": "PLAY/COACH MATCH", "cb": "PlayMatchSelected"}]},
    }
    transitions = {
        ("main", 0): "fmenu",
        ("fmenu", 0): "fmenu_ok",
        ("fmenu_ok", 0): "kind",
        ("kind", 0): "picker",
        ("picker", 0): "teamlist",
        ("teamlist", 2): "squad",
    }
    return FakeFriendlyAdapter(screens, transitions, "main")


class TestFriendlyExecution(unittest.TestCase):
    def test_friendly_happy_path(self):
        a = friendly_world()
        r = run_request(FRIENDLY_REQ, a, catalog=synth_catalog())
        self.assertEqual(r["status"], S_SUCCESS, r.get("detail"))
        self.assertTrue(a.match_entered)
        self.assertEqual(a.controls, {"ITALY": 2, "ENGLAND": 1})
        teams = {t["teamName"]: t["owner"]
                 for t in r["verified_match"]["teams"]}
        self.assertEqual(teams, {"ITALY": 1, "ENGLAND": 0})

    def test_control_cycle_is_effect_aware(self):
        """ITALY -> player-coach needs exactly 2 pulses (0->1->2), never a
        blind repeat past the target."""
        a = friendly_world()
        run_request(FRIENDLY_REQ, a, catalog=synth_catalog())
        italy_presses = [p for p in a.presses if p == ("teamlist", 0)]
        self.assertEqual(len(italy_presses), 2)

    def test_wrong_ownership_fails_assertion(self):
        a = friendly_world()
        orig = a.enter_match
        a.enter_match = lambda o, timeout=120: [
            {"side": "top", "teamName": "ITALY", "owner": 0},
            {"side": "bottom", "teamName": "ENGLAND", "owner": 0}]
        r = run_request(FRIENDLY_REQ, a, catalog=synth_catalog())
        self.assertEqual(r["status"], S_ASSERTION)


# ------------------------------------------------- real catalog (if present)
class TestRealCatalog(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.cat = load_catalog()
        except GoalError:
            cls.cat = None

    def test_arsenal_england(self):
        if self.cat is None or self.cat.teams_root is None:
            self.skipTest("no real teams catalog on this machine")
        r = self.cat.resolve_team("ARSENAL", kind="club", country="ENGLAND")
        self.assertEqual(r.continent_label, "EUROPE")
        self.assertEqual(r.division_label, "PREMIER LEAGUE")
        self.assertEqual(r.country_number, 8)

    def test_partizan_alias(self):
        if self.cat is None or self.cat.teams_root is None:
            self.skipTest("no real teams catalog on this machine")
        r = self.cat.resolve_team("Partizan Belgrade")
        self.assertEqual(r.name, "PARTIZAN B'GRADE")
        self.assertEqual(r.country_label, "YUGOSLAVIA")
        self.assertIsNotNone(r.division_label)

    def test_italy_national(self):
        if self.cat is None or self.cat.teams_root is None:
            self.skipTest("no real teams catalog on this machine")
        r = self.cat.resolve_team("ITALY", kind="national")
        self.assertEqual(r.kind, "national")


if __name__ == "__main__":
    unittest.main(verbosity=2)
