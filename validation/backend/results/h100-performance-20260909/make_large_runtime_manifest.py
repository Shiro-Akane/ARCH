"""Extend the maintained audit31/Helm application recipe without changing budgets.

Writes a new campaign manifest; never modifies the maintained historical recipe.
This is application/provider parity, not an independent large-network oracle.
"""
import argparse
import copy
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-root", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    raise RuntimeError("refusing to replace an existing campaign manifest")
original = json.loads((args.source_root / "validation/network/runtime_cases.json").read_text())
cases = []
for network in ("audit150", "audit200"):
    for ode in ("be_nr", "bd", "ros4"):
        key = f"generated_audit31_{ode}_helm"
        candidates = [case for case in original["cases"] if case["id"] == key]
        if len(candidates) != 1:
            raise RuntimeError(f"missing/ambiguous maintained recipe: {key}")
        case = copy.deepcopy(candidates[0])
        if case["plan_policy"]["network"] != "custom:audit31" \
                or case["overrides"]["network_name"] != "custom:audit31":
            raise RuntimeError("upstream network identity changed")
        case["id"] = f"generated_{network}_{ode}_helm"
        case["plan_policy"]["network"] = f"custom:{network}"
        case["overrides"]["network_name"] = f"custom:{network}"
        cases.append(case)
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps({"schema": 1,
    "description": "Large-server audit150/audit200 application parity campaign. "
                   "Only network identity differs from the maintained audit31/Helm recipes; "
                   "same physical inputs, accepted steps, scientific time and acceptance budgets. "
                   "Not an independent nuclear-reaction oracle or full release qualification.",
    "cases": cases}, indent=2) + "\n")
print(args.output)
