"""Independently compare fixed-grid ARCH/FLASH standing Jeans outputs.

Run manually after both codes reach the same CGS physical time. Each active
cell must cover exactly one pixel; no interpolation hides a grid mismatch.
The linear amplitude is a small-perturbation reference, not a claim that the
two solvers use identical timestepping or gravity coupling.
"""

import argparse
import json
import math

import h5py
import numpy as np


FIELDS = (("density", "DENS", "dens"),
          ("pressure", "PRES", "pres"),
          ("velocity_x", "VELX", "velx"))


def arch_fields(path, n, dx):
    """Map every ARCH interior cell centre onto the unrefined common grid."""
    result = {name: np.full((n, n), np.nan) for name, _, _ in FIELDS}
    cover = np.zeros((n, n), dtype=np.uint8)
    with h5py.File(path) as handle:
        time = float(handle.attrs["time"])
        count = len(handle["Grid/level"])
        if np.any(handle["Grid/level"][()] != 0):
            raise ValueError("ARCH Jeans comparison requires a fixed grid")
        x = handle["Grid/x"][()].reshape(count, 16, 16)
        y = handle["Grid/y"][()].reshape(count, 16, 16)
        cols = np.rint(x / dx - 0.5).astype(int)
        rows = np.rint(y / dx - 0.5).astype(int)
        if np.any(cols < 0) or np.any(cols >= n) or np.any(rows < 0) or np.any(rows >= n):
            raise ValueError("ARCH cell centre is outside the common grid")
        np.add.at(cover, (rows.ravel(), cols.ravel()), 1)
        for name, arch_key, _ in FIELDS:
            result[name][rows, cols] = handle[f"Data/{arch_key}"][()].reshape(count, 16, 16)
    if np.any(cover != 1):
        raise ValueError("ARCH cell coverage is not exactly one")
    return result, time


def flash_fields(path, n, dx):
    """Map FLASH leaf blocks to the same grid using physical bounds."""
    result = {name: np.full((n, n), np.nan) for name, _, _ in FIELDS}
    cover = np.zeros((n, n), dtype=np.uint8)
    with h5py.File(path) as handle:
        scalar_time = [float(row["value"]) for row in handle["real scalars"][()]
                       if row["name"].decode().strip() == "time"]
        if len(scalar_time) != 1:
            raise ValueError("FLASH output lacks a unique physical time")
        active = np.flatnonzero(handle["node type"][()] == 1)
        boxes = handle["bounding box"][()]
        for block in active:
            bounds = boxes[block]
            width = (bounds[:, 1] - bounds[:, 0])[:2] / 8
            if not np.allclose(width, dx, rtol=2e-5, atol=0):
                raise ValueError("FLASH cell width differs from the common grid")
            x0 = round(float(bounds[0, 0]) / dx)
            y0 = round(float(bounds[1, 0]) / dx)
            if x0 < 0 or y0 < 0 or x0 + 8 > n or y0 + 8 > n:
                raise ValueError("FLASH block is outside the common grid")
            cover[y0:y0 + 8, x0:x0 + 8] += 1
            for name, _, flash_key in FIELDS:
                result[name][y0:y0 + 8, x0:x0 + 8] = handle[flash_key][block, 0]
    if np.any(cover != 1):
        raise ValueError("FLASH cell coverage is not exactly one")
    return result, scalar_time[0]


def compare(arch_path, flash_path, n, rho0, pressure0, amplitude, length, gravity_g, gamma, mode):
    """Return full-field differences and an independent linear-mode reference."""
    dx = length / n
    arch, arch_time = arch_fields(arch_path, n, dx)
    flash, flash_time = flash_fields(flash_path, n, dx)
    if not math.isclose(arch_time, flash_time, rel_tol=0, abs_tol=1e-12):
        raise ValueError(f"Different physical times: {arch_time}, {flash_time}")
    k = 2 * math.pi * mode / length
    omega2 = gamma * pressure0 / rho0 * k * k - 4 * math.pi * gravity_g * rho0
    if omega2 <= 0:
        raise ValueError("The linear Jeans reference is not oscillatory")
    x = (np.arange(n) + 0.5) * dx
    basis = np.broadcast_to(np.cos(k * x), (n, n))
    expected = rho0 * amplitude * math.cos(math.sqrt(omega2) * arch_time)
    result = {"n": n, "arch_time": arch_time, "flash_time": flash_time,
              "analytic_density_mode": expected, "omega": math.sqrt(omega2)}
    for label, data in (("arch", arch), ("flash", flash)):
        mode_value = 2 * np.mean((data["density"] - rho0) * basis)
        result[f"{label}_density_mode"] = float(mode_value)
        result[f"{label}_density_mode_relative_error"] = float((mode_value - expected) / expected)
    for name, _, _ in FIELDS:
        a, f = arch[name], flash[name]
        scale = {"density": rho0, "pressure": pressure0,
                 "velocity_x": max(float(np.max(np.abs(f))), 1e-300)}[name]
        result[f"{name}_relative_l1"] = float(np.mean(np.abs(a - f)) / scale)
        result[f"{name}_max_absolute_difference"] = float(np.max(np.abs(a - f)))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", required=True)
    parser.add_argument("--flash", required=True)
    parser.add_argument("--n", type=int, required=True)
    args = parser.parse_args()
    print(json.dumps(compare(args.arch, args.flash, args.n, 1.5e7, 1.5e7,
                             0.001, 1.14411, 6.67408e-8, 5 / 3, 2), indent=2))
