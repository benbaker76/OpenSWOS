#!/usr/bin/env python3
"""Private endpoint for the owned Amiga UI session; leave other daemons alone.

Also teaches the daemon gdb's BACKGROUND resume, which is how a session is
handed to the user and taken back without a reboot:

  continue &   resumes the game and returns gdb's prompt at once
  interrupt    sends the remote break; the stub stops (SIGTRAP) in ~0.1 s

The stock daemon reads any prompt as "stopped", so after `continue &` its
HEALTH probed a running target. Here a background resume marks it running
(read-probes refused, HEALTH says so) until a stop signature arrives.
A SIGINT to gdb is NOT an interrupt here: Cygwin delivers it to the native
win32 gdb as a kill, and the one-shot debugger port cannot be reattached.
"""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "amiga" / "amigascope"))
import gdb_proxy

gdb_proxy.SOCK_PATH = os.environ.get("SWOS_GDB_SOCKET", "/tmp/swos-amiga-ui.sock")


def _stopped_in(text):
    # A real stop, not the prompt every reply ends with.
    return gdb_proxy._STOP_SIG.search(text.replace("(gdb)", "")) is not None


class BackgroundAwareDaemon(gdb_proxy.GdbDaemon):
    def cmd(self, command, timeout=10.0):
        c = command.strip()
        out = super().cmd(command, timeout)
        resumed = c.endswith("&") and "Continuing." in out
        if (resumed or c == "interrupt") and not _stopped_in(out):
            # The stop can beat us: a breakpoint on the next frame lands inside
            # this reply (checked above) or in the buffer before `running` is
            # set. Marking running over a stop that already happened makes
            # wait_stop hang and the next `interrupt` hit a stopped target,
            # which desyncs the remote link. Under the lock, the reader either
            # appended the stop already (seen here) or will see running=True.
            with self.buf_lock:
                self.running = not _stopped_in(
                    self.buf.decode("utf-8", errors="replace"))
        return out


gdb_proxy.GdbDaemon = BackgroundAwareDaemon

if __name__ == "__main__":
    gdb_proxy.main()
