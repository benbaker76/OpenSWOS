# OpenSWOS Setup's languages

The game's own: English (the source), `de`, `fr`, `es`, `it`, `nl`. One gettext file per language,
`<code>/LC_MESSAGES/openswos-setup.po`, serves both halves of the setup (app/tr.hpp):

- the `.slint` files' `@tr("...")` texts: bundled into the program by the Slint compiler
  (CMake `SLINT_BUNDLE_TRANSLATIONS`, no per-component context: one English text, one msgid);
- the texts C++ makes -- statuses, what was found, the C library's step names, the GAME OPTIONS:
  `T("...")` / `Tf("... {0} ...", args)` in main.cpp look them up in `../tr_table.inc`, made from
  the same files. `N_("...")` marks an English text translated later, where it is shown.

Placeholders are `{0}`, `{1}`, ... in both. A text with no translation shows in English.

After changing a text (or adding one):

```bash
python3 extract.py          # openswos-setup.pot: every text, from the sources
# update each .po from the .pot (msgmerge, Poedit, or by hand), translate what is new
python3 make_tr.py --check  # what is missing, placeholders that differ
python3 make_tr.py          # ../tr_table.inc (committed: the build needs no Python)
```

The translations were made by machine (2026-11) and want a native speaker's eye: corrections
are welcome as pull requests to the `.po` files.

The picker's flags are ours, in `../ui/flags` (make_flags.py).

**After changing a `.po` file, touch `../ui/app.slint`** (and `wizard.slint`) before building:
the Slint compiler bundles the translations when it compiles the UI, and the build does not
know the `.po` files are its inputs -- an unchanged UI keeps the old translations (2026-11-04:
the Mods screen's labels stayed English until it was touched).
