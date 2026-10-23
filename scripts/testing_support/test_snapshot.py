#!/usr/bin/env python3
"""Offline unit tests for snapshot.py — recorded/synthetic gdb output only,
no debugger, no game (docs/TESTING_FLOWS_DESIGN.md, delivery step 2's
fake-adapter principle applied from step 1).

    python3 -m unittest scripts.testing_support.test_snapshot  (from repo root)
    python3 scripts/testing_support/test_snapshot.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from testing_support.snapshot import (build_observe_script, make_nonce,
                                      parse_observe_output)

N = "abc123def456"
M = f"@{N}@"


def blob(*lines):
    return "\n".join(l for l in lines if l) + "\n"


def menu_blob(extra_entries="", domain=f"{M}DOM|seasonUpdateInProgress=0|gameType=1",
              end=f"{M}END", sel=f"{M}SEL|7|0x4015a0 <PlayMatchSelected>"):
    return blob(
        f"{M}BEGIN",
        f"{M}INIT|0x401000 <SWOSMainMenuInit>",
        f"{M}NENT|16",
        sel,
        f"{M}ENT|0|0|2|33|0x402000 <MenuExit>|s|EXIT{M}EOL",
        f"{M}ENT|1|0|2|33|0x402100 <CareerMenu>|s|CAREER{M}EOL",
        f"{M}ENT|2|1|2|33|0x0|s|DISABLED ROW{M}EOL",
        f"{M}ENT|3|0|4|33|0x402200 <SelectTeamsCommon>|b|PREMIER LEAGUE{M}EOL",
        f"{M}ENT|4|0|6|33|0x0|n|1701{M}EOL",
        f"{M}ENT|5|0|1|0|0x402300 <DrawVersionLine>|f|0x403000 <VersionText>{M}EOL",
        f"{M}ENT|6|0|5|33|0x0|m|{M}EOL",
        extra_entries.rstrip("\n") if extra_entries else None,
        domain,
        end,
    )


class TestScript(unittest.TestCase):
    def test_bounds_strictly_less(self):
        s = build_observe_script(N)
        self.assertIn("while $i < $n", s)
        self.assertNotIn("<= $n", s)

    def test_framing_and_label_gates(self):
        s = build_observe_script(N, {"gameType": ("(int)_gameType", "%d")})
        self.assertIn(f"{M}BEGIN", s)
        self.assertIn(f"{M}END", s)
        self.assertIn("gameType=%d", s)
        # content-form gates, not bare fg checks
        self.assertIn("$e->content == 2 && (unsigned long)$e->fg > 0x10000", s)
        self.assertIn("$e->content == 4 && (unsigned long)$e->fg > 0x10000", s)

    def test_no_domain_section_without_symbols(self):
        self.assertNotIn("DOM|", build_observe_script(N))


class TestParse(unittest.TestCase):
    def test_full_menu(self):
        s = parse_observe_output(menu_blob(), N, seq=3)
        self.assertEqual(s.phase, "menu")
        self.assertEqual(s.screen, "SWOSMainMenuInit")
        self.assertEqual(s.num_entries, 16)
        self.assertEqual(s.selected_ordinal, 7)
        self.assertEqual(s.selected_callback, "PlayMatchSelected")
        self.assertEqual(len(s.entries), 7)
        self.assertTrue(s.complete)
        self.assertEqual(s.seq, 3)
        self.assertEqual(s.domain, {"seasonUpdateInProgress": 0, "gameType": 1})

        by_ord = {e.ordinal: e for e in s.entries}
        self.assertEqual(by_ord[0].label, "EXIT")
        self.assertTrue(by_ord[0].enabled)
        self.assertEqual(by_ord[0].callback, "MenuExit")
        self.assertFalse(by_ord[2].enabled)
        self.assertIsNone(by_ord[2].callback)       # onSelect 0x0 -> None
        self.assertEqual(by_ord[3].label, "PREMIER LEAGUE")
        self.assertEqual(by_ord[3].label_kind, "b")
        self.assertEqual(by_ord[4].label, "1701")   # number payload
        self.assertEqual(by_ord[5].label, "VersionText")  # func identity
        self.assertEqual(by_ord[5].label_kind, "f")
        self.assertIsNone(by_ord[6].label)          # multiline undecoded

    def test_label_with_pipes_and_newlines(self):
        hostile = f"{M}ENT|9|0|2|33|0x402400 <Foo>|s|A|B|C\nSECOND LINE{M}EOL"
        s = parse_observe_output(menu_blob(extra_entries=hostile), N)
        row = next(e for e in s.entries if e.ordinal == 9)
        self.assertEqual(row.label, "A|B|C\nSECOND LINE")
        self.assertEqual(row.callback, "Foo")

    def test_symbol_offset_stripped(self):
        e = f"{M}ENT|9|0|2|33|0x402404 <Foo+4>|s|X{M}EOL"
        s = parse_observe_output(menu_blob(extra_entries=e), N)
        row = next(x for x in s.entries if x.ordinal == 9)
        self.assertEqual(row.callback, "Foo")

    def test_nomenu(self):
        s = parse_observe_output(blob(f"{M}BEGIN", f"{M}NOMENU", f"{M}END"), N)
        self.assertEqual(s.phase, "unknown")
        self.assertTrue(s.complete)
        self.assertEqual(s.entries, [])

    def test_empty_output(self):
        s = parse_observe_output("", N)
        self.assertEqual(s.phase, "unknown")
        self.assertFalse(s.complete)
        self.assertTrue(s.errors)

    def test_missing_end_marks_partial(self):
        s = parse_observe_output(menu_blob(end=""), N)
        self.assertFalse(s.complete)
        self.assertTrue(any("END marker" in e for e in s.errors))
        self.assertEqual(s.phase, "menu")   # partial data still reported

    def test_transition_phase(self):
        s = parse_observe_output(
            menu_blob(domain=f"{M}DOM|seasonUpdateInProgress=1|gameType=0"), N)
        self.assertEqual(s.phase, "transition")

    def test_wrong_nonce_matches_nothing(self):
        s = parse_observe_output(menu_blob(), make_nonce())
        self.assertEqual(s.phase, "unknown")
        self.assertTrue(s.errors)


class TestFingerprint(unittest.TestCase):
    def test_stable_across_seq(self):
        a = parse_observe_output(menu_blob(), N, seq=1)
        b = parse_observe_output(menu_blob(), N, seq=2)
        self.assertEqual(a.fingerprint, b.fingerprint)

    def test_changed_row_changes_fp_same_init(self):
        """A changed list shows progress even when onInit is unchanged —
        the dynamic-picker blind spot the fingerprint exists to fix."""
        a = parse_observe_output(menu_blob(), N)
        b = parse_observe_output(menu_blob(
            extra_entries=f"{M}ENT|9|0|2|33|0x402500 <SelectTeamsCommon>|s|ARSENAL{M}EOL"), N)
        self.assertEqual(a.screen, b.screen)
        self.assertNotEqual(a.fingerprint, b.fingerprint)

    def test_selection_changes_fp(self):
        a = parse_observe_output(menu_blob(), N)
        b = parse_observe_output(
            menu_blob(sel=f"{M}SEL|1|0x402100 <CareerMenu>"), N)
        self.assertNotEqual(a.fingerprint, b.fingerprint)

    def test_enable_state_changes_fp(self):
        a = parse_observe_output(menu_blob(), N)
        hostile = menu_blob().replace("ENT|2|1|", "ENT|2|0|")
        b = parse_observe_output(hostile, N)
        self.assertNotEqual(a.fingerprint, b.fingerprint)


if __name__ == "__main__":
    unittest.main()
