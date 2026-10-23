#!/usr/bin/env python3
"""Bounded observe→act→verify executor + run report
(docs/TESTING_FLOWS_DESIGN.md, "Execution and verification").

The executor runs a PLAN — an explicit list of steps — against an adapter
exposing observe()/act primitives. It never explores: a step that cannot
resolve its target to exactly one visible enabled entry fails with the
actual choices.

Retries are EFFECT-AWARE: an attempt is repeated only when the snapshot is
UNCHANGED since before the press (the dropped-pulse case) — a changed-but-
incomplete state stops the run as unknown_outcome rather than blindly
re-pressing (a replayed fire can double-select, cycle a control, or re-save).
Steps that are safe to repeat across state changes (memo chains) opt in with
retry_while_incomplete.
"""
import time
from typing import List, Optional

from testing_support.catalog import normalize
from testing_support.goals import (S_ASSERTION, S_PRECONDITION, S_SUCCESS,
                                   S_TIMEOUT, S_TRANSPORT, S_UNKNOWN)


# ---- predicates -------------------------------------------------------------
def check_predicate(pred: dict, snap, before=None) -> bool:
    """Evaluate one completion/precondition dict against a snapshot."""
    if not pred:
        return True
    if "screen_is" in pred:
        if snap.screen != pred["screen_is"]:
            return False
    if "screen_in" in pred:
        if snap.screen not in pred["screen_in"]:
            return False
    if "screen_not" in pred:
        if snap.screen == pred["screen_not"]:
            return False
    if pred.get("screen_changed"):
        if before is None or snap.screen == before.screen:
            return False
    if pred.get("changed"):
        if before is None or snap.fingerprint == before.fingerprint:
            return False
    if pred.get("rows_or_screen_changed"):
        if before is None:
            return False
        rows = [(e.ordinal, e.label, e.callback) for e in snap.entries]
        brows = [(e.ordinal, e.label, e.callback) for e in before.entries]
        if rows == brows and snap.screen == before.screen:
            return False
    if "label_visible" in pred:
        want = normalize(pred["label_visible"])
        if not any(normalize(e.label or "") == want for e in snap.entries):
            return False
    if "domain_eq" in pred:
        for k, v in pred["domain_eq"].items():
            if snap.domain.get(k) != v:
                return False
    return True


def find_label(snap, label: str):
    """EXACT normalized label match among visible entries. Returns
    (row, candidates): one row, or None + the near/exact candidates."""
    want = normalize(label)
    exact = [e for e in snap.entries
             if e.label and normalize(e.label) == want]
    if len(exact) == 1:
        return exact[0], exact
    return None, exact


def find_callback(snap, symbol: str):
    rows = [e for e in snap.entries if e.callback == symbol]
    return (rows[0], rows) if len(rows) == 1 else (None, rows)


class StepFailure(Exception):
    def __init__(self, status, detail, evidence=None):
        super().__init__(detail)
        self.status = status
        self.detail = detail
        self.evidence = evidence or {}


