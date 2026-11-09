#!/usr/bin/env bash
# check_win_dlls.sh -- does a drop's list of Cygwin DLLs hold every one SWOS.exe needs?
#
#   scripts/check_win_dlls.sh "<dll> <dll> ..."      # the list the drop ships
#
# cygcheck on bin/SWOS.exe AND on cygSDL3-0.dll (the sdl2-compat shim dlopen's it, which cygcheck
# cannot see from the exe: build_dist.sh's FULL_DLLS comment). Every cyg*.dll either names must be
# in the list; one that is not is printed and the exit status is 1 -- the exe would not start on a
# machine without Cygwin ("The code execution cannot proceed because cygintl-8.dll was not found":
# a tester, 2026-11-09, after the shader compiler brought libstdc++'s locale code -- gettext and
# iconv -- into the exe). Exit 0 when the list is complete, 2 when it cannot tell (no Cygwin, no
# SWOS.exe). Run by build_dist.sh and build_installer.sh before they package the DLLs.
set -uo pipefail
cd "$(dirname "$0")/.."

LIST=" ${1:-} "
CYGWIN_BASH="${CYGWIN_BASH:-/mnt/c/cygwin64/bin/bash.exe}"
[ -f bin/SWOS.exe ] || { echo "check_win_dlls: no bin/SWOS.exe" >&2; exit 2; }
if command -v cygcheck >/dev/null 2>&1; then
    run() { bash -lc "$1"; }                     # (in Cygwin itself)
elif [ -x "$CYGWIN_BASH" ]; then
    run() { "$CYGWIN_BASH" -lc "$1"; }
else
    echo "check_win_dlls: no Cygwin (CYGWIN_BASH=$CYGWIN_BASH): cannot check" >&2
    exit 2
fi
here=$(pwd)
case "$here" in /mnt/[a-z]/*) here="/cygdrive/${here#/mnt/}" ;; esac

need=$(run "cd '$here' && { cygcheck bin/SWOS.exe; cygcheck /usr/bin/cygSDL3-0.dll; } 2>&1" |
       grep -io 'cyg[a-z0-9_+-]*\.dll' | sort -fu)
[ -n "$need" ] || { echo "check_win_dlls: cygcheck listed nothing" >&2; exit 2; }
missing=""
for d in $need; do
    case "$LIST" in *" $d "*) ;; *) missing="$missing $d" ;; esac
done
if [ -n "$missing" ]; then
    echo "check_win_dlls: SWOS.exe needs Cygwin DLLs the drop does not ship:$missing" >&2
    echo "check_win_dlls: add them to FULL_DLLS (build_dist.sh) and DLLS (build_installer.sh)" >&2
    exit 1
fi
echo "check_win_dlls: every Cygwin DLL SWOS.exe needs is shipped ($(echo $need | wc -w))"
