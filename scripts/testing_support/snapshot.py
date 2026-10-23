#!/usr/bin/env python3
"""Snapshot model + the batched observe script + its parser. PURE — no
debugger I/O, so every function here is unit-testable offline against
recorded gdb output (docs/TESTING_FLOWS_DESIGN.md, "State observation").

The observe script is ONE gdb script sourced in ONE round trip. It walks the
current menu's entries exactly as the game does (`i < numEntries`,
MENUENTRY_STRIDE via CalcMenuEntryAddress) — NOT `<=`: numEntries is a count
(src/menu_system.c DrawMenu iterates `i < count`), and the historical
scan_text() `<=` loop read one entry past the end.

Every record is framed with a per-observation NONCE so a label can never be
mistaken for framing, whatever bytes it contains (pipes, newlines). Labels
are read only from content forms whose pointer semantics we understand
(include/menu_system.h):

  content 2 ENTRY_STRING          fg is a char* label            kind 's'
  content 4 ENTRY_STRING_TABLE    fg -> {void *indexPtr; u16 base;
                                   void *entries[]} (packed); the current
                                   label is entries[*indexPtr - base] —
                                   decoded the way DrawMenuItem does  kind 'b'
  content 5 ENTRY_MULTI_LINE_TEXT fg is a counted multi-line blob; not
                                   decoded in v1                      kind 'm'
  content 6 ENTRY_NUMBER          fgWord is the integer shown         kind 'n'
  content 1 ENTRY_FUNC2           fg is a draw function; its symbol
                                   name is the row's identity          kind 'f'
  anything else                                                        kind '-'

An arbitrary nonzero fg is NOT proof of a safe string pointer; the > 0x10000
pointer-sanity guard is applied on top of the content-form gate, never
instead of it.
"""
import hashlib
import re
import secrets
from dataclasses import dataclass, field
from typing import Dict, List, Optional

# Content forms (include/menu_system.h) the label reader understands.
ENTRY_FUNC2 = 1
ENTRY_STRING = 2
ENTRY_STRING_TABLE = 4
ENTRY_MULTI_LINE_TEXT = 5
ENTRY_NUMBER = 6

# Domain globals worth carrying in every snapshot, when the build has them.
# name -> (symbol to probe, gdb expression, printf format). Presence is
# probed once per session by the adapter; absent symbols are simply left out
# of the script (a missing symbol inside a sourced script would abort it
# mid-way).
DOMAIN_CANDIDATES = {
    "seasonUpdateInProgress": ("_seasonUpdateInProgress",
                               "(int)_seasonUpdateInProgress", "%d"),
    "gameType": ("_gameType", "(int)_gameType", "%d"),
}


@dataclass
class EntryRow:
    ordinal: int
    enabled: bool             # not isDisabled
    content: int
    control_mask: int
    callback: Optional[str]   # onSelect symbol name, None if 0/unresolved
    callback_addr: int
    label_kind: str           # 's' 'b' 'm' 'n' 'f' '-'
    label: Optional[str]


@dataclass
class Snapshot:
    phase: str                        # menu | transition | unknown
    screen: Optional[str]             # onInit symbol name
    screen_addr: int
    num_entries: Optional[int]
    selected_ordinal: Optional[int]
    selected_callback: Optional[str]
    entries: List[EntryRow]           # VISIBLE entries only
    domain: Dict[str, int]
    fingerprint: str
    seq: int
    complete: bool                    # END marker seen (script ran through)
    errors: List[str] = field(default_factory=list)
    stats: Dict[str, float] = field(default_factory=dict)

    def to_dict(self):
        d = dict(self.__dict__)
        d["entries"] = [dict(e.__dict__) for e in self.entries]
        return d


def make_nonce() -> str:
    return secrets.token_hex(6)


