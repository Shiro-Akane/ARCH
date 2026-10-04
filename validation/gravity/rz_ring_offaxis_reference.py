"""Independent finite-volume off-axis RZ reference, exterior observers only.

Source: constant density on [rL,rR] x [zL,zR], full 0..2*pi azimuth.
Observe point (R,0,Z), not a cell/face volume average.
Analytically integrate azimuth using complete elliptic integrals; Decimal
Gauss-Legendre integrates the remaining finite radial/axial source volume.
Cross-check uses direct 3-D Newton tensor integration without elliptic functions.

K/E AGM identities: NIST DLMF 19.8.5--6; parameter m=k^2.
No production gravity/GridMetrics/EOS code, softening, floors or tolerance gates.
The default exterior potential/force reference rejects interior/contact.
A separate --contact-review mode reports bounded potential estimates only; it
does not supply a certified near bound or a contact force reference.
"""
from decimal import Decimal, localcontext
from functools import lru_cache
import argparse
import json
import math
from pathlib import Path
from rz_ring_axis_reference import PI, G, decimal_value


def elliptic_ke(m):
    """Complete K(m), E(m), with m the parameter, not modulus k."""
    if not Decimal(0) <= m < Decimal(1):
        raise ValueError("Elliptic parameter must satisfy 0 <= m < 1")
    a, b = Decimal(1), (1-m).sqrt()
    correction, weight = m/2, Decimal(1)
    for _ in range(64):
        c = (a-b)/2
        correction += weight*c*c
        new_a, new_b = (a+b)/2, (a*b).sqrt()
        if new_a == a and new_b == b:
            k = PI/(2*new_a)
            return k, k*(1-correction)
        a, b = new_a, new_b
        weight *= 2
    raise ArithmeticError("AGM failed to converge at current precision")


