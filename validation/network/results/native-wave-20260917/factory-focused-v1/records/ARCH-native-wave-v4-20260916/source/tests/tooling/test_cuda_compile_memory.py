"""Compiler evidence attribution, independent of CUDA and GNU time availability."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import summarize_cuda_compile_memory as metrics


class CompileMemoryTests(unittest.TestCase):
    @staticmethod
    def command(source, *, seconds="1.25", rss=2048, code=0):
        return (f"ARCH_COMPILE_METRIC elapsed_seconds={seconds} peak_rss_kib={rss} "
                f"exit_code={code} command=/usr/bin/ccache nvcc -c {source} -o target.o")

    def test_host_device_and_failed_commands_remain_separate(self):
        rows = metrics.parse_commands([
            "an unrelated warning", self.command("/src/a.cu"),
            self.command("/src/owner.cpp", code=1, rss=4096),
            self.command("/src/a.cu", seconds="0.01", rss=1024)])
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0]["elapsed_seconds"], 1.25)
        self.assertEqual(rows[0]["peak_rss_mib"], 2.0)
        self.assertEqual(rows[1]["exit_code"], 1)
        self.assertEqual(rows[2]["peak_rss_kib"], 1024)
        self.assertTrue(all(row["measurement"] == "gnu_time_command_maxrss" for row in rows))

    def test_quoted_source_path(self):
        row, = metrics.parse_commands([self.command("'/src/my project/a.cu'")])
        self.assertEqual(row["translation_unit"], "/src/my project/a.cu")

    def test_malformed_or_ambiguous_commands_fail_closed(self):
        for line in (self.command("/src/a.cu", seconds="nan"),
                     self.command("/src/a.cu", seconds="9" * 400),
                     self.command("/src/a.cu", rss=-1),
                     self.command("/src/my project/a.cu"),
                     self.command("/src/a.cu -c /src/b.cu"),
                     self.command("/src/a.cu").replace(" -c ", " --link "),
                     self.command("/src/a.cu", code=-1)):
            with self.subTest(line=line), self.assertRaises(ValueError):
                metrics.parse_commands([line])

    def test_single_translation_unit_process_group_sample(self):
        rows = metrics.parse_process_samples([
            "100", "10 1 10 7 S 100 0 0 0 nvcc -c /src/a.cu",
            "11 10 10 7 S 200 0 0 0 ptxas",
            "101", "10 1 10 7 S 150 0 0 0 nvcc -c /src/a.cu"])
        self.assertEqual(rows[0]["peak_rss_kib"], 300)
        self.assertEqual(rows[0]["measurement"], "sampled_process_group_rss_sum")
        self.assertEqual(rows[0]["elapsed_seconds"], "")

    def test_process_group_reuse_must_not_misattribute_memory(self):
        with self.assertRaisesRegex(ValueError, "multiple translation units"):
            metrics.parse_process_samples([
                "100", "10 1 10 7 S 100 0 0 0 nvcc -c /src/a.cu",
                "101", "10 1 10 7 S 200 0 0 0 nvcc -c /src/b.cu"])

    def test_auto_detects_commands_and_sorts_by_peak(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "build.log"
            path.write_text(self.command("/src/a.cpp") + "\n"
                            + self.command("/src/b.cu", rss=8192) + "\n")
            rows = metrics.parse_samples(path)
            self.assertEqual([row["translation_unit"] for row in rows],
                             ["/src/b.cu", "/src/a.cpp"])


if __name__ == "__main__":
    unittest.main()