class Executor:
    def __init__(self, adapter, observe_timeout=30):
        self.a = adapter
        self.records: List[dict] = []

    # ---- actions -------------------------------------------------------------
    def _resolve_target(self, snap, action) -> Optional[int]:
        """Ordinal for an activate action, or None when the action has no
        menu target. Raises StepFailure on miss/ambiguity with evidence."""
        t = action["type"]
        if t == "activate_ordinal":
            return int(action["ordinal"])
        if t == "activate_label":
            row, cands = find_label(snap, action["label"])
            if row is None:
                raise StepFailure(
                    S_PRECONDITION,
                    f"label {action['label']!r} did not resolve to exactly "
                    f"one visible entry ({len(cands)} matches)",
                    {"visible": [(e.ordinal, e.label) for e in snap.entries
                                 if e.label]})
            if not row.enabled:
                raise StepFailure(S_PRECONDITION,
                                  f"entry {action['label']!r} is disabled",
                                  {"ordinal": row.ordinal})
            return row.ordinal
        if t == "activate_callback":
            row, cands = find_callback(snap, action["symbol"])
            if row is None:
                raise StepFailure(
                    S_PRECONDITION,
                    f"callback {action['symbol']!r} matched {len(cands)} "
                    "visible entries, need exactly 1",
                    {"visible_callbacks": sorted({e.callback for e in
                                                  snap.entries if e.callback})})
            if not row.enabled:
                raise StepFailure(S_PRECONDITION,
                                  f"entry for {action['symbol']} is disabled")
            return row.ordinal
        return None

    def _perform(self, snap, action, timeout):
        t = action["type"]
        if t in ("activate_label", "activate_callback", "activate_ordinal"):
            self.a.activate(self._resolve_target(snap, action), timeout=timeout)
        elif t == "dismiss_memo":
            self.a.pulse_short_fire(timeout=timeout)
        elif t == "raw_fire":
            self.a.pulse_raw_fire(timeout=timeout)
        elif t == "none":
            pass
        elif t == "custom":
            action["fn"](self.a, snap)
        else:
            raise StepFailure("unsupported_capability",
                              f"unknown action type {t!r}")

    # ---- the loop --------------------------------------------------------------
    def run_step(self, step: dict, snap=None):
        """One step: observe, precondition, act, verify — bounded. Returns the
        snapshot AFTER completion (for the next step to reuse)."""
        t0 = time.monotonic()
        req0 = self.a.requests
        rec = {"name": step.get("name", step["action"]["type"]),
               "action": {k: v for k, v in step["action"].items() if k != "fn"},
               "attempts": 0, "status": None}
        self.records.append(rec)
        try:
            if snap is None:
                snap = self.a.observe()
            rec["before_fingerprint"] = snap.fingerprint
            rec["before_screen"] = snap.screen

            if step.get("skip_if") and check_predicate(step["skip_if"], snap):
                rec["status"] = "skipped"
                rec["detail"] = f"skip_if matched: {step['skip_if']}"
                return snap

            pre = step.get("precondition")
            if pre and not check_predicate(pre, snap):
                raise StepFailure(S_PRECONDITION,
                                  f"precondition {pre} not met at "
                                  f"{snap.screen}",
                                  {"visible": [(e.ordinal, e.label) for e in
                                               snap.entries if e.label]})

            # an EXPLICIT empty completion ({}) means "the action verifies
            # itself" (custom asserts); only an ABSENT one defaults
            completion = step["completion"] if "completion" in step \
                else {"changed": True}
            max_attempts = step.get("max_attempts", 12)
            timeout = step.get("timeout", 20)
            baseline = snap
            for attempt in range(1, max_attempts + 1):
                rec["attempts"] = attempt
                self._perform(baseline if attempt == 1 else cur, step["action"],
                              timeout)
                cur = self.a.observe()
                if not cur.complete and not cur.entries and cur.phase == "unknown":
                    raise StepFailure(S_TRANSPORT,
                                      "observation failed after action",
                                      {"errors": cur.errors})
                if check_predicate(completion, cur, before=baseline):
                    rec["status"] = "ok"
                    rec["after_fingerprint"] = cur.fingerprint
                    rec["after_screen"] = cur.screen
                    return cur
                if cur.fingerprint == baseline.fingerprint:
                    if not step.get("safe_retry", True):
                        raise StepFailure(
                            S_UNKNOWN,
                            "no observable effect and the action is not safe "
                            "to retry — stopping for reconciliation")
                    continue      # dropped pulse: unchanged state, safe retry
                if step.get("retry_while_incomplete"):
                    baseline = cur
                    continue      # memo chains: advance baseline, keep going
                raise StepFailure(
                    S_UNKNOWN,
                    f"state changed ({baseline.screen} -> {cur.screen}) but "
                    f"completion {completion} not met — not retrying a press "
                    "whose effect is unknown",
                    {"after_screen": cur.screen,
                     "visible": [(e.ordinal, e.label) for e in cur.entries
                                 if e.label]})
            raise StepFailure(S_TIMEOUT,
                              f"completion {completion} not reached in "
                              f"{max_attempts} attempts")
        except StepFailure as f:
            rec["status"] = f.status
            rec["detail"] = f.detail
            rec["evidence"] = f.evidence
            raise
        finally:
            rec["elapsed_s"] = round(time.monotonic() - t0, 3)
            rec["requests"] = self.a.requests - req0

    def run_plan(self, plan: List[dict]) -> dict:
        """Run the whole plan; returns the report body. The target is LEFT
        STOPPED at the final verified boundary."""
        t0 = time.monotonic()
        req0 = self.a.requests
        status, detail = S_SUCCESS, ""
        snap = None
        try:
            for step in plan:
                snap = self.run_step(step, snap)
        except StepFailure as f:
            status, detail = f.status, f.detail
        return {
            "status": status,
            "detail": detail,
            "steps": self.records,
            "final_screen": snap.screen if snap else None,
            "final_fingerprint": snap.fingerprint if snap else None,
            "totals": {"elapsed_s": round(time.monotonic() - t0, 3),
                       "requests": self.a.requests - req0},
            "target_stopped": True,
        }
