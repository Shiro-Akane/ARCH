"""Fail-closed real sparse trajectory transcript controls; stdlib only."""
import json
from pathlib import Path
import unittest

from run_sparse_validation import parse_transcript, validate_storage_controls


class SparseTranscriptContract(unittest.TestCase):
    def setUp(self):
        self.metadata = {"species": [f"s{i}" for i in range(31)],
                         "auxiliary_equations": 0, "runtime_name": "custom:test"}
        self.controls = dict(rho=2.0, temperature=100.0, interval=1.0, cv=1.0, rtol=1e-7)
        self.lines = self.make_lines()

    def make_lines(self, storage=(2, 3), pool=2):
        # Synthetic parser fixtures only, never scientific validation evidence.
        lines = ["controls,custom:test,32,2,100,1,1,1e-7,2,selected_ode,-1"]
        if storage != (2, 3) or pool != 2:
            lines.append(f"storage_controls,{storage[0]},{storage[1]},{pool}")
        for method in (1, 2, 3):
            for cells in storage:
                for step in range(2):
                    lines += [f"cpu_step,{method},{cells},{step},{cells},0,0.01",
                                   f"gpu_step,{method},{cells},{step},4,3,0.02"]
                state = [2, 0, 0, 0, 200, 1, 1] + [0] * 30
                lines.append(f"state,{method},{cells}," + ",".join(map(str, state)))
            lines.append(f"metrics,{method},{2*sum(storage)},0,1e-12,1e-12,0.1,{pool},1024")
        lines.append("GENERATED_SPARSE_BURN_PARITY_PASS")
        return lines

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

    def test_capacity_matrix_and_controls(self):
        for storage, pool in [((2, 3), 2), ((32, 33), 32), ((128, 129), 32)]:
            with self.subTest(storage=storage):
                result = parse_transcript("\n".join(self.make_lines(storage, pool)),
                    self.metadata, self.controls, 2, storage, pool)
                self.assertEqual(result["storage_sizes"], storage)
                self.assertEqual(result["requested_pool_cells"], pool)
                self.assertEqual(result["field_budget"], 2.e-10)
                self.assertEqual(result["limiter_budget"], 2.e-8)

    def test_invalid_storage_extents(self):
        for storage, pool in [((0, 3), 2), ((3, 3), 2), ((4, 3), 2),
                              ((2, 3), 0), ((2, 3), 3), ((2,), 1),
                              ((2.0, 3), 2), ((True, 3), 1)]:
            with self.subTest(storage=storage, pool=pool), self.assertRaises(ValueError):
                validate_storage_controls(storage, pool)

    def test_missing_changed_or_exceeded_capacity(self):
        lines = self.make_lines((32, 33), 32)
        for replacement in ("", "storage_controls,32,34,32", "storage_controls,32,33,16"):
            changed = [line.replace("storage_controls,32,33,32", replacement) for line in lines]
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                parse_transcript("\n".join(changed), self.metadata, self.controls, 2, (32, 33), 32)
        changed = [line.replace(",0.1,32,1024", ",0.1,33,1024") for line in lines]
        with self.assertRaises(ValueError):
            parse_transcript("\n".join(changed), self.metadata, self.controls, 2, (32, 33), 32)
        with self.assertRaises(ValueError):
            self.parse(lines)

    def test_original_archived_audit31_transcript_is_compatible(self):
        archive = Path(__file__).parent / "results/sparse-native-20260907/release-900"
        evidence = json.loads((archive / "evidence.json").read_text())
        metadata = next(record["manifest"] for record in evidence["identity"]["build"]["registered_networks"]
                        if record["manifest"]["runtime_name"] == "custom:audit31")
        controls = dict(rho=1.e7, temperature=3.e9, interval=1.e-10, cv=1.e8, rtol=1.e-7)
        result = parse_transcript((archive / "trajectory/arch.stdout").read_text(), metadata, controls, 4)
        self.assertEqual(result["steps"], 4)
        self.assertEqual(result["storage_sizes"], (2, 3))


if __name__ == "__main__":
    unittest.main()
