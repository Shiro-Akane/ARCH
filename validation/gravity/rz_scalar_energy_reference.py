"""Independent continuous energy reference for the owning Euler homology lane.

Use the actual before/post density means and original boundary mass fluxes.
Integrate the Newton kernel over full native volumes/faces, rather than treating
cell/face center potentials as volume/face averages. The initial density is
uniform, so the linear increment needs one background source. The small positive
quadratic increment has a rigorous density bound; no 160-squared pair sweep is
needed. Quadrature convergence uncertainty is an estimate, explicitly separate
from that bound and from the production Poisson residual certificate.
"""
import argparse
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np

from rz_matched_native_reference import coalesce_exact_dense_source
from rz_ring_surface_reference import rational


@lru_cache(maxsize=8)
def gauss_unit(degree):
    """Open Gauss nodes on [0,1]; contact endpoints are integrated, not dropped."""
    x, w = np.polynomial.legendre.leggauss(degree)
    return (x + 1.) * .5, w * .5


def axial_integral(s, observer_z, source_z):
    """Exact dz dZ reduction: F''(t)=1/sqrt(s²+t²), J is its mixed difference."""
    def F(t):
        return t * np.arcsinh(t / s) - np.hypot(s, t)
    a, b = observer_z
    c, d = source_z
    return F(b-c)-F(a-c)-F(b-d)+F(a-d)


def radial_pair(observer_r, source_r, kernel, degree):
    """Integrate 4π*r*R*kernel(s) dr dR dθ over two radial intervals.

    Source contains observer. Split at real observer bounds. On the coincident
    square, symmetry gives twice r=a+h*u, R=a+h*u*v, Jacobian h²*u.
    θ=π*q² softens the integrable logarithm, including self contact. The rR
    cylindrical measure, triangle doubling and full-azimuth 4π stay explicit.
    """
    a, b = observer_r
    c, d = source_r
    if not c <= a < b <= d:
        raise ValueError("Scalar reference radial observer lies outside source")
    x, w = gauss_unit(degree)
    q, v = gauss_unit(2*degree)
    theta = (math.pi*q*q)[None, None, :]
    angular_weight = (2*math.pi*q*v)[None, None, :]

    def evaluate(r, R, radial_weight):
        s = np.hypot(r-R, 2*np.sqrt(r*R)*np.sin(theta*.5))
        return float(np.sum(r*R*kernel(s)*radial_weight*angular_weight,
                            dtype=np.longdouble))

    r = (a+(b-a)*x)[:, None, None]
    total = 0.
    for lo, hi in ((c, a), (b, d)):
        if hi > lo:
            R = (lo+(hi-lo)*x)[None, :, None]
            weights = ((b-a)*(hi-lo)*w[:, None]*w[None, :])[:, :, None]
            total += evaluate(r, R, weights)
    R = a+(b-a)*x[:, None, None]*x[None, :, None]
    weights = (2*(b-a)**2*x[:, None]*w[:, None]*w[None, :])[:, :, None]
    total += evaluate(r, R, weights)
    return 4*math.pi*total


def volume_pair(observer, source, degree):
    """Full-ring I_AB=∫A∫B 1/|x-y| dVx dVy, density excluded."""
    return radial_pair(observer[:2], source[:2],
                       lambda s: axial_integral(s, observer[2:], source[2:]), degree)


def face_pair(face, source, length, degree):
    """Integrate ∫face∫D 1/|x-y| dVy dAx over the actual whole native face."""
    axis = face["axis"]
    lower = np.asarray(face["fragment_lower"], dtype=float)/length
    upper = np.asarray(face["fragment_upper"], dtype=float)/length
    if axis == 1:
        z = lower[1]
        def kernel(s):
            return np.arcsinh((source[3]-z)/s)-np.arcsinh((source[2]-z)/s)
        return radial_pair((lower[0], upper[0]), source[:2], kernel, degree)
    if axis != 0 or lower[0] != upper[0]:
        raise ValueError("Scalar reference requires a real radial/axial face")
    r = lower[0]
    c, d = source[:2]
    x, w = gauss_unit(degree)
    q, v = gauss_unit(2*degree)
    if r == c:
        R = c+(d-c)*x*x
    elif r == d:
        R = d-(d-c)*x*x
    else:
        raise ValueError("Physical radial boundary must lie on the source root")
    weights = 2*(d-c)*x*w
    theta = math.pi*q*q
    s = np.hypot(r-R[:, None],
                 2*np.sqrt(r*R[:, None])*np.sin(theta[None, :]*.5))
    J = axial_integral(s, (lower[1], upper[1]), source[2:])
    return float(4*math.pi*np.sum(r*R[:, None]*J*weights[:, None]
                *(2*math.pi*q*v)[None, :], dtype=np.longdouble))


