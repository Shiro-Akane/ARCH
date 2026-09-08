"""Fail-closed real sparse trajectory transcript controls; stdlib only."""
import unittest

from run_sparse_validation import parse_transcript


class SparseTranscriptContract(unittest.TestCase):
    def setUp(self):
        self.metadata = {"species": [f"s{i}" for i in range(31)],
                         "auxiliary_equations": 0, "runtime_name": "custom:test"}
        self.controls = dict(rho=2.0, temperature=100.0, interval=1.0, cv=1.0, rtol=1e-7)
        self.lines = ["controls,custom:test,32,2,100,1,1,1e-7,2,selected_ode,-1"]
        for method in (1, 2, 3):
            for cells in (2, 3):
                for step in range(2):
                    self.lines += [f"cpu_step,{method},{cells},{step},{cells},0,0.01",
                                   f"gpu_step,{method},{cells},{step},4,3,0.02"]
                state = [2, 0, 0, 0, 200, 1, 1] + [0] * 30
                self.lines.append(f"state,{method},{cells}," + ",".join(map(str, state)))
            self.lines.append(f"metrics,{method},10,0,1e-12,1e-12,0.1,2,1024")
        self.lines.append("GENERATED_SPARSE_BURN_PARITY_PASS")

    def parse(self, lines):
        return parse_transcript("\n".join(lines), self.metadata, self.controls, 2)

    def test_complete_matrix(self):
        result = self.parse(self.lines)
        self.assertEqual(set(result["methods"]), {1, 2, 3})
        self.assertEqual(result["methods"][1]["attempts"], 10)
        self.assertEqual(result["methods"][1]["kernels"], 16)
        self.assertEqual(result["field_budget"], 2e-10)

    def test_partial_duplicate_selected_or_failed_matrix(self):
        variants = [self.lines[:-1], self.lines + [self.lines[-1]],
                    self.lines[1:], self.lines + [self.lines[0]],
                    [self.lines[0].replace("selected_ode,-1", "selected_ode,1")] + self.lines[1:],
                    self.lines + ["GENERATED_SPARSE_BURN_PARITY_FAIL"],
                    [line for line in self.lines if not line.startswith("metrics,3,")],
                    [line for line in self.lines if not line.startswith("state,3,3,")],
                    self.lines + [next(line for line in self.lines if line.startswith("state,"))]]
        for lines in variants:
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                self.parse(lines)

    def test_identity_and_step_coverage(self):
        for before, after in (("custom:test", "custom:other"),
                              ("test,32,", "test,33,"), ("32,2,100", "32,3,100"),
                              ("cpu_step,1,2,0,2,0", "cpu_step,1,2,0,0,0"),
                              ("cpu_step,1,2,0,2,0", "cpu_step,1,2,0,2,2"),
                              ("gpu_step,1,2,0", "gpu_step,1,2,7"),
                              ("gpu_step,1,2,0,4,3,0.02", "gpu_step,1,2,0,4,3,nan")):
            with self.subTest(before=before), self.assertRaises(ValueError):
                self.parse([line.replace(before, after) for line in self.lines])
        first = next(line for line in self.lines if line.startswith("cpu_step,"))
        for lines in (self.lines + [first], [line for line in self.lines if line != first]):
            with self.assertRaises(ValueError):
                self.parse(lines)

    def test_budgets_noop_totals_and_capacity_are_not_waived(self):
        original = "metrics,1,10,0,1e-12,1e-12,0.1,2,1024"
        for replacement in ("metrics,1,11,0,1e-12,1e-12,0.1,2,1024",
                            "metrics,1,10,0,3e-10,1e-12,0.1,2,1024",
                            "metrics,1,10,0,1e-12,3e-8,0.1,2,1024",
                            "metrics,1,10,0,nan,1e-12,0.1,2,1024",
                            "metrics,1,10,0,1e-12,1e-12,0,2,1024",
                            "metrics,1,10,0,1e-12,1e-12,0.1,3,1024",
                            "metrics,1,10,0,1e-12,1e-12,0.1,2,0"):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                self.parse([replacement if line == original else line for line in self.lines])

    def test_final_state_extent_nonfinite_and_closure(self):
        index = next(i for i, line in enumerate(self.lines) if line.startswith("state,"))
        row = self.lines[index].split(",")
        variants = [row[:-1], row + ["0"], row[:7] + ["nan"] + row[8:],
                    row[:9] + ["0.5"] + row[10:]]
        for replacement in variants:
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                self.parse(self.lines[:index] + [",".join(replacement)] + self.lines[index + 1:])


if __name__ == "__main__":
    unittest.main()
