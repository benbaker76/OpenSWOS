#!/usr/bin/env python3
"""Import SWOS 2020 menus (swos-2020/src/menus/*.mnu) into OpenSWOS's JSON menu format.

See docs/MENUS.md §10. SWOS 2020 describes its menus in a DSL compiled by its own
`mnu2h` tool into the DOS binary menu format -- the same opcode stream UnpackMenu
walks (docs/MENUS.md §0), in the DOS dialect: always-wide coords, deferred text as
opcode 8 with a -1 pointer. Rather than re-implement the DSL (#include, #repeat,
#join, expressions, delayed default properties, forward entry references), this
script imports swos-2020/mnu2h as a library, lets its Parser digest each file exactly
as the SWOS 2020 build does, and replaces its C++ CodeGenerator with a JSON backend.
The emission order below mirrors CodeGenerator.getEntryStructs.

Outputs:
  bin/data_json/menus/swos2020/<menuName>.json   one per menu, loaded at run time by
                                                 MenuJson_Load("swos2020/<menuName>")
  include/swos2020_menus.h                       entry ordinals, exported constants, and
                                                 the menu + callback lists as X-macros

What changes on the way in, and why:
  * Coordinates import UNCHANGED. DOS ConvertMenu adds (8, 0) to every entry and the
    Amiga UnpackMenu adds (24, 36): the same 304x200 menu panel at a different place
    on screen, so a packed coordinate names the same spot on both.
  * `text: @kAlloc` (a -1 pointer) becomes `text_no_ptr`, the Amiga's deferred-text
    opcode. The -1 would not survive a 64-bit build: UnpackMenu's allocation pass
    looks for the 32-bit 0xFFFFFFFF sentinel.
  * Quoted text becomes `"literal"` -- SWOS 2020 menus carry their strings inline,
    ours name a compiled string symbol. The loader interns literals.
  * Every menu gets `menu_flags.dos_redraw`: the DOS MenuProc redraws the whole menu,
    onDraw included, every frame, and SWOS 2020's handlers are written against that.

Run from the repo root:
  python3 scripts/import_swos2020_menus.py [--swos2020 DIR] [menu ...]
With no menu names, imports MULTIPLAYER_MENUS.
"""
import argparse
import ast
import json
import os
import subprocess
import sys
import types

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "bin", "data_json", "menus", "swos2020")
OUT_H = os.path.join(REPO, "include", "swos2020_menus.h")
SYMBOLS = os.path.join(REPO, "bin", "data_json", "menus", "_symbols.json")
DEFAULT_SWOS2020 = os.environ.get("SWOS2020_DIR", "/mnt/c/Projects/GitHub/swos-2020")

# The menus SWOS 2020's online play is built from. The comment on each group is the
# swos-2020/src file that #includes the generated .mnu.h -- i.e. where the menu's
# handler functions live, which is where to look when porting one.
MULTIPLAYER_MENUS = [
    # login, account, game setup                                    (game.cpp)
    "logIn", "news", "message", "aboutMP", "mpGameSetup",
    # matchmaking, lobby, rankings, leagues, match logs             (mpgame.cpp)
    "matchMaking", "openLobby", "mpWaiting", "hostGameMessage", "mmSettings",
    "rankings", "ranking", "playerInfo", "matchInfo", "oooInfo", "tournaInfo",
    "leagueInfoMain", "leagueInfo1", "leagueInfo2", "leagueInfoView",
    "matchView", "cupMatchView", "selectMyCustomTeams", "selectFunCupTeams",
    "mlogReader", "mlogTicker", "mlogStats",
    # online stats                                                  (replays/replaysMenu.cpp)
    "myRankedStats", "myFriendlyStats",
    # the pre-match uniform selector, from the PLAY MATCH screen    (game.cpp)
    "selectUniform",
]

# SWOS 2020's skip opcodes, by NUMBER (menuCodes.h: kLeftSkip = 17 ... kDownSkip = 20),
# mapped onto our JSON op names for the same numbers. Our names for 17..20 do not say
# which MenuEntry field the opcode fills -- UnpackMenu case 17 writes skipLeftEntry,
# exactly as kLeftSkip does -- so the mapping is by number, not by name.
SKIP_OPS = (("Left", "up_skip"), ("Right", "down_skip"), ("Up", "left_skip"), ("Down", "right_skip"))


