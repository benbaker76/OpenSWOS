# BUILD_MACHINE_THE5AR.md — the exact build commands on *this* Windows box

Both targets build here. **The paths in `.claude/CLAUDE.md` and in `build.cmd` are the
original author's machine** (`C:\Users\headk\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\…`,
`cd /d C:\Projects\AI\SWOS\OpenSWOS`); neither exists on this one, so anything driven from
them fails as `Error 127` (compiler not found) or in the wrong directory. Use what is below.

| | this machine |
|---|---|
| repo | `C:\Users\A7126\orca\OpenSWOS` |
| m68k toolchain | `C:\Users\A7126\tools\vscode-amiga-debug\bin\win32` — a plain **clone of the owner's fork**, NOT under `.vscode\extensions` |
| Cygwin (the SDL build) | `C:\cygwin64` — 3.6.11, gcc 14.4.0, python3 3.12.12, `libSDL2-devel` 2.32.74 |
| real Python for git-bash | `C:\Users\A7126\AppData\Local\Programs\Python\Python312\python.exe` |

## SDL build (`bin/SWOS.exe`, a Cygwin binary)

The Makefile picks the Windows target from the **host shell**, never from a flag, so it
has to be driven from the Cygwin shell. From git-bash / the Bash tool:

```sh
/c/cygwin64/bin/bash.exe -lc "cd /cygdrive/c/Users/A7126/orca/OpenSWOS && make -j8"
```

That shell has its own `make`, `gcc` and a real `python3`, so the data-regen rules
(`menu_data.c`, `competition_data.c`, `tactics_data.c`) work with no `PYTHON=` override.

## Amiga m68k build (`bin/SWOS2`)

Two traps, both of which look like something else:

- **`make` is not on the git-bash PATH.** The toolchain ships `gnumake.exe`; use that name.
- **bare `python3` in git-bash is the Windows Store stub** — the regen rules die with
  `Error 49` and abort the whole build. Pass the real interpreter.

```sh
AMIGA=/c/Users/A7126/tools/vscode-amiga-debug/bin/win32
PATH="$AMIGA/opt/bin:$AMIGA:$PATH" gnumake -f Makefile.amiga -j8 \
    PYTHON=/c/Users/A7126/AppData/Local/Programs/Python/Python312/python
```

Verified 2026-10-23: `m68k-amiga-elf-gcc (GCC) 15.1.0`, links `bin/SWOS2.elf` and
Elf2Hunks it to `bin/SWOS2` — the same 15.1.0 that the committed `obj/*.s` banners name,
i.e. this is the toolchain the tree was last built with.

`gnumake -f Makefile.amiga clean` wipes `obj/`, which a `rm -rf build` does **not** —
that tree is the Amiga target's own and a stale one reports "Nothing to be done for 'all'".

The same directory also holds `fs-uae`, `elf2hunk.exe`, `exe2adf.exe`, `vasmm68k_mot.exe`
and `winuae-gdb.exe`, so `scripts/run_amiga_fsuae.ps1` and the AmigaScope harness point here too.

## Which shell for what

| tool | shell |
|---|---|
| `make` (SDL), `scripts/*.sh`, `python3 scripts/*.py` | Cygwin bash |
| `gnumake -f Makefile.amiga` | git-bash (or Cygwin) with the `$AMIGA` PATH above |
| `scripts/*.ps1` (`swos_drive.ps1`, `run_amiga_fsuae.ps1`, `serve_playback.bat`) | PowerShell |
