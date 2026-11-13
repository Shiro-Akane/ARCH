"""Proposed MPI launch and actual startup share one policy before output acquisition."""
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import unittest

ARCH, ROOT = (Path(p).resolve() for p in sys.argv[1:3])
ENABLED = sys.argv[3] == "ON"
MPIEXEC = sys.argv[4] if len(sys.argv) > 4 else ""
del sys.argv[1:]


def parameters(text, values):
    for key, value in values.items():
        pattern = rf"(?m)^\s*{re.escape(key)}\s*=.*$"
        line = f"{key} = {value}"
        text = re.sub(pattern, lambda match: line, text) if re.search(pattern, text) else text + "\n" + line
    return text


BASE = parameters((ROOT / "simulation/Sod/Sod.par").read_text(), {
    "nblockx1": 7, "max_blocks": 8, "max_steps": 2,
    "lrefinemin": 0, "lrefinemax": 0, "chk_dt": -1, "chk_dstep": -1})
AMR_BASE = parameters((ROOT / "simulation/Sedov/Sedov.par").read_text(), {
    "nblockx1": 7, "nblockx2": 2, "max_blocks": 512, "max_steps": 2,
    "lrefinemin": 0, "lrefinemax": 2, "chk_dt": -1, "chk_dstep": -1})
