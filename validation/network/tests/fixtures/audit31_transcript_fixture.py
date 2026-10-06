"""Shared deterministic audit31 parser fixture; never scientific/timing evidence.

Original semantic metadata, controls and counters were extracted from the
2026-09-07 release-900 record. Original raw output remains local; finite final
states and zero diagnostic times below are explicit synthetic witnesses.
"""
import hashlib
import json
from pathlib import Path

FIXTURE_SHA256 = "d2acb011850217cfb264b213fb1ad47b2cd48ca25b717894bfe0c170c06ea432"


def load_audit31_fixture():
    """Verify the committed fixture identity and expand its compact CSV matrix."""
    path = Path(__file__).with_name("audit31_transcript_contract.json")
    payload = path.read_bytes()
    if hashlib.sha256(payload).hexdigest() != FIXTURE_SHA256:
        raise AssertionError("audit31 parser fixture identity changed")
    fixture = json.loads(payload)
    if fixture["schema"] != 1:
        raise AssertionError("unsupported audit31 parser fixture schema")
    metadata = fixture["metadata"]
    controls = {name: fixture["controls"][name]
                for name in ("rho", "temperature", "interval", "cv", "rtol")}
    equations = len(metadata["species"]) + 1 + metadata["auxiliary_equations"]
    lines = [",".join(map(str, ("controls", metadata["runtime_name"], equations,
        *controls.values(), fixture["steps"], "selected_ode", -1)))]
    storage = tuple(fixture["storage_sizes"])
    if storage != (2, 3) or fixture["pool_cells"] != 2:
        lines.append(",".join(map(str, ("storage_controls", *storage, fixture["pool_cells"]))))
    for record in fixture["methods"]:
        method = record["method"]
        for kind in ("cpu_step", "gpu_step"):
            for step in record[kind + "s"]:
                lines.append(",".join(map(str, (kind, method, *step, 0))))
        for cells in storage:
            lines.append(",".join(map(str,
                ("state", method, cells, *fixture["representative_state"]))))
        lines.append(",".join(map(str, ("metrics", method, *record["metrics"]))))
    lines.append("GENERATED_SPARSE_BURN_PARITY_PASS")
    fixture["controls"] = controls
    fixture["transcript"] = "\n".join(lines)
    fixture["fixture_identity"] = dict(name=path.name, sha256=FIXTURE_SHA256,
                                      scope=fixture["scope"])
    return fixture