def die(msg):
    sys.exit("import_swos2020_menus: " + msg)


# ---------------------------------------------------------------- expressions
# mnu2h hands back most values as C expression strings ("(120 -8 -5)", "(7 | 16)",
# "kGray"). Evaluate them with C integer semantics: `/` and `%` truncate toward zero.
def _c_div(a, b):
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def _ev(node, names):
    if isinstance(node, ast.Constant) and isinstance(node.value, int) and not isinstance(node.value, bool):
        return node.value
    if isinstance(node, ast.Name):
        if node.id not in names:
            raise ValueError("unknown name `%s'" % node.id)
        return c_int(names[node.id], names, node.id)
    if isinstance(node, ast.UnaryOp):
        v = _ev(node.operand, names)
        if isinstance(node.op, ast.USub):
            return -v
        if isinstance(node.op, ast.UAdd):
            return v
        if isinstance(node.op, ast.Invert):
            return ~v
        if isinstance(node.op, ast.Not):
            return int(not v)
    if isinstance(node, ast.BinOp):
        a, b = _ev(node.left, names), _ev(node.right, names)
        op = node.op
        if isinstance(op, ast.Add):
            return a + b
        if isinstance(op, ast.Sub):
            return a - b
        if isinstance(op, ast.Mult):
            return a * b
        if isinstance(op, (ast.Div, ast.FloorDiv)):
            return _c_div(a, b)
        if isinstance(op, ast.Mod):
            return a - b * _c_div(a, b)
        if isinstance(op, ast.LShift):
            return a << b
        if isinstance(op, ast.RShift):
            return a >> b
        if isinstance(op, ast.BitOr):
            return a | b
        if isinstance(op, ast.BitAnd):
            return a & b
        if isinstance(op, ast.BitXor):
            return a ^ b
    raise ValueError("unsupported expression node %s" % type(node).__name__)


def c_int(expr, names, where):
    if isinstance(expr, bool):
        return int(expr)
    if isinstance(expr, int):
        return expr
    if expr is None:
        return 0
    text = str(expr).strip().replace("@", "")
    try:
        return _ev(ast.parse(text, mode="eval").body, names)
    except (SyntaxError, ValueError, ZeroDivisionError) as e:
        die("%s: can't evaluate `%s': %s" % (where, expr, e))


def unparen(text):
    text = text.strip()
    while text.startswith("(") and text.endswith(")"):
        text = text[1:-1].strip()
    return text


_data_symbols = None


def load_data_symbols():
    """Every string/buffer name the menu symbol registry knows (scripts/export_menus.py)."""
    global _data_symbols
    if _data_symbols is None:
        with open(SYMBOLS) as f:
            manifest = json.load(f)
        _data_symbols = set(manifest.get("strings", [])) | set(manifest.get("buffers", []))
    return _data_symbols


# ---------------------------------------------------------------- fix-ups
#
# Gaps in SWOS 2020's OWN menu definitions, mended here so a re-import keeps the
# mend. Keyed by menu stem, then entry name; each value is the directions to set,
# by entry NAME (or -1 for none). Anything not named is left as the .mnu has it.
#
# logIn: passwordInput sends the cursor DOWN into rememberLogInInfo, but that
# entry and the two toggles under it carry no navigation of their own, so the
# cursor arrives and cannot leave in any direction -- reported 2026-09-20, "when
# I move to the REMEMBER LOGIN INFO setting it gets stuck". The buttons below
# reach back UP to passwordInput, over the toggles, which is the other half of
# the same gap. So: give the three toggles the column they already look like,
# and let the two button columns come back to the last of them. showPassword is
# hidden at run time (online_menus.c) and the menu walks THROUGH an invisible
# entry by its own links, so it keeps them -- but the visible two link to each
# other directly rather than through it. A tester's cursor still stuck on REMEMBER
# LOGIN INFO on 2026-09-24 in a way no other machine reproduced; the walk through a
# hidden entry was the one unusual step on that path, and it has no reason to be
# there when the entry is hidden for good.
NAV_FIXUPS = {
    "logIn": {
        "rememberLogInInfo": {"up": "passwordInput",     "down": "liveCast"},
        "showPassword":      {"up": "rememberLogInInfo", "down": "liveCast"},
        "liveCast":          {"up": "rememberLogInInfo", "down": "logInContinue"},
        "logInContinue":     {"up": "liveCast"},
        "registerUser":      {"up": "liveCast"},
    },
}


