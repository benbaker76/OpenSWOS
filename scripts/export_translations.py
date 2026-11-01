#!/usr/bin/env python3
"""Export the compiled UI strings to a per-language translation table.

Scans src/strings_data.c + src/swos_amiga_strings.c for `char NAME[] = "TEXT";`
definitions and writes bin/data_json/translations/en.json — a { symbol -> text }
map that the runtime tr() lookup (src/translations.c) resolves menu/UI text
through. en.json IS the English baseline (text identical to the compiled
strings); other languages are copies of it with the values translated.

This is the string analogue of the menu/competition JSON: the data files carry
the symbol NAME (e.g. "_aDivision1"), never the literal text, and the language
file maps name -> text. en.json is TRACKED in git, and the game reads it at start: a
string listed there wins over the compiled text, so a string changed in the C shows the
OLD text until this is rerun. Change a UI string -> run this -> commit en.json with it.

Run from the repo root:  python3 scripts/export_translations.py
                         python3 scripts/export_translations.py --check   # 1 if en.json is stale
"""
import os
import re
import sys
import json

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRCS = [os.path.join(REPO, "src", "strings_data.c"),
        os.path.join(REPO, "src", "swos_amiga_strings.c")]
OUT_DIR = os.path.join(REPO, "bin", "data_json", "translations")

# `[const] char NAME[opt-_size] = "BODY";` on one line. BODY allows C escapes.
DEF = re.compile(r'^\s*(?:const\s+)?char\s+([A-Za-z_]\w*)\s*\[\s*\d*\s*\]\s*=\s*'
                 r'"((?:[^"\\]|\\.)*)"\s*;', re.M)


def decode_c(body: str) -> str:
    """Decode the C escapes that show _up in SWOS strings, including the octal
    (\\NNN) and hex (\\xNN) escapes the packed country names use."""
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c == '\\' and i + 1 < len(body):
            n = body[i + 1]
            if n in '01234567':
                m = re.match(r'[0-7]{1,3}', body[i + 1:i + 4])
                out.append(chr(int(m.group(0), 8))); i += 1 + len(m.group(0))
            elif n == 'x':
                m = re.match(r'[0-9a-fA-F]{1,2}', body[i + 2:i + 4])
                out.append(chr(int(m.group(0), 16))); i += 2 + len(m.group(0))
            else:
                out.append({'n': '\n', 't': '\t', 'r': '\r'}.get(n, n)); i += 2
        else:
            out.append(c); i += 1
    return ''.join(out)


def split_country(v: str):
    """If `v` is a packed country/region name — a leading REGION byte then
    NAME\\0NATIONALITY — return [name, nationality]; else None. The leading byte is
    the continent index (0=Europe, 1=N.America, 2=S.America, 3=Asia, 4=Oceania,
    5=Africa), NOT a 0/1 flag — testing `<= 1` only caught Europe/N.America and
    dropped every other country (Argentina, Australia, India, Algeria…) into the
    plain "strings" section, where its leading byte + embedded NUL got JSON-escaped
    (\\u0002…\\u0000…) and rendered as literal garbage ("⌧0002SOUTH AMERICA").
    Any non-printable leading byte (< 0x20) before a NAME\\0NAT pair is a packed
    country. _countriesTable[] entries point at these; consumers read `str + 1`
    (skip the region byte) _up to the NUL, so it must be decomposed here, never
    emitted as a raw string with binary bytes in it."""
    if len(v) >= 2 and ord(v[0]) < 0x20 and '\x00' in v[1:]:
        parts = v[1:].split('\x00')
        return [parts[0], parts[1] if len(parts) > 1 else parts[0]]
    return None


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    strings = {}
    countries = {}
    dups = 0
    for src in SRCS:
        text = open(src, encoding="utf-8", errors="replace").read()
        for m in DEF.finditer(text):
            name, body = m.group(1), decode_c(m.group(2))
            if name in strings or name in countries:
                dups += 1
                continue            # first definition wins
            cc = split_country(body)
            if cc is not None:
                countries[name] = cc          # ["NAME", "NATIONALITY"] — clean, no escapes
            else:
                strings[name] = body

    out = {"language": "en",
           "strings": dict(sorted(strings.items())),
           "countries": dict(sorted(countries.items()))}
    path = os.path.join(OUT_DIR, "en.json")
    # ensure_ascii=True so the handful of strings carrying a high byte (0xFF
    # render marker, 0x9C pound glyph) are written as \uXXXX like the control
    # bytes already are — one consistent escape form for translations.c's
    # tr_dup() to decode back to a single byte. (Plain text is unaffected.)
    text = json.dumps(out, ensure_ascii=True, indent=1) + "\n"
    if "--check" in sys.argv[1:]:
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
        if old != text:
            print("%s is stale: a UI string changed in src/ without rerunning "
                  "scripts/export_translations.py" % os.path.relpath(path, REPO))
            sys.exit(1)
        print("%s is current" % os.path.relpath(path, REPO))
        return
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote %s: %d strings + %d countries (%d duplicate names skipped)"
          % (os.path.relpath(path, REPO), len(strings), len(countries), dups))


if __name__ == "__main__":
    main()
