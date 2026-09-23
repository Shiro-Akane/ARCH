"""Derive matched 2D/3D controls from the user-archive FLASH 4.8 Cellular input.

Only existing FLASH runtime parameters are changed. The archive-contained Cellular
Fortran implementation and the prebuilt 2D executable are left untouched.
"""

import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True,
                        help="FLASH4.8/source/Simulation/SimulationMain/Cellular/flash.par")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--dimension", type=int, choices=(2, 3), default=2)
    args = parser.parse_args()
    output_dir = args.output_dir.resolve()
    updates = {
        "lrefine_max": "2",
        "basenm": f'"{output_dir}/cellular_"',
        "log_file": f'"{output_dir}/cellular_.log"',
        "checkpointFileIntervalStep": "20",
        "plotFileIntervalStep": "20",
        "nend": "20",
        "noiseAmplitude": "0.0",
        "xmax": "64.",
        "nblockx": "8",
        "nblocky": "4",
        "usePseudo1d": ".true.",
        "pocket_mode": "4",
        "pocket_vol_frac_he": "1.0",
    }
    if args.dimension == 3:
        updates.update({"zmax": "16.", "nblockz": "2"})
    lines = args.source.read_text().splitlines()
    for key, value in updates.items():
        pattern = re.compile(rf"^\s*{re.escape(key)}\s*=")
        indices = [i for i, line in enumerate(lines) if pattern.match(line)]
        if len(indices) != 1:
            raise ValueError(f"Expected one active FLASH parameter {key}; found {len(indices)}")
        lines[indices[0]] = f"{key} = {value}"
    args.output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