# ---------------------------------------------------------------- our entries
#
# Entries OpenSWOS ADDS to an imported menu, so a re-import keeps them. Appended after
# the menu's own, so every SWOS 2020 ordinal stays what it was; the header names them
# like the rest. Coordinates are the menu's own (origin 0,0). Keyed by menu stem.
#
# mpGameSetup: the HOST'S SWITCHES (include/netplay_match.h, report 0042), on the left
# where SWOS 2020 has the chat this lobby hides -- one row each, level with the rules
# column, a label and an ON/OFF value the host selects. Navigation is set in code
# (online_menus.c lobby_layout), as for the rest of this screen.
def _switch_rows():
    rows = []
    # (CATCH UP and BALANCE were rows too, until 2026-10-30: removed, netplay problems)
    for i, (name, label) in enumerate((("ShortPresses", "SHORT PRESSES"),
                                       ("KickoffSync", "KICK-OFF SYNC"),
                                       ("CardsInjuries", "CARDS/INJURIES"))):
        y = 93 + 13 * i
        rows.append({"kind": "entry", "name": "switch%sLabel" % name,
                     "coords": {"x": -8, "y": y, "w": 120, "h": 12},
                     "commands": [{"op": "text", "flags": "0x0", "literal": label},
                                  {"op": "colors", "frame_color": 0, "background_color": 7}]})
        rows.append({"kind": "entry", "name": "switch%s" % name,
                     "coords": {"x": 113, "y": y, "w": 48, "h": 12},
                     "commands": [{"op": "text_no_ptr", "flags": "0x0"},
                                  {"op": "colors", "frame_color": 0, "background_color": 13},
                                  {"op": "onselect_fire", "func": "selectSwitch%s" % name}]})
    return rows


EXTRA_ENTRIES = {
    "mpGameSetup": _switch_rows(),
}


def apply_extra_entries(stem, items, entries, functions):
    """Append EXTRA_ENTRIES[stem] to the built items, and to `entries` for the header."""
    extra = EXTRA_ENTRIES.get(stem, [])
    origin = next(((it["x"], it["y"]) for it in reversed(items) if it["kind"] == "set_origin"), (0, 0))
    if extra and origin != (0, 0):
        items.append({"kind": "set_origin", "x": 0, "y": 0})
    for item in extra:
        items.append(json.loads(json.dumps(item)))
        entries.append(types.SimpleNamespace(name=item["name"], ordinal=len(entries)))
        for c in item["commands"]:
            if c.get("func"):
                functions.add(c["func"])
    return len(extra)


def apply_nav_fixups(stem, items, entries):
    """Set the navigation NAV_FIXUPS names, by entry name, on the built items."""
    fixups = NAV_FIXUPS.get(stem)
    if not fixups:
        return 0
    ordinals = {e.name: i for i, e in enumerate(entries) if e.name}
    by_name = {it.get("name"): it for it in items if it.get("kind") == "entry" and it.get("name")}
    changed = 0
    for name, dirs in fixups.items():
        item = by_name.get(name)
        if item is None:
            die("%s: nav fix-up names %s, which the menu does not have" % (stem, name))
        nav = next((c for c in item["commands"] if c.get("op") == "nav"), None)
        if nav is None:
            nav = {"op": "nav", "left": -1, "right": -1, "up": -1, "down": -1}
            item["commands"].append(nav)
        for direction, target in dirs.items():
            if target != -1 and target not in ordinals:
                die("%s.%s: nav fix-up points %s at %s, which the menu does not have"
                    % (stem, name, direction, target))
            nav[direction] = -1 if target == -1 else ordinals[target]
        changed += 1
    return changed


