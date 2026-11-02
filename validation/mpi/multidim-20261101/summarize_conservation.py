#!/usr/bin/env python3
"""Post-run conservation diagnostic; never changes a simulation or its pass gate."""
import argparse
import itertools
import json
import math
from pathlib import Path

import h5py
import numpy as np


def integrals(output):
    shards = sorted(output.glob("rank-*")) or [output]
    files = [sorted(shard.glob("*_plt_*.h5")) for shard in shards]
    assert files and files[0] and all(len(group) == len(files[0]) for group in files)
    result = []
    for frame in zip(*files):
        mass, energy, times = [], [], []
        for path in frame:
            with h5py.File(path) as f:
                assert f.attrs["plot_publication_state"] == "complete"
                times.append(float(f.attrs["time"]))
                volume = f["NativeGrid/cell_measure"][:].reshape(-1)
                mass.append(f["Data/DENS"][:].reshape(-1) * volume)
                # PlotIO.cpp stores s.get(idx).eng; metadata calls it total_energy_density.
                energy.append(f["Data/ENER"][:].reshape(-1) * volume)
        assert len(set(times)) == 1
        result.append({"time": times[0],
                       "mass": math.fsum(itertools.chain.from_iterable(mass)),
                       "total_energy": math.fsum(itertools.chain.from_iterable(energy))})
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    args = parser.parse_args()
    validation = json.loads((args.evidence / "summary.json").read_text())
    assert validation["state"] == "passed"
    report = {"scope": "post-hoc native-volume integrals; open-boundary drift includes boundary flux",
              "summation": "math.fsum of binary64 density * native cell measure", "cases": []}
    for case in validation["cases"]:
        root = args.evidence / case["case"]
        reference = integrals(root / "baseline" / "output")
        entry = {"case": case["case"], "baseline": reference, "lanes_equal_to_baseline": [],
                 "max_relative_drift": {
                     name: max(abs(frame[name] - reference[0][name]) for frame in reference)
                     / abs(reference[0][name]) for name in ("mass", "total_energy")}}
        for lane in case["comparisons"]:
            observed = integrals(root / lane["lane"] / "output")
            assert observed == reference, (case["case"], lane["lane"], "integral differs")
            entry["lanes_equal_to_baseline"].append(lane["lane"])
        files = sorted((root / "baseline" / "output").glob("*_plt_*.h5"))
        with h5py.File(files[0]) as first, h5py.File(files[-1]) as last:
            entry["max_density_change"] = float(np.max(np.abs(first["Data/DENS"][:] - last["Data/DENS"][:])))
            entry["max_energy_density_change"] = float(np.max(np.abs(first["Data/ENER"][:] - last["Data/ENER"][:])))
        assert entry["max_density_change"] > 0, "fixture did not evolve"
        report["cases"].append(entry)
    (args.evidence / "conservation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({c["case"]: c["max_relative_drift"] for c in report["cases"]}))


if __name__ == "__main__":
    main()
