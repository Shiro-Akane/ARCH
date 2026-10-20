"""Read-only full Cartesian leaf-cell coverage diagnostics, with no numerical tolerance."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import h5py
import numpy as np

def exact(value):
    return Fraction(float(value))

def number(value):
    return {"exact": str(value), "float": float(value)}

def coverage(rectangles, domain, budget=2000000):
    """Coordinate compression integrates exact binary64 endpoints and multiplicity."""
    dimension = len(domain)
    if dimension not in (1, 2):
        raise ValueError("Only 1D/2D Cartesian coverage is supported")
    axes = [sorted({*domain[a], *(v for rect in rectangles for v in rect[a])})
            for a in range(dimension)]
    shape = tuple(len(axis) for axis in axes)
    if np.prod(shape) > budget:
        raise ValueError("Coordinate compression exceeds diagnostic memory budget")
    indices = [{v: i for i, v in enumerate(axis)} for axis in axes]
    delta = np.zeros(shape, dtype=np.int64)
    for rect in rectangles:
        if any(lo >= hi for lo, hi in rect):
            raise ValueError("Non-positive rectangle extent")
        a, b = [indices[0][v] for v in rect[0]]
        if dimension == 1:
            delta[a] += 1
            delta[b] -= 1
        else:
            c, d = [indices[1][v] for v in rect[1]]
            delta[a, c] += 1
            delta[b, c] -= 1
            delta[a, d] -= 1
            delta[b, d] += 1
    counts = delta
    for axis in range(dimension):
        counts = counts.cumsum(axis=axis)
    measures = {k: Fraction(0) for k in ("gap", "overlap_excess", "outside", "covered")}
    bins = {k: 0 for k in ("gap", "overlap", "outside")}
    examples = []
    max_count = int(counts.max())
    for index in np.ndindex(*(len(axis)-1 for axis in axes)):
        bounds = [(axes[a][i], axes[a][i+1]) for a, i in enumerate(index)]
        size = np.prod([hi-lo for lo, hi in bounds])
        count = int(counts[index])
        inside = all(lo >= domain[a][0] and hi <= domain[a][1]
                     for a, (lo, hi) in enumerate(bounds))
        kind = None
        if inside:
            if count == 0:
                measures["gap"] += size
                bins["gap"] += 1
                kind = "gap"
            else:
                measures["covered"] += size
                if count > 1:
                    measures["overlap_excess"] += size*(count-1)
                    bins["overlap"] += 1
                    kind = "overlap"
        elif count:
            measures["outside"] += size*count
            bins["outside"] += 1
            kind = "outside"
        if kind and len(examples) < 6:
            examples.append({"kind": kind, "multiplicity": count,
                             "bounds": [[str(lo), str(hi)] for lo, hi in bounds]})
    return {"exactlyOnceWithinDomain": all(measures[k] == 0 for k in
            ("gap", "overlap_excess", "outside")),
            "measures": {k: number(v) for k, v in measures.items()},
            "compressedBins": bins, "maxMultiplicity": max_count,
            "compressedGridShape": list(shape), "examples": examples}

def inspect(plot, domain_values, roots):
    before = hashlib.sha256(plot.read_bytes()).hexdigest()
    domain = [(exact(lo), exact(hi)) for lo, hi in domain_values]
    dimension = len(domain)
    if any(lo >= hi for lo, hi in domain):
        raise ValueError("External domain must have positive extent")
    if len(roots) != dimension or any(type(n) is not int or n < 1 for n in roots):
        raise ValueError("Provide an external active root-block count per axis")
    with h5py.File(plot, "r") as p:
        if int(p.attrs["dim"]) != dimension or str(p.attrs["geometry"]) not in ("cartesian", "b'cartesian'"):
            raise ValueError("Unsupported geometry or external dimension mismatch")
        data = p["Data/DENS"]
        if data.dtype.kind != "f" or data.dtype.itemsize != 8 or len(data.shape) != dimension+1:
            raise ValueError("Expected native FP64 DENS shape")
        blocks, cell_shape = data.shape[0], tuple(reversed(data.shape[1:]))
        per = int(np.prod(cell_shape))
        levels = p["Grid/level"][:]
        logical = [p[f"NativeGrid/logical_x{a+1}"][:] for a in range(dimension)]
        bounds = []
        for a in range(dimension):
            lo, hi = p[f"NativeGrid/x{a+1}_lower"], p[f"NativeGrid/x{a+1}_upper"]
            if any(ds.dtype.kind != "f" or ds.dtype.itemsize != 8 or ds.shape != (blocks*per,)
                   for ds in (lo, hi)):
                raise ValueError("Expected aligned FP64 native bounds")
            bounds.append((lo[:], hi[:]))
        rectangles, logical_rectangles, keys = [], [], []
        max_delta = Fraction(0)
        for block in range(blocks):
            level = int(levels[block])
            if not 0 <= level <= 30:
                raise ValueError("Level outside bounded diagnostic range")
            key = (level, *(int(axis[block]) for axis in logical))
            keys.append(key)
            for local in range(per):
                index = block*per+local
                stored, expected = [], []
                stride = 1
                for a in range(dimension):
                    i = (local//stride) % cell_shape[a]
                    stride *= cell_shape[a]
                    lo, hi = bounds[a][0][index], bounds[a][1][index]
                    if not np.isfinite(lo) or not np.isfinite(hi):
                        raise ValueError("Non-finite native endpoint")
                    stored.append((exact(lo), exact(hi)))
                    spacing = (domain[a][1]-domain[a][0])/(roots[a]*cell_shape[a]*2**level)
                    origin = int(logical[a][block])*cell_shape[a]+i
                    expected.append((domain[a][0]+origin*spacing, domain[a][0]+(origin+1)*spacing))
                    max_delta = max(max_delta, *(abs(x-y) for x, y in zip(stored[-1], expected[-1])))
                rectangles.append(stored)
                logical_rectangles.append(expected)
        if len(set(keys)) != blocks:
            raise ValueError("Duplicate logical leaf-block keys")
    if hashlib.sha256(plot.read_bytes()).hexdigest() != before:
        raise ValueError("Source changed during read-only diagnostic")
    return {"scope": "all stored Cartesian leaf cells; exact endpoint diagnostics, not scientific tolerance acceptance",
            "plotfileSha256": before, "plotfileBytes": plot.stat().st_size,
            "dimension": dimension, "shape": list((blocks, *reversed(cell_shape))),
            "leafBlocks": blocks, "cells": blocks*per, "activeRootBlocks": roots,
            "externalDomain": domain_values, "nativeStoredCoverage": coverage(rectangles, domain),
            "logicalDyadicCoverage": coverage(logical_rectangles, domain),
            "maxStoredEndpointDifferenceFromLogicalDomain": number(max_delta),
            "sourceUnchanged": True}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plot", required=True, type=Path)
    parser.add_argument("--domain", required=True, help="External JSON [[min,max], ...], interpreted as binary64")
    parser.add_argument("--roots", required=True, help="External active root-block count JSON")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = inspect(args.plot, json.loads(args.domain), json.loads(args.roots))
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps(result, indent=2))
    # Numerical differences remain findings, not silently accepted by a tolerance.
    raise SystemExit(0 if result["nativeStoredCoverage"]["exactlyOnceWithinDomain"]
                     and result["logicalDyadicCoverage"]["exactlyOnceWithinDomain"] else 1)
