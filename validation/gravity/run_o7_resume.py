#!/usr/bin/env python3
"""Replay only the Core-approved O7 resume short gates; raw output stays local.

This does not freeze a long-run endpoint or approve independent JENS/RZ budgets.
An output directory must be new, so prior failure evidence cannot be overwritten.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import traceback

import h5py
import numpy as np

from run_self_gravity import Campaign
from radial_1d import RadialCampaign

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    executable, output = args.arch.resolve(), args.output.resolve()
    if not executable.is_file():
        parser.error("ARCH executable is missing")
    output.mkdir(parents=True, exist_ok=False)
    inputs = [
        "validation/gravity/run_self_gravity.py", "validation/gravity/radial_1d.py",
        "simulation/JeansWave/JeansWave.par", "simulation/GravityBox/GravityBox.par",
        "simulation/GravityBox/GravityBox.cpp", "src/physics/eos/IdealGas.h",
        "src/physics/constant/PhysicalConstants.h",
        "src/numerics/elliptic/CartesianPoisson.cpp",
        "src/numerics/elliptic/CartesianPoisson.h",
        "src/numerics/elliptic/CompositePoisson.h",
        "src/numerics/elliptic/CompositePoisson.cpp",
        "src/numerics/multigrid/CompositeMultigrid.cpp",
    ]
    result = dict(
        authority="11a321d5604f9ee62b9f9587c81f14de4f128bc4:2.1",
        executableSha256=digest(executable),
        sourceHead=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        repositoryDirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True)),
        inputFingerprints={name: digest(ROOT / name) for name in inputs},
        cases=[], scope="short gates only; not full scientific acceptance",
    )
    jobs = ["native-mixed-3d", "spherical", "cylindrical"]
    for name in jobs:
        try:
            if name == "native-mixed-3d":
                campaign = Campaign(executable, output / "jeans")
                _, _, record = campaign.run(
                    name, nblockx1=4, nblockx2=1, nblockx3=1,
                    x2_min=0, x3_min=0, x2_max=.25, x3_max=.25,
                    max_blocks=32, max_steps=2, tmax=.02, phase=math.pi/4,
                    lrefinemax=1, refine_threshold=2e-5, derefine_threshold=5e-6)
                record["endpointNote"] = "original max_steps=2; tmax is not a reached endpoint"
            else:
                campaign = RadialCampaign(executable, output / "radial")
                campaign.regrid_cycle(name)
                record = campaign.results[-1]
                initial = next((output / "radial" / (name + "-refine-coarsen")).glob("*plt_0000.h5"))
                with h5py.File(initial) as handle:
                    rho = handle["Data/DENS"][:]
                    specific_energy = handle["Data/ENER"][:] / rho - .5 * handle["Data/VELX"][:] ** 2
                    cv = float(campaign.base["gas_cv"])
                    error = float(np.max(np.abs(specific_energy / (cv * 1e9) - 1)))
                    if not error < 16 * np.finfo(float).eps:
                        raise RuntimeError("initial specific energy no longer matches unchanged IdealGas temperature")
                    record["initialSpecificEnergyRelativeError"] = error
                    record["initialDensityAboveUnchangedFloor"] = bool(np.min(rho) > float(campaign.base["sml_rho"]))
                    if not record["initialDensityAboveUnchangedFloor"]:
                        raise RuntimeError("initial density floor overlaps migrated state")
            result["cases"].append(dict(name=name, status="PASS", record=record))
        except Exception as error:
            result["cases"].append(dict(name=name, status="FAIL", error=str(error),
                                        traceback=traceback.format_exc()))
    result["status"] = "PASS" if all(case["status"] == "PASS" for case in result["cases"]) else "FAIL"
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