def build_observe_script(nonce: str, domain: Optional[Dict[str, tuple]] = None) -> str:
    """The batched gdb script for one observation. `domain` is the PROBED
    subset of DOMAIN_CANDIDATES as name -> (expr, fmt) (adapter supplies it);
    None means none."""
    m = f"@{nonce}@"
    lines = [
        f'printf "{m}BEGIN\\n"',
        "set $mn = (Menu *)_currentMenu",
        "if ($mn == 0)",
        f'  printf "{m}NOMENU\\n"',
        "else",
        f'  printf "{m}INIT|"',
        "  output/a $mn->onInit",
        f'  printf "\\n{m}NENT|%u\\n", $mn->numEntries',
        "  set $se = $mn->selectedEntry",
        "  if ($se != 0)",
        f'    printf "{m}SEL|%u|", $se->ordinal',
        "    output/a $se->onSelect",
        '    printf "\\n"',
        "  else",
        f'    printf "{m}SEL|-|\\n"',
        "  end",
        "  set $n = $mn->numEntries",
        # entry address arithmetic instead of calling CalcMenuEntryAddress():
        # an inferior call per entry is slow over the FS-UAE remote. Mirrors
        # MENU_HEADER_SIZE / MENUENTRY_STRIDE (include/menu_system.h), sized by
        # the TARGET's pointer width.
        "  set $hdr = (sizeof(void *) == 8) ? 38 : 22",
        "  set $stride = (sizeof(void *) == 8) ? 76 : 56",
        "  set $i = 0",
        "  while $i < $n",
        "    set $e = (MenuEntry *)((char *)$mn + $hdr + $i * $stride)",
        "    if ($e->isInvisible == 0)",
        f'      printf "{m}ENT|%u|%u|%u|%u|", $i, $e->isDisabled, $e->content, $e->controlMask',
        "      output/a $e->onSelect",
        # label: decide kind + payload via convenience vars, then emit framed
        "      set $lk = 45",            # '-'
        "      set $lp = (char *)0",
        f"      if ($e->content == {ENTRY_STRING} && (unsigned long)$e->fg > 0x10000)",
        "        set $lk = 115",         # 's'
        "        set $lp = (char *)$e->fg",
        "      end",
        f"      if ($e->content == {ENTRY_MULTI_LINE_TEXT} && (unsigned long)$e->fg > 0x10000)",
        "        set $lk = 109",         # 'm'
        "      end",
        f"      if ($e->content == {ENTRY_NUMBER})",
        "        set $lk = 110",         # 'n'
        "      end",
        f"      if ($e->content == {ENTRY_FUNC2} && (unsigned long)$e->fg > 0x10000)",
        "        set $lk = 102",         # 'f'
        "      end",
        f"      if ($e->content == {ENTRY_STRING_TABLE} && (unsigned long)$e->fg > 0x10000)",
        # { void *indexPtr; u16 base; void *entries[] } packed — decoded as
        # DrawMenuItem does, with pointer-sanity + index-sanity guards.
        "        set $th = (char *)$e->fg",
        "        set $tip = *(void **)$th",
        "        if ((unsigned long)$tip > 0x10000)",
        "          set $tix = (int)*(unsigned short *)$tip - (int)*(unsigned short *)($th + sizeof(void *))",
        "          if ($tix >= 0 && $tix < 512)",
        "            set $tsp = *(char **)($th + sizeof(void *) + 2 + $tix * sizeof(void *))",
        "            if ((unsigned long)$tsp > 0x10000)",
        "              set $lk = 98",    # 'b'
        "              set $lp = $tsp",
        "            end",
        "          end",
        "        end",
        "      end",
        '      printf "|%c|", $lk',
        "      if ($lp != 0)",
        '        printf "%s", $lp',
        "      end",
        "      if ($lk == 110)",
        '        printf "%u", $e->fgWord',
        "      end",
        "      if ($lk == 102)",
        "        output/a $e->fg",
        "      end",
        f'      printf "{m}EOL\\n"',
        "    end",
        "    set $i = $i + 1",
        "  end",
        "end",
    ]
    if domain:
        fmt = "|".join(f"{name}={df}" for name, (_, df) in sorted(domain.items()))
        args = ", ".join(expr for _, (expr, _) in sorted(domain.items()))
        lines.append(f'printf "{m}DOM|{fmt}\\n", {args}')
    lines.append(f'printf "{m}END\\n"')
    return "\n".join(lines) + "\n"


# `output/a` prints `0x1234 <SymbolName>` (or `0x1234 <Sym+12>`), or bare
# `0x0`. Symbols can carry a `+offset` which we strip — identity is the
# containing function.
_ADDR_SYM = re.compile(r"0x([0-9a-fA-F]+)(?:\s*<([^>+]+)(?:\+\d+)?>)?")


