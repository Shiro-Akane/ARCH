"""Consume explicit source/geometry/stamp from actual solved native probe.
Boundary/cell-center point-potential diagnostics with Duffy contact estimates.
No contact-force oracle or scientific PASS.
Raw field arrays stay local; no density inference from Poisson RHS.
"""
import argparse
import copy
import struct
import hashlib
import json
import math
from decimal import Decimal, localcontext
from fractions import Fraction
from pathlib import Path
from rz_matched_source_reference import potential_reference, validate_source, G

def coalesce_exact_dense_source(source, root_bounds, source_identity, _shared_budget=None):
    """Combine adjacent equal actual densities, with an exact union witness.

    This is optional reference preprocessing, not a different physical source.
    It consumes all explicit actual rounded rectangles and validates their exact
    dense coverage before and after merging. Only exactly touching rectangles
    of the same density bit pattern and same transverse interval can combine.
    No density, endpoint, observer, source stamp or reference allowance is
    reconstructed, rounded, approximated, omitted or inferred from Poisson RHS.

    Newton's potential/force integral is additive on disjoint source volumes.
    Thus equal-density rectangles whose exact union is another rectangle have
    the same full-ring source. Each final group retains every original leaf id
    and exact per-pi volume/density integral, including mathematical zero cells.
    The caller keeps the original dense record and authentic Runtime receipt;
    this mathematical union witness cannot authenticate a supplied JSON file.
    The reference's shared work/time/width limits are unchanged.
    """
    from rz_ring_surface_reference import validate_dense_source, rational

    actual = validate_dense_source(source, root_bounds, source_identity, _shared_budget)
    original = {leaf.identity: leaf for leaf in actual}
    records = {item["id"]: item for item in source["leaves"]}
    keys = ("r_lower", "r_upper", "z_lower", "z_upper")

    def density_key(value):
        # Actual Core density is FP64: even signed zero is a distinct pattern.
        if isinstance(value, float):
            return ("fp64", struct.pack(">d", value).hex())
        return (type(value).__name__, str(rational(value)))

    pieces = [{"bounds": [item[key] for key in keys],
               "rho": item["density"], "rho_key": density_key(item["density"]),
               "ids": [item["id"]]} for item in source["leaves"]]

    def merge_axis(values, axis):
        groups = {}
        lo, hi = 2 * axis, 2 * axis + 1
        other = 2 * (1 - axis)
        for piece in values:
            if _shared_budget is not None: _shared_budget.check_time()
            key = (piece["rho_key"], rational(piece["bounds"][other]),
                   rational(piece["bounds"][other + 1]))
            groups.setdefault(key, []).append(piece)
        result = []
        for key in sorted(groups):
            chain = sorted(groups[key], key=lambda v: rational(v["bounds"][lo]))
            current = copy.deepcopy(chain[0])
            for piece in chain[1:]:
                if _shared_budget is not None: _shared_budget.check_time()
                if rational(current["bounds"][hi]) == rational(piece["bounds"][lo]):
                    # Select the actual supplied endpoint; never compute it.
                    current["bounds"][hi] = piece["bounds"][hi]
                    current["ids"].extend(piece["ids"])
                else:
                    result.append(current)
                    current = copy.deepcopy(piece)
            result.append(current)
        return result

    while True:
        count = len(pieces)
        # Axial then radial permits a mixed-width constant AMR source to form
        # common full-height columns without splitting or inventing rectangles.
        pieces = merge_axis(merge_axis(pieces, 1), 0)
        if len(pieces) == count:
            break
    partition, merged = [], []
    visited = set()
    for piece in pieces:
        if _shared_budget is not None: _shared_budget.check_time()
        ids = sorted(piece["ids"])
        if visited.intersection(ids):
            raise ValueError("Exact source partition duplicated an original leaf")
        visited.update(ids)
        L, H, A, B = map(rational, piece["bounds"])
        measure = (H * H - L * L) * (B - A)  # full-ring volume / mathematical pi
        original_measure = sum((original[key].H**2 - original[key].L**2)
                               * (original[key].B - original[key].A) for key in ids)
        if measure != original_measure or any(
                density_key(records[key]["density"]) != piece["rho_key"] for key in ids):
            raise ValueError("Exact source partition changed volume or actual density")
        name = "exact-union:" + hashlib.sha256(json.dumps(ids).encode()).hexdigest()
        merged.append(dict(id=name, **dict(zip(keys, piece["bounds"])), density=piece["rho"]))
        partition.append(dict(id=name, original_leaf_ids=ids,
                              full_ring_volume_over_pi_exact=str(measure),
                              mass_over_pi_exact=str(rational(piece["rho"]) * measure),
                              density_pattern=list(piece["rho_key"])))
    if visited != set(original):
        raise ValueError("Exact source partition omitted an original leaf")
    dense_payload = dict(source=source, root_bounds=root_bounds, source_identity=source_identity)
    input_hash = hashlib.sha256(json.dumps(dense_payload, sort_keys=True,
        allow_nan=False, separators=(",", ":")).encode()).hexdigest()
    combined = dict(sourceId=source["sourceId"] + ":exact-union:" + input_hash, leaves=merged)
    validate_dense_source(combined, root_bounds, source_identity, _shared_budget)
    return dict(source=combined, original_dense_input_sha256=input_hash,
                original_leaf_count=len(actual), coalesced_leaf_count=len(merged),
                exact_partition=partition, source_identity=copy.deepcopy(source_identity),
                root_bounds=copy.deepcopy(root_bounds), science_accepted=False,
                core_binding_qualified=False,
                scope="Exact union of all supplied rings only; original Runtime authentication separate")


