"""Offline unit tests for the frozen RZ qualification runners.

These tests import the runners as libraries (no build, no candidate run) and
exercise the frozen checks with synthetic rows, hashes and exit codes only.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

RUNNER_DIR = Path(__file__).resolve().parents[2] / "validation/core_contracts/rz"


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, RUNNER_DIR / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CLOSURE = load_module("rz_closure_under_test", "run_frozen_rz_closure.py")
MUTATION = load_module("rz_mutation_under_test", "run_production_binding_mutations.py")

METHODS = ("Euler", "RK2", "RK3")
DIRECTIONS = (0, 1)
OPEN_FLAGS = (0, 1)
PHIS = ("-0.025", "0.025")
GOOD_SHA = "a" * 64


def row(prefix, **fields):
    return prefix + " " + " ".join("%s=%s" % (key, value) for key, value in fields.items())


def evolution_row(direction, open_flag, phi, method, j=1e-14, mass=2e-14, energy=3e-14,
                  species=4e-14, inner=1, steps=10, **extra):
    fields = dict(method=method, direction=direction, open=open_flag, external_phi=phi,
                  inner=inner, steps=steps, J_error=j, mass_error=mass, E_error=energy,
                  species_error=species)
    fields.update(extra)
    return row("RZ_ROTATING_BUDGET", **fields)


def restart_row(direction, open_flag, phi, method, **overrides):
    fields = dict(method=method, direction=direction, open=open_flag, phi=phi,
                  split_time=5e-4, final_time=1e-3, split_step=5, final_step=10,
                  bit_words=10240, checkpoint_sha=GOOD_SHA)
    fields.update(overrides)
    return row("RZ_FROZEN_CHECKPOINT_PASS", **fields)


def product_rows(skip=None, duplicate=None):
    rows = []
    for direction in DIRECTIONS:
        for open_flag in OPEN_FLAGS:
            for phi in PHIS:
                for method in METHODS:
                    case = (direction, open_flag, phi, method)
                    if case == skip:
                        continue
                    rows.append(CLOSURE.fields(evolution_row(*case)))
                    if duplicate == case:
                        rows.append(CLOSURE.fields(evolution_row(*case)))
    return rows


def restart_rows(evolution):
    return [CLOSURE.fields(restart_row(*key)) for key in evolution]


def sha256_of(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class FakeSubprocess:
    """Stand-in for the mutation module's ``subprocess`` (no real compiler run)."""
    TimeoutExpired = subprocess.TimeoutExpired

    def __init__(self, returncode=1, stdout="", stderr=""):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

    def run(self, *args, **kwargs):
        return self


def integration_layout(root):
    """Create a synthetic private source/build/output tree (never compiled)."""
    integration = root / "studio/.local/integration"
    source, build, output = integration / "src", integration / "build", integration / "out"
    for _, relative, old, _, _ in MUTATION.MUTATIONS:
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("// synthetic fixture\n" + old + "\n// tail\n")
    build.mkdir(parents=True, exist_ok=True)
    elf = build / "arch_curvilinear_metrics"
    elf.write_bytes(b"original-elf-bytes")
    (build / "CMakeCache.txt").write_text("CMAKE_HOME_DIRECTORY:INTERNAL=" + str(source) + "\n")
    fixture = source / MUTATION.closure.FIXTURE_RELATIVE
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_bytes(b"fixture-bytes")
    patch = root / MUTATION.closure.PATCH_RELATIVE
    patch.parent.mkdir(parents=True, exist_ok=True)
    patch.write_bytes(b"patch-bytes")
    identity = {"elf_sha256": sha256_of(elf), "fixture_sha256": sha256_of(fixture),
                "patch_sha256": sha256_of(patch)}
    return source, build, output, identity


def runner_argv(source, build, output, identity):
    return ["runner", "--source", str(source), "--build", str(build),
            "--output", str(output), "--identity", json.dumps(identity)]


