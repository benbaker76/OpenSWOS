"""testing_support — the flow-testing framework (docs/TESTING_FLOWS_DESIGN.md).

Layering (delivery step 1 implements the bottom of the stack):

  swos_test.py        thin CLI (inspect; plan/run arrive with later steps)
  adapter.py          SwosStateAdapter — live observation over the gdb harness
  snapshot.py         PURE: snapshot model, the batched observe script,
                      its parser and the fingerprint (no debugger I/O,
                      unit-testable offline with recorded output)

The adapter builds on scripts/swos_plugin.py (SwosHarness) and
scripts/harness_lib.py (GdbHarness) over AmigaScope's gdb_proxy.py, so one
observation mechanism serves both the SDL and FS-UAE targets.
"""