def load(path):
    """Read finite numeric observations; file existence alone grants no authority."""
    def invalid(token):
        raise ValueError("Nonfinite scalar evidence: "+token)
    return json.loads(path.read_text(), parse_constant=invalid)


def collect(directory, degrees=(12, 24, 48)):
    """Compare one actual Euler step with an independent symmetric functional.

    ΔW=-G*ρ0*S-G*T/2, S=∫δρ(x)∫D K(x,y), with
    δmin²*I_DD <= T <= δmax²*I_DD. The source and numerical flux are unchanged.
    The finite budget is 1% of |Q|+|B_Phi_cont|; references use at most 1/10.
    """
    started = time.monotonic()
    before = load(directory/"before-materialized-native-source.json")
    after = load(directory/"post-materialized-native-source.json")
    energy = load(directory/"energy-diagnostic.json")
    fluxes = load(directory/"boundary-mass-flux.json")
    if (energy["method"] != "Euler" or fluxes["method"] != "Euler"
        or before["root_bounds"] != after["root_bounds"]
        or before["source_identity"]["G"] != after["source_identity"]["G"]
        or fluxes["time"] != before["source_identity"]["time"]
        or fluxes["source_generation"] != before["candidate_field"]["source_generation"]
        or fluxes["field_generation"] != before["candidate_field"]["field_generation"]):
        raise ValueError("Scalar energy observations differ from the actual owning step")
    old = {leaf["id"]: leaf for leaf in before["source"]["leaves"]}
    new = {leaf["id"]: leaf for leaf in after["source"]["leaves"]}
    if old.keys() != new.keys():
        raise ValueError("Scalar energy reference requires the original common partition")
    rho0 = rational(next(iter(old.values()))["density"])
    keys = ("r_lower", "r_upper", "z_lower", "z_upper")
    deltas = []
    for identity, a in old.items():
        b = new[identity]
        if rational(a["density"]) != rho0 or any(a[k] != b[k] for k in keys):
            raise ValueError("Scalar reference requires an exact uniform before source")
        deltas.append(rational(b["density"])-rho0)
    if min(deltas) < 0 or rho0 <= 0:
        raise ValueError("Positive contrast bound is inapplicable to this actual source")
    # Existing exact coverage/union validation; no nearly-equal density grouping.
    combined = coalesce_exact_dense_source(after["source"], after["root_bounds"],
                                            after["source_identity"])["source"]
    coalesce_exact_dense_source(before["source"], before["root_bounds"], before["source_identity"])
    length = 10000.
    domain = tuple(float(x)/length for x in before["root_bounds"])
    pieces = [(tuple(float(leaf[k])/length for k in keys), float(rational(leaf["density"])-rho0))
              for leaf in combined["leaves"]]
    G = before["source_identity"]["G"]
    r0, dmin, dmax = float(rho0), float(min(deltas)), float(max(deltas))
    faces = {f["face_index"]: f for f in before["candidate_field"]["face_values"]
             if f["boundary_side"] >= 0}
    observations = {f["face_index"]: f for f in fluxes["faces"]}
    if len(observations) != len(fluxes["faces"]) or faces.keys() != observations.keys():
        raise ValueError("Actual boundary mass-flux coverage mismatch")
    stored_terms = []
    for index, f in faces.items():
        row = observations[index]
        if (row["axis"] != f["axis"] or row["boundary_side"] != f["boundary_side"]
            or row["area"] != f["area"] or row["dt"] != energy["dt"]
            or row["stage_weight"] != 1.):
            raise ValueError("Boundary flux does not belong to the actual native face")
        sign = 1 if row["boundary_side"] % 2 else -1
        stored_terms.append(row["dt"]*row["stage_weight"]*sign*row["F_rho"]*row["area"]*row["stored_point_phi"])
    stored_BP = math.fsum(stored_terms)
    if abs(stored_BP-energy["B_Phi"]) > 1e-12*math.fsum(map(abs, stored_terms)):
        raise ValueError("Passive boundary observations disagree with original accounting")
    levels = []
    for degree in degrees:
        I = volume_pair(domain, domain, degree)*length**5
        S = math.fsum(delta*volume_pair(bounds, domain, degree)*length**5
                     for bounds, delta in pieces if delta)
        boundary = []
        for index, face in faces.items():
            row = observations[index]
            sign = 1 if row["boundary_side"] % 2 else -1
            integral_phi = -G*r0*face_pair(face, domain, length, degree)*length**4
            boundary.append(row["dt"]*row["stage_weight"]*sign*row["F_rho"]*integral_phi)
        levels.append(dict(degree=degree, I_DD=I, linear_delta_W=-G*r0*S,
                           W0=-.5*G*r0*r0*I, B_Phi=math.fsum(boundary),
                           absolute_boundary_terms=math.fsum(map(abs, boundary))))
    if not all(math.isfinite(value) for level in levels for value in level.values()):
        raise ValueError("Independent energy quadrature became nonfinite")
    coarse, medium, fine = levels
    # Deliberately reported as convergence ESTIMATES, not interval certificates.
    qI = 4*abs(fine["I_DD"]-medium["I_DD"])
    qL = 4*abs(fine["linear_delta_W"]-medium["linear_delta_W"])
    qB = 4*abs(fine["B_Phi"]-medium["B_Phi"])
    lower = -.5*G*dmax*dmax*(fine["I_DD"]+qI)
    upper = -.5*G*dmin*dmin*max(0., fine["I_DD"]-qI)
    quadratic = (lower+upper)*.5
    deltaW = fine["linear_delta_W"]+quadratic
    reference_error = qL+(upper-lower)*.5+qB
    scale = abs(energy["Q"])+abs(fine["B_Phi"])
    D = math.fsum([energy["DeltaE"], energy["B_E"], deltaW, fine["B_Phi"]])
    monotone = all(abs(fine[key]-medium[key]) < abs(medium[key]-coarse[key])
                   for key in ("I_DD", "linear_delta_W", "B_Phi"))
    uncertainty_pass = reference_error <= .001*scale and monotone
    physical_pass = (abs(D)+reference_error) <= .01*scale
    return dict(schema="arch-native-continuous-energy-reference-1", cells=len(old),
        same_endpoint=True, method="Euler", dt=energy["dt"], physical_time0=energy["time0"],
        physical_time1=energy["time1"], exact_coalesced_pieces=len(pieces),
        original_boundary_faces=len(faces), rho0=r0, delta_min_exact=str(min(deltas)),
        delta_max_exact=str(max(deltas)), levels=levels,
        quadratic_delta_W_bounds=[lower, upper], delta_W=deltaW, B_Phi=fine["B_Phi"],
        D_continuous=D, normalization=scale, normalized_balance=abs(D)/scale,
        normalized_reference_uncertainty=reference_error/scale,
        physical_budget=.01, reference_budget=.001, reference_uncertainty_pass=uncertainty_pass,
        quadrature_monotone=monotone, finite_balance_pass=physical_pass,
        status="PASS_FINITE_EULER_ENERGY" if uncertainty_pass and physical_pass else "NEEDS_REVIEW",
        qualification="one actual owning Euler endpoint only; no long-time/AMR/Device grant",
        uncertainty_kind="three-order numerical convergence estimate plus rigorous positive-density quadratic bound; not an outward quadrature enclosure",
        original_point_B_Phi=energy["B_Phi"], stored_B_Phi_reproduction_error=stored_BP-energy["B_Phi"],
        raw_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (
            directory/"before-materialized-native-source.json", directory/"post-materialized-native-source.json",
            directory/"energy-diagnostic.json", directory/"boundary-mass-flux.json")},
        wall_seconds=time.monotonic()-started)


