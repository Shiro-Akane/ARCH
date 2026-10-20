#!/usr/bin/env python3
"""Probe a candidate Plotfile without fixing the production HDF5 layout.

Workflow: resolve an explicit layout, read the bounded JSON header, check field
shapes and declarations, then read one native cell by hyperslab. Old files get
a structural ``legacy`` result. ``candidate`` denotes local consistency only:
it cannot certify publication, provenance, full AMR coverage or physical accuracy.
"""

import argparse
import json
import math
from pathlib import Path
import sys

import h5py
import numpy as np


DEFAULT_LAYOUT = {
    "header": {"object": "/", "attribute": "plotfile_candidate"},
    "data": "/Data", "bounds": "/Native/bounds", "measure": "/Native/measure",
    "coordinates": ["/Grid/x", "/Grid/y", "/Grid/z"],
    "level": "/Grid/level", "morton": "/Grid/morton",
}
IDENTITIES = ("case", "config", "build", "binary", "eos")
SCALAR_UNITS = {
    "DENS": "g/cm^3", "PRES": "erg/cm^3", "ENER": "erg/cm^3", "TEMP": "K",
    "ENUC": "erg/g/s", "GPOT": "cm^2/s^2", "DIVV": "1/s", "VORT": "1/s",
}
VECTOR_UNITS = {**dict.fromkeys(("VELX", "VELY", "VELZ"), "cm/s"),
                **dict.fromkeys(("GACX", "GACY", "GACZ"), "cm/s^2")}
MEASURES = {1: ("cm", "per_unit_transverse_area"),
            2: ("cm^2", "per_unit_transverse_length")}
LIMITATIONS = [
    "One cell only; no full-field, overlap, missing-leaf or AMR completeness proof.",
    "Self-declared identities do not prove producer freshness or data provenance.",
    "Publication marker checked; atomic rename, concurrent writes and I/O faults unverified.",
    "Logical payload bytes are not physical I/O, decompressed chunk bytes or peak memory.",
    "Header size checks do not bound HDF5 allocation for attributes or variable-length strings.",
]


class ContractError(ValueError):
    """Carry a stable diagnostic without exposing a host exception or path."""

    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def require(condition, code, message):
    """Reject a failed contract condition; never substitute a default value."""
    if not condition:
        raise ContractError(code, message)


def strict_json(text):
    """Reject duplicate keys and the non-JSON NaN/Infinity extensions."""
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "json", "Duplicate JSON key.")
            result[key] = value
        return result

    def constant(_):
        raise ContractError("json", "Nonfinite JSON constant.")

    def finite_float(value):
        number = float(value)
        require(math.isfinite(number), "json", "JSON number exceeds finite floating-point range.")
        return number

    return json.loads(text, object_pairs_hook=pairs, parse_constant=constant,
                      parse_float=finite_float)


def text_value(raw, limit):
    """Decode one scalar UTF-8 header and check its encoded byte length."""
    require(np.ndim(raw) == 0, "header", "Header must be one text scalar.")
    raw = raw.item() if isinstance(raw, np.ndarray) else raw
    if isinstance(raw, bytes):
        require(len(raw) <= limit, "header_size", "Header exceeds metadata budget.")
        raw = raw.decode("utf-8")
    require(isinstance(raw, str), "header", "Header must be UTF-8 text.")
    require(len(raw.encode("utf-8")) <= limit, "header_size", "Header exceeds metadata budget.")
    return raw


def resolve(handle, path, optional=False):
    """Resolve a local HDF path while refusing soft/external link traversal."""
    require(isinstance(path, str) and path.startswith("/"), "layout", "Use absolute HDF paths.")
    prefix = ""
    for part in filter(None, path.split("/")):
        prefix += "/" + part
        link = handle.get(prefix, getlink=True)
        if link is None:
            require(optional, "missing", "Mapped HDF object is missing.")
            return None
        require(isinstance(link, h5py.HardLink), "link", "Soft/external HDF links are unsupported.")
    return handle[path]


def dataset(handle, path, integral=False):
    """Check storage precision without loading a scientific field array."""
    obj = resolve(handle, path)
    require(isinstance(obj, h5py.Dataset), "dataset", "Mapped object must be a dataset.")
    require(not obj.is_virtual and not obj.external, "link", "External/virtual datasets are unsupported.")
    kind = obj.dtype.kind
    require(kind in "iu" if integral else kind == "f" and obj.dtype.itemsize == 8,
            "dtype", "Require integral block identifiers and FP64 scientific data.")
    return obj


