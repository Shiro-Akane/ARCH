"""Compare matched ARCH and original FLASH Cellular leaf plots on one CGS grid.

The comparison uses piecewise constant cell values; it does not claim
identical numerical methods or a four-module FLASH reference. Both two- and
three-dimensional leaf outputs are supported. Every target
pixel must be covered by exactly one active leaf cell from each program.
"""

import argparse
import json
import math
from pathlib import Path

import h5py
import numpy as np

FIELDS = {
    "density": ("DENS", "dens"),
    "temperature": ("TEMP", "temp"),
    "pressure": ("PRES", "pres"),
    "velocity_x": ("VELX", "velx"),
    "helium_4": ("he4", "he4"),
    "carbon_12": ("c12", "c12"),
}


def blank(shape):
    """Allocate field and coverage arrays for the common finest grid."""
    return ({name: np.full(shape, np.nan) for name in FIELDS},
            np.zeros(shape, dtype=np.uint8))


def paint(fields, cover, start, extent, values):
    """Write one source cell into its exact pixel box, detecting overlap."""
    region = tuple(slice(first, first + size) for first, size in zip(start, extent))
    area = cover[region]
    if area.shape != extent or np.any(area):
        raise ValueError("Leaf cell is outside the common grid or overlaps another")
    area[:] = 1
    for name, value in values.items():
        fields[name][region] = value


def arch_plot(path, shape, dx):
    """Rasterize active ARCH blocks, using the stored native cell centres."""
    fields, cover = blank(shape)
    with h5py.File(path) as handle:
        time = float(handle.attrs["time"])
        levels = handle["Grid/level"][()]
        blocks = len(levels)
        x = handle["Grid/x"][()].reshape(blocks, 16, 16)
        y = handle["Grid/y"][()].reshape(blocks, 16, 16)
        values = {name: handle[f"Data/{field}"][()].reshape(blocks, 16, 16)
                  for name, (field, _) in FIELDS.items()}
        for block, level in enumerate(levels):
            if level not in (0, 1):
                raise ValueError("This fixed comparison expects ARCH AMR levels 0 and 1")
            factor = 2 if level == 0 else 1
            width = dx * factor
            for j in range(16):
                for i in range(16):
                    col = round((x[block, j, i] - width / 2) / dx)
                    row = round((y[block, j, i] - width / 2) / dx)
                    paint(fields, cover, (row, col), (factor, factor),
                          {name: array[block, j, i] for name, array in values.items()})
        histogram = {str(int(level)): int(count) for level, count in
                     zip(*np.unique(levels, return_counts=True))}
    if not np.all(cover):
        raise ValueError(f"ARCH plot leaves {int(np.count_nonzero(cover == 0))} pixels empty")
    return fields, histogram, time


def flash_plot(path, shape, dx):
    """Rasterize only FLASH leaf blocks from its original HDF5 output."""
    fields, cover = blank(shape)
    with h5py.File(path) as handle:
        time_values = [float(row["value"]) for row in handle["real scalars"][()]
                       if row["name"].decode().strip() == "time"]
        if len(time_values) != 1:
            raise ValueError("FLASH plot must contain exactly one time scalar")
        time = time_values[0]
        active = np.flatnonzero(handle["node type"][()] == 1)
        levels = handle["refine level"][()]
        boxes = handle["bounding box"][()]
        values = {name: handle[field][()] for name, (_, field) in FIELDS.items()}
        for block in active:
            bounds = boxes[block]
            width_x = (bounds[0, 1] - bounds[0, 0]) / 8
            width_y = (bounds[1, 1] - bounds[1, 0]) / 8
            fx, fy = round(width_x / dx), round(width_y / dx)
            if fx <= 0 or fy <= 0:
                raise ValueError("FLASH cell finer than the requested common grid")
            for j in range(8):
                for i in range(8):
                    col = round((bounds[0, 0] + i * width_x) / dx)
                    row = round((bounds[1, 0] + j * width_y) / dx)
                    paint(fields, cover, (row, col), (fy, fx),
                          {name: array[block, 0, j, i] for name, array in values.items()})
        histogram = {str(int(level)): int(count) for level, count in
                     zip(*np.unique(levels[active], return_counts=True))}
    if not np.all(cover):
        raise ValueError(f"FLASH plot leaves {int(np.count_nonzero(cover == 0))} pixels empty")
    return fields, histogram, time


def arch_plot_3d(path, shape, dx):
    """Rasterize the active ARCH 3D hierarchy in z/y/x array order."""
    fields, cover = blank(shape)
    with h5py.File(path) as handle:
        time = float(handle.attrs["time"])
        levels = handle["Grid/level"][()]
        blocks = len(levels)
        xyz = [handle[f"Grid/{axis}"][()].reshape(blocks, 16, 16, 16)
               for axis in "xyz"]
        values = {name: handle[f"Data/{field}"][()].reshape(blocks, 16, 16, 16)
                  for name, (field, _) in FIELDS.items()}
        for block, level in enumerate(levels):
            if level not in (0, 1):
                raise ValueError("This fixed comparison expects ARCH AMR levels 0 and 1")
            factor = 2 if level == 0 else 1
            width = dx * factor
            for k, j, i in np.ndindex(16, 16, 16):
                start = tuple(round((coord[block, k, j, i] - width / 2) / dx)
                              for coord in reversed(xyz))
                paint(fields, cover, start, (factor,) * 3,
                      {name: array[block, k, j, i] for name, array in values.items()})
        histogram = {str(int(level)): int(count) for level, count in
                     zip(*np.unique(levels, return_counts=True))}
    if not np.all(cover):
        raise ValueError(f"ARCH plot leaves {int(np.count_nonzero(cover == 0))} pixels empty")
    return fields, histogram, time