def _addr_sym(text: str):
    mm = _ADDR_SYM.search(text or "")
    if not mm:
        return 0, None
    addr = int(mm.group(1), 16)
    sym = mm.group(2)
    return addr, (sym if (sym and addr) else None)


def parse_observe_output(text: str, nonce: str, seq: int = 0) -> Snapshot:
    """Parse one observation's raw gdb output into a Snapshot."""
    m = re.escape(f"@{nonce}@")
    errors: List[str] = []
    snap = Snapshot(phase="unknown", screen=None, screen_addr=0,
                    num_entries=None, selected_ordinal=None,
                    selected_callback=None, entries=[], domain={},
                    fingerprint="", seq=seq, complete=False)

    if not re.search(m + "BEGIN", text):
        errors.append("observe script produced no output (target not stopped, "
                      "or the script failed before its first printf)")
        snap.errors = errors
        snap.fingerprint = _fingerprint(snap)
        return snap

    snap.complete = re.search(m + "END", text) is not None
    if not snap.complete:
        errors.append("END marker missing — the script aborted mid-way "
                      "(a read faulted or a symbol was missing); the "
                      "snapshot may be partial")

    if re.search(m + "NOMENU", text):
        snap.errors = errors
        snap.fingerprint = _fingerprint(snap)
        return snap

    init = re.search(m + r"INIT\|(.*)", text)
    if init:
        snap.screen_addr, snap.screen = _addr_sym(init.group(1))

    nent = re.search(m + r"NENT\|(\d+)", text)
    if nent:
        snap.num_entries = int(nent.group(1))

    sel = re.search(m + r"SEL\|(-|\d+)\|(.*)", text)
    if sel and sel.group(1) != "-":
        snap.selected_ordinal = int(sel.group(1))
        _, snap.selected_callback = _addr_sym(sel.group(2))

    # ENT records: header fields, then callback, then |<kind>|<label> up to
    # the EOL frame. DOTALL because a label may legally contain newlines.
    ent_re = re.compile(
        m + r"ENT\|(\d+)\|(\d+)\|(\d+)\|(\d+)\|(.*?)\|(.)\|(.*?)" + m + "EOL",
        re.DOTALL)
    for g in ent_re.finditer(text):
        cb_addr, cb_sym = _addr_sym(g.group(5))
        kind = g.group(6)
        label: Optional[str] = g.group(7)
        if kind == "f":
            _, label = _addr_sym(label or "")
        elif kind in ("-", "m"):
            label = None
        snap.entries.append(EntryRow(
            ordinal=int(g.group(1)),
            enabled=int(g.group(2)) == 0,
            content=int(g.group(3)),
            control_mask=int(g.group(4)),
            callback=cb_sym, callback_addr=cb_addr,
            label_kind=kind, label=label))

    dom = re.search(m + r"DOM\|(.*)", text)
    if dom:
        for part in dom.group(1).split("|"):
            if "=" in part:
                k, _, v = part.partition("=")
                try:
                    snap.domain[k] = int(v)
                except ValueError:
                    errors.append(f"domain value unparsable: {part!r}")

    if snap.screen_addr:
        snap.phase = ("transition"
                      if snap.domain.get("seasonUpdateInProgress") else "menu")
    snap.errors = errors
    snap.fingerprint = _fingerprint(snap)
    return snap


def _fingerprint(snap: Snapshot) -> str:
    """Identity of what is on screen + what is selected + relevant domain
    state. Includes row identities so a CHANGED LIST shows progress even when
    the menu initializer is unchanged; excludes seq/stats/errors."""
    h = hashlib.sha256()
    h.update(f"screen={snap.screen or ''}@{snap.screen_addr:x};"
             f"sel={snap.selected_ordinal};phase={snap.phase};".encode())
    for e in snap.entries:
        h.update(f"|{e.ordinal}:{e.label_kind}:{e.label or ''}:"
                 f"{e.callback or ''}@{e.callback_addr:x}:"
                 f"{int(e.enabled)}:{e.control_mask:x}".encode())
    for k in sorted(snap.domain):
        h.update(f";{k}={snap.domain[k]}".encode())
    return h.hexdigest()[:16]
