import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "validation" / "io"))
from audit_build_generator_identity import audit

class GeneratorIdentityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="arch generator ")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / "source"
        self.build = self.root / "build"
        self.source.mkdir()
        self.build.mkdir()
        self.cmake = self.root / "cmake"
        self.ninja = self.root / "ninja"
        # If executed this payload would create a marker; audits must only read.
        for tool in [self.cmake, self.ninja]:
            tool.write_text("#!/bin/sh\ntouch '" + str(self.root / "executed") + "'\n")
            tool.chmod(0o700)
        self.values = dict(CMAKE_HOME_DIRECTORY=str(self.source),
                          CMAKE_CACHEFILE_DIR=str(self.build), CMAKE_GENERATOR="Ninja",
                          CMAKE_COMMAND=str(self.cmake), CMAKE_MAKE_PROGRAM=str(self.ninja))
        self.write_cache()

    def write_cache(self, extra=""):
        (self.build / "CMakeCache.txt").write_text(
            "\n".join(k + ":INTERNAL=" + v for k, v in self.values.items()) + "\n" + extra)

    def read(self):
        return audit(self.source, self.build, str(self.cmake))

    def test_content_identity_changes_and_no_program_executes(self):
        first = self.read()
        self.assertFalse(first["dependenciesComplete"])
        self.assertFalse(first["observedExecution"])
        self.ninja.write_text("#!/bin/sh\nexit 1\n")
        self.assertNotEqual(first["tools"]["ninja"]["sha256"], self.read()["tools"]["ninja"]["sha256"])
        self.assertFalse((self.root / "executed").exists())

    def test_source_binding_duplicate_and_generator_rejected(self):
        for key, value in [("CMAKE_HOME_DIRECTORY", "/different"), ("CMAKE_GENERATOR", "Unix Makefiles")]:
            old = self.values[key]
            self.values[key] = value
            self.write_cache()
            with self.assertRaises(ValueError):
                self.read()
            self.values[key] = old
        self.write_cache("CMAKE_GENERATOR:INTERNAL=Ninja\n")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.read()

    def test_missing_relative_nonexecutable_and_host_mismatch_rejected(self):
        for value in ["relative-ninja", str(self.root / "absent")]:
            self.values["CMAKE_MAKE_PROGRAM"] = value
            self.write_cache()
            with self.assertRaises((ValueError, OSError)):
                self.read()
        self.values["CMAKE_MAKE_PROGRAM"] = str(self.ninja)
        self.ninja.chmod(0o600)
        self.write_cache()
        with self.assertRaises(ValueError):
            self.read()
        self.ninja.chmod(0o700)
        self.values["CMAKE_COMMAND"] = str(self.ninja)
        self.write_cache()
        with self.assertRaisesRegex(ValueError, "differs"):
            self.read()

    def test_oversized_cache_rejected(self):
        (self.build / "CMakeCache.txt").write_bytes(b"x" * (2 * 1024 * 1024 + 1))
        with self.assertRaisesRegex(ValueError, "budget"):
            self.read()

if __name__ == "__main__":
    unittest.main()