# ---------------------------------------------------------------- entries
class MenuImporter:
    def __init__(self, mnu2h, menu_name, menu, src_rel, commit):
        self.mnu2h = mnu2h
        self.name = menu_name
        self.menu = menu
        self.src_rel = src_rel
        self.commit = commit
        self.names = dict(mnu2h.Constants.kConstants)
        # A few menus colour an entry `kNone`, which mnu2h passes through to C++ where
        # it is swos-2020's src/controls/controls.h `enum Controls` first member: 0,
        # i.e. no background.
        self.names.setdefault("kNone", 0)
        for var, (value, _token) in menu.exportedVariables.items():
            self.names[var] = value
        self.functions = set()
        self.data_symbols = load_data_symbols()

    def where(self, entry=None):
        label = self.name
        if entry is not None:
            label += "." + (entry.name or "#%d" % entry.ordinal)
        return label

    def func(self, value, entry=None):
        """A handler function reference -> its name, or None."""
        if value in (None, 0, "0", "nullptr"):
            return None
        name = str(value)
        if not name.isidentifier():
            # ~"SWOS::foo" style raw names (a SWOS asm routine in SWOS 2020). Keep the
            # last component; the registry will report it unresolved until ported.
            clean = name.replace("::", "_").replace(".", "_")
            print("  WARN %s: handler `%s' renamed `%s'" % (self.where(entry), name, clean))
            name = clean
        self.functions.add(name)
        return name

    def string_symbol(self, name, entry):
        """SWOS 2020 names a SWOS string the DOS way (`aExit`); our registry has the
        same string with a leading underscore (`_aExit`). Using ours also makes the
        label translatable through translations/<lang>.json."""
        for candidate in ("_" + name, name):
            if candidate in self.data_symbols:
                return candidate
        print("  WARN %s: string `%s' is not in the menu symbol registry" % (self.where(entry), name))
        return name

    def flags(self, value, entry):
        return "0x%X" % (c_int(value, self.names, self.where(entry)) & 0xFFFF)

    def commands(self, e):
        Util = self.mnu2h.Util
        where = self.where(e)
        cmds = []

        if e.text is not None:
            raw = str(e.text).strip()
            bare = unparen(raw)
            cmd = {"op": "text", "flags": self.flags(e.textFlags, e)}
            if bare == "-1":                          # @kAlloc: filled in at run time
                cmd["op"] = "text_no_ptr"
            elif bare == "0":                         # @kNull: pointer assigned by code
                cmd["str"] = None
            elif Util.isString(raw):
                cmd["literal"] = Util.unquotedString(raw)
            elif raw.isidentifier():                  # a SWOS string variable
                cmd["str"] = self.string_symbol(raw, e)
            else:
                die("%s: unsupported text value `%s'" % (where, raw))
            cmds.append(cmd)

        if e.stringTable:
            die("%s: stringTable is not supported by the importer yet" % where)

        if e.number is not None:
            cmds.append({"op": "number", "flags": self.flags(e.textFlags, e),
                         "value": c_int(e.number, self.names, where)})

        if e.sprite is not None:
            cmds.append({"op": "sprite_fg", "index": c_int(e.sprite, self.names, where)})

        if e.customDrawForeground is not None:
            cmds.append({"op": "fg_func", "func": self.func(e.customDrawForeground, e)})

        if e.customDrawBackground is not None:
            cmds.append({"op": "bg_func", "func": self.func(e.customDrawBackground, e)})

        color = c_int(e.color, self.names, where)
        if color:
            cmds.append({"op": "colors", "frame_color": (color >> 4) & 0x0F,
                         "background_color": color & 0x0F})

        if c_int(e.invisible, self.names, where):
            cmds.append({"op": "invisible"})

        nav = [c_int(getattr(e, d), self.names, where)
               for d in ("leftEntry", "rightEntry", "upEntry", "downEntry")]
        if any(v != -1 for v in nav):
            for v in nav:
                if not -1 <= v <= 254:
                    die("%s: nav target %d does not fit the ordinal byte" % (where, v))
            cmds.append({"op": "nav", "left": nav[0], "right": nav[1], "up": nav[2], "down": nav[3]})

        for direction, op in SKIP_OPS:
            new_dir = c_int(getattr(e, "direction" + direction), self.names, where)
            skip = c_int(getattr(e, "skip" + direction), self.names, where)
            if new_dir != -1 or skip != -1:
                if new_dir == -1:
                    new_dir = self.mnu2h.Constants.kConstants["k" + direction]
                cmds.append({"op": op, "skip": skip, "dir": new_dir})

        mask = c_int(e.controlsMask, self.names, where)
        if mask:
            cmds.append({"op": "input_func", "mask": "0x%X" % (mask & 0xFFFF),
                         "func": self.func(e.onSelect, e)})
        elif e.onSelect:
            cmds.append({"op": "onselect_fire", "func": self.func(e.onSelect, e)})

        if e.beforeDraw:
            cmds.append({"op": "before_draw", "func": self.func(e.beforeDraw, e)})

        if e.onReturn:
            cmds.append({"op": "after_draw", "func": self.func(e.onReturn, e)})

        return cmds

    def build(self):
        Entry = self.mnu2h.Entry
        menu = self.menu
        props = menu.properties
        items = []

        cur_x = c_int(menu.initialX, self.names, self.name)
        cur_y = c_int(menu.initialY, self.names, self.name)
        if cur_x or cur_y:
            items.append({"kind": "set_origin", "x": cur_x, "y": cur_y})

        entries = []
        for e in menu.entries.values():
            if isinstance(e, Entry.ResetTemplateEntry):
                items.append({"kind": "reset_template"})
                continue
            is_template = e.isTemplate()
            if is_template:
                items.append({"kind": "start_template"})
            else:
                mx = c_int(e.menuX, self.names, self.where(e))
                my = c_int(e.menuY, self.names, self.where(e))
                if (mx, my) != (cur_x, cur_y):
                    items.append({"kind": "set_origin", "x": mx, "y": my})
                    cur_x, cur_y = mx, my
                entries.append(e)

            coords = {k: c_int(getattr(e, attr), self.names, self.where(e))
                      for k, attr in (("x", "x"), ("y", "y"), ("w", "width"), ("h", "height"))}
            if is_template:
                # CodeGenerator does the same: the SWOS 2020 Entry constructor asserts on 0.
                coords["w"] = coords["w"] or 1
                coords["h"] = coords["h"] or 1
            item = {"kind": "entry"}
            if e.name and not is_template:
                item["name"] = e.name
            item["coords"] = coords
            item["commands"] = self.commands(e)
            items.append(item)

        if len(entries) > 255:
            die("%s: %d entries; ordinals are a byte" % (self.name, len(entries)))

        stem = self.src_rel.rsplit("/", 1)[-1][:-4]
        apply_extra_entries(stem, items, entries, self.functions)
        if len(entries) > 255:
            die("%s: %d entries with ours; ordinals are a byte" % (self.name, len(entries)))
        apply_nav_fixups(stem, items, entries)

        doc = {
            "name": self.name,
            "source": {"project": "swos-2020", "file": self.src_rel, "commit": self.commit},
            "menu_flags": {"dos_redraw": True},
            "onInit": self.func(props.get("onInit")),
            "onReturn": self.func(props.get("onReturn")),
            "onDraw": self.func(props.get("onDraw")),
            "defaultEntry": c_int(props.get("initialEntry", 0), self.names, self.name),
            "exports": {var: c_int(value, self.names, self.name + "." + var)
                        for var, (value, _token) in menu.exportedVariables.items()},
            "items": items,
        }
        return doc, entries


