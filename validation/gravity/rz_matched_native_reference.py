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


def validate_native_field_proof_metadata(record):
    """Validate present current-publication metadata without authenticating JSON.

    Historical mathematical records remain immutable. Metadata supplied by a
    newer writer must be complete and internally consistent; its presence never
    substitutes for the actual producer/ELF/source fence or scientific gates.
    """
    field, identity = record["candidate_field"], record["source_identity"]
    def require(condition, message):
        if not condition: raise ValueError(message)
    def nonnegative(value):
        require(type(value) in (int, float) and math.isfinite(value) and value >= 0,
                "Invalid field proof bound")
        return value
    keys = ("purpose", "runtime_lease_generation", "runtime_lease_authenticated", "runtime_authority_scope")
    if any(key in item for item in (identity, field) for key in keys):
        require(all(key in item for item in (identity, field) for key in keys),
                "Incomplete actual field purpose metadata")
        require(identity["purpose"] == field["purpose"] == "AcceptedCurrent"
                and type(identity["runtime_lease_generation"]) is int
                and identity["runtime_lease_generation"] > 0
                and type(field["runtime_lease_generation"]) is int
                and field["runtime_lease_generation"] == identity["runtime_lease_generation"]
                and identity["runtime_lease_authenticated"] is True
                and field["runtime_lease_authenticated"] is True
                and identity["runtime_authority_scope"] == "checked-source-materialization-only"
                and field["runtime_authority_scope"] == "accepted-current-numerical-field-only",
                "Actual Current purpose/issuer metadata mismatch")
    if any(key in item for item in (identity, field) for key in keys):
        require("native_discrete_certificate" in field, "Current publication lost its same-field proof")
    if "native_discrete_certificate" not in field: return
    proof = field["native_discrete_certificate"]
    require(type(identity.get("generation")) is int and identity["generation"] > 0,
            "Invalid actual proof source generation type")
    require(isinstance(proof, dict), "Missing field proof object")
    expected = dict(schema="arch-private-native-discrete-field-certificate-1",
        scope="ideal-root-dyadic-native-discrete-operator", physical_qualified=False,
        continuous_potential_error_certified=False, per_cell_residual_error_exported=False,
        norm_scope="RootDyadicRzWeights", norm_kind="volume-normalized-RMS",
        weighted_L2_exported=False, potential_semantics="actual-stored-cell-center-point-Phi",
        residual_vector_binding="original-certified-operator-residual;not-long-double-row-diagnostic")
    require(all(proof.get(key) == value and type(proof.get(key)) is type(value)
                for key, value in expected.items()), "Unknown field proof semantics")
    require(all(type(proof.get(key)) is int and proof[key] > 0 and proof[key] == field[key]
                for key in ("source_generation", "field_generation")), "Field proof generation mismatch")
    rms, measure, residual, marginal = (proof[key] for key in
        ("native_potential_rms", "native_measure", "conditional_residual", "marginal_error_scalars"))
    require(all(isinstance(item, dict) for item in (rms, measure, residual, marginal)),
            "Invalid field proof enclosure object")
    require(rms.get("status") == measure.get("status") == "Bounded"
            and rms.get("units") == "cm^2/s^2" and measure.get("units") == "cm^3",
            "Unknown field norm/measure units")
    require(nonnegative(rms["lower"]) <= nonnegative(rms["upper"])
            and 0 < nonnegative(measure["total_volume_lower"]) <= nonnegative(measure["total_volume_upper"]),
            "Unordered field norm/measure enclosure")
    require(residual.get("units") == marginal.get("units") == "s^-2"
            and marginal.get("additive_floor") is False
            and marginal.get("stored_norm_scope") == "StoredNativeWeights"
            and marginal.get("native_norm_scope") == "RootDyadicRzWeights"
            and marginal.get("authoritative_complete_floor") == "conditional_residual.complete_residual_error_upper",
            "Unknown correlated field residual semantics")
    require(residual.get("status") == "Accepted"
            and type(field.get("conditional_status")) is int and field["conditional_status"] == 0
            and type(field.get("physical_status")) is int and 0 <= field["physical_status"] <= 4
            and residual.get("error_composition") in ("SeparateRhsAndOperator", "CorrelatedPrescribedBoundary"),
            "Unknown field residual status/composition")
    for key in ("complete_residual_error_upper", "tolerance_safe", "rhs_norm_lower", "rhs_norm_upper",
                "residual_norm_upper", "rhs_error_upper", "total_residual_upper"):
        nonnegative(residual[key])
    for key in ("source_stored_upper", "rhs_assembly_stored_upper", "residual_arithmetic_stored_upper",
                "boundary_construction_native_upper", "boundary_potential_native_upper",
                "operator_construction_native_upper", "residual_evaluation_native_upper", "complete_native_upper"):
        nonnegative(marginal[key])
    require(residual["rhs_norm_lower"] <= residual["rhs_norm_upper"]
            and marginal["complete_native_upper"] == residual["complete_residual_error_upper"],
            "Field residual bound identity mismatch")
    require(Fraction(residual["total_residual_upper"]) >= Fraction(residual["residual_norm_upper"])
                + Fraction(residual["complete_residual_error_upper"])
            and (residual["status"] != "Accepted"
                 or residual["total_residual_upper"] <= residual["tolerance_safe"]),
            "Inconsistent conditional field acceptance")


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
    validate_native_field_proof_metadata(record)
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
    summary = dict(profile=result["profile"], status=result["status"],
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
    if "matched_resolution" in result:
        summary["matched_resolution"] = copy.deepcopy(result["matched_resolution"])
    return summary


def full_materialized_resource_profile(budget):
    """Validate one frozen full-request budget and describe its real resources.

    Default behavior remains the original complete 240s/1800000 request. New
    matched profiles are internal fixed-layout diagnostics, not user tolerances.
    """
    from rz_ring_surface_reference import Budget
    if not isinstance(budget, Budget):
        raise ValueError("Complete-domain diagnostic requires its one frozen resource profile")
    name = budget.resource_profile
    expected = {"full-domain-diagnostic-1": (1800000, 240.),
                "matched-resolution-1": (6000000, 600.),
                "matched-resolution-2": (22000000, 1800.)}
    if name not in expected or (budget.max_calls, budget.timeout_seconds) != expected[name]:
        raise ValueError("Complete-domain diagnostic requires its one frozen resource profile")
    return dict(profile="actual-materialized-"+name,
                resource_limits=dict(max_calls=budget.max_calls, wall_seconds=budget.timeout_seconds))


def validate_matched_materialized_profile(record, budget):
    """Check fixed supplied layout/source identity before exact dense coalescing.

    The caller first runs the complete existing actual-record validator. This
    additional schema check does not authenticate Runtime/ELF provenance, infer
    ideal source faces, or replace exact all-leaf coverage/observer validation.
    Every supplied source offset must represent one real active logical cell.
    """
    from rz_ring_surface_reference import CGS_G, rational
    level = budget.matched_resolution_level
    if not level: return None
    budget.check_time()
    def require(value, message):
        if not value: raise ValueError("Fixed matched source: "+message)
    def integers(value, expected):
        return isinstance(value, list) and len(value) == len(expected)             and all(type(a) is int and a == b for a,b in zip(value,expected))
    def exact(value, expected):
        return type(value) in (int,float) and math.isfinite(value)             and rational(value) == rational(expected)             and struct.pack(">d",value) == struct.pack(">d",expected)
    def coordinates(value, expected):
        return isinstance(value,list) and len(value)==len(expected)             and all(exact(a,b) for a,b in zip(value,expected))
    blocks = (2*(1<<level), 1<<level); count = 512*(4**level)
    binding = record["native_binding"]; identity = record["source_identity"]
    leaves = record["source"]["leaves"]; cells = record["candidate_field"]["cell_values"]
    require(len(leaves)==count and len(cells)==count, "wrong complete cell count")
    require(coordinates(record["root_bounds"],[0.,1.,-.5,.5])
            and coordinates(binding["origin"],[0.,-.5,0.])
            and coordinates(binding["root_upper"],[1.,.5,1.]), "root coordinates differ")
    require(integers(binding["root_cells"],[16*blocks[0],16*blocks[1],1])
            and binding["periodic"] == [False,False,False]
            and all(type(v) is bool for v in binding["periodic"]), "root cells/periodicity differ")
    require(exact(identity["G"],float(CGS_G)) and exact(identity["time"],0.), "CGS G/time differ")
    service=record["service_configuration"]
    # Actual private producer uses its real copied type='none'; it directly
    # exercises SelfGravity without forging a public gravity-type dispatch.
    require(service["type"]=="none" and service["boundary"]=="isolated"
            and exact(service["relative_tolerance"],1e-10)
            and exact(service["absolute_tolerance"],0.)
            and type(service["max_cycles"]) is int and service["max_cycles"]==200
            and all(exact(service[k],0.) for k in ("g_x","g_y","g_z")), "actual service configuration differs")
    patches=binding["patches"]; inputs=identity["inputs"]
    require(len(patches)==blocks[0]*blocks[1] and len(inputs)==len(patches), "patch/dependency count differs")
    seen_blocks=set(); seen_uids=set(); layouts=[]
    for patch,dependency in zip(patches,inputs):
        budget.check_time(); root=patch["bound_root_identity"]; layout=patch["layout"]; native=patch["native_layout"]
        logical=root["logical_block"]
        require(isinstance(logical,list) and len(logical)==2
                and all(type(v) is int for v in logical)
                and 0<=logical[0]<blocks[0] and 0<=logical[1]<blocks[1]
                and tuple(logical) not in seen_blocks, "invalid/duplicate actual root block")
        seen_blocks.add(tuple(logical))
        require(integers(root["root_blocks"],list(blocks)) and type(root["level"]) is int
                and root["level"]==0 and root["periodic_axial"] is False
                and coordinates(root["root_lower"],[0.,-.5])
                and coordinates(root["root_upper"],[1.,.5]), "bound root/level differs")
        require(type(dependency["uid"]) is int and dependency["uid"]>0 and dependency["uid"] not in seen_uids
                and type(dependency["epoch"]) is int and dependency["epoch"]>0
                and type(dependency["slot"]) is int and dependency["slot"]==0
                and type(dependency["version"]) is int and dependency["version"]==1
                and type(dependency["storage_generation"]) is int and dependency["storage_generation"]>0,
                "invalid/duplicate Current input identity")
        seen_uids.add(dependency["uid"])
        begin=layout["active_begin"]; end=layout["active_end"]; stride=layout["stride"]; extent=layout["extent"]
        require(all(isinstance(a,list) and len(a)==3 and all(type(v) is int for v in a)
                    for a in (begin,end,stride,extent)), "invalid actual layout arrays")
        require(layout["dimension"]==2 and layout["centering"]==0 and native["dimension"]==2
                and type(native["ng"]) is int and native["ng"]==4
                and begin==[4,4,0] and end==[20,20,1]
                and extent[0]>=24 and extent[1:]==[24,1]
                and stride==[1,extent[0],extent[0]*extent[1]]
                and native["stride_y"]==stride[1] and native["stride_z"]==stride[2]
                and native["total_size"]==stride[2], "real active/stride layout differs")
        lower=native["origin"]; upper=native["actual_block_upper"]
        require(isinstance(lower,list) and len(lower)==3 and all(type(v) in (int,float) and math.isfinite(v) for v in lower)
                and isinstance(upper,list) and len(upper)==2 and all(type(v) in (int,float) and math.isfinite(v) for v in upper)
                and 0<=lower[0]<upper[0]<=1 and -.5<=lower[1]<upper[1]<=.5 and exact(lower[2],0.),
                "actual patch bounds differ")
        layouts.append((logical,begin,stride,lower,upper))
    per_patch=[set() for _ in patches]
    for leaf in leaves:
        budget.check_time(); block=leaf["binding_block_index"]; logical=leaf["logical_index"]
        require(type(leaf["level"]) is int and leaf["level"]==0 and exact(leaf["density"],1.), "source level/density differs")
        require(isinstance(logical,list) and len(logical)==3 and all(type(v) is int for v in logical)
                and logical[2]==0, "invalid actual logical cell")
        origin,begin,stride,lower,upper=layouts[block]
        i=logical[0]-16*origin[0]; j=logical[1]-16*origin[1]
        require(0<=i<16 and 0<=j<16 and (i,j) not in per_patch[block], "missing/duplicate active logical cell")
        require(leaf["source_offset"]==(begin[0]+i)*stride[0]+(begin[1]+j)*stride[1]+begin[2]*stride[2],
                "source offset does not match actual active logical layout")
        require(lower[0]<=leaf["r_lower"]<leaf["r_upper"]<=upper[0]
                and lower[1]<=leaf["z_lower"]<leaf["z_upper"]<=upper[1], "source outside actual patch bounds")
        per_patch[block].add((i,j))
    require(all(len(cells)==256 for cells in per_patch), "incomplete actual patch active cover")
    return dict(level=level, expected_cells=count, root_blocks=list(blocks),
                root_cells=binding["root_cells"], scope="Fixed supplied schema; outer Runtime authentication required",
                batch_limits=dict(wall_seconds=2400., enforcement="External serial campaign owner; no allowance transfer"))


def audit_materialized_full_record(record, dependency_directory=None, _shared_budget=None):
    """Opt-in complete actual-field diagnostic, distinct from the unchanged subset.

    The default manager-frozen 240s/1800000-callback request, or an explicitly
    fixed matched-layout request, begins before complete schema,
    dense/source validation, exact source union and symmetry scheduling. It stays
    shared across every site/source/component and all final target mappings.
    Every original target and all three reference components remain in the local
    result. None of the field's unqualified metadata is promoted to public science.
    """
    from rz_ring_surface_reference import (Budget, CGS_G, WorkLimit, rational,
        load_optional_flint, evaluate_reference)
    budget = Budget.full_domain_diagnostic() if _shared_budget is None else _shared_budget
    resources = full_materialized_resource_profile(budget)
    widths = {key: str(CGS_G/Fraction(10**12)) for key in ("Phi", "g_r", "g_z")}
    result = dict(profile=resources["profile"],
        status="FULL_DOMAIN_UNVERIFIED_DIAGNOSTIC", science_accepted=False,
        physical_qualified=False, core_binding_qualified=False, reference_complete=False,
        coverage_complete=False, target_widths_exact=widths, targets=[], sites=[],
        resource_authority="Manager-frozen distinct full diagnostic request; not a user hard budget",
        resource_limits=resources["resource_limits"])
    schedule = None
    try:
        budget.check_time()
        validated = validate_materialized_record(record, budget)
        leaves, cells, faces, by_cell, by_face = validated
        if budget.matched_resolution_level:
            result["matched_resolution"] = validate_matched_materialized_profile(record,budget)
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


def _reuse_canonical_sha(value):
    """Digest the complete JSON value; not a Runtime or interval authority."""
    return hashlib.sha256(json.dumps(value, sort_keys=True, allow_nan=False,
        separators=(",", ":")).encode()).hexdigest()


def _reuse_exact_wire(value):
    """Compare finite numeric values as both rationals and stored binary64 bits."""
    from rz_ring_surface_reference import rational
    if type(value) in (int, float):
        if not math.isfinite(value): raise ValueError("Nonfinite exact reuse input")
        return ("number", rational(value), struct.pack(">d", float(value)).hex())
    if isinstance(value, list): return tuple(_reuse_exact_wire(item) for item in value)
    if isinstance(value, dict): return tuple((key, _reuse_exact_wire(value[key])) for key in sorted(value))
    return (type(value).__name__, value)


def _reuse_require(condition, message):
    """Reject incomplete provenance before reusing any mathematical endpoint."""
    if not condition: raise ValueError(message)


def _reuse_interval(interval, maximum_width, *, require_width=True):
    """Validate rational bounds; derive the original site's exact width.

    Original integral site rows contain lower/upper/ball. Their enclosing width
    is hi-lo in exact rational arithmetic; ball text is not a scalar certificate.
    A supplied site width must still match, while mapped target rows always
    require their original explicit width field. Neither path widens the budget.
    """
    lo, hi = (Fraction(interval[key]) for key in ("lower_rational", "upper_rational"))
    width = hi-lo
    _reuse_require(not require_width or "width_rational" in interval,
                   "Missing required target interval width")
    _reuse_require(lo <= hi and width <= maximum_width
        and ("width_rational" not in interval or Fraction(interval["width_rational"]) == width),
        "Missing/reversed/inconsistent/too-wide certified interval")
    return dict(lower_rational=str(lo), upper_rational=str(hi), width_rational=str(width))


def _reuse_validate_reference(old, reference, validated, combined, raw_sha, budget):
    """Bind EVERY imported site/target to the old actual source and exact schedule.

    This checks the complete previously accepted artifact, not an aggregate or
    a PASS string. The outer owner must retain its accepted full-file hash and
    producer receipt: structural checks cannot independently prove an arbitrary
    caller's interval mathematics or authenticate JSON as a live Runtime.
    """
    from rz_ring_surface_reference import Budget, CGS_G, rational
    # This descriptor selects only existing immutable resource policies. Its
    # preflight checks borrow the SAME mapping deadline: no old request restart,
    # new integration budget, allowance transfer or callback charge is made.
    policies = {"actual-materialized-full-domain-diagnostic-1": dict(_full_domain_diagnostic=True),
                "actual-materialized-matched-resolution-1": dict(_matched_resolution=1),
                "actual-materialized-matched-resolution-2": dict(_matched_resolution=2)}
    _reuse_require(reference.get("profile") in policies, "Unknown original full reference resource profile")
    class ImportedProfileBudget(Budget):
        def check_time(self):
            """Keep fixed-layout validation under the actual mapping deadline."""
            budget.check_time()
    original_policy = ImportedProfileBudget(started=budget.started, **policies[reference["profile"]])
    resources = full_materialized_resource_profile(original_policy)
    _reuse_require(reference.get("resource_limits") == resources["resource_limits"],
                   "Original full reference resource limits mismatch")
    if original_policy.matched_resolution_level:
        expected_matched = validate_matched_materialized_profile(old, original_policy)
        _reuse_require(reference.get("matched_resolution") == expected_matched,
                       "Original matched reference layout metadata mismatch")
    else:
        _reuse_require(reference.get("matched_resolution") is None,
                       "Default reference carries conflicting matched layout metadata")
    _, cells, faces, by_cell, by_face = validated
    schedule = materialized_full_schedule(old, cells, faces, by_cell, by_face, budget)
    widths = {key: str(CGS_G/Fraction(10**12)) for key in ("Phi", "g_r", "g_z")}
    _reuse_require(reference.get("profile") == resources["profile"]
        and reference.get("reference_complete") is True and reference.get("coverage_complete") is True
        and reference.get("status") == "FULL_DOMAIN_MATHEMATICAL_REFERENCE_DIAGNOSTIC_ONLY"
        and reference.get("reference_status") == "MathematicalIntervalsCertified"
        and reference.get("failure") is None, "Original complete reference is unverified")
    _reuse_require(all(reference.get(key) is False for key in
        ("science_accepted", "physical_qualified", "core_binding_qualified")), "Reference imported a scientific grant")
    _reuse_require(reference.get("materializedRecordSha256") == raw_sha
        and reference.get("actual_record_canonical_sha256") == _reuse_canonical_sha(old)
        and reference.get("original_source_input_sha256") == combined["original_dense_input_sha256"],
        "Original raw/canonical/source input identity mismatch")
    _reuse_require(reference.get("original_source_id") == old["source"]["sourceId"]
        and reference.get("backend",{}).get("ctx_dps") == 70
        and isinstance(reference.get("backend",{}).get("python_flint_version"), str),
        "Original source label/backend provenance mismatch")
    producer=dict(source_only_checked=old["source_only_checked"],field_call=old["field_call"],service_configuration=old["service_configuration"])
    _reuse_require(reference.get("producer_identity") == producer, "Original producer/configuration provenance mismatch")
    _reuse_require(reference.get("source_identity") == old["source_identity"]
        and reference.get("field_identity") == dict(source_generation=old["candidate_field"]["source_generation"],
            field_generation=old["candidate_field"]["field_generation"])
        and reference.get("exact_union") == combined, "Original source/field/exact-union provenance mismatch")
    _reuse_require(reference.get("target_widths_exact") == widths
        and reference.get("target_mapping") == schedule["targets"]
        and reference.get("target_count") == len(schedule["targets"])
        and reference.get("unique_site_count") == len(schedule["sites"])
        and reference.get("exact_z_reflection") is schedule["exact_z_reflection"]
        and reference.get("symmetry_midpoint_exact") == schedule["symmetry_midpoint_exact"],
        "Original complete observer/width/reflection identity mismatch")
    reference_input = dict(source=combined["source"], root_bounds=old["root_bounds"],
                           source_identity=old["source_identity"], observers=schedule["sites"])
    identity = reference["reference_identity"]
    _reuse_require(identity.get("input") == reference_input
        and identity.get("sha256") == _reuse_canonical_sha(reference_input)
        and identity.get("core_binding_qualified") is False,
        "Original reference input digest/observer identity mismatch")
    history = reference["budget"]
    _reuse_require(type(history.get("calls")) is int and 0 < history["calls"] <= original_policy.max_calls
        and type(history.get("max_calls")) is int and history["max_calls"] == original_policy.max_calls
        and type(history.get("timeout_seconds")) in (int, float)
        and history["timeout_seconds"] == original_policy.timeout_seconds
        and type(history.get("wall_seconds")) in (int, float)
        and math.isfinite(history["wall_seconds"])
        and 0 <= history["wall_seconds"] < original_policy.timeout_seconds
        and history.get("resource_profile") == original_policy.resource_profile,
        "Invalid original reference work history")
    site_rows = reference["sites"]
    _reuse_require(isinstance(site_rows, list) and len(site_rows) == len(schedule["sites"]),
                   "Incomplete original reference sites")
    sites = {}
    for actual, row in zip(schedule["sites"], site_rows):
        budget.check_time()
        _reuse_require(row.get("observer_id") == actual["id"] and actual["id"] not in sites
            and row.get("math_certificate_meets_target") is True
            and row.get("observer") == dict(R_exact=str(rational(actual["r_observer"])),
                                             Z_exact=str(rational(actual["z_observer"]))),
            "Original site certificate/coordinate identity mismatch")
        sites[actual["id"]] = {key: _reuse_interval(row["intervals"][key], Fraction(widths[key]), require_width=False)
                                for key in widths}
    target_rows = reference["targets"]
    _reuse_require(isinstance(target_rows, list) and len(target_rows) == len(schedule["targets"]),
                   "Incomplete original target interval coverage")
    verified = []
    for expected, row in zip(schedule["targets"], target_rows):
        budget.check_time()
        _reuse_require(all(row.get(key) == value for key, value in expected.items())
            and row.get("science_accepted") is False
            and row.get("math_interval_meets_original_width") is True
            and row.get("reference_status") == "MathematicalIntervalsCertified", "Original target is not certified")
        intervals = reflected_reference_intervals(sites[expected["site_id"]], expected["g_z_sign"])
        actual = {key: _reuse_interval(row["intervals"][key], Fraction(widths[key])) for key in widths}
        _reuse_require(actual == intervals, "Original target interval does not match certified site reflection")
        verified.append((expected, intervals))
    return schedule, verified


def _reuse_join_sources(old, new, old_values, new_values, budget):
    """Require identical true physical input/geometry; keep execution stamps apart."""
    _reuse_require(_reuse_exact_wire(old["root_bounds"]) == _reuse_exact_wire(new["root_bounds"])
        and _reuse_exact_wire(old["source_identity"]["G"]) == _reuse_exact_wire(new["source_identity"]["G"])
        and _reuse_exact_wire(old["native_binding"]) == _reuse_exact_wire(new["native_binding"]),
        "Actual root/G/native-binding changed")
    _reuse_require(_reuse_exact_wire(old["source"]["leaves"]) == _reuse_exact_wire(new["source"]["leaves"]),
                   "Actual source bounds/density/storage changed")
    _reuse_require(_reuse_exact_wire(old["observers"]) == _reuse_exact_wire(new["observers"]),
                   "Actual observer geometry changed")
    old_faces, new_faces = old_values[2], new_values[2]
    geometry_keys = ("face_index", "axis", "left", "right", "boundary_side", "construction", "native_bounds",
                     "area", "center", "fragment_lower", "fragment_upper", "fragment_width")
    _reuse_require(len(old_faces) == len(new_faces), "Actual face count changed")
    for before, after in zip(old_faces, new_faces):
        budget.check_time()
        _reuse_require(_reuse_exact_wire({key: before[key] for key in geometry_keys})
            == _reuse_exact_wire({key: after[key] for key in geometry_keys}), "Actual face incidence/measure changed")


def _reuse_acceleration_rows(record, face_intervals, budget):
    """Reference the real A-weighted fragment-center gather, then the half-sum.

    Exact rational A/sum(A) retains negative intervals and axis-zero rows. The
    actual FP64 gather performs rounded accumulation/division; its difference
    is recorded, never assumed zero. This is neither a continuous face average
    nor a volume-mean/cell-center force identity.
    """
    from rz_ring_surface_reference import rational
    field = record["candidate_field"]; cells = field["cell_values"]; faces = field["face_values"]
    rows = [[] for _ in range(6*len(cells))]
    for i, face in enumerate(faces):
        budget.check_time()
        area = rational(face["area"])
        for cell in (face["left"], face["right"]):
            if cell < 0: continue
            side = 1 if cell == face["left"] else 0
            leaf = record["source"]["leaves"][cell]
            lower_key, upper_key = ("r_lower", "r_upper") if face["axis"] == 0 else ("z_lower", "z_upper")
            _reuse_require(rational(face["center"][face["axis"]]) == rational(leaf[upper_key if side else lower_key]),
                           "Face is not on its actual cell side")
            rows[6*cell+2*face["axis"]+side].append((i, area))
    sides, comparisons = [], []
    for i, entries in enumerate(rows):
        budget.check_time()
        axis, side, cell = (i%6)//2, i%2, i//6
        if axis == 2:
            _reuse_require(not entries, "Inactive axis has a face")
            lo = hi = Fraction(0)
        elif not entries:
            # The authentic elliptic mesh omits zero-measure r=0 faces.
            # Its gather leaves this lower radial side at symmetry zero; no
            # other missing active side is a complete geometric observation.
            _reuse_require(axis == 0 and side == 0
                and rational(record["root_bounds"][0]) == 0
                and rational(record["source"]["leaves"][cell]["r_lower"]) == 0,
                "Missing active cell-side face coverage")
            lo = hi = Fraction(0)
        else:
            _reuse_require(bool(entries), "Missing active cell-side face coverage")
            total = sum((area for _, area in entries), Fraction(0))
            if total == 0:
                _reuse_require(axis == 0 and side == 0
                    and rational(record["source"]["leaves"][cell]["r_lower"]) == 0
                    and all(faces[f]["boundary_side"] == 0 and rational(faces[f]["center"][0]) == 0
                            and Fraction(face_intervals[f]["lower_rational"]) <= 0
                            <= Fraction(face_intervals[f]["upper_rational"]) for f, _ in entries),
                    "Zero-area side is not the actual symmetry axis")
                lo = hi = Fraction(0)
            else:
                lo = sum((area*Fraction(face_intervals[f]["lower_rational"]) for f, area in entries), Fraction(0))/total
                hi = sum((area*Fraction(face_intervals[f]["upper_rational"]) for f, area in entries), Fraction(0))/total
        interval = dict(lower_rational=str(lo), upper_rational=str(hi), width_rational=str(hi-lo))
        sides.append(interval)
        error = full_interval_error(field["side_acceleration"][i], interval)
        error["actual_binary64_hex"] = struct.pack(">d", float(field["side_acceleration"][i])).hex()
        comparisons.append(dict(cell_index=cell, axis=axis, side=side, interval=interval, error=error))
    cell_comparisons = []
    for cell, value in enumerate(cells):
        for axis in range(3):
            budget.check_time()
            lower, upper = sides[6*cell+2*axis:6*cell+2*axis+2]
            lo = (Fraction(lower["lower_rational"])+Fraction(upper["lower_rational"]))/2
            hi = (Fraction(lower["upper_rational"])+Fraction(upper["upper_rational"]))/2
            interval = dict(lower_rational=str(lo), upper_rational=str(hi), width_rational=str(hi-lo))
            error = full_interval_error(value["acceleration"][axis], interval)
            error["actual_binary64_hex"] = struct.pack(">d", float(value["acceleration"][axis])).hex()
            cell_comparisons.append(dict(cell_index=cell, axis=axis, interval=interval, error=error))
    return comparisons, cell_comparisons


def reuse_materialized_full_reference(record, old_record, reference, *, new_raw_sha256,
        old_raw_sha256, reference_raw_sha256, expected_reference_sha256, _shared_budget=None):
    """Recompare a fresh authenticated field with the complete accepted reference.

    Workflow: validate BOTH actual records and complete dense source unions;
    verify the old reference's raw/canonical/input/site/target provenance; exact
    physical source/geometry join; re-evaluate only new stored field rows; map
    all side/cell force enclosures. No flint, kernel, new integral budget or
    production accuracy gate is invoked. Runtime/file authority stays outside.
    """
    from rz_ring_surface_reference import Budget, CGS_G, WorkLimit, rational
    budget = Budget.full_domain_diagnostic() if _shared_budget is None else _shared_budget
    if not isinstance(budget, Budget) or budget.timeout_seconds != 240. or budget.calls != 0:
        raise ValueError("Pure mapping requires its one unused 240s budget")
    result = dict(profile="actual-materialized-full-reference-reuse-1", status="REFERENCE_REUSE_UNVERIFIED",
        science_accepted=False, physical_qualified=False, core_binding_qualified=False,
        reference_complete=False, coverage_complete=False, reference_status="UNVERIFIED", targets=[],
        target_widths_exact={key: str(CGS_G/Fraction(10**12)) for key in ("Phi", "g_r", "g_z")},
        materializedRecordSha256=new_raw_sha256, reference_materialized_record_sha256=old_raw_sha256,
        reused_reference_raw_sha256=reference_raw_sha256, failure=None)
    try:
        budget.check_time()
        for digest in (new_raw_sha256, old_raw_sha256, reference_raw_sha256, expected_reference_sha256):
            _reuse_require(isinstance(digest, str) and len(digest) == 64
                and all(c in "0123456789abcdef" for c in digest), "Missing actual input-file SHA")
        _reuse_require(reference_raw_sha256 == expected_reference_sha256,
            "Imported reference differs from the externally accepted full-file SHA")
        result["externally_accepted_reference_sha256"] = expected_reference_sha256
        old_values = validate_materialized_record(old_record, budget)
        new_values = validate_materialized_record(record, budget)
        old_union = coalesce_exact_dense_source(old_record["source"], old_record["root_bounds"], old_record["source_identity"], budget)
        new_union = coalesce_exact_dense_source(record["source"], record["root_bounds"], record["source_identity"], budget)
        schedule, verified = _reuse_validate_reference(old_record, reference, old_values, old_union, old_raw_sha256, budget)
        _reuse_join_sources(old_record, record, old_values, new_values, budget)
        _, cells, faces, _, _ = new_values
        normal_intervals = {}
        for target, intervals in verified:
            budget.check_time()
            i = target["actual_index"]
            row = dict(target, science_accepted=False, math_interval_meets_original_width=True,
                reference_status="MathematicalIntervalsCertified", intervals=copy.deepcopy(intervals), comparisons={})
            if target["kind"] == "cell":
                row["comparisons"]["point_cell_potential"] = full_interval_error(cells[i]["potential"], intervals["Phi"])
            else:
                face = faces[i]; component = "g_r" if face["axis"] == 0 else "g_z"
                normal_intervals[i] = intervals[component]
                row["comparisons"]["force_minus_original_gradient"] = full_interval_error(-rational(face["gradient"]), intervals[component])
                derived = sum((rational(cells[j]["potential"])*rational(c)
                    for j, c in zip(face["value_samples"], face["value_coefficients"])), Fraction(0))
                derived += rational(face["value_boundary_coefficient"])*rational(face["boundary_datum"])
                row["comparisons"]["derived_original_value_row_vs_point_potential"] = full_interval_error(derived, intervals["Phi"])
                if face["boundary_side"] >= 0 and not face["value_samples"] and rational(face["value_boundary_coefficient"]) == 1:
                    row["comparisons"]["actual_boundary_datum_potential"] = full_interval_error(face["boundary_datum"], intervals["Phi"])
            result["targets"].append(row)
        sides, cell_g = _reuse_acceleration_rows(record, normal_intervals, budget)
        budget.check_time()
        result.update(status="COMPLETE_REFERENCE_REUSE_DIAGNOSTIC_ONLY", reference_status="MathematicalIntervalsCertified",
            reference_complete=True, coverage_complete=True, target_count=len(verified), unique_site_count=len(schedule["sites"]),
            exact_z_reflection=schedule["exact_z_reflection"], source_identity=copy.deepcopy(record["source_identity"]),
            reference_source_identity=copy.deepcopy(old_record["source_identity"]),
            field_identity=dict(source_generation=record["candidate_field"]["source_generation"], field_generation=record["candidate_field"]["field_generation"]),
            reference_field_identity=copy.deepcopy(reference["field_identity"]),
            actual_record_canonical_sha256=_reuse_canonical_sha(record), original_source_input_sha256=new_union["original_dense_input_sha256"],
            reference_original_source_input_sha256=old_union["original_dense_input_sha256"],
            producer_identity=dict(source_only_checked=record["source_only_checked"],field_call=copy.deepcopy(record["field_call"]),service_configuration=copy.deepcopy(record["service_configuration"])),
            reference_producer_identity=copy.deepcopy(reference["producer_identity"]),
            reference_history=copy.deepcopy(reference["budget"]), side_acceleration_rows=sides, cell_acceleration_rows=cell_g,
            exact_join_proof="Both complete actual records/dense unions; old full site/target certificate provenance; exact source bounds/density rational+FP64 bits/G/root/native binding/face geometry/observers; new fields only")
    except (ValueError, TypeError, KeyError, AttributeError, ArithmeticError, WorkLimit) as exc:
        result.update(failure=str(exc), failure_type=type(exc).__name__, reference_status="WorkLimit" if isinstance(exc, WorkLimit) else "UNVERIFIED")
    result["mapping_budget"] = budget.record()
    result["budget"] = dict(result["mapping_budget"], resource_profile="pure-exact-reference-mapping-1", kernel_evaluations=0)
    result["limitations"] = ["Only previously accepted mathematical intervals are reused; imported bytes are not self-authenticating",
        "New/old Runtime producer, source and field stamps stay separate; old field comparisons/grants are never reused",
        "Fragment-center area-weighted reference and cell half-sum are not continuous surface/volume averages",
        "Rounded production gather normalization contributes to the recorded FP64 error; no zero-arithmetic-error assumption",
        "All public/native/Device/scientific acceptance gates remain with their actual owners"]
    return result


def _reuse_full_reference_cli(arguments, summary_output):
    """Read three immutable local artifacts once and run no optional backend."""
    from rz_ring_surface_reference import Budget, WorkLimit
    budget = Budget.full_domain_diagnostic()
    snapshots = []
    def read(path):
        budget.check_time()
        stat = path.stat(); raw = path.read_bytes(); after = path.stat()
        identity = lambda x: (x.st_dev, x.st_ino, x.st_size, x.st_mtime_ns, x.st_ctime_ns)
        _reuse_require(identity(stat) == identity(after), "Input changed during pure reference mapping read")
        value = json.loads(raw, parse_constant=lambda token: (_ for _ in ()).throw(ValueError("Nonfinite JSON: "+token)),
            parse_int=lambda token: -0.0 if token == "-0" else int(token))
        snapshots.append((path, identity(after), hashlib.sha256(raw).hexdigest()))
        return value, snapshots[-1][2]
    try:
        new, new_sha = read(arguments.materialized_record)
        old, old_sha = read(arguments.reference_materialized_record)
        reference, reference_sha = read(arguments.reuse_full_reference)
        result = reuse_materialized_full_reference(new, old, reference, new_raw_sha256=new_sha,
            old_raw_sha256=old_sha, reference_raw_sha256=reference_sha,
            expected_reference_sha256=arguments.reuse_full_reference_sha256, _shared_budget=budget)
        for path, identity, sha in snapshots:
            budget.check_time(); stat = path.stat()
            _reuse_require((stat.st_dev,stat.st_ino,stat.st_size,stat.st_mtime_ns,stat.st_ctime_ns) == identity
                and hashlib.sha256(path.read_bytes()).hexdigest() == sha, "Input changed during exact reference mapping")
        result["read_only_input_identities"] = [dict(path=str(path),device=identity[0],inode=identity[1],size=identity[2],mtime_ns=identity[3],ctime_ns=identity[4],sha256=sha) for path,identity,sha in snapshots]
    except (OSError, ValueError, TypeError, KeyError, AttributeError, ArithmeticError, WorkLimit) as exc:
        result = dict(profile="actual-materialized-full-reference-reuse-1", status="REFERENCE_REUSE_UNVERIFIED",
            science_accepted=False, physical_qualified=False, core_binding_qualified=False,
            reference_complete=False, coverage_complete=False, targets=[], failure=str(exc), failure_type=type(exc).__name__,
            reference_status="WorkLimit" if isinstance(exc, WorkLimit) else "UNVERIFIED", mapping_budget=budget.record(), budget=budget.record())
        from rz_ring_surface_reference import CGS_G
        result["target_widths_exact"] = {key: str(CGS_G/Fraction(10**12)) for key in ("Phi", "g_r", "g_z")}
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    summary = full_materialized_summary(result)
    summary.update(reference_materialized_record_sha256=result.get("reference_materialized_record_sha256"),
        materializedRecordSha256=result.get("materializedRecordSha256"), reused_reference_raw_sha256=result.get("reused_reference_raw_sha256"),
        reference_history=result.get("reference_history"), mapping_budget=result.get("mapping_budget"),
        side_acceleration_count=len(result.get("side_acceleration_rows",[])), cell_acceleration_count=len(result.get("cell_acceleration_rows",[])),
        externally_accepted_reference_sha256=result.get("externally_accepted_reference_sha256"),
        completeLocalResultSha256=hashlib.sha256(arguments.output.read_bytes()).hexdigest())
    summary["weighted_acceleration_maximum_absolute_error_upper_exact"] = {
        name:str(max((Fraction(row["error"]["absolute_error_upper_exact"]) for row in result.get(name,[])),default=Fraction(0)))
        for name in ("side_acceleration_rows","cell_acceleration_rows")}
    summary_output.parent.mkdir(parents=True, exist_ok=True)
    summary_output.write_text(json.dumps(summary, indent=2, allow_nan=False)+"\n")
    print("ACTUAL_MATERIALIZED_REFERENCE_REUSE_DIAGNOSTIC", result["reference_status"], len(result["targets"]))


def write_matched_materialized_outputs(result, output, summary_output, budget):
    """Charge full matched output/summary IO to the original request's clock.

    Timeout never preserves a successful whole-request status. Final failure
    evidence may still be written after exhaustion; the external batch/process
    guard is the hard bound for uninterruptible Python/backend/OS operations.
    All original targets and interval columns remain in the complete local file.
    """
    from rz_ring_surface_reference import WorkLimit
    phase="complete-output-serialization"
    try:
        budget.check_time()
        result["budget"]=budget.record()
        text=json.dumps(result,indent=2,allow_nan=False)+"\n"
        budget.check_time(); output.parent.mkdir(parents=True,exist_ok=True)
        phase="complete-output-write"; output.write_text(text); budget.check_time()
        phase="summary-mapping"; summary=full_materialized_summary(result); budget.check_time()
        if "materializedRecordSha256" in result: summary["materializedRecordSha256"]=result["materializedRecordSha256"]
        phase="complete-output-identity"; summary["completeLocalResultSha256"]=hashlib.sha256(output.read_bytes()).hexdigest()
        budget.check_time(); summary["budget"]=budget.record()
        phase="summary-write"; summary_output.parent.mkdir(parents=True,exist_ok=True)
        summary_output.write_text(json.dumps(summary,indent=2,allow_nan=False)+"\n"); budget.check_time()
    except (OSError,ValueError,TypeError,ArithmeticError,WorkLimit) as exc:
        result.update(status="FULL_DOMAIN_UNVERIFIED_DIAGNOSTIC", reference_complete=False,
            coverage_complete=False, reference_status="WorkLimit" if isinstance(exc,WorkLimit) else "UNVERIFIED",
            failure=str(exc), failure_type=type(exc).__name__, failure_phase=phase, budget=budget.record())
        # Terminal evidence is never a second evaluation/request or a grant.
        output.parent.mkdir(parents=True,exist_ok=True)
        output.write_text(json.dumps(result,indent=2,allow_nan=False)+"\n")
        summary=full_materialized_summary(result)
        if "materializedRecordSha256" in result: summary["materializedRecordSha256"]=result["materializedRecordSha256"]
        summary.update(failure_phase=phase, failure_type=type(exc).__name__,
            completeLocalResultSha256=hashlib.sha256(output.read_bytes()).hexdigest())
        summary_output.parent.mkdir(parents=True,exist_ok=True)
        summary_output.write_text(json.dumps(summary,indent=2,allow_nan=False)+"\n")
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    records=p.add_mutually_exclusive_group(required=True)
    records.add_argument("--probe-record",type=Path)
    records.add_argument("--materialized-record",type=Path)
    p.add_argument("--reuse-full-reference",type=Path, help="Pure exact reuse of a complete accepted local reference; no integral backend")
    p.add_argument("--reference-materialized-record",type=Path, help="Original actual record bound to the imported reference")
    p.add_argument("--reuse-full-reference-sha256", help="Externally accepted reference full-file SHA; required provenance pin, not a physical parameter")
    p.add_argument("--dependency-directory",type=Path)
    p.add_argument("--output",required=True,type=Path)
    p.add_argument("--full-domain", action="store_true",
                   help="Opt-in complete materialized-field diagnostic; all targets remain local")
    p.add_argument("--reference-profile", choices=("matched-resolution-1","matched-resolution-2"),
                   help="Maintainer-only fixed actual 2048/8192-cell full reference; unchanged math/width")
    p.add_argument("--summary-output", type=Path,
                   help="Separate compact full-domain aggregate (default: output name + .summary.json)")
    p.add_argument("--order",type=int,default=16)
    p.add_argument("--precision",type=int,default=80)
    p.add_argument("--t-panels",type=int,default=1)
    p.add_argument("--observer-set", choices=("boundary", "cells"), default="boundary")
    a=p.parse_args()
    if a.output.exists():p.error("output must be new")
    if a.reference_profile is not None and (not a.full_domain or a.materialized_record is None):
        p.error("--reference-profile requires --materialized-record and --full-domain")
    if a.reference_profile is not None and any(item is not None for item in
            (a.reuse_full_reference,a.reference_materialized_record,a.reuse_full_reference_sha256)):
        p.error("fixed matched reference profile cannot be combined with reference reuse")
    reuse_inputs=(a.reuse_full_reference,a.reference_materialized_record,a.reuse_full_reference_sha256)
    if any(item is not None for item in reuse_inputs) and not all(item is not None for item in reuse_inputs):p.error("reuse requires complete reference, original materialized record and accepted full-file SHA")
    if a.reuse_full_reference is not None and (not a.full_domain or a.materialized_record is None):p.error("reuse requires --materialized-record and --full-domain")
    if a.reuse_full_reference_sha256 is not None and (len(a.reuse_full_reference_sha256)!=64 or any(c not in "0123456789abcdef" for c in a.reuse_full_reference_sha256)):p.error("accepted reference SHA must be 64 lower-case hex characters")
    if a.full_domain and a.materialized_record is None:p.error("--full-domain requires --materialized-record")
    if a.summary_output is not None and not a.full_domain:p.error("--summary-output requires --full-domain")
    if a.full_domain:
        summary_output = a.summary_output or a.output.with_name(a.output.name+".summary.json")
        if summary_output == a.output or summary_output.exists():p.error("summary output must be separate and new")
    if a.reuse_full_reference is not None:
        _reuse_full_reference_cli(a, summary_output)
        return
    if a.materialized_record is not None:
        def invalid_constant(value): raise ValueError("Nonfinite JSON constant: "+value)
        if a.full_domain:
            from rz_ring_surface_reference import Budget, CGS_G, WorkLimit, load_optional_flint
            # The outer CLI owns the one explicit precision freeze. Library
            # audits/evaluators only check it; none mutate the shared context.
            # Startup, input reading and auditing all charge this SAME budget.
            full_budget = (Budget.full_domain_diagnostic() if a.reference_profile is None else
                           Budget.matched_resolution(int(a.reference_profile.rsplit("-",1)[1])))
            resources = full_materialized_resource_profile(full_budget)
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
                result = dict(profile=resources["profile"],
                    status="FULL_DOMAIN_UNVERIFIED_DIAGNOSTIC", science_accepted=False,
                    physical_qualified=False, core_binding_qualified=False,
                    reference_complete=False, coverage_complete=False, targets=[], sites=[],
                    reference_status="WorkLimit" if isinstance(exc, WorkLimit) else "UNVERIFIED",
                    failure=str(exc), failure_type=type(exc).__name__, failure_phase=phase,
                    input_path=str(a.materialized_record), input_read=raw is not None,
                    target_widths_exact={key: str(CGS_G/Fraction(10**12))
                                        for key in ("Phi", "g_r", "g_z")},
                    resource_authority="Manager-frozen distinct full diagnostic request; not a user hard budget",
                    resource_limits=resources["resource_limits"],
                    budget=full_budget.record())
            if raw is not None:
                result["materializedRecordSha256"]=hashlib.sha256(raw).hexdigest()
        else:
            raw=a.materialized_record.read_bytes()
            data=json.loads(raw,parse_constant=invalid_constant)
            result = audit_materialized_record(data,a.dependency_directory)
            result["materializedRecordSha256"]=hashlib.sha256(raw).hexdigest()
        if a.reference_profile is not None:
            write_matched_materialized_outputs(result,a.output,summary_output,full_budget)
            print("ACTUAL_MATERIALIZED_FULL_DOMAIN_DIAGNOSTIC",result["reference_status"],len(result["targets"]))
            return
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