def source_from_case(case):
    identity=case["source_identity"]
    for key in ("topology","operator_revision","boundary_revision","accuracy_revision","generation"):
        value=identity[key]
        if type(value) is not int or value <= 0:
            raise ValueError("Missing/nonpositive identity revision")
    if not identity["inputs"]:
        raise ValueError("Missing source dependency")
    for item in identity["inputs"]:
        if item["epoch"] != identity["topology"] or item["slot"] not in (0,1,2):
            raise ValueError("Source topology/slot mismatch")
        for key in ("uid","version","storage_generation"):
            if type(item[key]) is not int or item[key] <= 0:
                raise ValueError("Invalid source dependency")
    for key in ("time","G"):
        if not Decimal.from_float(float(identity[key])).is_finite():
            raise ValueError("Nonfinite source stamp")
    if identity["G"] <= 0:raise ValueError("Invalid G")
    leaves=[]
    for i,cell in enumerate(case["cells"]):
        edges=cell["edges"]  # explicit actual bounds; never infer density
        if len(edges)!=4:raise ValueError("Missing leaf bounds")
        for axis in range(2):
            h=Fraction(case["spacing"][axis])/2**cell["level"]
            for side in range(2):
                ideal=Fraction(case["origin"][axis])+(cell["index"][axis]+side)*h
                if Fraction(edges[2*axis+side])!=ideal:
                    raise ValueError("Rounded root/leaf geometry not certified by this adapter")
        leaves.append(dict(id=str(i),r_lower=edges[0],r_upper=edges[1],
                           z_lower=edges[2],z_upper=edges[3],density=cell["density"]))
    source=dict(sourceId=hashlib.sha256(json.dumps(dict(identity=identity,leaves=leaves),
                 sort_keys=True,allow_nan=False).encode()).hexdigest(),leaves=leaves)
    validate_source(source)
    return source

def audit_case(case,precision=80,order=16,t_panels=1):
    source=source_from_case(case)
    rows=[]
    with localcontext() as ctx:
        ctx.prec=precision
        ratio=Decimal.from_float(float(case["source_identity"]["G"]))/G
        for face in case["faces"]:
            if face["boundary_side"]<0:continue
            observer=dict(r_observer=face["center"][0],z_observer=face["center"][1])
            values=potential_reference(source,observer,order=order,
                                       precision=precision,t_panels=t_panels)
            phi=values["potential"]*ratio
            actual_phi=Decimal.from_float(float(case["face_values"][face["index"]]))
            rows.append(dict(faceIndex=face["index"],
                potentialAbsoluteDelta=str(abs(phi-actual_phi)),
                referencePotentialDecimal=str(phi),
                contactLeaves=values["contactLeaves"],
                exteriorLeaves=values["exteriorLeaves"],
                axisLeaves=values["axisLeaves"],certified=False))
    return dict(radialOrigin=case["radial_origin"],mixedAmr=bool(case["mixed"]),
        sourceId=source["sourceId"],sourceIdentity=case["source_identity"],
        cells=len(case["cells"]),boundaryObservers=len(rows),rows=rows)


def audit_cell_case(case, precision=80, order=16, t_panels=1):
    """Compare actual solved point Phi at every native geometric cell center."""
    source = source_from_case(case)
    actual = case["potential"]
    if len(actual) != len(case["cells"]):
        raise ValueError("Solved potential/native cell extent mismatch")
    if not all(Decimal.from_float(float(v)).is_finite() for v in actual):
        raise ValueError("Nonfinite solved potential")
    rows = []
    with localcontext() as ctx:
        ctx.prec = precision
        ratio = Decimal.from_float(float(case["source_identity"]["G"])) / G
        squared_error = total_weight = Decimal(0)
        for i, cell in enumerate(case["cells"]):
            e = [Fraction(v) for v in cell["edges"]]
            center = [(e[2*a] + e[2*a+1]) / 2 for a in range(2)]
            # Only exact representable native centers; no rounding tolerance.
            if any(Fraction(float(v)) != v for v in center):
                raise ValueError("Rounded native cell center")
            observer = dict(r_observer=float(center[0]), z_observer=float(center[1]))
            values = potential_reference(source, observer, order=order,
                                         precision=precision, t_panels=t_panels)
            phi = values["potential"] * ratio
            delta = abs(phi - Decimal.from_float(float(actual[i])))
            # Full-ring volume common pi cancels in the normalized RMS.
            q = (e[1]*e[1] - e[0]*e[0]) * (e[3] - e[2])
            weight = Decimal(q.numerator) / Decimal(q.denominator)
            squared_error += weight * delta * delta
            total_weight += weight
            rows.append(dict(cellIndex=i, level=cell["level"],
                observer=observer, potentialAbsoluteDelta=str(delta),
                referencePotentialDecimal=str(phi),
                contactLeaves=values["contactLeaves"],
                exteriorLeaves=values["exteriorLeaves"],
                axisLeaves=values["axisLeaves"], certified=False))
        rms = (squared_error / total_weight).sqrt()
    return dict(radialOrigin=case["radial_origin"], mixedAmr=bool(case["mixed"]),
        sourceId=source["sourceId"], sourceIdentity=case["source_identity"],
        cells=len(rows), cellObservers=len(rows), rows=rows,
        nativeVolumeRmsPointPotentialDelta=str(rms),
        maximumPointPotentialDelta=str(max(Decimal(v["potentialAbsoluteDelta"]) for v in rows)),
        referenceQuality="estimate only; no certified quadrature/spatial acceptance")


