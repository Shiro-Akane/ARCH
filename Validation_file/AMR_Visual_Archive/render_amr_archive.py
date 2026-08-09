#!/usr/bin/env python3
# =============================================================================
# File: render_amr_archive.py
# Brief: Reproducibly render ARCH AMR mesh and flow diagnostics from plot HDF5.
# Workflow:
# 1. Read archived leaf-block fields and physical cell centres from plot HDF5.
# 2. Reconstruct Cartesian or polar cell interfaces for each AMR leaf patch.
# 3. Render fields and patch boundaries without changing any solver output.
# =============================================================================

"""Render AMR fields as physical cells instead of centre-point diagnostics."""

from __future__ import annotations

import argparse
from pathlib import Path

import h5py
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.cm import ScalarMappable
from matplotlib.colors import LogNorm, Normalize


CASES = {
    "sedov": ("output/diag_sedov_dynamic_ppm_cfl03/SedovDynamicPPMCFL03_HLL_plt_0004.h5", False),
    "gaussian": ("output/diag_gaussian_amr_rkl_sts/GaussianAMRRKLSTSShort_HLL_plt_0002.h5", False),
    "rt": ("output/diag_rt_amr_gravity_diffusion/RTAMRGravityDiffusion_HLL_plt_0001.h5", False),
    "cellular": ("output/diag_cellular_amr_burn/CellularAMRBurn_HLL_plt_0005.h5", True),
}


def load_plot(path: Path) -> dict:
    """Load block-local fields and physical cell centres from a plot HDF5."""
    with h5py.File(path, "r") as file:
        rho = np.asarray(file["Data/rho"])
        shape = rho.shape
        result = {
            "dim": int(file.attrs["dim"]),
            "time": float(file.attrs["time"]),
            "geometry": str(file.attrs["geometry"]),
            "rho": rho,
            "u": np.asarray(file["Data/u"]),
            "v": np.asarray(file["Data/v"]) if "Data/v" in file else np.zeros_like(rho),
            "w": np.asarray(file["Data/w"]) if "Data/w" in file else np.zeros_like(rho),
            "x": np.asarray(file["Grid/x"]).reshape(shape),
            "y": np.asarray(file["Grid/y"]).reshape(shape),
            "level": np.asarray(file["Grid/level"]),
        }
        species = [key for key in file["Data"] if key.startswith("X_")]
        if species:
            scalar = {key: np.asarray(file["Data"][key]) for key in species}
            name = max(species, key=lambda key: np.ptp(scalar[key]))
            result["scalar_name"] = name
            result["scalar"] = scalar[name]
        else:
            result["scalar_name"] = None
            result["scalar"] = None
    return result


def cell_edges(centres: np.ndarray) -> np.ndarray:
    """Recover cell interfaces from ordered cell centres on one logical axis."""
    centres = np.asarray(centres, dtype=float)
    if centres.size == 1:
        return np.array([centres[0] - 0.5, centres[0] + 0.5])
    edges = np.empty(centres.size + 1, dtype=float)
    edges[1:-1] = 0.5 * (centres[:-1] + centres[1:])
    edges[0] = centres[0] - 0.5 * (centres[1] - centres[0])
    edges[-1] = centres[-1] + 0.5 * (centres[-1] - centres[-2])
    return edges


def cartesian_patch(data: dict, block: int) -> tuple[np.ndarray, np.ndarray]:
    """Return Cartesian x/y interfaces for a logically rectangular AMR block."""
    x = np.nanmedian(data["x"][block], axis=0)
    y = np.nanmedian(data["y"][block], axis=1)
    return cell_edges(x), cell_edges(y)


def polar_patch(data: dict, block: int) -> tuple[np.ndarray, np.ndarray]:
    """Return radial and angular interfaces for a spherical 2D AMR block."""
    x, y = data["x"][block], data["y"][block]
    radius = np.hypot(x, y)
    angle = np.unwrap(np.arctan2(y, x), axis=0)
    radial_centres = np.nanmedian(radius, axis=0)
    angular_centres = np.nanmedian(angle, axis=1)
    return np.maximum(0.0, cell_edges(radial_centres)), cell_edges(angular_centres)


def patch_vertices(data: dict, block: int) -> tuple[np.ndarray, np.ndarray]:
    """Return physical interfaces appropriate for the plot geometry."""
    if data["geometry"] == "spherical":
        radius, angle = polar_patch(data, block)
        radii, angles = np.meshgrid(radius, angle)
        return radii * np.cos(angles), radii * np.sin(angles)
    x, y = cartesian_patch(data, block)
    return np.meshgrid(x, y)


