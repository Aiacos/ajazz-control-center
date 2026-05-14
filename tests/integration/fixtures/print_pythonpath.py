# SPDX-License-Identifier: GPL-3.0-or-later
#
# Test fixture for the CR-01 regression integration test
# (`tests/integration/test_oop_plugin_host_win32_env.cpp`).
#
# Print the value of `PYTHONPATH` (or empty string if unset) to stdout
# and exit. The integration test spawns this script via `CreateProcessW`
# with a `Win32EnvBlock` containing a sentinel `PYTHONPATH=...` entry
# and asserts the captured stdout equals the sentinel — proving the
# env block delivers the override to the child.
import os
import sys

sys.stdout.write(os.environ.get("PYTHONPATH", ""))
sys.stdout.flush()