def flash_plot_3d(path, shape, dx):
    """Rasterize only active FLASH 3D leaf blocks at the common pixel scale."""
    fields, cover = blank(shape)
    with h5py.File(path) as handle:
        time_values = [float(row["value"]) for row in handle["real scalars"][()]
                       if row["name"].decode().strip() == "time"]
        if len(time_values) != 1:
            raise ValueError("FLASH plot must contain exactly one time scalar")
        time = time_values[0]
        active = np.flatnonzero(handle["node type"][()] == 1)
        levels = handle["refine level"][()]
        boxes = handle["bounding box"][()]
        values = {name: handle[field][()] for name, (_, field) in FIELDS.items()}
        for block in active:
            bounds = boxes[block]
            width = [(bounds[axis, 1] - bounds[axis, 0]) / 8 for axis in range(3)]
            factor = tuple(round(length / dx) for length in reversed(width))
            if min(factor) <= 0:
                raise ValueError("FLASH cell finer than the requested common grid")
            for k, j, i in np.ndindex(8, 8, 8):
                start = (round((bounds[2, 0] + k * width[2]) / dx),
                         round((bounds[1, 0] + j * width[1]) / dx),
                         round((bounds[0, 0] + i * width[0]) / dx))
                paint(fields, cover, start, factor,
                      {name: array[block, k, j, i] for name, array in values.items()})
        histogram = {str(int(level)): int(count) for level, count in
                     zip(*np.unique(levels[active], return_counts=True))}
    if not np.all(cover):
        raise ValueError(f"FLASH plot leaves {int(np.count_nonzero(cover == 0))} pixels empty")
    return fields, histogram, time


def compare(arch_path, flash_path, shape, dx):
    """Compute volume-weighted L1 errors and extrema at one matched time."""
    read_arch, read_flash = ((arch_plot, flash_plot) if len(shape) == 2
                             else (arch_plot_3d, flash_plot_3d))
    arch, arch_levels, arch_time = read_arch(arch_path, shape, dx)
    flash, flash_levels, flash_time = read_flash(flash_path, shape, dx)
    if not (math.isfinite(arch_time) and math.isfinite(flash_time)
            and math.isclose(arch_time, flash_time, rel_tol=1e-12, abs_tol=1e-25)):
        raise ValueError(f"Plots have different physical times: ARCH={arch_time}, FLASH={flash_time}")
    result = {"time_seconds": arch_time,
              "arch_leaves_by_level": arch_levels,
              "flash_leaves_by_level": flash_levels, "fields": {}}
    for name in FIELDS:
        a, f = arch[name], flash[name]
        if not np.all(np.isfinite(a)) or not np.all(np.isfinite(f)):
            raise ValueError(f"Non-finite value in {name}")
        absolute = float(np.mean(np.abs(a - f)))
        scale = float(np.mean(np.abs(f)))
        result["fields"][name] = {
            "absolute_L1": absolute,
            "relative_L1": absolute / scale if scale else None,
            "arch_min": float(a.min()), "arch_max": float(a.max()),
            "flash_min": float(f.min()), "flash_max": float(f.max()),
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("arch_initial", "flash_initial", "arch_final", "flash_final"):
        parser.add_argument(f"--{name.replace('_', '-')}", type=Path, required=True)
    parser.add_argument("--width", type=float, default=64.0)
    parser.add_argument("--height", type=float, default=32.0)
    parser.add_argument("--depth", type=float, default=16.0)
    parser.add_argument("--dimension", type=int, choices=(2, 3), default=2)
    parser.add_argument("--flash-post-regrid", type=Path,
                        help="optional FLASH final plot after its step-20 AMR update")
    parser.add_argument("--dx", type=float, default=0.5)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    shape = (round(args.height / args.dx), round(args.width / args.dx))
    domain = [args.width, args.height]
    if args.dimension == 3:
        shape = (round(args.depth / args.dx),) + shape
        domain.append(args.depth)
    report = {"domain_cm": domain, "comparison_dx_cm": args.dx,
              "initial": compare(args.arch_initial, args.flash_initial, shape, args.dx),
              "step_20": compare(args.arch_final, args.flash_final, shape, args.dx)}
    if args.flash_post_regrid:
        report["post_regrid"] = compare(args.arch_final, args.flash_post_regrid,
                                        shape, args.dx)
    serialized = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(serialized)
    else:
        print(serialized, end="")


if __name__ == "__main__":
    main()