def read_header(handle, selector, limit):
    """Read a JSON attribute or a scalar JSON dataset selected by the adapter."""
    if isinstance(selector, dict):
        require(set(selector) == {"object", "attribute"}, "layout", "Invalid header selector.")
        require(isinstance(selector["attribute"], str) and selector["attribute"].strip(),
                "layout", "Header attribute must have a name.")
        obj = resolve(handle, selector["object"], optional=True)
        if obj is None or selector["attribute"] not in obj.attrs:
            return None
        raw = obj.attrs[selector["attribute"]]
    else:
        obj = resolve(handle, selector, optional=True)
        if obj is None:
            return None
        require(isinstance(obj, h5py.Dataset) and obj.shape == (), "header", "Header dataset must be scalar.")
        require(not obj.is_virtual and not obj.external, "link", "External header storage is unsupported.")
        require(obj.nbytes <= limit, "header_size", "Header exceeds metadata budget.")
        raw = obj[()]
    return strict_json(text_value(raw, limit))


def nonempty(value):
    """Identify a meaningful declared string, without interpreting its contents."""
    return isinstance(value, str) and bool(value.strip())


def validate_header(header, expected):
    """Validate the Cartesian test slice and match supplied trusted identities."""
    require(isinstance(header, dict), "header", "Header must be an object.")
    require(type(header.get("schema_version")) is int and header["schema_version"] == 1,
            "version", "Unsupported candidate adapter version.")
    require(header.get("publication") == "complete", "publication", "File is not marked complete.")
    dim = header.get("dimension")
    require(type(dim) is int and dim in MEASURES and header.get("geometry") == "cartesian",
            "geometry", "Candidate probe supports Cartesian 1D/2D only.")
    require(header.get("axes") == ["x", "y"][:dim]
            and header.get("storage_order") == (["block", "i"] if dim == 1 else ["block", "j", "i"]),
            "order", "Native axis and stored index order disagree.")
    require(header.get("active_leaf_only") is True, "leaves", "Declare active-leaf-only output.")
    time = header.get("time")
    require(type(time) in (int, float) and math.isfinite(time) and time >= 0,
            "time", "Time must be finite and nonnegative.")
    measure = header.get("cell_measure")
    require(isinstance(measure, dict)
            and (measure.get("unit"), measure.get("normalization")) == MEASURES[dim],
            "measure_unit", "Cell measure unit/normalization must match dimension.")
    identities = header.get("identities")
    require(isinstance(identities, dict), "identity", "Declare all producer identities.")
    for key in IDENTITIES:
        item = identities.get(key)
        require(isinstance(item, dict), "identity", "Producer identity is missing.")
        state = item.get("state")
        require((state == "known" and nonempty(item.get("value")))
                or (state == "unknown" and nonempty(item.get("reason"))),
                "identity", "Identity needs a known value or an explicit unknown reason.")
    require(isinstance(expected, dict) and all(k in IDENTITIES and nonempty(v) for k, v in expected.items()),
            "identity", "Invalid trusted identity expectations.")
    for key, value in expected.items():
        require(identities[key].get("state") == "known" and identities[key].get("value") == value,
                "identity_mismatch", "Producer identity does not match trusted expectation.")
    fields = header.get("fields")
    require(isinstance(fields, dict) and fields, "fields", "Declare exported fields.")
    for name, item in fields.items():
        require(nonempty(name) and "/" not in name and isinstance(item, dict), "fields", "Invalid field declaration.")
        require(nonempty(item.get("unit")) and nonempty(item.get("meaning"))
                and item.get("centering") == "cell" and item.get("basis") in ("scalar", "cartesian"),
                "field_semantics", "Declare field unit, meaning, cell centering and basis.")
        units = VECTOR_UNITS if name in VECTOR_UNITS else SCALAR_UNITS
        if name in units:
            require(item["unit"] == units[name]
                    and item["basis"] == ("cartesian" if name in VECTOR_UNITS else "scalar"),
                    "field_semantics", "Built-in field unit or basis is inconsistent.")
        if name == "ENTR":
            require(item["meaning"] == "pressure_density_proxy" and item["unit"].lower() != "erg/g/k",
                    "entropy", "ENTR is P/rho^Gamma1, not thermodynamic specific entropy.")
    return dim