ENV = dict(os.environ, OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")


class MpiPreflight(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="arch-mpi-preflight-")
        self.addCleanup(self.tmp.cleanup)
        self.cwd = Path(self.tmp.name)

    def query(self, text, ranks, expected, resolved="cpu", case="Sod"):
        before = sorted(self.cwd.rglob("*"))
        result = subprocess.run([str(ARCH), "--check-run", case, "--config-stdin",
            "--request-id", "mpi-plan", "--mpi-ranks", str(ranks)],
            input=text, text=True, capture_output=True, cwd=self.cwd, env=ENV, timeout=30)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(before, sorted(self.cwd.rglob("*")), "query wrote output")
        out = json.loads(result.stdout)
        self.assertEqual(out["identity"], dict(caseId=case, requestId="mpi-plan",
            configRevision=hashlib.sha256(text.encode()).hexdigest(), mpiRanks=ranks))
        self.assertEqual(out["status"], "ok" if expected == 0 else "error")
        if expected:
            self.assertIsNone(out["data"])
        else:
            self.assertEqual(out["data"]["mpiEnabled"], ENABLED)
            if resolved is not None:
                self.assertEqual(out["data"]["resolvedBackend"], resolved)
        return out

    def test_build_capability_and_launch_identity(self):
        self.query(BASE, 1, 0)
        for ranks in (2, 4, 7):
            out = self.query(BASE, ranks, 0 if ENABLED else 3)
            if not ENABLED:
                self.assertIn("MPI build disabled", str(out["diagnostics"]))
        self.query(BASE, 2147483647, 3)

    def test_auto_and_explicit_cuda(self):
        automatic = parameters(BASE, {"compute_backend": "auto"})
        single = self.query(automatic, 1, 0, resolved=None)
        resolved = single["data"]["resolvedBackend"]
        if ENABLED:
            multi = self.query(automatic, 2, 0, resolved=resolved)
            self.assertEqual(multi["data"]["requestedBackend"], "auto")
            self.assertEqual(multi["data"]["fallbackReason"], single["data"]["fallbackReason"])
        expected = 0 if ENABLED and resolved == "cuda" else 3
        out = self.query(parameters(BASE, {"compute_backend": "cuda"}), 2, expected, resolved="cuda")
        if not ENABLED:
            self.assertIn("MPI build disabled", str(out["diagnostics"]))
        elif expected:
            self.assertIn(single["data"]["fallbackReason"], str(out["diagnostics"]))

    def test_native_amr_checkpoint_and_restart_requests(self):
        for changes in ({"chk_dstep": 1}, {"chk_dt": 0.1},
                        {"restart": "true", "restart_file": "saved.collection"},
                        {"nblockx2": 2, "lrefinemax": 1, "max_blocks": 512},
                        {"nblockx2": 2, "lrefinemax": 2, "max_blocks": 512}):
            with self.subTest(changes=changes):
                dynamic = "nblockx2" in changes
                self.query(parameters(AMR_BASE if dynamic else BASE, changes),
                           2, 0 if ENABLED else 3, case="Sedov" if dynamic else "Sod")

    def test_malformed_and_inapplicable_options(self):
        for values in (["0"], ["-1"], ["2.5"], ["2147483648"], ["2x"], [], ["2", "--mpi-ranks", "4"]):
            result = subprocess.run([str(ARCH), "--check-run", "Sod", "--config-stdin",
                "--mpi-ranks", *values], input=BASE, text=True, capture_output=True,
                cwd=self.cwd, env=ENV, timeout=30)
            self.assertEqual(result.returncode, 3)
            self.assertEqual(json.loads(result.stdout)["diagnostics"][0]["code"], "INVALID_REQUEST")
        result = subprocess.run([str(ARCH), "--inspect-config", "Sod", "--config-stdin",
            "--mpi-ranks", "2"], input=BASE, text=True, capture_output=True,
            cwd=self.cwd, env=ENV, timeout=30)
        self.assertNotEqual(result.returncode, 0)

    def test_rejected_queries_match_real_startup(self):
        cases = {
            "amr": {"lrefinemax": 1},
            "empty-rank": {"nblockx1": 1},
            "pool": {"max_blocks": 6},
            "amr-level": {"nblockx2": 2, "lrefinemax": 3},
            "amr-3d": {"nblockx2": 1, "nblockx3": 1, "lrefinemax": 1},
            "restart-shard": {"restart": "true", "restart_file": "rank-0/state.h5"},
            "output-name": {"base_name": "../escape"},
            "cuda": {"compute_backend": "cuda", "cuda_device": 2147483647},
            "one-sided-periodic": {"x1l_boundary_type": "periodic"},
            "gravity": {"gravity_type": "external", "gravity_g_x": 1, "gravity_g_y": 0, "gravity_g_z": 0},
        }
        for name, changes in cases.items():
            with self.subTest(case=name):
                output = self.cwd / (name + "-output")
                dynamic = "nblockx2" in changes
                case = "Sedov" if dynamic else "Sod"
                text = parameters(AMR_BASE if dynamic else BASE, {"out_dir": output, **changes})
                out = self.query(text, 2, 3, case=case)
                reason = next(d["message"] for d in out["diagnostics"] if d["severity"] == "error")
                if not ENABLED:
                    self.assertIn("MPI build disabled", reason)
                    continue
                expected = {
                    "amr": "MPI AMR requires", "empty-rank": "X slabs per rank",
                    "pool": "max_blocks", "amr-level": "MPI AMR requires",
                    "amr-3d": "MPI AMR requires", "restart-shard": "published .collection",
                    "output-name": "one filename component", "cuda": "CUDA",
                    "one-sided-periodic": "paired periodic faces", "gravity": "MPI sources",
                }
                self.assertIn(expected[name], reason)
                par = self.cwd / (name + ".par")
                par.write_text(text)
                child = subprocess.Popen([MPIEXEC, "-n", "2", str(ARCH), case, str(par)],
                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, cwd=self.cwd, env=ENV, start_new_session=(os.name == "posix"))
                try:
                    log, _ = child.communicate(timeout=30)
                except subprocess.TimeoutExpired:
                    if os.name == "posix":
                        os.killpg(child.pid, signal.SIGKILL)
                    else:
                        child.kill()
                    child.communicate()
                    self.fail("MPI rejection hung: " + name)
                self.assertNotEqual(child.returncode, 0, log)
                self.assertIn(reason, log, "query and launch disagreed")
                self.assertFalse(output.exists(), "rejection acquired output resources")


if __name__ == "__main__":
    unittest.main()
