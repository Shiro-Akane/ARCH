#!/usr/bin/env python3
"""Re-run approved t=0 inputs with ALL plot fields; preserve raw outputs locally."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def entries(text):
    result = {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        key, sep, value = line.partition("=")
        if not sep or key.strip() in result:
            raise ValueError("Reference must have unique plain assignment lines.")
        result[key.strip()] = value.strip()
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--references", required=True, help="JSON list of approved t=0 evidence directories")
    parser.add_argument("--output-root", required=True, help="Persistent local raw-data directory")
    args = parser.parse_args()
    binary = Path(args.binary).resolve()
    references = json.loads(Path(args.references).read_text())
    if not isinstance(references, list) or not 1 <= len(references) <= 2:
        raise ValueError("Use one or two existing approved references.")
    root = Path(args.output_root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    rows = []
    for reference in references:
        ref = Path(reference).resolve()
        record = json.loads((ref / "summary.json").read_text())
        case = record["case"]
        if case not in ("Sod", "CellularDet"):
            raise ValueError("This tool is scoped to approved Sod/CellularDet t=0 inputs.")
        source = ref / (case + ".par")
        text = source.read_text()
        values = entries(text)
        if sha(source) != record["inputSha256"] or sha(binary) != record["binarySha256"]:
            raise ValueError("Reference input or executable identity changed.")
        if values.get("tmax") != "0" or values.get("max_steps") != "-1" or values.get("compute_backend") != "cpu":
            raise ValueError("Only explicitly recorded CPU t=0 inputs may run.")
        evidence = Path(tempfile.mkdtemp(prefix=case + "-", dir=root))
        output = evidence / "output"
        updated = re.sub(r"^plt_variables\s*=.*$", "plt_variables=ALL", text, flags=re.MULTILINE)
        updated = re.sub(r"^out_dir\s*=.*$", "out_dir=" + str(output), updated, flags=re.MULTILINE)
        changes = {key for key in values if values[key] != entries(updated).get(key)}
        if changes != {"plt_variables", "out_dir"} or set(entries(updated)) != set(values):
            raise ValueError("Unexpected change outside output selection/directory.")
        config = evidence / (case + ".par")
        config.write_text(updated)
        env = dict(os.environ, OMP_NUM_THREADS="1", CUDA_VISIBLE_DEVICES="")
        error = None
        with (evidence / "stdout.log").open("w") as stdout, (evidence / "stderr.log").open("w") as stderr:
            try:
                result = subprocess.run([str(binary), case, str(config)], cwd=evidence,
                                        env=env, stdout=stdout, stderr=stderr, timeout=90)
                exit_code = result.returncode
            except subprocess.TimeoutExpired:
                exit_code, error = 124, "t=0 process exceeded 90-second wall limit"
            except OSError:
                exit_code, error = 127, "t=0 executable could not be started"
        if sha(binary) != record["binarySha256"]:
            raise ValueError("Executable identity changed during the t=0 run.")
        row = {"case": case, "scope": "t=0 only; no evolution acceptance",
               "referenceInputSha256": sha(source), "inputSha256": sha(config),
               "binarySha256": sha(binary), "changedKeys": sorted(changes),
               "exitCode": exit_code, "error": error, "localEvidenceDirectory": str(evidence)}
        (evidence / "summary.json").write_text(json.dumps(row, indent=2) + "\n")
        rows.append(row)
        (root / "runs.json").write_text(json.dumps(rows, indent=2) + "\n")
        if exit_code:
            print(json.dumps({"status": "failed", "runs": rows}))
            return exit_code
    print(json.dumps({"status": "completed", "runs": rows}, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
