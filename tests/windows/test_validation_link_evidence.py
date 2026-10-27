"""Run explicitly with native Windows Python; Linux's strict suite is separate."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

if os.name != "nt":
    raise RuntimeError("This suite requires native Windows; it must not report a skipped PASS")

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import validation_provenance as provenance
import windows_link_evidence as windows


class WindowsLinkEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="arch Windows 链接 ")
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name) / "build"
        self.build.mkdir()
        self.output = self.build / "bin/ARCH.exe"

    def test_windows_quotes_and_backslashes_are_preserved(self):
        library = self.build / "deps with 空格/klu_static.lib"
        command = f'cmd.exe /C "cd . && link.exe /out:bin\\ARCH.exe "{library}" && cd ."'
        self.assertIn(str(library), windows.command_arguments(command))
        self.assertIn("/out:bin\\ARCH.exe", windows.command_arguments(command))

    def test_unexpanded_response_file_and_ambiguous_wrapper_fail(self):
        for command in ("link.exe @ARCH.rsp", 'cmd.exe /C link.exe "bad outer wrapper"'):
            with self.assertRaises(RuntimeError):
                windows.command_arguments(command)

    def recipe(self, command="link.exe /out:bin\\ARCH.exe"):
        return {"directory": str(self.build), "output": "bin/ARCH.exe", "command": command}

    def query(self, entries):
        reply = subprocess.CompletedProcess([], 0, json.dumps(entries), "")
        with patch.object(windows.subprocess, "run", return_value=reply) as run:
            result = windows.ninja_link_recipe(["ninja", "-C", str(self.build)], self.build, self.output)
        self.assertEqual(run.call_args.args[0][-3:], ["-t", "compdb", "-x"])
        return result

    def test_unique_exact_recipe_is_required(self):
        self.assertIn("/out:", self.query([self.recipe()]))
        for entries in ([], [self.recipe(), self.recipe()],
                        [self.recipe("link.exe /out:other.exe")],
                        [self.recipe("link.exe /out:bin\\ARCH.exe /OUT:other.exe")],
                        [{**self.recipe(), "directory": str(self.build.parent)}]):
            with self.assertRaises(RuntimeError):
                self.query(entries)

    def fixtures(self):
        names = ["klu_static.lib", "amd_static.lib", "colamd_static.lib", "btf_static.lib",
                 "suitesparseconfig_static.lib", "arch_cuda_sparse_provider.lib", "cudss.lib", "cublas.lib"]
        paths = []
        for name in names:
            path = self.build / name
            path.write_bytes((name + " fixture").encode())
            paths.append(path)
        return paths

    def test_required_providers_and_every_linked_file_are_fingerprinted(self):
        paths = self.fixtures()
        command = "link.exe /out:bin\\ARCH.exe " + " ".join(f'"{p}"' for p in paths)
        before = provenance.sparse_link_command_identity(self.build, command, {"klu", "cudss"})
        self.assertEqual(len(before["libraries"]), len(paths))
        for path in paths:
            original = path.read_bytes()
            path.write_bytes(original + b" changed")
            self.assertNotEqual(before, provenance.sparse_link_command_identity(self.build, command, {"klu", "cudss"}))
            path.write_bytes(original)
        with self.assertRaises(RuntimeError):
            provenance.sparse_link_command_identity(self.build, command.replace(f'"{paths[-2]}"', "-lcudss"), {"klu", "cudss"})
        with self.assertRaises(RuntimeError):
            provenance.sparse_link_command_identity(self.build, command + f' "{self.build / "missing/amd_static.lib"}"', {"klu", "cudss"})

    def dll_fixtures(self):
        options = {}
        for name, key, dll_key, filename in (
            ("cudss", "CuDSS_LIBRARY", "CuDSS_RUNTIME_LIBRARY", "cudss64_0.dll"),
            ("cublas", "CUDA_cublas_LIBRARY", "CUDA_cublas_RUNTIME_LIBRARY", "cublas64_12.dll"),
            ("cublasLt", "CUDA_cublasLt_LIBRARY", "CUDA_cublasLt_RUNTIME_LIBRARY", "cublasLt64_12.dll"),
        ):
            imported = self.build / (name + ".lib")
            imported.write_bytes(b"import binding fixture\x00" + filename.encode() + b"\x00")
            runtime = self.build / filename
            runtime.write_bytes((name + " DLL identity fixture").encode())
            options[key], options[dll_key] = str(imported), str(runtime)
        return options

    def test_runtime_dlls_and_import_bindings_change_identity(self):
        options = self.dll_fixtures()
        before = windows.configured_provider_dlls(options, provenance.file_identity)
        self.assertEqual(before["runtime_library_scope"], "configured-windows-provider-dlls")
        self.assertEqual(len(before["runtime_libraries"]), 3)
        for value in options.values():
            path = Path(value)
            original = path.read_bytes()
            path.write_bytes(original + b" changed")
            self.assertNotEqual(before, windows.configured_provider_dlls(options, provenance.file_identity))
            path.write_bytes(original)

    def test_missing_relative_or_wrong_dll_binding_fails(self):
        options = self.dll_fixtures()
        for replacement in ("", "cudss64_0.dll", str(self.build / "cudss64_1.dll")):
            with self.assertRaises(RuntimeError):
                windows.configured_provider_dlls({**options, "CuDSS_RUNTIME_LIBRARY": replacement}, provenance.file_identity)
        Path(options["CuDSS_RUNTIME_LIBRARY"]).unlink()
        with self.assertRaises(RuntimeError):
            windows.configured_provider_dlls(options, provenance.file_identity)

    def test_native_gcc_static_cpu_spelling_remains_valid(self):
        self.assertEqual(windows.provider_name("libklu.a"), "klu")
        self.assertIsNone(windows.provider_name("-lklu"))


if __name__ == "__main__":
    unittest.main()