def structures(handle, layout):
    """Check common field/block/flattened-center shapes, including legacy 3D."""
    group = resolve(handle, layout["data"])
    require(isinstance(group, h5py.Group) and len(group), "fields", "Data must be a nonempty field group.")
    fields = {name: dataset(handle, group.name.rstrip("/") + "/" + name) for name in group}
    shape = next(iter(fields.values())).shape
    require(len(shape) in (2, 3, 4) and all(n > 0 for n in shape)
            and all(obj.shape == shape for obj in fields.values()), "shape", "Field shapes disagree.")
    coords = [dataset(handle, path) for path in layout["coordinates"]]
    require(len(coords) == 3 and all(obj.shape == (math.prod(shape),) for obj in coords),
            "shape", "Cartesian center arrays must be flattened in field order.")
    blocks = [dataset(handle, layout[key], integral=True) for key in ("level", "morton")]
    require(all(obj.shape == (shape[0],) for obj in blocks), "shape", "Block identifier shapes disagree.")
    return fields, shape, coords, blocks


def root_consistency(handle, dim, geometry, time):
    """Reject conflicting original attributes rather than choosing one copy."""
    for key, want in (("dim", dim), ("geometry", geometry), ("time", time)):
        if key in handle.attrs:
            value = handle.attrs[key]
            value = value.decode() if isinstance(value, bytes) else value
            if key in ("dim", "time"):
                require(not isinstance(value, (bool, np.bool_)), "metadata_conflict", "Numeric metadata cannot be boolean.")
            if key == "dim":
                require(isinstance(value, (int, np.integer)), "metadata_conflict", "Dimension must be an integer.")
            require(np.ndim(value) == 0 and value == want, "metadata_conflict", "Header and original attributes disagree.")


