"""Compile and test collection persistence without touching player saves.
Run in Cygwin (or another hosted GCC environment): python3 scripts/test_challenge_progress.py
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="swos-progress-") as tmp:
    work = Path(tmp)
    exe = work / "progress-test.exe"
    subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-DSWOS_SDL_BUILD", "-I" + str(ROOT / "include"),
                    str(ROOT / "tests/challenge_progress_test.c"),
                    str(ROOT / "src/challenge_progress.c"),
                    str(ROOT / "src/challenge_catalog.c"), "-o", str(exe)], check=True)
    def run(mode, index, count):
        subprocess.run([str(exe), mode, str(index), str(count)], cwd=work, check=True)

    run("status", 0, 0)
    for mode in ("abort", "unloaded", "draw", "loss"):
        run(mode, 0, 0)
    assert not list(work.glob("*.PRG"))
    run("replay", 0, 1)
    run("status", 0, 1)
    run("loss", 0, 1)  # Completed stays completed after a failed replay.
    run("away", 1, 2)  # Human side 2 is compared in team order, not pitch order.
    run("status", 0, 2)
    (work / "CHPROG2.PRG").write_bytes(b"interrupted write")
    run("status", 1, 1)  # Recover the previous valid generation.
    run("retry", 1, 2)
    run("status", 0, 2)
    for index in range(2, 6):
        run("win", index, index + 1)
    run("status", 0, 6)
    print("PASS: challenge replay, win/draw/loss/abort, both human sides, restart persistence, backup recovery, save retry, 6/6 completion")
