#!/usr/bin/env python3
"""Check proposed fixture consistency, not ARCH implementation acceptance."""
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def read(name):
    return json.loads((HERE / name).read_text())


def check():
    schema = read("schema.json")
    standard = {p["key"]: p for p in schema["parameters"]}
    assert len(standard) == len(schema["parameters"]) == 94
    assert "gravity_G" not in standard
    assert "gravity_G" in schema["retiredParameters"]
    classes = read("classification.json")["parameters"]
    assert len({p["key"] for p in classes}) == len(classes) == 95
    assert {p["key"] for p in classes} == set(standard) | {"gravity_G"}
    for category, expected in [("required", 19), ("conditional", 50),
                               ("documented-default-allowed", 25), ("retired", 1)]:
        assert sum(p["classification"] == category for p in classes) == expected
    declarations = schema["parameters"] + schema["auxiliaryParameters"]
    declarations += schema["caseDeclarations"][0]["parameters"]
    declared = {(p["caseId"], p["key"]): p for p in declarations}
    assert len(declared) == len(declarations) == 102
    assert sum(p["allowedDefault"] is not None for p in standard.values()) == 25
    for p in declarations:
        assert "defaultValue" not in p and "defaultSource" not in p
        if p["requirement"]["kind"] != "optional":
            assert p["allowedDefault"] is None
        if p["options"]:
            assert p["options"]["unknownBehavior"] == "error"

    replies = {}
    for stem in ["syntax-errors", "empty", "sod-valid", "missing-switch"]:
        raw = (HERE / (stem + ".par")).read_bytes()
        result = read(stem + ".json")
        replies[stem] = result
        assert result["version"] == "3" and result["schemaVersion"] == "1.0"
        assert result["identity"]["configRevision"] == hashlib.sha256(raw).hexdigest()
        assert (HERE / (stem + ".json")).stat().st_size < 8 * 1024 * 1024
        assert result["execution"]["setup"] == "not_executed"
        assert result["execution"]["simulationReadiness"] == "not_checked"
        assert len({(p["caseId"], p["key"]) for p in result["parameters"]}) == len(result["parameters"])
        lines = raw.splitlines()
        for entry in result["parameters"] + result["diagnostics"]:
            for loc in entry["locations"]:
                line = lines[loc["line"] - 1]
                assert 1 <= loc["column"] <= loc["endColumn"] <= len(line) + 1
                if loc["rawValue"] is not None:
                    assert line[loc["column"] - 1:loc["endColumn"] - 1].decode() == loc["rawValue"]
        for p in result["parameters"]:
            assert (p["caseId"], p["key"]) in declared
            if p["inputState"] == "missing":
                assert p["rawValue"] is None and p["parsedValue"] is None and not p["locations"]
            if p["inputState"] in ("invalid", "duplicate"):
                assert p["parsedValue"] is None and p["resolvedValue"] is None and p["valueSource"] is None
            if p["requirement"]["state"] == "unknown-dependency":
                assert p["requirement"]["required"] is None
            if p["valueSource"] is not None:
                assert p["resolvedValue"] is not None and p["sourceEvidence"] is not None

    valid = replies["sod-valid"]
    assert valid["status"] == "ok" and valid["completeness"]["state"] == "complete"
    assert all(valid["coverage"].values()) and not valid["diagnostics"]
    assert {(p["caseId"], p["key"]) for p in valid["parameters"]} == set(declared)
    for p in valid["parameters"]:
        if p["requirement"]["required"]:
            assert p["resolvedValue"] is not None
    by_key = {p["key"]: p for p in valid["parameters"]}
    assert by_key["nblockx2"]["parsedValue"] == 0
    assert by_key["use_burn"]["parsedValue"] is False
    assert by_key["log_dir"]["valueSource"] == "derived"
    assert by_key["dt_min"]["parsedValue"] is None
    assert by_key["dt_min"]["valueSource"] == "documented-default"

    missing = replies["missing-switch"]
    by_key = {p["key"]: p for p in missing["parameters"]}
    assert missing["status"] == "error"
    assert by_key["use_burn"]["resolvedValue"] is None
    assert by_key["ode_rtol"]["requirement"]["state"] == "unknown-dependency"
    assert [d["parameterKey"] for d in missing["diagnostics"] if d["code"] == "MISSING_PARAMETER"] == ["use_burn"]
    empty = replies["empty"]
    assert sum(d["code"] == "MISSING_PARAMETER" for d in empty["diagnostics"]) == 19
    assert next(p for p in empty["parameters"] if p["key"] == "restart_file")["requirement"]["required"] is False
    print("PASS: candidate schema, declaration coverage, four envelopes, hashes, source spans and absence invariants.")
    print("NOT production acceptance: Core/Host still implement configuration v2.")


if __name__ == "__main__":
    check()