class IdentityTests(unittest.TestCase):
    def _artifacts(self, root):
        elf, fixture, patch = root / "elf", root / "fixture", root / "patch"
        elf.write_bytes(b"elf-bytes")
        fixture.write_bytes(b"fixture-bytes")
        patch.write_bytes(b"patch-bytes")
        paths = {"elf": elf, "fixture": fixture, "patch": patch}
        identity = {"elf_sha256": sha256_of(elf), "fixture_sha256": sha256_of(fixture),
                    "patch_sha256": sha256_of(patch)}
        return paths, identity

    def test_identity_match_from_file_and_inline_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths, identity = self._artifacts(Path(tmp))
            declared = CLOSURE.read_identity(json.dumps(identity))
            expected = {key: identity[key] for key in CLOSURE.IDENTITY_KEYS}
            self.assertEqual(CLOSURE.verify_identity(declared, paths, "preflight"), expected)
            identity_file = Path(tmp) / "identity.json"
            identity_file.write_text(json.dumps(identity))
            self.assertEqual(CLOSURE.read_identity(str(identity_file)), declared)

    def test_identity_change_before_execution_rejects(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths, identity = self._artifacts(Path(tmp))
            declared = CLOSURE.read_identity(json.dumps(identity))
            paths["elf"].write_bytes(b"tampered")
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.verify_identity(declared, paths, "preflight")

    def test_identity_change_during_execution_rejects(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths, identity = self._artifacts(Path(tmp))
            declared = CLOSURE.read_identity(json.dumps(identity))
            CLOSURE.verify_identity(declared, paths, "preflight")
            paths["fixture"].write_bytes(b"changed-during-run")
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.verify_identity(declared, paths, "post-run owner")

    def test_mismatched_declared_identity_rejects(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths, identity = self._artifacts(Path(tmp))
            identity["patch_sha256"] = "b" * 64
            declared = CLOSURE.read_identity(json.dumps(identity))
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.verify_identity(declared, paths, "preflight")

    def test_identity_sha_format_is_enforced(self):
        for bad in ("A" * 64, "a" * 63, "z" * 64, 1234, None):
            payload = {"elf_sha256": bad, "fixture_sha256": GOOD_SHA, "patch_sha256": GOOD_SHA}
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.read_identity(json.dumps(payload))

    def test_identity_missing_key_rejects(self):
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.read_identity(json.dumps({"elf_sha256": GOOD_SHA}))

    def test_realistic_inline_three_hash_json_is_parsed_before_any_path_lookup(self):
        identity = {"elf_sha256": "a" * 64, "fixture_sha256": "b" * 64,
                    "patch_sha256": "c" * 64}
        compact = json.dumps(identity)
        longest = json.dumps(identity, indent=2)
        self.assertGreater(len(longest), 255)
        self.assertEqual(CLOSURE.read_identity(compact), identity)
        # A >255 character argument must never be handed to Path.is_file(),
        # which raises OSError (ENAMETOOLONG) for a single over-long component.
        self.assertEqual(CLOSURE.read_identity(longest), identity)

    def test_unusable_identity_path_raises_contract_error(self):
        for bad in ("/nonexistent/rz/identity.json", "/" + "a" * 400 + "/identity.json",
                    "not json at all"):
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.read_identity(bad)


class EvolutionProductTests(unittest.TestCase):
    def test_full_24_case_matrix_is_accepted(self):
        cases = CLOSURE.validate_evolution_rows(product_rows())
        self.assertEqual(len(cases), 24)
        self.assertEqual(set(cases), set(CLOSURE.EXPECTED_PRODUCT))

    def test_duplicate_case_rejects(self):
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_evolution_rows(product_rows(duplicate=(0, 0, "0.025", "RK2")))

    def test_missing_case_rejects(self):
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_evolution_rows(product_rows(skip=(1, 1, "-0.025", "RK3")))

    def test_out_of_contract_values_reject(self):
        for bad in (evolution_row(2, 0, "0.025", "RK2"),
                    evolution_row(0, 2, "0.025", "RK2"),
                    evolution_row(0, 0, "0.05", "RK2"),
                    evolution_row(0, 0, "0.025", "RK4")):
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.product_key(CLOSURE.fields(bad))

    def test_inner_and_steps_are_enforced(self):
        rows = product_rows()
        rows[0] = CLOSURE.fields(evolution_row(0, 0, "-0.025", "Euler", inner=2))
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_evolution_rows(rows)
        rows = product_rows()
        rows[0] = CLOSURE.fields(evolution_row(0, 0, "-0.025", "Euler", steps=9))
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_evolution_rows(rows)

    def test_error_metrics_reject_nan_inf_negative_and_large(self):
        for value in ("nan", "inf", "-inf", "-1e-14", "1e-6"):
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.check_error_metric(CLOSURE.fields(
                    evolution_row(0, 0, "0.025", "Euler", j=value)), "J_error")

    def test_error_metric_boundary_is_inclusive(self):
        record = CLOSURE.fields(evolution_row(0, 0, "0.025", "Euler", j=1e-12, mass=0,
                                              energy=0, species=0))
        for key in CLOSURE.ERROR_KEYS:
            CLOSURE.check_error_metric(record, key)

    def test_missing_error_metric_rejects(self):
        record = CLOSURE.fields(evolution_row(0, 0, "0.025", "Euler"))
        del record["species_error"]
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.check_error_metric(record, "species_error")

    def test_phi_must_equal_the_frozen_float_without_invented_tolerance(self):
        near_miss = repr(0.025 + 5e-13)
        self.assertNotEqual(float(near_miss), 0.025)
        self.assertAlmostEqual(abs(float(near_miss) - 0.025), 5e-13, delta=1e-18)
        for bad in (near_miss, "-" + near_miss, "0.05", "nan", "inf"):
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.canonical_phi(CLOSURE.fields(evolution_row(0, 0, bad, "Euler")))
        self.assertEqual(CLOSURE.canonical_phi(CLOSURE.fields(
            evolution_row(0, 0, "0.025", "Euler"))), "0.025")
        self.assertEqual(CLOSURE.canonical_phi(CLOSURE.fields(
            evolution_row(0, 0, "-0.025", "Euler"))), "-0.025")


class RestartTests(unittest.TestCase):
    def setUp(self):
        self.evolution = CLOSURE.validate_evolution_rows(product_rows())

    def test_full_restart_matrix_is_accepted(self):
        cases = CLOSURE.validate_restart_rows(restart_rows(self.evolution), self.evolution)
        self.assertEqual(len(cases), 24)

    def test_restart_product_mismatch_rejects(self):
        rows = restart_rows(self.evolution)
        rows[0] = CLOSURE.fields(restart_row(0, 0, "0.025", "Euler"))
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_restart_rows(rows, self.evolution)

    def test_restart_parameters_are_enforced(self):
        for field, bad in (("split_time", "6e-4"), ("final_time", "2e-3"),
                           ("split_step", "4"), ("final_step", "11"), ("bit_words", "10241")):
            rows = restart_rows(self.evolution)
            rows[0] = CLOSURE.fields(restart_row(0, 0, "-0.025", "Euler", **{field: bad}))
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.validate_restart_rows(rows, self.evolution)

    def test_checkpoint_sha_must_be_64_lowercase_hex(self):
        for bad in ("A" * 64, "a" * 63, "not-a-sha"):
            rows = restart_rows(self.evolution)
            rows[0] = CLOSURE.fields(restart_row(0, 0, "-0.025", "Euler", checkpoint_sha=bad))
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.validate_restart_rows(rows, self.evolution)

    def test_restart_endpoints_and_step_tokens_are_exact(self):
        drifted_split = repr(5e-4 + 1e-16)
        drifted_final = repr(1e-3 - 1e-16)
        self.assertNotEqual(float(drifted_split), 5e-4)
        self.assertNotEqual(float(drifted_final), 1e-3)
        for field, bad in (("split_time", drifted_split), ("final_time", drifted_final),
                           ("split_step", "5.000000000000001"), ("final_step", "10.0"),
                           ("bit_words", "10240.0"), ("bit_words", "1e4")):
            rows = restart_rows(self.evolution)
            rows[0] = CLOSURE.fields(restart_row(0, 0, "-0.025", "Euler", **{field: bad}))
            with self.assertRaises(CLOSURE.ContractError):
                CLOSURE.validate_restart_rows(rows, self.evolution)


class ProbeDiagnosticTests(unittest.TestCase):
    def test_owner_requires_exactly_one_row_and_rejects_11(self):
        good = row("RZ_PRODUCTION_OWNER_PASS", cache_once=1, rejects=11)
        self.assertEqual(CLOSURE.validate_owner_rows([CLOSURE.fields(good)])["rejects"], "11")
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_owner_rows([CLOSURE.fields(good), CLOSURE.fields(good)])
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_owner_rows([CLOSURE.fields(
                row("RZ_PRODUCTION_OWNER_PASS", cache_once=1, rejects=10))])
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_owner_rows([])

    def test_rejection_requires_one_row_per_method(self):
        rows = [CLOSURE.fields(row("RZ_NUMERICAL_REJECTION_PASS", method=method))
                for method in METHODS]
        self.assertEqual(len(CLOSURE.validate_rejection_rows(rows)), 3)
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_rejection_rows(rows[:2])
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_rejection_rows([rows[0], rows[0], rows[1]])

    def test_axis_failure_requires_exit_2_and_not_cleared(self):
        text = "RZ_EQUILIBRIUM_SPATIAL_GATE=NOT_CLEARED\nRZ_EQUILIBRIUM inner=1 gate=0\n"
        rows = CLOSURE.rows_for(text, "RZ_EQUILIBRIUM inner=")
        self.assertTrue(CLOSURE.validate_axis_failure(2, text, rows))
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_axis_failure(0, text, rows)
        with self.assertRaises(CLOSURE.ContractError):
            CLOSURE.validate_axis_failure(2, "RZ_EQUILIBRIUM_SPATIAL_GATE=CLEARED\n", rows)


class MutationEvidenceTests(unittest.TestCase):
    ORIGINAL = "1" * 64
    MUTANT = "2" * 64

    def test_expected_diagnostic_with_exact_exit_1_passes(self):
        for name, diagnostic in MUTATION.MUTATION_EXPECTATIONS.items():
            record = MUTATION.mutation_record(name=name, exit_code=1, output_text=diagnostic,
                                              original_elf_sha=self.ORIGINAL,
                                              mutant_elf_sha=self.MUTANT)
            self.assertEqual(record["exit_code"], 1)
            self.assertEqual(record["expected_diagnostic"], diagnostic)

    def test_signal_never_passes(self):
        name = "missing_numerical_rollback"
        diagnostic = MUTATION.MUTATION_EXPECTATIONS[name]
        for code in (-6, -11, -15):
            with self.assertRaises(MUTATION.closure.ContractError):
                MUTATION.mutation_record(name, code, diagnostic, self.ORIGINAL, self.MUTANT)

    def test_unrelated_exit_code_never_passes(self):
        name = "PCM_wrong_source_limiter"
        diagnostic = MUTATION.MUTATION_EXPECTATIONS[name]
        for code in (0, 2, 134, 255):
            with self.assertRaises(MUTATION.closure.ContractError):
                MUTATION.mutation_record(name, code, diagnostic, self.ORIGINAL, self.MUTANT)

    def test_timeout_never_passes(self):
        name = "duplicate_cache_consumption"
        diagnostic = MUTATION.MUTATION_EXPECTATIONS[name]
        with self.assertRaises(MUTATION.closure.ContractError):
            MUTATION.mutation_record(name, None, diagnostic, self.ORIGINAL, self.MUTANT,
                                     timed_out=True)

    def test_unchanged_mutant_elf_rejects(self):
        name = "duplicate_cache_consumption"
        diagnostic = MUTATION.MUTATION_EXPECTATIONS[name]
        with self.assertRaises(MUTATION.closure.ContractError):
            MUTATION.mutation_record(name, 1, diagnostic, self.ORIGINAL, self.ORIGINAL)

    def test_unexpected_diagnostic_rejects(self):
        name = "missing_numerical_rollback"
        with self.assertRaises(MUTATION.closure.ContractError):
            MUTATION.mutation_record(name, 1, "some other failure", self.ORIGINAL, self.MUTANT)

    def test_jobs_must_be_a_positive_integer(self):
        self.assertEqual(MUTATION.positive_int("3"), 3)
        for bad in ("0", "-1", "two", ""):
            with self.assertRaises(Exception):
                MUTATION.positive_int(bad)


class MutationCleanupTests(unittest.TestCase):
    """Mocked main(): an unrelated rejection must still restore and re-validate."""

    UNRELATED_EXIT = 3

    def test_unrelated_rejection_still_restores_and_revalidates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            source, build, output, identity = integration_layout(root)
            originals = {str(source / relative): (source / relative).read_bytes()
                         for _, relative, _, _, _ in MUTATION.MUTATIONS}
            rebuild_labels, verified_labels = [], []
            real_verify = MUTATION.closure.verify_identity

            def fake_rebuild(build_arg, output_arg, jobs, label):
                rebuild_labels.append(label)

            def spy_verify(identity_arg, paths, label="preflight"):
                verified_labels.append(label)
                return real_verify(identity_arg, paths, label)

            with mock.patch.object(MUTATION, "ROOT", root), \
                    mock.patch.object(MUTATION.closure, "ROOT", root), \
                    mock.patch.object(MUTATION, "subprocess",
                                      FakeSubprocess(returncode=self.UNRELATED_EXIT)), \
                    mock.patch.object(MUTATION, "rebuild_mutant", fake_rebuild), \
                    mock.patch.object(MUTATION.closure, "verify_identity", spy_verify), \
                    mock.patch.object(sys, "argv",
                                      runner_argv(source, build, output, identity)):
                with self.assertRaises(MUTATION.closure.ContractError):
                    MUTATION.main()
            # The mutant rebuild ran first; the cleanup rebuild must still follow it.
            self.assertEqual(rebuild_labels, ["missing_numerical_rollback", "restored"])
            self.assertIn("restored", verified_labels)
            for path, original in originals.items():
                self.assertEqual(Path(path).read_bytes(), original)

    def test_cleanup_failure_is_chained_under_the_original_reason(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            source, build, output, identity = integration_layout(root)

            def corrupting_rebuild(build_arg, output_arg, jobs, label):
                if label == "restored":
                    (build_arg / "arch_curvilinear_metrics").write_bytes(b"tampered-elf")

            with mock.patch.object(MUTATION, "ROOT", root), \
                    mock.patch.object(MUTATION.closure, "ROOT", root), \
                    mock.patch.object(MUTATION, "subprocess",
                                      FakeSubprocess(returncode=self.UNRELATED_EXIT)), \
                    mock.patch.object(MUTATION, "rebuild_mutant", corrupting_rebuild), \
                    mock.patch.object(sys, "argv",
                                      runner_argv(source, build, output, identity)):
                with self.assertRaises(RuntimeError) as caught:
                    MUTATION.main()
            self.assertNotIsInstance(caught.exception, MUTATION.closure.ContractError)
            self.assertIsInstance(caught.exception.__cause__, MUTATION.closure.ContractError)
            self.assertIn("restored ELF check failed", str(caught.exception))


def load_tests(loader, existing, pattern):
    """Run the independent exact RZ references in the existing runner owner.

    This extends discovery instead of scheduling a duplicate CI campaign.
    These reference identities certify their mathematics, not production gates.
    """
    original = list(sys.path)
    try:
        sys.path.insert(0, str(RUNNER_DIR))
        reference = load_module("rz_analytic_reference_tests", "test_analytic_reference.py")
        existing.addTests(loader.loadTestsFromModule(reference))
    finally:
        sys.path[:] = original
    return existing


if __name__ == "__main__":
    unittest.main()
