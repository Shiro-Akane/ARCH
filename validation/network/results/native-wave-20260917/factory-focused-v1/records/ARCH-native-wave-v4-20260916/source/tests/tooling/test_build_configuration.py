"""Public presets and small host-only linker/LTO configuration controls."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildPresetTests(unittest.TestCase):
    def test_presets_have_portable_separate_application_outputs(self):
        data = json.loads((ROOT / "CMakePresets.json").read_text())
        presets = {item["name"]: item for item in data["configurePresets"]}
        self.assertEqual(set(presets), {"cpu-release", "cuda-release", "cuda-debug"})
        directories = set()
        for name, preset in presets.items():
            with self.subTest(preset=name):
                self.assertEqual(preset["generator"], "Ninja")
                binary = preset["binaryDir"]
                directories.add(binary)
                options = preset["cacheVariables"]
                self.assertEqual(options["ARCH_RUNTIME_OUTPUT_DIRECTORY"], binary + "/bin")
                self.assertEqual(options["CMAKE_BUILD_TYPE"],
                                 "Debug" if name.endswith("debug") else "Release")
                self.assertEqual(options["BUILD_TESTING"],
                                 "ON" if name.endswith("debug") else "OFF")
                self.assertEqual(options["ARCH_ENABLE_CUDA"],
                                 "ON" if name.startswith("cuda") else "OFF")
                if name.startswith("cuda"):
                    self.assertEqual(options["CMAKE_CUDA_ARCHITECTURES"], "native")
                    self.assertEqual(options["ARCH_CUDA_HEAVY_COMPILE_JOBS"], "1")
        self.assertEqual(len(directories), len(presets))

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required")
    def test_cmake_accepts_preset_schema(self):
        run = subprocess.run(["cmake", "--list-presets"], cwd=ROOT, text=True,
                             capture_output=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        for name in ("cpu-release", "cuda-release", "cuda-debug"):
            self.assertIn('"' + name + '"', run.stdout)


@unittest.skipUnless(os.name == "posix" and shutil.which("cmake")
                     and shutil.which("ninja") and shutil.which("cc")
                     and shutil.which("c++"),
                     "Linux host C/C++ compilers, CMake and Ninja are required")
class IpoLinkerTests(unittest.TestCase):
    def configure(self, directory, *, available=(), reject=(), reject_lto=False,
                  build_type="Release", link_flag=""):
        root = Path(directory)
        # These wrappers only simulate an incompatible optional linker or flag;
        # successful controls still compile/link real C and C++ LTO objects.
        for language, compiler in (("c", "cc"), ("cxx", "c++")):
            wrapper = root / (language + "-compiler")
            wrapper.write_text(f'''#!{sys.executable}
import os
import sys
arguments = sys.argv[1:]
if any(flag in arguments for flag in {tuple(reject)!r}):
    sys.stderr.write("fixture: rejected linker option\\n")
    sys.exit(83)
if {reject_lto!r} and "-c" not in arguments and any(
        flag.startswith("-flto") for flag in arguments):
    sys.stderr.write("fixture: rejected LTO link\\n")
    sys.exit(84)
os.execv({shutil.which(compiler)!r}, [{shutil.which(compiler)!r}] + arguments)
''')
            wrapper.chmod(0o755)
        (root / "main.cpp").write_text("int main() { return 0; }\n")
        (root / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.22)
project(LinkerControl LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_{build_type.upper()} "{link_flag}")
set(MOLD_LINKER "{'fixture-discovery' if 'mold' in available else ''}")
set(LLD_LINKER "{'fixture-discovery' if 'lld' in available else ''}")
include("{ROOT / 'cmake/SelectIpoLinker.cmake'}")
arch_select_ipo_linker(selected)
file(WRITE "${{CMAKE_BINARY_DIR}}/selected.txt" "${{selected}}")
add_executable(control main.cpp)
set_property(TARGET control PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
if(selected)
    target_link_options(control PRIVATE "${{selected}}")
endif()
''')
        run = subprocess.run([
            "cmake", "-S", str(root), "-B", str(root / "build"), "-G", "Ninja",
            "-DCMAKE_C_COMPILER=" + str(root / "c-compiler"),
            "-DCMAKE_CXX_COMPILER=" + str(root / "cxx-compiler"),
            "-DCMAKE_BUILD_TYPE=" + build_type,
        ], text=True, capture_output=True, timeout=60)
        return run

    def assert_builds(self, root):
        run = subprocess.run(["cmake", "--build", str(Path(root) / "build"),
                              "--parallel", "1", "--verbose"],
                             text=True, capture_output=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("-flto", run.stdout)
        executable = Path(root) / "build/control"
        self.assertEqual(subprocess.run([str(executable)], timeout=10).returncode, 0)
        return run.stdout

    def test_default_linker_retains_real_lto(self):
        with tempfile.TemporaryDirectory() as root:
            run = self.configure(root)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual((Path(root) / "build/selected.txt").read_text(), "")
            self.assertIn("Using default linker", run.stdout)
            self.assert_builds(root)

    @unittest.skipUnless(shutil.which("mold"), "mold is not installed")
    def test_installed_mold_retains_real_lto(self):
        with tempfile.TemporaryDirectory() as root:
            run = self.configure(root, available=("mold",))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual((Path(root) / "build/selected.txt").read_text(), "-fuse-ld=mold")
            self.assertIn("-fuse-ld=mold", self.assert_builds(root))

    def test_incompatible_optional_linkers_fall_back_without_disabling_lto(self):
        with tempfile.TemporaryDirectory() as root:
            run = self.configure(root, available=("mold", "lld"),
                                 reject=("-fuse-ld=mold", "-fuse-ld=lld"))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("mold linker rejected", run.stdout)
            self.assertIn("lld linker rejected", run.stdout)
            self.assertIn("Using default linker", run.stdout)
            self.assert_builds(root)

    def test_no_working_lto_linker_fails_configuration(self):
        with tempfile.TemporaryDirectory() as root:
            run = self.configure(root, available=("mold", "lld"), reject_lto=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("no candidate linker", run.stdout + run.stderr)
            self.assertIn("not been silently disabled", run.stdout + run.stderr)

    def test_configuration_link_flags_are_part_of_the_probe(self):
        for build_type in ("Release", "RelWithDebInfo"):
            with self.subTest(build_type=build_type), tempfile.TemporaryDirectory() as root:
                run = self.configure(root, build_type=build_type,
                                     link_flag="-Wl,--arch-deliberately-unsupported")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("no candidate linker", run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
