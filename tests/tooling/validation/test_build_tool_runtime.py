import sys
import os
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "validation/io"))
from audit_build_tool_runtime import audit, capture, fingerprint, parse_cache, parse_elf

HEADER = "Class: ELF64\nMachine: Advanced Micro Devices X86-64\n"
class ToolRuntimeTests(unittest.TestCase):
    def test_parser_retains_needed_interpreter_and_rpath(self):
        obj = parse_elf(HEADER + "(NEEDED) Shared library: [libm.so.6]\n"
            "[Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]\n"
            "(RUNPATH) Library runpath: [$ORIGIN/lib]\n")
        self.assertEqual(obj["needed"], ["libm.so.6"])
        self.assertEqual(obj["searchPaths"], ["$ORIGIN/lib"])
        self.assertEqual(obj["interpreter"], "/lib64/ld-linux-x86-64.so.2")

    def test_bad_architecture_duplicate_and_path_needed_rejected(self):
        for text in ["Class: ELF32\nMachine: Intel 80386",
            HEADER+"(NEEDED) Shared library: [/unsafe/lib.so]\n",
            HEADER+"(NEEDED) Shared library: [x.so]\n(NEEDED) Shared library: [x.so]\n",
            HEADER+"[Requesting program interpreter: relative]\n"]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_elf(text)

    def test_cache_keeps_ambiguity_and_excludes_other_architecture(self):
        c = parse_cache("libx.so (libc6,x86-64) => /a/x.so\n"
            "libx.so (libc6,x86-64) => /b/x.so\nlibx.so (libc6) => /i386/x.so\n")
        self.assertEqual(c, {"libx.so": ["/a/x.so", "/b/x.so"]})

    def test_untrusted_inspector_never_runs(self):
        with self.assertRaisesRegex(ValueError, "fixed"):
            capture("/bin/sh", ["-c", "exit 0"])

    def test_non_elf_payload_is_read_not_executed(self):
        with tempfile.TemporaryDirectory(prefix="ARCH runtime ") as tmp:
            root=Path(tmp); marker=root/"executed"; payload=root/"tool"
            payload.write_text("#!/bin/sh\ntouch '"+str(marker)+"'\n");payload.chmod(0o700)
            with self.assertRaises(ValueError):
                audit([str(payload)])
            self.assertFalse(marker.exists())

    def test_missing_ambiguous_and_rpath_candidates_not_guessed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); tool=root/"tool"; tool.write_bytes(b"read-only fixture")
            a=root/"a.so";a.write_bytes(b"a");b=root/"b.so";b.write_bytes(b"b")
            variants=[("", "", "missing-or-ambiguous-cache-candidate"),
                ("libx.so (libc6,x86-64) => "+str(a)+"\nlibx.so (libc6,x86-64) => "+str(b)+"\n",
                 "", "missing-or-ambiguous-cache-candidate"),
                ("libx.so (libc6,x86-64) => "+str(a)+"\n",
                 "(RUNPATH) Library runpath: [$ORIGIN/lib]\n", "unmodeled-rpath")]
            for cache, path, reason in variants:
                def fake_capture(program, args):
                    return cache if program.endswith("ldconfig") else HEADER+"(NEEDED) Shared library: [libx.so]\n"+path
                with self.subTest(reason=reason), patch("audit_build_tool_runtime.capture",side_effect=fake_capture):
                    result=audit([str(tool)])
                    self.assertEqual(result["unresolved"][0]["reason"],reason)
                    self.assertFalse(result["dependenciesComplete"])
                    self.assertEqual(len(result["nodes"]),1)

    def test_cache_dependency_cycle_is_bounded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);tool=root/"tool";tool.write_bytes(b"fixture")
            library=root/"x.so";library.write_bytes(b"x")
            def fake_capture(program,args):
                if program.endswith("ldconfig"):
                    return "libx.so (libc6,x86-64) => "+str(library)+"\n"
                return HEADER+"(NEEDED) Shared library: [libx.so]\n"
            with patch("audit_build_tool_runtime.capture",side_effect=fake_capture):
                result=audit([str(tool)])
                self.assertEqual(len(result["nodes"]),2)
                self.assertEqual(len(result["edges"]),2)
                self.assertFalse(result["dependenciesComplete"])
            with patch("audit_build_tool_runtime.capture",side_effect=fake_capture), \
                 patch("audit_build_tool_runtime.MAX_NODES",1), self.assertRaisesRegex(ValueError,"budget"):
                audit([str(tool)])

    def test_content_symlink_and_budget_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); original=root/"a"; original.write_bytes(b"a")
            target=root/"b"; target.write_bytes(b"b"); link=root/"link";link.symlink_to(original)
            first=fingerprint(str(link));link.unlink();link.symlink_to(target)
            self.assertNotEqual(first, fingerprint(str(link)))
            target.write_bytes(b"c")
            self.assertNotEqual(first["sha256"],fingerprint(str(target))["sha256"])
            with self.assertRaises(ValueError):fingerprint("relative")
            with self.assertRaises((ValueError, OSError)):fingerprint(str(root))
            os.mkfifo(root/"fifo")
            with self.assertRaises(ValueError):fingerprint(str(root/"fifo"))
            with (root/"huge").open("wb") as f:f.truncate(128*1024*1024+1)
            with self.assertRaises(ValueError):fingerprint(str(root/"huge"))

if __name__ == "__main__":unittest.main()
