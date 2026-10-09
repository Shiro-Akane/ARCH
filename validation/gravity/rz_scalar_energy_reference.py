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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--step-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = collect(args.step_dir)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+"\n")
    print(json.dumps({k:result[k] for k in ("status", "normalized_balance",
        "normalized_reference_uncertainty", "wall_seconds")}))
    return 0 if result["status"] == "PASS_FINITE_EULER_ENERGY" else 1


if __name__ == "__main__":
    raise SystemExit(main())