# ---------------------------------------------------------------- header
def enum_prefix(menu_name):
    """Same namespace rule as mnu2h's CodeGenerator.outputMenuVariables."""
    ns = menu_name[0].upper() + menu_name[1:]
    return ns + "NS" if ns == menu_name else ns


def write_header(results, commit):
    lines = [
        "/* GENERATED by scripts/import_swos2020_menus.py -- do not edit by hand.",
        " * Source: the .mnu files in swos-2020/src/menus @ %s. See docs/MENUS.md section 10." % commit,
        " *",
        " * Entry ordinals and exported layout constants for the imported SWOS 2020",
        " * menus, spelled the way SWOS 2020's C++ spells them (LogInMenu::header ->",
        " * LogInMenu_header), so a handler ported from swos-2020 reads the same. */",
        "#ifndef SWOS2020_MENUS_H",
        "#define SWOS2020_MENUS_H",
        "",
        "/* X(menuName, loadKey, EnumPrefix, numEntries) -- loadKey is the name to pass to",
        " * MenuJson_Load(). */",
        "#define SWOS2020_MENUS(X) \\",
    ]
    for r in results:
        lines.append('    X(%s, "swos2020/%s", %s, %d) \\'
                     % (r["name"], r["name"], enum_prefix(r["name"]), len(r["entries"])))
    lines.append("")
    callbacks = sorted(set().union(*(r["functions"] for r in results)))
    lines.append("/* Every handler name the imported menus reference (%d), sorted. */" % len(callbacks))
    lines.append("#define SWOS2020_MENU_CALLBACKS(X) \\")
    for cb in callbacks:
        lines.append("    X(%s) \\" % cb)
    lines.append("")
    for r in results:
        prefix = enum_prefix(r["name"])
        lines.append("/* ---- %s -- %s ---- */" % (r["name"], r["src_rel"]))
        named = [e for e in r["entries"] if e.name]
        if named:
            lines.append("enum {")
            for e in named:
                lines.append("    %s_%s = %d," % (prefix, e.name, e.ordinal))
            lines.append("};")
        lines.append("#define %s_numEntries %d" % (prefix, len(r["entries"])))
        for var, value in r["doc"]["exports"].items():
            lines.append("#define %s_%s (%d)" % (prefix, var, value))
        lines.append("")
    lines.append("#endif /* SWOS2020_MENUS_H */")
    with open(OUT_H, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------- main
class Mnu2h:
    """The swos-2020/mnu2h modules, imported from its directory."""
    def __init__(self, root):
        tool = os.path.join(root, "mnu2h")
        if not os.path.isfile(os.path.join(tool, "Parser.py")):
            die("no mnu2h at %s (pass --swos2020 or set SWOS2020_DIR)" % tool)
        sys.path.insert(0, tool)
        import Constants, Entry, Parser, Util   # noqa: E401 (mnu2h's own module names)
        self.Constants, self.Entry, self.Parser, self.Util = Constants, Entry, Parser, Util


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--swos2020", default=DEFAULT_SWOS2020, help="swos-2020 checkout")
    ap.add_argument("menus", nargs="*", help="menu file stems (default: MULTIPLAYER_MENUS)")
    args = ap.parse_args()

    root = os.path.abspath(args.swos2020)
    mnu2h = Mnu2h(root)
    try:
        commit = subprocess.check_output(["git", "-C", root, "rev-parse", "--short", "HEAD"],
                                         text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        commit = "unknown"

    os.makedirs(OUT_DIR, exist_ok=True)
    results = []
    for stem in args.menus or MULTIPLAYER_MENUS:
        src_rel = "src/menus/%s.mnu" % stem
        path = os.path.join(root, src_rel)
        if not os.path.isfile(path):
            die("missing %s" % path)
        parser = mnu2h.Parser.Parser(path)
        for menu_name, menu in parser.getMenus().items():
            imp = MenuImporter(mnu2h, menu_name, menu, src_rel, commit)
            doc, entries = imp.build()
            with open(os.path.join(OUT_DIR, menu_name + ".json"), "w", newline="\n") as f:
                json.dump(doc, f, indent=2)
                f.write("\n")
            results.append({"name": menu_name, "src_rel": src_rel, "doc": doc,
                            "entries": entries, "functions": imp.functions})
            print("  %-26s %3d entries  %2d handlers  <- %s"
                  % (menu_name, len(entries), len(imp.functions), src_rel))

    names = [r["name"] for r in results]
    if len(set(names)) != len(names):
        die("duplicate menu names: %r" % sorted(n for n in names if names.count(n) > 1))
    write_header(results, commit)
    total = sum(len(r["entries"]) for r in results)
    callbacks = set().union(*(r["functions"] for r in results))
    print("Imported %d menus (%d entries, %d distinct handlers) from swos-2020 @ %s"
          % (len(results), total, len(callbacks), commit))
    print("  -> %s/" % os.path.relpath(OUT_DIR, REPO))
    print("  -> %s" % os.path.relpath(OUT_H, REPO))


if __name__ == "__main__":
    main()
