"""Exercise the real bundled client without sending sound to a speaker."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="quiet-audio-runtime-") as temporary:
    environment = dict(os.environ)
    environment.pop("LD_LIBRARY_PATH", None)
    environment.pop("LD_PRELOAD", None)
    environment["XDG_CONFIG_HOME"] = temporary
    # The private directory contains no server socket. Forcing WSL routing also
    # prevents ALSA or Windows fallback from playing on a real output.
    environment["PULSE_SERVER"] = "unix:" + str(Path(temporary) / "no-server")
    environment["WSL_DISTRO_NAME"] = "Quiet audio runtime test"
    result = subprocess.run([sys.argv[1], "--test-sound", "--output=linux"],
                            env=environment, capture_output=True, text=True, timeout=5)
    if result.returncode != 1 or "PulseAudio:" not in result.stderr or "could not load" in result.stderr:
        sys.exit("Bundled audio runtime failed to load and reach the server connection API:\n" + result.stderr)
    print("Real audio runtime loaded; missing test server correctly reported.")
