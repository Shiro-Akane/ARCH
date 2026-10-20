#!/usr/bin/env python3
"""Export one native-cell handoff; never rewrite HDF or export whole fields.

This is storage/mapping evidence, not independent scientific acceptance,
publication-process proof, producer freshness or global AMR coverage.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import h5py
import numpy as np


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            h.update(chunk)
    return h.hexdigest()


def local(handle, path):
    prefix = ""
    for part in path.strip("/").split("/"):
        prefix += "/" + part
        if not isinstance(handle.get(prefix, getlink=True), h5py.HardLink):
            raise ValueError("nonlocal or missing object: " + path)
    obj = handle[path]
    if isinstance(obj, h5py.Dataset) and (obj.is_virtual or obj.external):
        raise ValueError("external dataset: " + path)
    return obj


def scalar(value):
    if isinstance(value, bytes):
        return value.decode("utf-8")
    if isinstance(value, np.generic):
        return value.item()
    if not isinstance(value, (str, int, float, bool)):
        raise ValueError("nonscalar handoff attribute")
    return value


def attributes(obj):
    return {key: scalar(value) for key, value in obj.attrs.items()}


def fp64(handle, path, shape):
    obj = local(handle, path)
    if not isinstance(obj, h5py.Dataset) or obj.shape != shape:
        raise ValueError("shape mismatch: " + path)
    if obj.dtype.kind != "f" or obj.dtype.itemsize != 8:
        raise ValueError("FP64 required: " + path)
    return obj


def inspect(path, index):
    path = Path(path)
    if ".partial" in path.name:
        raise ValueError("temporary publication path refused")
    before = digest(path)
    with h5py.File(path, "r") as h:
        root = attributes(h)
        dim = root.get("dim")
        if type(dim) is not int or dim not in (1, 2) or root.get("geometry") != "cartesian":
            raise ValueError("only Cartesian 1D/2D supported")
        if root.get("plot_publication_state") != "complete":
            raise ValueError("complete publication declaration required")
        grid = local(h, "/NativeGrid")
        if grid.attrs.get("block_kind") != "active-leaf" or grid.attrs.get("ghost_cells") != 0:
            raise ValueError("active leaf/no ghost declaration required")
        data = local(h, "/Data")
        names = sorted(data.keys())
        if not names:
            raise ValueError("no fields")
        shape = local(h, "/Data/" + names[0]).shape
        if len(shape) != dim + 1 or any(n <= 0 for n in shape):
            raise ValueError("invalid field shape")
        if len(index) != len(shape) or any(type(i) is not int or not 0 <= i < n for i, n in zip(index, shape)):
            raise ValueError("invalid native index")
        flat = int(np.ravel_multi_index(tuple(index), shape))
        count = math.prod(shape)
        fields = {}
        for name in names:
            obj = fp64(h, "/Data/" + name, shape)
            raw = np.float64(obj[tuple(index)])
            if not np.isfinite(raw):
                raise ValueError("nonfinite field sample")
            fields[name] = {
                "path": obj.name, "shape": list(shape), "dtype": str(obj.dtype),
                "attributes": attributes(obj), "value": float(raw),
                "fp64Bits": format(int(raw.view(np.uint64)), "016x"),
            }
        bounds = []
        for axis in range(1, dim + 1):
            bounds.append([float(fp64(h, "/NativeGrid/x%d_%s" % (axis, edge), (count,))[flat])
                           for edge in ("lower", "upper")])
        measure = float(fp64(h, "/NativeGrid/cell_measure", (count,))[flat])
        centers = [float(fp64(h, "/Grid/" + axis, (count,))[flat]) for axis in "xyz"]
        if not all(math.isfinite(v) for v in [measure, *centers, *sum(bounds, [])]):
            raise ValueError("nonfinite native geometry")
        identifiers = {}
        for name in ("level", "morton"):
            obj = local(h, "/Grid/" + name)
            if obj.shape != (shape[0],) or obj.dtype.kind not in "iu":
                raise ValueError("invalid block identifier")
            identifiers[name] = int(obj[index[0]])
        result = {
            "scope": "one stored cell, all exported fields; not scientific acceptance",
            "fileSha256": before, "rootAttributes": root,
            "sourceIdentityAttributes": attributes(local(h, "/SourceIdentity")),
            "nativeGridAttributes": attributes(grid), "shape": list(shape),
            "index": index, "flatIndex": flat, "fields": fields,
            "bounds": bounds, "cellMeasure": measure, "centers": centers,
            "block": identifiers, "arrayOrder": "stored block order, x1-fastest",
            "limitations": [
                "complete marker is not proof of successful close/rename",
                "identity declarations do not establish independent producer freshness",
                "one cell does not establish global AMR coverage or scientific accuracy",
                "whole-file SHA scans the file; payload count is not physical HDF I/O",
            ],
        }
    after = digest(path)
    if before != after:
        raise ValueError("file changed during inspection")
    result["fileUnchanged"] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", required=True)
    parser.add_argument("--cell", required=True, help="block,i or block,j,i")
    args = parser.parse_args()
    try:
        result = inspect(args.file, [int(i) for i in args.cell.split(",")])
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(json.dumps({"status": "invalid", "error": str(exc)}))
        return 1
    print(json.dumps(result, ensure_ascii=False, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
