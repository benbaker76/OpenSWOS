#!/usr/bin/env python3
"""FastGdb — GdbHarness with the per-command `python3 gdb_proxy.py ...`
SUBPROCESS replaced by direct unix-socket I/O to the resident daemon.

Why: under Cygwin each subprocess spawn costs ~0.1-0.2 s (fork emulation +
interpreter startup); a friendly run makes ~155 debugger requests, so the
spawns dominated the runtime. The daemon protocol is a one-line request over
/tmp/swos_gdb.sock (`CMD TIMEOUT=N\\t<gdb command>` / `PING` / `HEALTH` /
`WAITSTOP n` / `POLL n`, terminated by a blank line, reply until EOF) — the
same wire format gdb_proxy.py's own client uses, so behavior is identical,
just without the interpreter per call.

The subprocess path stays available (SWOS_TEST_FAST=0): it is what
amiga_gdb.sh and the historical helpers use, and the fallback if the socket
semantics ever drift.
"""
import socket

from harness_lib import GdbHarness

SOCK_PATH = "/tmp/swos_gdb.sock"


class FastGdb(GdbHarness):
    def _send(self, req, timeout):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(max(timeout, 5.0))
        try:
            s.connect(SOCK_PATH)
            s.sendall((req + "\n\n").encode("utf-8"))
            s.shutdown(socket.SHUT_WR)
            out = b""
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                out += chunk
            return out.decode("utf-8", errors="replace")
        except OSError as e:
            return f"[fastproxy error] {e}"
        finally:
            s.close()

    def cmd(self, command, timeout=None):
        t = timeout if timeout is not None else self.timeout
        # +15 socket headroom: the daemon enforces the gdb-side timeout itself.
        return self._send(f"CMD TIMEOUT={t}\t{command}", t + 15)

    def _simple(self, *args):
        name = args[0]
        if name == "ping":
            return self._send("PING", 10)
        if name == "health":
            return self._send("HEALTH", 15)
        if name == "waitstop":
            t = float(args[1]) if len(args) > 1 else 300.0
            return self._send(f"WAITSTOP {t}", t + 15)
        if name == "poll":
            t = float(args[1]) if len(args) > 1 else 0.5
            return self._send(f"POLL {t}", t + 15)
        # anything else (stop/status/...) keeps the subprocess path
        return super()._simple(*args)