def _closed_digest(value):
    """Require a bound SHA256; pending/unknown provenance is never acceptance."""
    if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
        raise ValueError("Closed-run identity is not a bound SHA256")
    return value


def validate_closed_contract(parameters, contract):
    """Workflow: bind the prospective contract to exact input bytes and scope."""
    import sys
    from rz_ring_surface_reference import CGS_G
    if contract["schema"] != "arch-rz-closed-uniform-long-contract-1" or contract["model"] != "GravityBox":
        raise ValueError("Unsupported closed-uniform contract")
    _closed_digest(contract["expected_binary_sha256"])
    digest = hashlib.sha256(parameters.read_bytes()).hexdigest()
    if digest != _closed_digest(contract["input"]["sha256"]):
        raise ValueError("Closed input differs from the prospective contract")
    for key in ("rho0", "G", "t_end", "physical_budget", "reference_budget", "mass_budget",
                "signal_ratio", "roundoff_observation_multiplier", "physical_endpoint_time_relative_budget"):
        value = contract[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            raise ValueError("Missing/nonpositive closed contract control: "+key)
    if (rational(contract["G"]) != CGS_G or contract["quadrature_degrees"] != [12,24,48]
        or contract["zero_repairs"] is not True or contract["roundoff_observation_multiplier"] != 8
        or not 0 < contract["reference_budget"] < contract["physical_budget"] < 1
        or not 0 < contract["mass_budget"] < 1 or contract["signal_ratio"] < 1
        or contract["physical_endpoint_time_relative_budget"] >= 1):
        raise ValueError("Invalid closed reference budgets/scope")
    root = list(map(rational, contract["root_bounds"]))
    if len(root) != 4 or not (root[0] == 0 < root[1] and root[2] < root[3]):
        raise ValueError("Closed reference needs the actual axis/root")
    tools_path = str(Path(__file__).resolve().parents[2]/"tools")
    if tools_path not in sys.path: sys.path.insert(0,tools_path)
    from validate_backend_results import read_parameter_map
    values = read_parameter_map(parameters)
    required = dict(geometry="cylindrical", compute_backend="cpu", gravity_type="self", gravity_boundary="isolated",
        eos_type="ideal", solver="HLLC", reconstruct="muscl", limiter="mc", time_integrator="RK2",
        use_burn="false", use_diffusion="false", network_name="none", hydrostatic_radial="false")
    required.update({"x"+str(a)+side+"_boundary_type":"reflecting" for a in (1,2,3) for side in ("l","r")})
    if any(values[key] != expected for key, expected in required.items()):
        raise ValueError("Closed-uniform reference scope differs from actual parameters")
    numeric = dict(nblockx1=1, nblockx2=1, nblockx3=0, lrefinemin=0, lrefinemax=0, max_steps=-1,
        amplitude=0, temperature_amplitude=0, velocity0=0, rho0=contract["rho0"], tmax=contract["t_end"],
        x1_min=contract["root_bounds"][0], x1_max=contract["root_bounds"][1],
        x2_min=contract["root_bounds"][2], x2_max=contract["root_bounds"][3],
        temperature0=1e4, gas_cv=1.2471693927e8, gamma=5/3, cfl=.3, dt_max=-1)
    if any(float(values[key]) != expected for key, expected in numeric.items()):
        raise ValueError("Closed-uniform physical parameters changed")
    return digest


def _closed_partition(records, root_bounds):
    """Reuse exact mathematical coverage without fabricating Runtime stamps."""
    from rz_ring_surface_reference import Leaf, _validate_nonoverlapping_rectangles
    leaves = tuple(Leaf.from_record(record) for record in records)
    L,H,A,B = map(rational, root_bounds)
    if not leaves or len({x.identity for x in leaves}) != len(leaves) or any(
        not (L <= x.L < x.H <= H and A <= x.A < x.B <= B) for x in leaves):
        raise ValueError("Closed plot has missing/duplicate/outside PWC cells")
    _validate_nonoverlapping_rectangles(leaves, None)
    if sum((x.H-x.L)*(x.B-x.A) for x in leaves) != (H-L)*(B-A):
        raise ValueError("Closed plot PWC partition does not cover the root")
    return leaves


def read_closed_plot(path, contract, parameter_sha256):
    """Read one complete formal PWC publication, preserving native stored V."""
    import h5py
    text = lambda value: value.decode() if isinstance(value, bytes) else str(value)
    with h5py.File(path, "r") as h:
        attrs = dict(plot_publication_version="arch-plot-publication-1", plot_publication_state="complete",
            plot_identity_state="recorded", geometry="cylindrical", geometry_chart="axisymmetric-rz", time_unit="s")
        if any(text(h.attrs[key]) != value for key,value in attrs.items()) or h.attrs["dim"] != 2:
            raise ValueError("Closed input is not a formal Native RZ publication")
        identity = h["SourceIdentity"]
        if (text(identity.attrs["version"]) != "arch-plot-identity-1" or text(identity.attrs["scope"]) != "resolved-runtime"
            or text(identity.attrs["binary_sha256"]) != contract["expected_binary_sha256"]
            or text(identity.attrs["raw_config_sha256"]) != parameter_sha256
            or text(identity.attrs["eos_type"]) != "ideal" or text(identity.attrs["eos_unit_system"]) != "cgs"):
            raise ValueError("Closed plot binary/config/EOS identity mismatch")
        native = h["NativeGrid"]
        attrs = dict(version="arch-native-axisymmetric-rz-2", x1_axis="r", x2_axis="z", x3_axis="inactive",
            x1_unit="cm", x2_unit="cm", centering="cell", block_kind="active-leaf", measure_unit="cm^3",
            measure_normalization="full_rotation", measure_source="GridMetrics::CellVolume",
            measure_convention="full-rotation-axisymmetric-ring", native_geometry="cylindrical")
        if any(text(native.attrs[key]) != value for key,value in attrs.items()) or native.attrs["ghost_cells"] != 0:
            raise ValueError("Closed NativeGrid axes/measure/active-cell identity mismatch")
        def stored(dataset, unit=None):
            values = dataset[()]
            if values.dtype != np.float64 or not np.all(np.isfinite(values)) or (unit is not None and text(dataset.attrs["unit"]) != unit):
                raise ValueError("Closed plot requires finite stored FP64 with actual units")
            return values
        fields = {name:stored(h["Data/"+name],unit) for name,unit in
            dict(DENS="g/cm^3", ENER="erg/cm^3", GPOT="cm^2/s^2", TEMP="K", PRES="erg/cm^3").items()}
        shape = fields["DENS"].shape
        if len(shape) != 3 or shape[0] != len(h["Grid/level"]) or any(x.shape != shape for x in fields.values()):
            raise ValueError("Closed fields disagree with active Data layout")
        for name in ("DENS","ENER"):
            if text(h["Data/"+name].attrs["averaging"]) != "native-volume-average":
                raise ValueError("Closed density/energy is not the actual Native V-mean")
        if any(np.any(fields[name] <= 0) for name in ("DENS","TEMP","PRES")):
            raise ValueError("Closed plot has nonpositive density/temperature/pressure")
        V = stored(native["cell_measure"])
        bounds = [stored(native[name]) for name in ("x1_lower","x1_upper","x2_lower","x2_upper")]
        n = fields["DENS"].size
        if any(x.shape != (n,) for x in [V]+bounds) or np.any(V <= 0):
            raise ValueError("Closed plot is missing active cell bounds/positive V")
        records = [dict(id=str(i), r_lower=float(bounds[0][i]), r_upper=float(bounds[1][i]),
            z_lower=float(bounds[2][i]), z_upper=float(bounds[3][i]), density=float(fields["DENS"].flat[i])) for i in range(n)]
        _closed_partition(records, contract["root_bounds"])
        measure = V.astype(np.longdouble)
        rho = fields["DENS"].reshape(-1).astype(np.longdouble)
        terms = fields["ENER"].reshape(-1).astype(np.longdouble)*measure
        physical_time = float(h.attrs["time"])
        if not math.isfinite(physical_time) or physical_time < 0:
            raise ValueError("Closed plot time is not a finite nonnegative value")
        return dict(path=str(path), time=physical_time, records=records,
            mass=np.sum(rho*measure,dtype=np.longdouble), Egas=np.sum(terms,dtype=np.longdouble),
            absolute_Egas_terms=np.sum(np.abs(terms),dtype=np.longdouble),
            discrete_Wh=np.sum(rho*measure*fields["GPOT"].reshape(-1).astype(np.longdouble),dtype=np.longdouble)/2,
            field_min_max={name:[float(x.min()),float(x.max())] for name,x in fields.items()},
            run_id=text(identity.attrs["run_id"]), effective_config_sha256=_closed_digest(text(identity.attrs["effective_config_sha256"])),
            sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def closed_uniform_reference(records, rho0, root_bounds, G, degrees=(12,24,48), initial_records=None):
    """Use actual PWC endpoints about rho0, including initial averaging roundoff.

    With eta_i=rho_i-rho0 and eta_f=rho_f-rho0,
    DeltaW=-G*rho0*<rho_f-rho_i,K*1>-G*(<eta_f,K*eta_f>-<eta_i,K*eta_i>)/2.
    Positivity of the Newton kernel quadratic form bounds the second term by
    [-G*a_f**2*I_DD/2,+G*a_i**2*I_DD/2]. The initial term is retained, rather
    than demanding that a volume-averaged uniform input be bitwise constant.
    Omitting initial_records retains the exact uniform mathematical reference.
    """
    from rz_ring_surface_reference import CGS_G
    if rational(G) != CGS_G or rational(rho0) <= 0 or tuple(degrees) != (12,24,48):
        raise ValueError("Invalid closed Newton controls")
    leaves = _closed_partition(records, root_bounds)
    cell_key = lambda x: (x.L,x.H,x.A,x.B)
    initial = None
    if initial_records is not None:
        initial = {cell_key(x):x.rho for x in _closed_partition(initial_records,root_bounds)}
        if set(initial) != {cell_key(x) for x in leaves}:
            raise ValueError("Closed initial/final PWC cell bounds differ")
    length = max(float(root_bounds[1])-float(root_bounds[0]), float(root_bounds[3])-float(root_bounds[2]))
    domain = tuple(float(x)/length for x in root_bounds)
    background = rational(rho0)
    old = [background if initial is None else initial[cell_key(x)] for x in leaves]
    deltas = [x.rho-rho_i for x,rho_i in zip(leaves,old)]
    exact_a = max(abs(x.rho-background) for x in leaves)
    exact_initial_a = max(abs(rho_i-background) for rho_i in old)
    a = float(exact_a)
    if rational(a) < exact_a: a = math.nextafter(a, math.inf)
    initial_a = float(exact_initial_a)
    if rational(initial_a) < exact_initial_a: initial_a = math.nextafter(initial_a, math.inf)
    levels = []
    for degree in degrees:
        I = volume_pair(domain, domain, degree)*length**5
        S = math.fsum(float(delta)*volume_pair(tuple(float(v)/length for v in (x.L,x.H,x.A,x.B)),domain,degree)*length**5
                      for x,delta in zip(leaves,deltas) if delta)
        levels.append(dict(degree=degree,I_DD=I,S=S,linear_delta_W=-G*rho0*S))
    if not all(math.isfinite(value) for level in levels for value in level.values()):
        raise ValueError("Closed Newton quadrature became nonfinite")
    coarse,medium,fine = levels
    qI = 4*abs(fine["I_DD"]-medium["I_DD"])
    qL = 4*abs(fine["linear_delta_W"]-medium["linear_delta_W"])
    interval = [-.5*G*a*a*(fine["I_DD"]+qI),.5*G*initial_a*initial_a*(fine["I_DD"]+qI)]
    center = fine["linear_delta_W"]+(interval[0]+interval[1])/2
    monotone = all(abs(fine[key]-medium[key]) <= abs(medium[key]-coarse[key]) for key in ("I_DD","linear_delta_W"))
    return dict(levels=levels,S=fine["S"],a=a,a_exact=str(exact_a),initial_a=initial_a,
        initial_a_exact=str(exact_initial_a),quadratic_delta_W_interval=interval,
        DeltaWcenter=center,Uref=qL+(interval[1]-interval[0])/2,quadrature_I_estimate=qI,quadrature_linear_estimate=qL,
        quadrature_monotone=monotone,uncertainty_kind="signed PSD density bound with numerical convergence estimates, not outward quadrature enclosure")


def collect_closed_run(directory, parameters, contract_path):
    """Workflow: authenticate all samples -> observe gates -> require the actual final endpoint."""
    started = time.monotonic()
    contract = load(contract_path)
    parameter_sha = validate_closed_contract(parameters,contract)
    samples = sorted((read_closed_plot(path,contract,parameter_sha) for path in directory.glob("*_plt_*.h5")),key=lambda x:x["time"])
    if len(samples) < 2 or samples[0]["time"] != 0 or any(b["time"] <= a["time"] for a,b in zip(samples,samples[1:])):
        raise ValueError("Closed run requires unique physical times beginning at zero")
    initial = samples[0]
    if any(x["run_id"] != initial["run_id"] or x["effective_config_sha256"] != initial["effective_config_sha256"] for x in samples):
        raise ValueError("Closed plot samples came from different runs/configurations")
    repairs_path = directory/"state_repairs.txt"
    pairs = [line.split("=",1) for line in repairs_path.read_text().splitlines() if "=" in line]
    if len({k for k,v in pairs}) != len(pairs) or int(dict(pairs)["events"]) != 0:
        raise ValueError("Closed run has missing/duplicate/nonzero terminal repair events")
    rows = []
    for sample in samples:
        ref = closed_uniform_reference(sample["records"],contract["rho0"],contract["root_bounds"],contract["G"],
            initial_records=initial["records"])
        deltaE = sample["Egas"]-initial["Egas"]
        # Frozen diagnostic rounding estimate, not a strict certificate.
        Uround = 8*np.finfo(np.float64).eps*(initial["absolute_Egas_terms"]+sample["absolute_Egas_terms"])
        scale = max(abs(deltaE),abs(np.longdouble(ref["DeltaWcenter"])))
        residual = deltaE+np.longdouble(ref["DeltaWcenter"])
        mass_error = abs(sample["mass"]-initial["mass"])/initial["mass"]
        gates = dict(mass=bool(mass_error <= contract["mass_budget"]),
            signal=bool(scale >= contract["signal_ratio"]*Uround),
            reference=bool(ref["Uref"] <= contract["reference_budget"]*scale and ref["quadrature_monotone"]),
            energy=bool(abs(residual)+ref["Uref"]+Uround <= contract["physical_budget"]*scale))
        rows.append(dict(time=sample["time"],path=sample["path"],sha256=sample["sha256"],cells=len(sample["records"]),
            Egas=str(sample["Egas"]),mass=str(sample["mass"]),discrete_Wh=str(sample["discrete_Wh"]),
            DeltaEgas=str(deltaE),Uround=str(Uround),residual=str(residual),scale=str(scale),mass_relative_error=float(mass_error),
            field_min_max=sample["field_min_max"],gates=gates,**ref))
    endpoint = abs(samples[-1]["time"]-contract["t_end"]) <= contract["physical_endpoint_time_relative_budget"]*contract["t_end"]
    passed = endpoint and all(row["gates"]["mass"] for row in rows) and all(rows[-1]["gates"].values())
    return dict(schema="arch-rz-closed-uniform-reference-1",status="PASS_CLOSED_UNIFORM_OBSERVED_GATES" if passed else "NEEDS_REVIEW",
        endpoint_reached=endpoint,samples=rows,contract=contract,parameter_sha256=parameter_sha,
        contract_sha256=hashlib.sha256(contract_path.read_bytes()).hexdigest(),
        terminal_repair_events=0,terminal_repairs_path=str(repairs_path),terminal_repairs_sha256=hashlib.sha256(repairs_path.read_bytes()).hexdigest(),
        repair_identity_scope="terminal original report; Root production receipt binds run directory, no embedded binary/config identity",
        boundary_scope="stationary reflecting insulated input and original wall owners; plot does not independently observe zero face flux",
        roundoff_kind="8eps64 absolute-energy sensitivity estimate, not a strict rounding certificate",
        core_binding_qualified=False,reference_scope="formal plot PWC mathematics, not Runtime issuer; numerical uncertainty estimates only",
        energy_budget_scope="actual final endpoint; all written samples retain observations and require mass/finite-positive fields",
        wall_seconds=time.monotonic()-started)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--step-dir", type=Path)
    mode.add_argument("--closed-run", type=Path)
    parser.add_argument("--parameters", type=Path)
    parser.add_argument("--contract", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.closed_run is not None:
        if args.parameters is None or args.contract is None:
            parser.error("--closed-run requires --parameters and --contract")
        result = collect_closed_run(args.closed_run,args.parameters,args.contract)
    else:
        if args.parameters is not None or args.contract is not None:
            parser.error("--parameters/--contract belong only to --closed-run")
        result = collect(args.step_dir)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    if args.closed_run is not None:
        print(json.dumps({k:result[k] for k in ("status","endpoint_reached","wall_seconds")}))
        return 0 if result["status"] == "PASS_CLOSED_UNIFORM_OBSERVED_GATES" else 1
    print(json.dumps({k:result[k] for k in ("status", "normalized_balance",
        "normalized_reference_uncertainty", "wall_seconds")}))
    return 0 if result["status"] == "PASS_FINITE_EULER_ENERGY" else 1


if __name__ == "__main__":
    raise SystemExit(main())