def probe_plotfile(path, layout=None, *, cell=None, field=None,
                   expected_identities=None, max_metadata_bytes=65536):
    """Inspect one stored (block,i)/(block,j,i) cell, returning a scoped result."""
    result = {"status": "invalid", "scope": "local reader consistency, not scientific acceptance",
              "errors": [], "limitations": list(LIMITATIONS)}
    try:
        require(type(max_metadata_bytes) is int and max_metadata_bytes > 0, "budget", "Invalid metadata budget.")
        mapping = DEFAULT_LAYOUT if layout is None else layout
        require(isinstance(mapping, dict) and set(mapping) == set(DEFAULT_LAYOUT), "layout", "Supply a complete layout mapping.")
        require(isinstance(mapping["coordinates"], list) and len(mapping["coordinates"]) == 3,
                "layout", "Map three Cartesian center datasets.")
        # Validate every supplied selector, even if a malformed path is not read.
        paths = [mapping[k] for k in ("data", "bounds", "measure", "level", "morton")] + mapping["coordinates"]
        require(all(isinstance(p, str) and p.startswith("/") for p in paths), "layout", "Use absolute HDF paths.")
        with h5py.File(path, "r") as handle:
            header = read_header(handle, mapping["header"], max_metadata_bytes)
            fields, shape, coords, blocks = structures(handle, mapping)
            result.update(shape=list(shape), fields={n: {"shape": list(o.shape), "dtype": str(o.dtype)} for n, o in fields.items()})
            if header is None:
                require(layout is None and expected_identities is None, "header", "Candidate header required by caller is missing.")
                dim = len(shape) - 1
                require(all(k in handle.attrs for k in ("dim", "geometry", "time")), "legacy", "Legacy root metadata is missing.")
                time = float(handle.attrs["time"])
                require(math.isfinite(time) and time >= 0, "time", "Legacy time is invalid.")
                geometry = handle.attrs["geometry"]
                geometry = geometry.decode() if isinstance(geometry, bytes) else str(geometry)
                require(geometry in ("cartesian", "cylindrical", "spherical"), "geometry", "Legacy geometry is invalid.")
                root_consistency(handle, dim, geometry, time)
                result.update(status="legacy", dim=dim, geometry=geometry, time=time,
                              missing_semantics=["publication", "identities", "units", "basis", "native_bounds", "measure"],
                              limitations=["Structure only; scientific values and missing semantics are unverified.",
                                           "No publication, provenance or full AMR completeness acceptance."])
                return result
            dim = validate_header(header, {} if expected_identities is None else expected_identities)
            require(len(shape) == dim + 1 and set(fields) == set(header["fields"]), "shape", "Declared fields or dimension disagree.")
            root_consistency(handle, dim, header["geometry"], header["time"])
            bounds, measure = dataset(handle, mapping["bounds"]), dataset(handle, mapping["measure"])
            require(bounds.shape == shape + (dim, 2) and measure.shape == shape, "shape", "Native bounds/measure shapes disagree.")
            index = (0,) * len(shape) if cell is None else tuple(cell)
            require(len(index) == len(shape) and all(type(i) is int and 0 <= i < n for i, n in zip(index, shape)),
                    "index", "Index must contain a valid block and all stored spatial indexes.")
            selected = next(iter(header["fields"])) if field is None else field
            require(isinstance(selected, str) and selected in fields, "field", "Requested field is not exported.")
            value, volume = float(fields[selected][index]), float(measure[index])
            edges = np.asarray(bounds[index])
            flat = int(np.ravel_multi_index(index, shape))
            centers = [float(obj[flat]) for obj in coords]
            identifiers = [int(obj[index[0]]) for obj in blocks]
            require(all(math.isfinite(v) for v in [value, volume, *centers, *edges.flat]), "nonfinite", "Sample contains a nonfinite value.")
            widths = edges[:, 1] - edges[:, 0]
            require(np.all(widths > 0) and np.all(np.isfinite(widths)) and volume > 0
                    and all(v >= 0 for v in identifiers), "bounds", "Invalid native bounds, measure or block identifier.")
            eps, tiny = np.finfo(float).eps, np.finfo(float).smallest_subnormal
            reference = math.prod(widths)
            # V=prod(hi-lo); propagate endpoint subtraction roundoff per axis.
            budget = 32 * eps * abs(reference) + 32 * tiny
            budget += sum(8 * eps * (abs(lo) + abs(hi)) * math.prod(widths[:a]) * math.prod(widths[a+1:])
                          for a, (lo, hi) in enumerate(edges))
            require(math.isfinite(reference) and reference > 0 and math.isfinite(budget) and abs(volume - reference) <= budget,
                    "measure", "Measure disagrees with Cartesian bounds.")
            for axis, (lo, hi) in enumerate(edges):
                midpoint = 0.5 * lo + 0.5 * hi
                tolerance = 32 * eps * max(abs(lo), abs(hi), abs(centers[axis])) + 32 * tiny
                require(abs(centers[axis] - midpoint) <= tolerance, "center", "Cartesian center disagrees with native axis bounds.")
            result.update(status="candidate", metadata=header, query={
                "cell": list(index), "field": selected, "value": value,
                "native_bounds": edges.tolist(), "measure": volume, "cartesian_center": centers,
                "level": identifiers[0], "morton": identifiers[1]},
                logical_payload_bytes_read=8 * (2 + 2 * dim + 3) + sum(o.dtype.itemsize for o in blocks))
            result["limitations"].extend("Unknown identity: " + key for key in IDENTITIES if header["identities"][key]["state"] == "unknown")
    except ContractError as error:
        result.update(status="invalid", errors=[{"code": error.code, "message": str(error)}])
    except (OSError, ValueError, TypeError, KeyError, OverflowError, RuntimeError) as error:
        result.update(status="invalid", errors=[{"code": "input", "message": "Unreadable or malformed Plotfile input (" + type(error).__name__ + ")."}])
    return result


class Parser(argparse.ArgumentParser):
    """Keep malformed command lines on the same structured-error channel."""

    def error(self, message):
        raise ContractError("arguments", "Invalid command-line arguments.")


def main(argv=None):
    """Print one JSON result; exits 0=candidate, 2=legacy, 1=invalid."""
    parser = Parser(description=__doc__)
    parser.add_argument("--file", required=True)
    parser.add_argument("--layout")
    parser.add_argument("--expected-identities")
    parser.add_argument("--cell", help="block,i or block,j,i")
    parser.add_argument("--field")
    try:
        args = parser.parse_args(argv)
        def read_json(path):
            if path is None:
                return None
            require(Path(path).stat().st_size <= 65536, "budget", "CLI JSON exceeds metadata budget.")
            return strict_json(Path(path).read_text(encoding="utf-8"))
        cell = [int(v) for v in args.cell.split(",")] if args.cell is not None else None
        result = probe_plotfile(args.file, read_json(args.layout), cell=cell, field=args.field,
                                expected_identities=read_json(args.expected_identities))
    except (ValueError, OSError, TypeError) as error:
        result = {"status": "invalid", "errors": [{"code": getattr(error, "code", "arguments"),
                  "message": "Invalid CLI input."}]}
    print(json.dumps(result, allow_nan=False))
    return {"candidate": 0, "legacy": 2, "invalid": 1}[result["status"]]


if __name__ == "__main__":
    sys.exit(main())
