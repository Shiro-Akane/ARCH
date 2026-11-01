"""Native dependency preparation, including failure before publication.

These controls compile a tiny HDF5 consumer and a tiny CMake application; they
do not build ARCH's scientific sources or fetch third-party dependencies.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(os.name == "posix" and shutil.which("cmake")
                     and shutil.which("ninja") and shutil.which("gcc")
                     and shutil.which("g++") and shutil.which("h5c++"),
                     "Native Linux compilers, CMake, Ninja and HDF5 C++ are required")
class PrepareProfileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="arch prepare ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        target = self.root / "cmake/dependencies"
        target.mkdir(parents=True)
        for name in ("Prepare.cmake", "PrepareHdf5.cmake"):
            shutil.copy2(ROOT / "cmake/dependencies" / name, target / name)
        shutil.copy2(ROOT / "CMakePresets.json", self.root / "CMakePresets.json")
        # Local source overrides are only checked for existence by preparation;
        # the real dependency owner validates/uses their CMake projects later.
        for name in ("highfive", "suitesparse"):
            directory = self.root / name
            directory.mkdir()
            (directory / "CMakeLists.txt").write_text("# local source fixture\n")
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.22)\n'
            'project(PreparedWitness LANGUAGES C CXX)\n'
            'include(CheckIPOSupported)\ncheck_ipo_supported(RESULT supported)\n'
            'add_executable(witness main.cpp)\n')
        (self.root / "main.cpp").write_text("int main() { return 0; }\n")
        self.user_presets = self.root / "CMakeUserPresets.json"
        self.user_presets.write_text(json.dumps({"version": 3, "configurePresets": []}))

    def prepare(self, *extra, profile="linux-core"):
        return subprocess.run([
            "cmake", "-DARCH_PROFILE=" + profile, "-DARCH_PREPARE_OFFLINE=ON",
            "-DFETCHCONTENT_SOURCE_DIR_HIGHFIVE=" + str(self.root / "highfive"),
            "-DFETCHCONTENT_SOURCE_DIR_SUITESPARSE=" + str(self.root / "suitesparse"),
            *extra, "-P", "cmake/dependencies/Prepare.cmake"],
            cwd=self.root, capture_output=True, text=True, timeout=90)

    def test_core_reuses_hdf5_without_node_and_repeat_preserves_toolchain(self):
        user_before = self.user_presets.read_bytes()
        first = self.prepare()
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        self.assertIn("Verified native HDF5", first.stdout)
        toolchain = self.root / "build/linux-core/prepared-toolchain.cmake"
        before = toolchain.read_bytes(), toolchain.stat().st_mtime_ns
        self.assertNotIn(b"set(ARCH_STUDIO_NODE", before[0])
        second = self.prepare()
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        self.assertEqual((toolchain.read_bytes(), toolchain.stat().st_mtime_ns), before)
        self.assertEqual(self.user_presets.read_bytes(), user_before)
        self.assertFalse((self.root / "build/dependencies/downloads").exists())
        configure = subprocess.run(["cmake", "--preset", "linux-core"], cwd=self.root,
                                   capture_output=True, text=True, timeout=90)
        self.assertEqual(configure.returncode, 0, configure.stdout + configure.stderr)
        build = subprocess.run(["cmake", "--build", "--preset", "linux-core"], cwd=self.root,
                               capture_output=True, text=True, timeout=60)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        self.assertEqual(subprocess.run([str(self.root / "build/linux-core/witness")], timeout=10).returncode, 0)
        # Changing compiler identity must fail before touching a new dependency
        # cache, even when the wrapper would forward to the same system GCC.
        wrapper = self.root / "other-cxx"
        wrapper.write_text('#!/bin/sh\nexec "' + shutil.which("g++") + '" "$@"\n')
        wrapper.chmod(0o755)
        dependency_dirs = sorted((self.root / "build/dependencies").iterdir())
        changed = self.prepare("-DCMAKE_CXX_COMPILER=" + str(wrapper))
        self.assertNotEqual(changed.returncode, 0)
        self.assertIn("different CXX compiler", " ".join(changed.stderr.split()))
        self.assertEqual(sorted((self.root / "build/dependencies").iterdir()), dependency_dirs)
        self.assertEqual(toolchain.read_bytes(), before[0])

    def test_explicit_invalid_hdf5_root_is_not_replaced_by_system_copy(self):
        wrong = self.root / "wrong ABI or incomplete installation"
        wrong.mkdir()
        result = self.prepare("-DHDF5_ROOT=" + str(wrong))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("HDF5_ROOT failed", result.stderr)
        self.assertFalse((self.root / "build/linux-core/prepared-toolchain.cmake").exists())

    def test_profile_rejects_foreign_host_before_writing(self):
        result = self.prepare(profile="windows-core")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native host", result.stderr)
        self.assertFalse((self.root / "build").exists())

    def test_desktop_rejects_missing_node_before_writing(self):
        result = self.prepare("-DARCH_STUDIO_NODE=" + str(self.root / "missing-node"), profile="linux-desktop")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ARCH_STUDIO_NODE", result.stderr)
        self.assertFalse((self.root / "build/linux-desktop/prepared-toolchain.cmake").exists())


if __name__ == "__main__":
    unittest.main()
