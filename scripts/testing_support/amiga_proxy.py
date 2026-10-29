#!/usr/bin/env python3
"""Private endpoint for the owned Amiga UI session; leave other daemons alone."""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "amiga" / "amigascope"))
import gdb_proxy

gdb_proxy.SOCK_PATH = os.environ.get("SWOS_GDB_SOCKET", "/tmp/swos-amiga-ui.sock")

if __name__ == "__main__":
    gdb_proxy.main()
