"""Rename an actual running test image before its first fingerprint request."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    root.mkdir(parents=True, exist_ok=True)
    expected = hashlib.sha256(executable.read_bytes()).hexdigest()
    for label in ("ascii", "Unicode-中文 space"):
        with tempfile.TemporaryDirectory(prefix=label, dir=str(root)) as directory:
            launch = Path(directory) / ("running" + executable.suffix)
            renamed = Path(directory) / ("renamed" + executable.suffix)
            shutil.copy2(executable, launch)
            process = subprocess.Popen([str(launch), "--fingerprint-before"],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, encoding="utf-8")
            try:
                if process.stdout.readline().strip() != "READY":
                    raise RuntimeError("Child did not reach the pre-fingerprint boundary")
                launch.rename(renamed)
                launch.write_bytes(b"This is a different file at the old launch path.\n")
                output, error = process.communicate("continue\n", timeout=30)
                if process.returncode or output.strip() != "DIGEST " + expected:
                    raise RuntimeError("Running-image identity mismatch: " + output + error)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
        print("RUNNING_IMAGE_IDENTITY_PASS " + label)


if __name__ == "__main__":
    main()
