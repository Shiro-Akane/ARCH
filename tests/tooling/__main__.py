"""One complete Tooling entry for local CTest and hosted CI.

Discovery owns the file selection; the existing report checker owns completion.
Numerical applications, independent scientific oracles and device execution stay
in their own layers. A skipped or empty suite cannot certify this entry.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from check_ci_results import check_tooling_result


def main():
    suite = unittest.defaultTestLoader.discover(str(ROOT / "tests/tooling"), pattern="test_*.py")
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    try:
        count = check_tooling_result(result)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"tooling: all {count} tests passed without skips")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
