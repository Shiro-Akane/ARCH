"""CMake image-list normalization without a GPU or CUDA compiler."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class CudaCodeImagesTests(unittest.TestCase):
    def configure(self, requested, *, resolved="", flag="", config=""):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "images.cmake"
            script.write_text(f'''
set(CMAKE_CUDA_ARCHITECTURES "{requested}")
set(CMAKE_CUDA_ARCHITECTURES_NATIVE "{resolved}")
set(CMAKE_CUDA_ARCHITECTURES_ALL "{resolved}")
set(CMAKE_CUDA_ARCHITECTURES_ALL_MAJOR "{resolved}")
set(CMAKE_CUDA_FLAGS{config} "{flag}")
include("{ROOT / 'cmake/CudaCodeImages.cmake'}")
arch_resolve_cuda_code_images(actual)
message(STATUS "IMAGES=${{actual}}")
''')
            return subprocess.run(["cmake", "-P", str(script)], text=True,
                                  capture_output=True, timeout=30)

    def test_numeric_targets_and_suffixes(self):
        run = self.configure("80;86-real;90-virtual;80")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("IMAGES=80;86-real;90-virtual", run.stdout)

    def test_compiler_resolved_special_targets(self):
        for name in ("native", "all", "all-major"):
            with self.subTest(name=name):
                run = self.configure(name, resolved="80-real;86-real;90")
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertIn("IMAGES=80-real;86-real;90", run.stdout)

    def test_unknown_metadata_fails_closed(self):
        for value in ("", "OFF", "native", "all", "86;90a", "100f", "86-real,bad"):
            with self.subTest(value=value):
                self.assertNotEqual(self.configure(value).returncode, 0)

    def test_manual_architecture_flags_cannot_disagree(self):
        for config in ("", "_DEBUG", "_RELEASE", "_RELWITHDEBINFO", "_MINSIZEREL"):
            for flag in ("-gencode=arch=compute_80,code=sm_80", "--gpu-architecture=sm_80",
                         "-arch sm_80", "--gpu-code=sm_80"):
                with self.subTest(config=config, flag=flag):
                    self.assertNotEqual(self.configure("86", flag=flag, config=config).returncode, 0)


if __name__ == "__main__":
    unittest.main()