def limits(data: dict) -> tuple[tuple[float, float], tuple[float, float]]:
    """Compute compact common axes from physical AMR cell interfaces."""
    xmin, xmax, ymin, ymax = np.inf, -np.inf, np.inf, -np.inf
    for block in range(len(data["level"])):
        x, y = patch_vertices(data, block)
        xmin, xmax = min(xmin, float(x.min())), max(xmax, float(x.max()))
        ymin, ymax = min(ymin, float(y.min())), max(ymax, float(y.max()))
    span = max(xmax - xmin, ymax - ymin, 1.0e-12)
    pad = 0.02 * span
    return (xmin - pad, xmax + pad), (ymin - pad, ymax + pad)


def field_norm(values: np.ndarray, logarithmic: bool = False):
    """Build a stable shared normalisation for all AMR patches in one field."""
    finite = values[np.isfinite(values)]
    if logarithmic:
        finite = finite[finite > 0.0]
        if finite.size:
            lower, upper = float(finite.min()), float(finite.max())
            if upper > lower:
                return LogNorm(vmin=lower, vmax=upper)
            return LogNorm(vmin=lower / 1.01, vmax=lower * 1.01)
    if finite.size:
        lower, upper = float(finite.min()), float(finite.max())
        if upper > lower:
            return Normalize(vmin=lower, vmax=upper)
        scale = max(abs(lower), 1.0)
        return Normalize(vmin=lower - 0.01 * scale, vmax=upper + 0.01 * scale)
    return Normalize(vmin=0.0, vmax=1.0)


def polar_boundary(axis, radius: np.ndarray, angle: np.ndarray, **style) -> None:
    """Draw only one spherical AMR patch boundary, never a false Cartesian box."""
    curve = np.linspace(angle[0], angle[-1], 65)
    for value in (radius[0], radius[-1]):
        axis.plot(value * np.cos(curve), value * np.sin(curve), **style)
    for value in (angle[0], angle[-1]):
        axis.plot(radius * np.cos(value), radius * np.sin(value), **style)


def mesh_overlay(axis, data: dict, colored: bool = False) -> None:
    """Overlay AMR leaf-patch boundaries without drawing artificial cell grids."""
    maximum = max(1, int(data["level"].max()))
    for block, level in enumerate(data["level"]):
        color = plt.get_cmap("viridis")(level / maximum) if colored else "black"
        style = {"color": color, "linewidth": 0.6 if colored else 0.25, "alpha": 0.9}
        if data["geometry"] == "spherical":
            radius, angle = polar_patch(data, block)
            polar_boundary(axis, radius, angle, **style)
        else:
            x, y = cartesian_patch(data, block)
            axis.plot([x[0], x[-1], x[-1], x[0], x[0]],
                      [y[0], y[0], y[-1], y[-1], y[0]], **style)


def set_physical_axes(axis, data: dict) -> None:
    """Apply the same compact physical extent to every 2D diagnostic panel."""
    x_limits, y_limits = limits(data)
    axis.set_xlim(*x_limits)
    axis.set_ylim(*y_limits)
    axis.set_aspect("equal", adjustable="box")
    axis.set_xlabel("x")
    axis.set_ylabel("y")


def draw_field(axis, data: dict, values: np.ndarray, title: str, cmap: str,
               logarithmic: bool = False) -> None:
    """Render each leaf patch with physical quadrilateral cell faces."""
    norm = field_norm(values, logarithmic)
    for block in range(len(data["level"])):
        x, y = patch_vertices(data, block)
        axis.pcolormesh(x, y, values[block], shading="flat", cmap=cmap, norm=norm,
                        edgecolors="none", antialiased=False, rasterized=True)
    mesh_overlay(axis, data)
    set_physical_axes(axis, data)
    axis.set_title(title)
    mapper = ScalarMappable(norm=norm, cmap=cmap)
    mapper.set_array([])
    plt.colorbar(mapper, ax=axis, pad=0.02)


def render_2d(data: dict, name: str, log_density: bool, output: Path) -> None:
    """Create physical mesh, density, velocity, and composition AMR panels."""
    speed = np.sqrt(data["u"] ** 2 + data["v"] ** 2 + data["w"] ** 2)
    figure, axes = plt.subplots(2, 2, figsize=(14, 11), constrained_layout=True)
    mesh_overlay(axes[0, 0], data, colored=True)
    set_physical_axes(axes[0, 0], data)
    axes[0, 0].set_title(f"{name}: AMR leaf patches, max L{data['level'].max()}")
    draw_field(axes[0, 1], data, data["rho"], "DENS (viridis)", "viridis", log_density)
    draw_field(axes[1, 0], data, speed, "speed (magma)", "magma")
    if data["scalar"] is None:
        axes[1, 1].axis("off")
    else:
        draw_field(axes[1, 1], data, data["scalar"], data["scalar_name"], "cividis")
    figure.suptitle(f"ARCH AMR: {name}; t={data['time']:.6e}; {data['geometry']}")
    figure.savefig(output, dpi=220)
    plt.close(figure)