def validate_materialized_record(record, budget):
    """Check complete supplied actual record shape; this is NOT Runtime authority.

    Rounded source bounds and original field rows are used verbatim. Every leaf,
    face, side entry and observer is checked, even though diagnostics select a
    small deterministic subset. No ideal-grid or Poisson-RHS adapter is called.
    """
    def require(condition, message):
        if not condition: raise ValueError(message)
    def number(value):
        require(type(value) in (int, float) and math.isfinite(value), "Invalid actual numeric value")
        return value
    def same(a, b):
        return struct.pack(">d", number(a)) == struct.pack(">d", number(b))
    def array(value, length):
        require(isinstance(value, list) and len(value) == length, "Actual array extent mismatch")
        for item in value: number(item)
        return value
    require(isinstance(record, dict) and record.get("schema") == "arch-materialized-native-source-1",
            "Unknown actual materialized-source schema")
    require(record.get("source_only_checked") is True and record.get("physical_qualified") is False,
            "Missing checked source-only record")
    require(record.get("scope") == "materialized_source_only", "Wrong actual source export scope")
    service = record["service_configuration"]
    require(service.get("origin") == "actual-SelfGravity-constructor-copy", "Missing actual service configuration")
    for key in ("g_x", "g_y", "g_z", "relative_tolerance", "absolute_tolerance"): number(service[key])
    require(type(service.get("max_cycles")) is int and service["max_cycles"] > 0, "Invalid actual solver work setting")
    binding = record["native_binding"]
    require(binding.get("dimension") == 2 and isinstance(binding.get("patches"), list) and binding["patches"],
            "Missing actual native source binding")
    origin = array(binding["origin"], 3); upper = array(binding["root_upper"], 3)
    root = array(record["root_bounds"], 4)
    require(all(same(a,b) for a,b in zip(root,(origin[0],upper[0],origin[1],upper[1]))), "Actual root/binding drift")
    field = record["candidate_field"]
    require(isinstance(field, dict) and field.get("physical_qualified") is False,
            "Missing actual unqualified field receipt")
    call = record["field_call"]
    require(call.get("invoke_failed") is False and call.get("field_solve_failed") is False
            and call.get("actual_candidate_observed") is True, "Actual field call did not succeed")
    identity = record["source_identity"]
    require(type(field.get("source_generation")) is int
            and field["source_generation"] == identity["generation"]
            and type(field.get("field_generation")) is int and field["field_generation"] > 0,
            "Actual field/source generation mismatch")
    leaves = record["source"]["leaves"]; cells = field["cell_values"]; faces = field["face_values"]
    require(isinstance(leaves, list) and leaves and isinstance(cells, list)
            and len(cells) == len(leaves) and isinstance(faces, list) and faces,
            "Incomplete actual cell/face record")
    n, m = len(cells), len(faces)
    patches = binding["patches"]; inputs = identity["inputs"]
    require(len(patches) == len(inputs), "Actual patch dependency count mismatch")
    for patch, dependency in zip(patches, inputs):
        budget.check_time()
        require(patch.get("uid") == dependency["uid"] and patch.get("epoch") == dependency["epoch"]
                and patch.get("field_memory") == 0, "Actual Host patch identity mismatch")
        bound = patch["bound_root_identity"]
        require(bound.get("bound") is True, "Unbound actual native source patch")
        require(all(same(a,b) for a,b in zip(array(bound["root_lower"],2),(root[0],root[2])))
                and all(same(a,b) for a,b in zip(array(bound["root_upper"],2),(root[1],root[3]))), "Actual patch root mismatch")
    array(field["side_acceleration"], 6*n); gradients = array(field["face_gradients"], m)
    require(field["side_acceleration_layout"] == "cell-major: 6*cell+2*axis+side; side0=low, side1=high"
            and field["face_gradient_semantics"] == "increasing-coordinate derivative; force=-gradient",
            "Unknown original field storage/gradient semantics")
    observers = record["observers"]
    require(isinstance(observers, list) and len(observers) == n+m, "Incomplete actual observer record")
    by_cell, by_face, names = {}, {}, set()
    for observer in observers:
        budget.check_time()
        require(isinstance(observer.get("id"), str) and observer["id"]
                and observer["id"] not in names, "Invalid/duplicate actual observer id")
        names.add(observer["id"]); number(observer["r_observer"]); number(observer["z_observer"])
        if observer.get("kind") == "cell-center":
            observer_index = observer.get("source_index"); target = by_cell
        elif observer.get("kind") == "face-fragment-center":
            observer_index = observer.get("face_index"); target = by_face
        else: raise ValueError("Unknown actual observer kind")
        require(type(observer_index) is int and observer_index >= 0 and observer_index not in target, "Invalid/duplicate actual observer index")
        target[observer_index] = observer
    require(set(by_cell) == set(range(n)) and set(by_face) == set(range(m)), "Missing actual observers")
    for i, (leaf, cell) in enumerate(zip(leaves, cells)):
        budget.check_time()
        require(type(leaf.get("source_index")) is int and leaf["source_index"] == i
                and type(cell.get("source_index")) is int and cell["source_index"] == i
                and cell.get("leaf_id") == leaf.get("id"), "Actual dense cell order/id mismatch")
        require(type(leaf.get("binding_block_index")) is int and 0 <= leaf["binding_block_index"] < len(patches)
                and type(leaf.get("source_offset")) is int and 0 <= leaf["source_offset"] < patches[leaf["binding_block_index"]]["native_layout"]["total_size"],
                "Invalid actual source storage mapping")
        require(number(leaf["density"]) > 0, "Nonpositive actual production source density")
        for key in ("r_lower", "r_upper", "z_lower", "z_upper", "stored_operator_volume"): number(leaf[key])
        require(leaf["stored_operator_volume"] > 0, "Invalid original operator volume")
        center = array(leaf["center"], 3); number(cell["potential"]); array(cell["acceleration"], 3)
        require(same(by_cell[i]["r_observer"], center[0]) and same(by_cell[i]["z_observer"], center[1]),
                "Actual cell observer drift")
    for i, face in enumerate(faces):
        budget.check_time()
        require(type(face.get("face_index")) is int and face["face_index"] == i
                and type(face.get("axis")) is int and face["axis"] in (0, 1)
                and type(face.get("boundary_side")) is int and face["boundary_side"] in (-1, 0, 1, 2, 3)
                and face.get("native_bounds") is True
                and type(face.get("construction")) is int and face["construction"] in (0, 1, 2),
                "Invalid actual native face identity")
        indices = (face["left"], face["right"])
        require(all(type(v) is int and -1 <= v < n for v in indices) and indices != (-1, -1),
                "Invalid actual face cell indices")
        require((face["boundary_side"] == -1 and min(indices) >= 0)
                or (face["boundary_side"] >= 0 and min(indices) == -1
                    and face["boundary_side"]//2 == face["axis"]), "Face side/cell mismatch")
        require(number(face["area"]) >= 0 and same(face["gradient"], gradients[i]), "Face gradient/area mismatch")
        center = array(face["center"], 3)
        for key in ("fragment_lower", "fragment_upper", "fragment_width"): array(face[key], 3)
        for key in ("boundary_datum", "boundary_coefficient", "anchor_coefficient", "value_boundary_coefficient"): number(face[key])
        for ik, ck in (("gradient_samples", "gradient_coefficients"), ("value_samples", "value_coefficients")):
            samples = face[ik]; require(isinstance(samples, list), "Missing actual stencil indices")
            array(face[ck], len(samples))
            require(all(type(v) is int and 0 <= v < n for v in samples), "Invalid original stencil sample")
        observer = by_face[i]
        require(same(observer["r_observer"], center[0]) and same(observer["z_observer"], center[1])
                and all(observer.get(k) == face[k] for k in ("left", "right", "axis", "boundary_side", "native_bounds"))
                and same(observer["area"], face["area"]), "Actual face observer drift")
        for key in ("center", "fragment_lower", "fragment_upper"):
            array(observer[key], 3)
            require(all(same(a,b) for a,b in zip(observer[key],face[key])), "Actual face observer bounds drift")
    return leaves, cells, faces, by_cell, by_face


def select_materialized_observers(cells, faces, by_cell, by_face):
    """First/middle/last in original order for each actual class, no value bias."""
    cell_indices = sorted({0, len(cells)//2, len(cells)-1})
    groups = {}
    for i, face in enumerate(faces): groups.setdefault((face["axis"], face["boundary_side"]), []).append(i)
    face_indices = set()
    for group in groups.values(): face_indices.update((group[0], group[len(group)//2], group[-1]))
    selected = [("cell", i, by_cell[i]) for i in cell_indices]
    selected += [("face", i, by_face[i]) for i in sorted(face_indices)]
    return selected


def audit_materialized_record(record, dependency_directory=None):
    """Bounded subset diagnostic from actual original arrays, never science PASS.

    One budget begins before all dense validation/coalescing, and is passed to
    the unchanged one-angle integration. Original G*1e-12 interval widths are
    fixed before seeing a result. Require the previously frozen 70 dps context;
    the runner owns its context and this reader never changes precision.
    """
    from rz_ring_surface_reference import (Budget, CGS_G, WorkLimit,
        rational, load_optional_flint, evaluate_reference)
    budget = Budget.start()
    widths = {key: str(CGS_G/Fraction(10**12)) for key in ("Phi", "g_r", "g_z")}
    result = dict(status="ACTUAL_MATERIALIZED_SUBSET_DIAGNOSTIC_NOT_SCIENTIFIC_ACCEPTANCE",
                  science_accepted=False, physical_qualified=False, core_binding_qualified=False,
                  target_widths_exact=widths, rows=[], derived_row_semantics="original stored value row, exact rational evaluation; not actual face-point Phi")
    try:
        leaves, cells, faces, by_cell, by_face = validate_materialized_record(record, budget)
        combined = coalesce_exact_dense_source(record["source"], record["root_bounds"], record["source_identity"], budget)
        selected = select_materialized_observers(cells, faces, by_cell, by_face)
        backend = load_optional_flint(dependency_directory)
        if backend.ctx.dps != 70: raise ValueError("Runner must freeze optional flint context at exactly 70 dps")
        budget.check_time()
        reference = evaluate_reference(combined["source"], record["root_bounds"], record["source_identity"],
            [entry[2] for entry in selected], widths, dependency_directory, _shared_budget=budget)
        result.update(original_leaf_count=len(leaves), coalesced_leaf_count=combined["coalesced_leaf_count"],
                      exact_union_input_sha256=combined["original_dense_input_sha256"],
                      actual_face_count=len(faces), selected_observer_count=len(selected),
                      reference_status=reference["status"], reference_complete=reference["certified"],
                      backend=reference.get("backend"), reference_failure=reference.get("failure"))
        resolved = {row["observer_id"]: row for row in reference["rows"]}
        def delta(actual, interval):
            lo, hi = Fraction(interval["lower_rational"]), Fraction(interval["upper_rational"])
            value = rational(actual); error_lo, error_hi = value-hi, value-lo
            return dict(actual_exact=str(value), reference_lower_exact=str(lo), reference_upper_exact=str(hi),
                        signed_error_lower_exact=str(error_lo), signed_error_upper_exact=str(error_hi),
                        absolute_error_lower_exact=str(0 if error_lo <= 0 <= error_hi else min(abs(error_lo),abs(error_hi))),
                        absolute_error_upper_exact=str(max(abs(error_lo),abs(error_hi))), outside_reference=not(lo <= value <= hi))
        for kind, index, observer in selected:
            budget.check_time()
            row = dict(observer_id=observer["id"], kind=kind, actual_index=index, science_accepted=False)
            actual_ref = resolved.get(observer["id"])
            if actual_ref is None:
                row["reference_status"] = "UNVERIFIED_NOT_EVALUATED"; result["rows"].append(row); continue
            row["math_interval_meets_original_width"] = actual_ref["math_certificate_meets_target"]
            intervals = actual_ref["intervals"]
            if kind == "cell": row["point_cell_potential"] = delta(cells[index]["potential"], intervals["Phi"])
            else:
                face = faces[index]; component = "g_r" if face["axis"] == 0 else "g_z"
                row["force_minus_original_gradient"] = delta(-rational(face["gradient"]), intervals[component])
                derived = sum((rational(cells[j]["potential"])*rational(c)
                    for j,c in zip(face["value_samples"],face["value_coefficients"])), Fraction(0))
                derived += rational(face["value_boundary_coefficient"])*rational(face["boundary_datum"])
                row["derived_original_value_row_potential"] = delta(derived, intervals["Phi"])
                if face["boundary_side"] >= 0 and not face["value_samples"] and rational(face["value_boundary_coefficient"]) == 1:
                    row["actual_boundary_datum_potential"] = delta(face["boundary_datum"], intervals["Phi"])
                else: row["boundary_datum_potential_scope"] = "Not an actual pure Dirichlet point-value row"
            result["rows"].append(row)
    except (ValueError, TypeError, KeyError, ArithmeticError, WorkLimit) as exc:
        result.update(reference_complete=False, reference_status="WorkLimit" if isinstance(exc,WorkLimit) else "UNVERIFIED", reference_failure=str(exc))
    result["budget"] = budget.record()
    result["limitations"] = ["Deterministic observer subset only; no full-domain scientific acceptance or new production tolerance",
        "Record shape is checked; genuine Runtime/ELF/file provenance remains the outer producer owner",
        "Only source rectangles are coalesced by exact union; original selected sites/arrays remain unchanged",
        "Side accelerations are shape-checked, not independently qualified by this subset reference",
        "Unfinished/failed reference rows are UNVERIFIED, never PASS; interval containment is not a science gate"]
    # The local diagnostic remains compact; never emit the dense source or an
    # unbounded reference stamp. A transport overflow is an explicit failure,
    # not permission to silently trim rows into a successful subset.
    encoded = json.dumps(result, sort_keys=True, allow_nan=False)
    if len(encoded.encode("utf-8")) > 60*1024:
        result.update(reference_complete=False, reference_status="UNVERIFIED_OUTPUT_BOUND",
                      reference_failure="Compact diagnostic exceeds 60 KiB before outer record hash", rows=[])
    return result



def materialized_full_schedule(record, cells, faces, by_cell, by_face, budget):
    """Map EVERY validated actual target to exact duplicates/reflections only.

    Caller must first validate the complete actual schema and exact dense source
    coverage. The reflection proof tests every original leaf's actual rational
    bounds, exact density value and FP64 density pattern against its exact partner.
    Numeric integers are supported by the actual validator, so FP64 conversion
    alone cannot prove equality above 2**53. Missing partners
    disable reflection for the whole source, without deleting any target. Root
    authentication stays with the producer; this is mathematical scheduling.
    """
    from rz_ring_surface_reference import rational
    budget.check_time()
    leaves = record["source"]["leaves"]
    root = list(map(rational, record["root_bounds"]))
    midpoint = (root[2] + root[3]) / 2
    def leaf_key(leaf):
        return tuple(rational(leaf[key]) for key in
                     ("r_lower", "r_upper", "z_lower", "z_upper")) + (
                     rational(leaf["density"]),
                     struct.pack(">d", float(leaf["density"])).hex())
    keys = set()
    for leaf in leaves:
        budget.check_time(); keys.add(leaf_key(leaf))
    symmetric = len(keys) == len(leaves)
    for leaf in leaves:
        budget.check_time()
        L, H, A, B, density, density_bits = leaf_key(leaf)
        symmetric = symmetric and (L, H, 2*midpoint-B, 2*midpoint-A,
                                    density, density_bits) in keys
    source_payload = json.dumps(dict(source=record["source"], root_bounds=record["root_bounds"],
        source_identity=record["source_identity"]), sort_keys=True, allow_nan=False,
        separators=(",", ":")).encode()
    source_hash = hashlib.sha256(source_payload).hexdigest()
    selected = [("cell", i, by_cell[i]) for i in range(len(cells))]
    selected += [("face", i, by_face[i]) for i in range(len(faces))]
    sites, mapping, site_index = [], [], {}
    for kind, index, observer in selected:
        budget.check_time()
        R, Z = rational(observer["r_observer"]), rational(observer["z_observer"])
        sign = -1 if symmetric and Z < midpoint else 1
        canonical_Z = 2*midpoint-Z if sign == -1 else Z
        key = (R, canonical_Z)
        if key not in site_index:
            identity = "full-site:" + hashlib.sha256(
                (source_hash+":"+str(R)+":"+str(canonical_Z)).encode()).hexdigest()
            site_index[key] = identity
            sites.append(dict(id=identity, r_observer=str(R), z_observer=str(canonical_Z)))
        mapping.append(dict(observer_id=observer["id"], kind=kind, actual_index=index,
            site_id=site_index[key], r_exact=str(R), z_exact=str(Z), g_z_sign=sign))
    return dict(sites=sites, targets=mapping, exact_z_reflection=symmetric,
        symmetry_midpoint_exact=str(midpoint), original_leaf_count=len(leaves),
        original_target_count=len(selected), unique_site_count=len(sites),
        source_input_sha256=source_hash,
        proof="All original exact rectangles, exact densities and FP64 bits; Phi/g_r even, g_z odd" if symmetric
              else "No complete exact source-reflection proof; exact positional duplicates only")


def reflected_reference_intervals(intervals, g_z_sign):
    """Transport enclosing endpoints EXACTLY; reflection never uses a midpoint.

    Rational arithmetic is outward-exact. Phi/g_r are unchanged under z
    reflection; g_z in [l,u] becomes [-u,-l]. These are mathematical intervals,
    not an assertion that any supplied Core field is scientifically qualified.
    """
    if g_z_sign not in (-1, 1):
        raise ValueError("Invalid exact reference reflection orientation")
    result = {}
    for component in ("Phi", "g_r", "g_z"):
        data = intervals[component]
        lo, hi = Fraction(data["lower_rational"]), Fraction(data["upper_rational"])
        if lo > hi:
            raise ValueError("Reversed rigorous reference interval")
        if component == "g_z" and g_z_sign == -1:
            lo, hi = -hi, -lo
        result[component] = dict(lower_rational=str(lo), upper_rational=str(hi),
                                 width_rational=str(hi-lo))
    return result


def full_interval_error(actual, interval):
    """Keep a supplied FP64 field value and its error enclosure as exact rationals."""
    from rz_ring_surface_reference import rational
    value = rational(actual)
    lo, hi = Fraction(interval["lower_rational"]), Fraction(interval["upper_rational"])
    low, high = value-hi, value-lo
    return dict(actual_exact=str(value), reference_lower_exact=str(lo),
        reference_upper_exact=str(hi), signed_error_lower_exact=str(low),
        signed_error_upper_exact=str(high),
        absolute_error_lower_exact=str(0 if low <= 0 <= high else min(abs(low), abs(high))),
        absolute_error_upper_exact=str(max(abs(low), abs(high))))


def full_materialized_summary(result):
    """Return a separate bounded aggregate, never trim the complete local result.

    Maximum errors are diagnostics only, not empirical scientific tolerances.
    Every quantity reports its number of successfully mapped original targets.
    Missing rows remain UNVERIFIED and cannot become complete by aggregation.
    """
    maxima, counts = {}, {}
    for row in result.get("targets", []):
        for key, value in row.get("comparisons", {}).items():
            error = Fraction(value["absolute_error_upper_exact"])
            maxima[key] = max(maxima.get(key, Fraction(0)), error)
            counts[key] = counts.get(key, 0) + 1
    return dict(profile=result["profile"], status=result["status"],
        science_accepted=False, physical_qualified=False, core_binding_qualified=False,
        reference_complete=result.get("reference_complete", False),
        coverage_complete=result.get("coverage_complete", False),
        target_count=result.get("target_count", 0),
        verified_target_count=sum(row.get("math_interval_meets_original_width") is True
                                  for row in result.get("targets", [])),
        unique_site_count=result.get("unique_site_count", 0),
        exact_z_reflection=result.get("exact_z_reflection", False),
        source_identity=result.get("source_identity"), field_identity=result.get("field_identity"),
        actual_record_canonical_sha256=result.get("actual_record_canonical_sha256"),
        original_source_input_sha256=result.get("original_source_input_sha256"),
        reference_status=result.get("reference_status"), failure=result.get("failure"),
        target_widths_exact=result["target_widths_exact"], budget=result["budget"],
        comparison_counts=counts, maximum_absolute_error_upper_exact={k: str(v) for k,v in maxima.items()},
        scope="Complete-domain mathematical interval diagnostic; no frozen production accuracy/science grant")


def audit_materialized_full_record(record, dependency_directory=None, _shared_budget=None):
    """Opt-in complete actual-field diagnostic, distinct from the unchanged subset.

    One manager-frozen 240s/1800000-callback request begins before complete schema,
    dense/source validation, exact source union and symmetry scheduling. It stays
    shared across every site/source/component and all final target mappings.
    Every original target and all three reference components remain in the local
    result. None of the field's unqualified metadata is promoted to public science.
    """
    from rz_ring_surface_reference import (Budget, CGS_G, WorkLimit, rational,
        load_optional_flint, evaluate_reference)
    budget = Budget.full_domain_diagnostic() if _shared_budget is None else _shared_budget
    if not isinstance(budget, Budget) or budget.max_calls != 1800000 or budget.timeout_seconds != 240.:
        raise ValueError("Complete-domain diagnostic requires its one frozen resource profile")
    widths = {key: str(CGS_G/Fraction(10**12)) for key in ("Phi", "g_r", "g_z")}
    result = dict(profile="actual-materialized-full-domain-diagnostic-1",
        status="FULL_DOMAIN_UNVERIFIED_DIAGNOSTIC", science_accepted=False,
        physical_qualified=False, core_binding_qualified=False, reference_complete=False,
        coverage_complete=False, target_widths_exact=widths, targets=[], sites=[],
        resource_authority="Manager-frozen distinct full diagnostic request; not a user hard budget",
        resource_limits=dict(max_calls=1800000, wall_seconds=240.))
    schedule = None
    try:
        budget.check_time()
        validated = validate_materialized_record(record, budget)
        leaves, cells, faces, by_cell, by_face = validated
        # Exact dense coverage must be validated before scheduling any sites.
        combined = coalesce_exact_dense_source(record["source"], record["root_bounds"],
                                              record["source_identity"], budget)
        schedule = materialized_full_schedule(record, cells, faces, by_cell, by_face, budget)
        encoded = json.dumps(record, sort_keys=True, allow_nan=False, separators=(",", ":")).encode()
        result.update(actual_record_canonical_sha256=hashlib.sha256(encoded).hexdigest(),
            source_identity=copy.deepcopy(record["source_identity"]),
            field_identity=dict(source_generation=record["candidate_field"]["source_generation"],
                                field_generation=record["candidate_field"]["field_generation"]),
            producer_identity=dict(source_only_checked=record["source_only_checked"],
                field_call=copy.deepcopy(record["field_call"]),
                service_configuration=copy.deepcopy(record["service_configuration"])),
            original_source_id=record["source"]["sourceId"],
            original_source_input_sha256=combined["original_dense_input_sha256"],
            exact_union=combined, target_count=schedule["original_target_count"],
            unique_site_count=schedule["unique_site_count"],
            exact_z_reflection=schedule["exact_z_reflection"],
            symmetry_midpoint_exact=schedule["symmetry_midpoint_exact"],
            symmetry_proof=schedule["proof"], target_mapping=copy.deepcopy(schedule["targets"]))
        backend = load_optional_flint(dependency_directory)
        if backend.ctx.dps != 70:
            raise ValueError("Runner must freeze optional flint context at exactly 70 dps")
        budget.check_time()
        reference = evaluate_reference(combined["source"], record["root_bounds"], record["source_identity"],
            schedule["sites"], widths, dependency_directory, _shared_budget=budget)
        result.update(reference_status=reference["status"], backend=reference.get("backend"),
            reference_identity=reference.get("identity"), sites=reference["rows"],
            failure=reference.get("failure"))
        resolved = {row["observer_id"]: row for row in reference["rows"]}
        for target in schedule["targets"]:
            budget.check_time()
            row = dict(target, science_accepted=False, math_interval_meets_original_width=False,
                       reference_status="UNVERIFIED_NOT_EVALUATED")
            math_row = resolved.get(target["site_id"])
            if math_row is not None and math_row.get("math_certificate_meets_target") is True:
                intervals = reflected_reference_intervals(math_row["intervals"], target["g_z_sign"])
                if any(Fraction(value["width_rational"]) > Fraction(widths[key])
                       for key, value in intervals.items()):
                    raise ValueError("Mapped rigorous interval exceeds original fixed width")
                row.update(intervals=intervals, math_interval_meets_original_width=True,
                           reference_status="MathematicalIntervalsCertified", comparisons={})
                i = target["actual_index"]
                if target["kind"] == "cell":
                    row["comparisons"]["point_cell_potential"] = full_interval_error(cells[i]["potential"], intervals["Phi"])
                else:
                    face = faces[i]; component = "g_r" if face["axis"] == 0 else "g_z"
                    row["comparisons"]["force_minus_original_gradient"] = full_interval_error(-rational(face["gradient"]), intervals[component])
                    derived = sum((rational(cells[j]["potential"])*rational(c)
                        for j,c in zip(face["value_samples"], face["value_coefficients"])), Fraction(0))
                    derived += rational(face["value_boundary_coefficient"])*rational(face["boundary_datum"])
                    row["comparisons"]["derived_original_value_row_vs_point_potential"] = full_interval_error(derived, intervals["Phi"])
                    if face["boundary_side"] >= 0 and not face["value_samples"] and rational(face["value_boundary_coefficient"]) == 1:
                        row["comparisons"]["actual_boundary_datum_potential"] = full_interval_error(face["boundary_datum"], intervals["Phi"])
            result["targets"].append(row)
        budget.check_time()
        complete = reference.get("certified") is True and len(result["targets"]) == result["target_count"] \
            and all(row["math_interval_meets_original_width"] for row in result["targets"])
        result.update(reference_complete=complete, coverage_complete=complete,
            status="FULL_DOMAIN_MATHEMATICAL_REFERENCE_DIAGNOSTIC_ONLY" if complete
                   else "FULL_DOMAIN_UNVERIFIED_DIAGNOSTIC")
    except (ValueError, TypeError, KeyError, AttributeError, ArithmeticError, WorkLimit, ImportError) as exc:
        result.update(reference_status="WorkLimit" if isinstance(exc, WorkLimit) else "UNVERIFIED",
                      failure=str(exc), reference_complete=False, coverage_complete=False)
    # Preserve every scheduled target even if shared budget expired mid-mapping.
    # These are explicit UNVERIFIED records, never silently omitted observations.
    if schedule is not None:
        retained = {row["observer_id"] for row in result["targets"]}
        for target in schedule["targets"]:
            if target["observer_id"] not in retained:
                result["targets"].append(dict(target, science_accepted=False,
                    math_interval_meets_original_width=False, reference_status="UNVERIFIED_NOT_EVALUATED"))
    result["budget"] = budget.record()
    result["limitations"] = [
        "Complete mathematical interval coverage is separate from production accuracy and scientific acceptance",
        "Actual Runtime/ELF/file authentication remains the outer producer owner",
        "Exact reflection maps all actual points; no approximate deletion or old18 cache reuse",
        "Original source and field stamps remain separate; no finalPhi or manufactured-source substitution",
        "Face-value row comparison is a discrete-row diagnostic, not an independent face-point Phi identity",
        "Side/cell acceleration mappings and original physical fixture gates require separate owner acceptance",
        "Earlier requests' callback/wall/provider costs remain in their historical accumulated records"]
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    records=p.add_mutually_exclusive_group(required=True)
    records.add_argument("--probe-record",type=Path)
    records.add_argument("--materialized-record",type=Path)
    p.add_argument("--dependency-directory",type=Path)
    p.add_argument("--output",required=True,type=Path)
    p.add_argument("--full-domain", action="store_true",
                   help="Opt-in complete materialized-field diagnostic; all targets remain local")
    p.add_argument("--summary-output", type=Path,
                   help="Separate compact full-domain aggregate (default: output name + .summary.json)")
    p.add_argument("--order",type=int,default=16)
    p.add_argument("--precision",type=int,default=80)
    p.add_argument("--t-panels",type=int,default=1)
    p.add_argument("--observer-set", choices=("boundary", "cells"), default="boundary")
    a=p.parse_args()
    if a.output.exists():p.error("output must be new")
    if a.full_domain and a.materialized_record is None:p.error("--full-domain requires --materialized-record")
    if a.summary_output is not None and not a.full_domain:p.error("--summary-output requires --full-domain")
    if a.full_domain:
        summary_output = a.summary_output or a.output.with_name(a.output.name+".summary.json")
        if summary_output == a.output or summary_output.exists():p.error("summary output must be separate and new")
    if a.materialized_record is not None:
        def invalid_constant(value): raise ValueError("Nonfinite JSON constant: "+value)
        if a.full_domain:
            from rz_ring_surface_reference import Budget, CGS_G, WorkLimit, load_optional_flint
            # The outer CLI owns the one explicit precision freeze. Library
            # audits/evaluators only check it; none mutate the shared context.
            # Startup, input reading and auditing all charge this SAME budget.
            full_budget = Budget.full_domain_diagnostic()
            raw = None
            phase = "optional-backend-precision-freeze"
            try:
                full_budget.check_time()
                backend = load_optional_flint(a.dependency_directory)
                backend.ctx.dps = 70
                if backend.ctx.dps != 70:
                    raise ValueError("Unable to freeze optional flint context at exactly 70 dps")
                full_budget.check_time()
                phase = "materialized-input-read"
                raw = a.materialized_record.read_bytes()
                full_budget.check_time()
                data=json.loads(raw,parse_constant=invalid_constant)
                phase = "complete-materialized-audit"
                result = audit_materialized_full_record(data,a.dependency_directory,full_budget)
            except (OSError, ValueError, TypeError, AttributeError, ArithmeticError, WorkLimit, ImportError) as exc:
                # No schema/target identity is guessed after failed startup.
                # Explicit failure evidence is still written to both outputs.
                result = dict(profile="actual-materialized-full-domain-diagnostic-1",
                    status="FULL_DOMAIN_UNVERIFIED_DIAGNOSTIC", science_accepted=False,
                    physical_qualified=False, core_binding_qualified=False,
                    reference_complete=False, coverage_complete=False, targets=[], sites=[],
                    reference_status="WorkLimit" if isinstance(exc, WorkLimit) else "UNVERIFIED",
                    failure=str(exc), failure_type=type(exc).__name__, failure_phase=phase,
                    input_path=str(a.materialized_record), input_read=raw is not None,
                    target_widths_exact={key: str(CGS_G/Fraction(10**12))
                                        for key in ("Phi", "g_r", "g_z")},
                    resource_authority="Manager-frozen distinct full diagnostic request; not a user hard budget",
                    resource_limits=dict(max_calls=1800000, wall_seconds=240.),
                    budget=full_budget.record())
            if raw is not None:
                result["materializedRecordSha256"]=hashlib.sha256(raw).hexdigest()
        else:
            raw=a.materialized_record.read_bytes()
            data=json.loads(raw,parse_constant=invalid_constant)
            result = audit_materialized_record(data,a.dependency_directory)
            result["materializedRecordSha256"]=hashlib.sha256(raw).hexdigest()
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+"\n")
        if a.full_domain:
            summary = full_materialized_summary(result)
            if "materializedRecordSha256" in result:
                summary["materializedRecordSha256"] = result["materializedRecordSha256"]
            if "failure_phase" in result:
                summary.update(failure_phase=result["failure_phase"], failure_type=result["failure_type"],
                               input_path=result["input_path"], input_read=result["input_read"])
            summary["completeLocalResultSha256"] = hashlib.sha256(a.output.read_bytes()).hexdigest()
            summary_output.parent.mkdir(parents=True,exist_ok=True)
            summary_output.write_text(json.dumps(summary,indent=2,allow_nan=False)+"\n")
            print("ACTUAL_MATERIALIZED_FULL_DOMAIN_DIAGNOSTIC",result["reference_status"],len(result["targets"]))
        else:
            print("ACTUAL_MATERIALIZED_SUBSET_DIAGNOSTIC",result["reference_status"],len(result["rows"]))
        return
    data=json.loads(a.probe_record.read_text())
    audit = audit_case if a.observer_set == "boundary" else audit_cell_case
    rows=[audit(c,a.precision,a.order,a.t_panels) for c in data["cases"]]
    result=dict(status=a.observer_set.upper()+"_POTENTIAL_DIAGNOSTIC_NOT_SCIENTIFIC_ACCEPTANCE",
        observerSet=a.observer_set,
        order=a.order,precision=a.precision,tPanels=a.t_panels,
        probeRecordSha256=hashlib.sha256(a.probe_record.read_bytes()).hexdigest(),
        rows=rows,units=dict(potential="cm^2/s^2",acceleration="cm/s^2"),
        GSemantics="Analytic Decimal reference rescaled to exact input FP64 G before comparison",
        limitations=["Actual static numerical density/stamp, not Runtime all-block stage/regrid publication",
                     "Point Phi estimate only; continuous face force and certified spatial accuracy not covered",
                     "No certified reference error or new threshold; original continuous science gates remain"])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+"\n")
    count_key = "boundaryObservers" if a.observer_set == "boundary" else "cellObservers"
    print("MATCHED_NATIVE_"+a.observer_set.upper()+"_PHI_DIAGNOSTIC",sum(r[count_key] for r in rows))
if __name__=="__main__":main()