@lru_cache(maxsize=16)
def decimal_gauss(order, precision):
    """Decimal roots/weights; float cosine supplies only Newton's starting seed."""
    if not 2 <= order <= 64 or not 40 <= precision <= 120:
        raise ValueError("Diagnostic bounds: order 2..64, precision 40..120")
    with localcontext() as ctx:
        ctx.prec = precision
        roots = []
        for i in range(1, (order+1)//2+1):
            x = Decimal.from_float(math.cos(math.pi*(i-.25)/(order+.5)))
            for _ in range(128):
                p0, p1 = Decimal(1), x
                for n in range(2, order+1):
                    p0, p1 = p1, ((2*n-1)*x*p1-(n-1)*p0)/n
                derivative = order*(x*p1-p0)/(x*x-1)
                next_x = x-p1/derivative
                if next_x == x or abs(next_x-x) < Decimal(10)**(-precision+5):
                    x = next_x
                    break
                x = next_x
            else:
                raise ArithmeticError("Legendre root failed to converge")
            p0, p1 = Decimal(1), x
            for n in range(2, order+1):
                p0, p1 = p1, ((2*n-1)*x*p1-(n-1)*p0)/n
            derivative = order*(x*p1-p0)/(x*x-1)
            weight = 2/((1-x*x)*derivative*derivative)
            roots.append((x, weight))
            if not order % 2 or i != (order+1)//2:
                roots.append((-x, weight))
        return tuple(sorted(roots))


def source_arguments(case, *, allow_contact=False):
    values = tuple(decimal_value(case[k]) for k in
        ("r_lower", "r_upper", "z_lower", "z_upper", "density",
         "r_observer", "z_observer"))
    if not all(v.is_finite() for v in values):
        raise ValueError("Finite inputs required")
    rl, rr, zl, zr, rho, R, Z = values
    if not (0 <= rl < rr and zl < zr and rho > 0 and R >= 0):
        raise ValueError("Positive density and ordered annulus required")
    if not allow_contact and rl <= R <= rr and zl <= Z <= zr:
        raise ValueError("Interior/contact observer not covered by this reference")
    return values


def finite_volume_reference(case, order=32, precision=80):
    """Return Decimal point potential, radial/axial force and full-volume mass."""
    rl, rr, zl, zr, rho, R, Z = source_arguments(case)
    with localcontext() as ctx:
        ctx.prec = precision
        nodes = decimal_gauss(order, precision)
        phi, gr, gz = Decimal(0), Decimal(0), Decimal(0)
        for nr, wr in nodes:
            radius = rl+(nr+1)*(rr-rl)/2
            for nz, wz in nodes:
                u = zl+(nz+1)*(zr-zl)/2-Z
                s2 = (R+radius)**2+u*u
                s = s2.sqrt()
                d2 = (R-radius)**2+u*u
                measure = radius*wr*wz*(rr-rl)*(zr-zl)/4
                if R == 0:
                    angular_phi = 2*PI/s
                    angular_gr = Decimal(0)
                    angular_gz = 2*PI*u/(s2*s)
                else:
                    k, e = elliptic_ke(4*R*radius/s2)
                    angular_phi = 4*k/s
                    angular_gr = 2*(e*(radius*radius-R*R+u*u)/d2-k)/(R*s)
                    angular_gz = 4*u*e/(s*d2)
                phi -= G*rho*measure*angular_phi
                gr += G*rho*measure*angular_gr
                gz += G*rho*measure*angular_gz
        # Exact reflection symmetry of this uniform source, not an epsilon cutoff.
        if 2*Z == zl+zr:
            gz = Decimal(0)
        return dict(potential=+phi, radial_acceleration=+gr,
                    axial_acceleration=+gz,
                    mass=+(PI*rho*(rr*rr-rl*rl)*(zr-zl)))


def partitioned_reference(case, order=64, precision=100):
    """Fixed 2x2 source partition, same physical finite volume, no adaptive tuning."""
    rl, rr, zl, zr, _, _, Z = source_arguments(case)
    with localcontext() as ctx:
        ctx.prec = precision
        rm, zm = (rl+rr)/2, (zl+zr)/2
        total = None
        for ra, rb in [(rl,rm),(rm,rr)]:
            for za, zb in [(zl,zm),(zm,zr)]:
                values = finite_volume_reference(dict(case,r_lower=ra,r_upper=rb,
                    z_lower=za,z_upper=zb),order,precision)
                if total is None:
                    total = values
                else:
                    total = {k:total[k]+values[k] for k in values}
        if 2*Z == zl+zr:
            total["axial_acceleration"] = Decimal(0)
        return total


def finite_source_multipoles(case):
    """Analytic full-volume moments; far-expansion diagnostic, no opening rule."""
    rl, rr, zl, zr, rho, R, Z = map(float, source_arguments(case))
    dz = zr-zl
    u = Z-(zl+zr)/2
    d = math.hypot(R,u)
    enclosing_radius = math.hypot(rr,dz/2)
    if d <= enclosing_radius:
        return {"available":False,"reason":"Observer not outside enclosing source sphere"}
    mass = math.pi*rho*(rr*rr-rl*rl)*dz
    # Full Cartesian moments of the uniform rotating annulus, about its centroid.
    ixx = mass*(rr*rr+rl*rl)/4
    izz = mass*dz*dz/12
    qzz = 2*(izz-ixx)
    qrr = -qzz/2
    qv = qrr*R*R+qzz*u*u
    monopole = dict(potential=-float(G)*mass/d,
        radial_acceleration=-float(G)*mass*R/d**3,
        axial_acceleration=-float(G)*mass*u/d**3)
    quadrupole = dict(potential=monopole["potential"]-float(G)*qv/(2*d**5),
        radial_acceleration=monopole["radial_acceleration"]+float(G)*(qrr*R/d**5-2.5*qv*R/d**7),
        axial_acceleration=monopole["axial_acceleration"]+float(G)*(qzz*u/d**5-2.5*qv*u/d**7))
    return dict(available=True, expansion="3D Newton monopole + traceless quadrupole",
        centroid_z=(zl+zr)/2, mass=mass,
        second_moments=dict(Ixx=ixx,Iyy=ixx,Izz=izz),
        quadrupole=dict(Qxx=qrr,Qyy=qrr,Qzz=qzz),
        enclosing_radius=enclosing_radius,observer_distance=d,
        monopole=monopole,through_quadrupole=quadrupole)


def direct_newton_tensor(case, order):
    """Independent binary64 3-D integration, no ring/elliptic-kernel reuse."""
    import numpy as np
    rl, rr, zl, zr, rho, R, Z = map(float, source_arguments(case))
    n, w = np.polynomial.legendre.leggauss(order)
    radial = (rl+(n+1)*(rr-rl)/2)[:, None, None]
    axial = (zl+(n+1)*(zr-zl)/2-Z)[None, :, None]
    # Separate azimuth quadrature order, with all Cartesian source directions.
    an, aw = np.polynomial.legendre.leggauss(2*order)
    angle = (math.pi*(an+1))[None, None, :]
    dx = radial*np.cos(angle)-R
    dy = radial*np.sin(angle)
    distance2 = dx*dx+dy*dy+axial*axial
    distance = np.sqrt(distance2)
    weight = (radial*w[:, None, None]*(rr-rl)/2
              *w[None, :, None]*(zr-zl)/2*aw[None, None, :]*math.pi*rho)
    force_factor = float(G)*weight/(distance2*distance)
    return dict(potential=math.fsum((-float(G)*weight/distance).ravel()),
                radial_acceleration=math.fsum((force_factor*dx).ravel()),
                axial_acceleration=math.fsum((force_factor*axial).ravel()),
                mass=math.fsum(np.broadcast_to(weight, distance.shape).ravel()))


CASES = [
 dict(name="solid_offaxis_midplane", r_lower=0, r_upper=2, z_lower=-1,
      z_upper=1, density=3, r_observer=3, z_observer=0),
 dict(name="solid_offaxis_above", r_lower=0, r_upper=2, z_lower=-1,
      z_upper=1, density=3, r_observer=3, z_observer=2),
 dict(name="solid_offaxis_below", r_lower=0, r_upper=2, z_lower=-1,
      z_upper=1, density=3, r_observer=3, z_observer=-2),
 dict(name="hollow_cavity", r_lower=1, r_upper=2, z_lower=-1,
      z_upper=1, density=3, r_observer="0.25", z_observer=0),
 dict(name="solid_near_exterior", r_lower=0, r_upper=2, z_lower=-1,
      z_upper=1, density=3, r_observer="2.125", z_observer=0),
 dict(name="solid_far_offaxis", r_lower=0, r_upper=2, z_lower=-1,
      z_upper=1, density=3, r_observer=30, z_observer=40),
]


def contact_potential_reference(case, order=32, precision=80, t_panels=1):
    """Independent Decimal Duffy potential, estimate-only bounded diagnostic.

    Existing exterior force reference keeps rejecting contact. No production
    kernel imports. Vary order, precision and t partition independently.
    """
    rl, rr, zl, zr, rho, R, Z = source_arguments(case, allow_contact=True)
    if not (rl <= R <= rr and zl <= Z <= zr and R > 0):
        raise ValueError("Dedicated contact reference requires positive-R inside/contact")
    if not 1 <= t_panels <= 4:
        raise ValueError("Diagnostic t partition limited to 1..4")
    with localcontext() as ctx:
        ctx.prec = precision
        rs, zs = sorted(set((rl, R, rr))), sorted(set((zl, Z, zr)))
        nodes = decimal_gauss(order, precision)
        integral = Decimal(0)
        work = 0
        for ra, rb in zip(rs, rs[1:]):
            for za, zb in zip(zs, zs[1:]):
                a, b = (rb if ra == R else ra)-R, (zb if za == Z else za)-Z
                if a == 0 or b == 0:
                    continue
                for triangle in (0,1):
                    for panel in range(t_panels):
                        tlo, thi = Decimal(panel)/t_panels, Decimal(panel+1)/t_panels
                        for nt, wt in nodes:
                            tv = tlo+(nt+1)*(thi-tlo)/2
                            for nu, wu in nodes:
                                uv = (nu+1)/2
                                dr = tv*(a if triangle == 0 else (1-uv)*a)
                                dz = tv*(uv*b if triangle == 0 else b)
                                radius = R+dr
                                s2 = (R+radius)**2+dz*dz
                                # Decimal independent parameter m=k^2, not modulus.
                                kv, _ = elliptic_ke(4*R*radius/s2)
                                integral += wt*wu*(thi-tlo)/4 * tv*abs(a*b)*radius*kv/s2.sqrt()
                                work += 1
        return dict(potential=+(-4*G*rho*integral), kernel_evaluations=work,
                    certified=False)


CONTACT_CASES = [
    dict(name="outer_face", r_lower=".5", r_upper=1, z_lower="-.375",
         z_upper=".375", density=1, r_observer=1, z_observer=0),
    dict(name="outer_corner", r_lower=".5", r_upper=1, z_lower="-.375",
         z_upper=".375", density=1, r_observer=1, z_observer=".375"),
    dict(name="interior", r_lower=".5", r_upper=1, z_lower="-.375",
         z_upper=".375", density=1, r_observer=".75", z_observer=0),
]


def contact_review(output, probe=None):
    import subprocess
    rows = []
    for case in CONTACT_CASES:
        sequence = []
        for order, panels, precision in [(32,1,80),(64,1,80),(64,2,80),(64,2,100)]:
            value = contact_potential_reference(case,order,precision,panels)
            sequence.append(dict(order=order,t_panels=panels,precision=precision,
                potential=float(value["potential"]),decimal_potential=str(value["potential"]),
                kernel_evaluations=value["kernel_evaluations"],certified=False))
            print(case["name"],order,panels,precision,"processed",flush=True)
        row = dict(source=case,reference_sequence=sequence,
            precision_binary64_equal=sequence[-1]["potential"]==sequence[-2]["potential"],
            order_32_to_64_delta=abs(sequence[0]["potential"]-sequence[1]["potential"]),
            t_partition_1_to_2_delta=abs(sequence[1]["potential"]-sequence[2]["potential"]),
            certified=False)
        if probe:
            command = [str(Path(probe).resolve()),"ring-probe",
                *(str(case[k]) for k in ("r_lower","r_upper","z_lower","z_upper","density","r_observer","z_observer")),
                "1e-7","128","200000"]
            run = subprocess.run(command,text=True,capture_output=True,check=True,timeout=120)
            value = json.loads(run.stdout)
            row["production_math_probe"] = value
            row["production_absolute_delta"] = abs(value["value"]-sequence[-1]["potential"])
        rows.append(row)
    report = dict(scope="Independent bounded contact point-potential review only; no certified near bound or production RZ acceptance",
        G_cgs=str(G),potential_unit="cm^2/s^2",normalization="full rotating volume",
        method="Independent Decimal AGM + split rectangle triangular Duffy; separate precision/order/t partition",
        source_identity="matched piecewise-constant annulus",rows=rows,
        limits=["Potential only; no contact force reference","Quadrature difference is estimate only",
                "Existing exterior reference still rejects contact","No production kernel imported",
                "No AMR/operator/CUDA/evolution or global residual certification"])
    Path(output).write_text(json.dumps(report,indent=2)+"\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    parser.add_argument("--contact-review", action="store_true")
    parser.add_argument("--kernel-probe", help="Existing arch_composite_poisson test binary")
    args = parser.parse_args()
    if args.contact_review:
        contact_review(args.output,args.kernel_probe)
        return
    rows = []
    for case in CASES:
        references = []
        for order in (16, 32, 64):
            low = finite_volume_reference(case, order, 60)
            high = finite_volume_reference(case, order, 100)
            # Precision convergence does not imply source quadrature convergence.
            references.append(dict(order=order,
                precision_binary64_equal=all(float(low[k]) == float(high[k]) for k in high),
                decimal_values={k:str(v) for k,v in high.items()},
                values={k:float(v) for k,v in high.items()}))
        partition_low = partitioned_reference(case,64,60)
        partition_high = partitioned_reference(case,64,100)
        target = {k:float(v) for k,v in partition_high.items()}
        quadrature = []
        for order in (8, 16, 32, 64):
            values = direct_newton_tensor(case, order)
            quadrature.append(dict(order_rz=order, order_phi=2*order,
                values=values, absolute_errors={k:abs(values[k]-target[k]) for k in target}))
        multipoles = finite_source_multipoles(case)
        if multipoles["available"]:
            multipoles["absolute_errors"]={method:{k:abs(v-target[k]) for k,v in multipoles[method].items()}
                for method in ("monopole","through_quadrupole")}
        rows.append(dict(source=case,reference_sequence=references,multipoles=multipoles,
            partition_reference=dict(source_parts=[2,2],order_per_part=64,
                precision_digits=[60,100],
                precision_binary64_equal=all(float(partition_low[k]) == float(partition_high[k]) for k in target),
                decimal_values={k:str(v) for k,v in partition_high.items()},values=target),
            whole_64_to_partition_64_absolute_delta={k:abs(references[-1]["values"][k]-target[k]) for k in target},
            source_order_32_to_64_absolute_delta={k:abs(references[-2]["values"][k]-references[-1]["values"][k]) for k in target},
            direct_newton=quadrature))
        print(case["name"], "processed", flush=True)
    report = dict(scope="Independent finite uniform annulus exterior point reference; physics-review-pending",
        G_cgs=str(G),normalization="full rotating volume, 2*pi",
        potential_unit="cm^2/s^2",acceleration_unit="cm/s^2",mass_unit="g",
        method="analytic azimuth K/E AGM; Decimal finite-source r/z Gauss-Legendre",
        formula_source="https://dlmf.nist.gov/19.8#E5 and #E6",
        rows=rows,limits=["No production kernel or capability changes",
            "Source quadrature convergence and arithmetic precision reported separately",
            "Interior/contact observers rejected; no epsilon or softening",
            "Point values, not cell/face averages",
            "No production near/far switch, order or scientific error budget selected",
            "No AMR/operator/CUDA/evolution coverage"])
    Path(args.output).write_text(json.dumps(report, indent=2)+"\n")


if __name__ == "__main__":
    main()