def one_dimensional_extent(data: dict) -> tuple[float, float]:
    """Return physical 1D cell-interface limits across all leaf patches."""
    low, high = np.inf, -np.inf
    for block in range(len(data["level"])):
        edges = cell_edges(data["x"][block].ravel())
        low, high = min(low, float(edges[0])), max(high, float(edges[-1]))
    return low, high


def draw_profile(axis, x: np.ndarray, values: np.ndarray, title: str, ylabel: str,
                 cmap: str, logarithmic: bool = False) -> None:
    """Draw a colour-mapped 1D AMR cell-centre diagnostic with a colourbar."""
    norm = field_norm(values, logarithmic)
    image = axis.scatter(x, values, c=values, cmap=cmap, norm=norm, s=11,
                         linewidths=0.0, rasterized=True)
    if logarithmic:
        axis.set_yscale("log")
    axis.set_title(title)
    axis.set_ylabel(ylabel)
    plt.colorbar(image, ax=axis, pad=0.015)


def render_1d(data: dict, name: str, log_density: bool, output: Path) -> None:
    """Create 1D AMR levels plus colour-mapped density and velocity profiles."""
    x = data["x"].ravel()
    rho = data["rho"].ravel()
    speed = np.sqrt(data["u"].ravel() ** 2 + data["v"].ravel() ** 2 + data["w"].ravel() ** 2)
    x_limits = one_dimensional_extent(data)
    figure, axes = plt.subplots(3, 1, figsize=(13, 9), constrained_layout=True)
    maximum = max(1, int(data["level"].max()))
    for block, level in enumerate(data["level"]):
        edges = cell_edges(data["x"][block].ravel())
        color = plt.get_cmap("viridis")(level / maximum)
        axes[0].plot([edges[0], edges[-1]], [level, level], linewidth=5,
                     color=color, solid_capstyle="butt")
        axes[0].vlines([edges[0], edges[-1]], level - 0.3, level + 0.3,
                       color="black", linewidth=0.4)
    axes[0].set_title(f"{name}: AMR leaf patches")
    axes[0].set_ylabel("level")
    axes[0].set_ylim(-0.5, maximum + 0.5)
    draw_profile(axes[1], x, rho, "DENS (viridis)", "DENS", "viridis", log_density)
    draw_profile(axes[2], x, speed, "speed (magma)", "speed", "magma")
    for axis in axes:
        axis.set_xlim(*x_limits)
    axes[2].set_xlabel("x")
    figure.suptitle(f"ARCH AMR: {name}; t={data['time']:.6e}; {data['geometry']}")
    figure.savefig(output, dpi=220)
    plt.close(figure)


def main() -> None:
    """Render the four documented AMR visual-regression cases."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="ARCH repository root")
    parser.add_argument("--output", default="Validation_file/AMR_Visual_Archive", help="archive directory")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    archive = (root / args.output).resolve()
    archive.mkdir(parents=True, exist_ok=True)
    lines = [
        "# AMR visual validation archive",
        "",
        "This archive contains the reproducible renderer and AMR field figures generated",
        "from stored HDF5 plot output. Rendering never changes solver state or HDF5 data.",
        "Cartesian fields use physical cell quadrilaterals; spherical fields use polar",
        "cell quadrilaterals. The mesh overlay contains leaf-patch boundaries only.",
        "",
        "## Regenerate",
        "",
        "From the ARCH repository root:",
        "",
        "    work",
        "    python Validation_file/AMR_Visual_Archive/render_amr_archive.py",
        "",
        "## Inputs and figures",
        "",
        "| Case | Figure | Plot input |",
        "| --- | --- | --- |",
    ]
    for name, (relative, log_density) in CASES.items():
        data = load_plot(root / relative)
        image = archive / f"{name}_amr.png"
        if data["dim"] == 1:
            render_1d(data, name, log_density, image)
        else:
            render_2d(data, name, log_density, image)
        lines.append(f"| {name} | [{image.name}]({image.name}) | {relative} |")
        print(f"wrote {image}")
    (archive / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
