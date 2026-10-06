"""Exact polynomial RZ fixture references; no production imports or solver calls.

Integrals labelled per_2pi use dV/(2*pi) = r dr dz. Multiply each integral,
including W and J, by the same full-azimuth 2*pi factor for physical totals.
This is an analytic fixture oracle, not a general reconstruction algorithm.
"""
from __future__ import annotations

import json
from fractions import Fraction as Q


def poly(values):
    result = tuple(Q(v) for v in values)
    while len(result) > 1 and result[-1] == 0:
        result = result[:-1]
    return result or (Q(0),)


def add(a, b):
    return poly((a[i] if i < len(a) else 0) +
                (b[i] if i < len(b) else 0)
                for i in range(max(len(a), len(b))))


def scale(a, value):
    return poly(x * Q(value) for x in a)


def multiply(a, b):
    result = [Q(0)] * (len(a) + len(b) - 1)
    for i, x in enumerate(a):
        for j, y in enumerate(b):
            result[i + j] += x * y
    return poly(result)


def derivative(a):
    return poly(i * a[i] for i in range(1, len(a)))


def radial_power(a, power):
    if power >= 0:
        return poly((Q(0),) * power + tuple(a))
    count = -power
    if any(a[i] for i in range(min(count, len(a)))):
        raise ValueError("Fixture expression has a nonregular negative radial power")
    return poly(a[count:])


def value_at(a, radius):
    radius = Q(radius)
    result = Q(0)
    for x in reversed(a):
        result = result * radius + x
    return result


def integral(a, left, right):
    left, right = Q(left), Q(right)
    if not 0 <= left < right:
        raise ValueError("Require finite rational 0 <= r_left < r_right")
    return sum((x * (right ** (i + 1) - left ** (i + 1)) / (i + 1)
                for i, x in enumerate(a)), Q(0))


def average(a, left, right, radial_weight):
    return integral(radial_power(a, radial_weight), left, right) / integral(
        radial_power((Q(1),), radial_weight), left, right)


def external_source(left, right, dz, rho, g_phi, m_phi, dt):
    """Integrate a prescribed physical stage field, not stored W means.

    rho is spatially constant in this fixture. g_phi and m_phi are polynomial
    physical fields. At the axis, g_phi must vanish. The constant nonzero
    g_phi proposal is therefore confined to an off-axis annulus.
    """
    left, right, dz, rho, dt = map(Q, (left, right, dz, rho, dt))
    if dz <= 0 or rho <= 0 or dt < 0:
        raise ValueError("Require dz/rho > 0 and dt >= 0")
    g_phi, m_phi = poly(g_phi), poly(m_phi)
    if left == 0 and value_at(g_phi, 0) != 0:
        raise ValueError("Nonzero constant azimuthal acceleration is nonregular at axis")
    v = dz * integral((Q(0), Q(1)), left, right)
    w = dz * integral((Q(0), Q(0), Q(1)), left, right)
    torque = rho * dz * integral(radial_power(g_phi, 2), left, right)
    work = dz * integral(radial_power(multiply(m_phi, g_phi), 1), left, right)
    return {
        "volume_per_2pi": v,
        "angular_measure_per_2pi": w,
        "m_phi_mean_W": average(m_phi, left, right, 2),
        "m_phi_mean_V": average(m_phi, left, right, 1),
        "delta_J_per_2pi": dt * torque,
        "delta_m_phi_W": dt * torque / w,
        "delta_E_total_per_2pi": dt * work,
        "delta_E_mean_V": dt * work / v,
    }


def rotating_native_mean(left, right, rho=1, omega=1, internal=Q(1, 64)):
    """Exact constant-density, regular rigid-rotation mean witness.

    Specific internal energy is spatially constant and positive. V/W/I/J are
    per_2pi per unit dz. The Cauchy--Schwarz angular minimum is a total energy
    J**2/(2*I); divide by V for a conserved energy-density mean. This reference
    diagnoses a mixed-measure proxy; it does not define a new production EOS,
    AMR closure or sufficiency test for unknown subcell density fields.
    """
    left, right, rho, omega, internal = map(Q, (left, right, rho, omega, internal))
    if rho <= 0 or internal <= 0:
        raise ValueError("Require positive density and specific internal energy")
    v = integral((0, 1), left, right)
    w = integral((0, 0, 1), left, right)
    inertia = rho * integral((0, 0, 0, 1), left, right)
    angular = omega * inertia
    m_phi = angular / w
    kinetic = rho * omega * omega * average((0, 0, 1), left, right, 1) / 2
    energy = rho * internal + kinetic
    proxy = (energy - m_phi * m_phi / (2 * rho)) / rho
    minimum_density = angular * angular / (2 * inertia * v)
    return {"volume_per_2pi": v, "angular_measure_per_2pi": w,
            "inertia_per_2pi": inertia, "J_per_2pi": angular,
            "m_phi_mean_W": m_phi, "energy_mean_V": energy,
            "actual_specific_internal": internal,
            "generic_proxy_specific_internal": proxy,
            "actual_kinetic_mean_V": kinetic,
            "minimum_angular_kinetic_mean_V": minimum_density}



def variable_density_rotation(left, right, density=(Q(7, 8), 0, Q(1, 4)),
                              omega=1, internal=Q(1, 64)):
    """Exact smooth-density counterexample to an inferred constant-density I.

    This oracle integrates a specified polynomial physical field. Its true
    inertia is known from that field, not inferred from a production mean.
    """
    density = poly(density)
    omega, internal = Q(omega), Q(internal)
    if len(density) > 3:
        raise ValueError("This exact fixture accepts at most quadratic density")
    left, right = Q(left), Q(right)
    probes = [left, right]
    if len(density) == 3 and density[2] != 0:
        vertex = -density[1]/(2*density[2])
        if left < vertex < right:
            probes.append(vertex)
    if internal <= 0 or any(value_at(density,r) <= 0 for r in probes):
        raise ValueError("Require positive density on the entire fixture cell and thermal energy")
    v = integral((0, 1), left, right)
    w = integral((0, 0, 1), left, right)
    mass = integral(radial_power(density, 1), left, right)
    inertia = integral(radial_power(density, 3), left, right)
    angular = omega * inertia
    total = mass * internal + omega * omega * inertia / 2
    constant_inertia = mass / v * integral((0, 0, 0, 1), left, right)
    return {"volume_per_2pi": v, "angular_measure_per_2pi": w,
            "mass_per_2pi": mass, "inertia_per_2pi": inertia,
            "J_per_2pi": angular, "energy_per_2pi": total,
            "actual_specific_internal": internal,
            "constant_density_proxy_internal": (total-angular*angular/(2*constant_inertia))/mass}


def newton_contact_tube_force_bound(observer_radius, cutoff, rho_max=1, gravity=1):
    """Exact conservative bound for each omitted Newton force component.

    The omitted set is |r-R|<=d, |z-Z|<=d, all azimuth. This is a reference
    integration partition, not softened physics. A component is at most
    G*rho_max*integral(1/distance**2 dV). For R>=2d, let
    A=2*sqrt(R*(R-d)); the angular split with sin(eta/2)=d/A bounds
    the near cube by 8*pi*sqrt(3)*(R+d)*d/sqrt(A*A-d*d) <=66d,
    and the far angular tail by 16*(R+d)*d*d*cot(eta/2)/A**2 <=24d.
    Here sqrt(A*A-d*d)>=R, A>=R, pi<22/7, sqrt(3)<7/4.
    For R<2d the entire set lies in a ball of radius sqrt(26)*d<6d;
    the integral is <24*pi*d<90d. Thus 90*G*rho_max*d bounds all R,
    including axis/source contact, without transcendental rounding.
    This proves a reference omission bound only, not a production gate.
    """
    radius, d, rho, g = map(Q, (observer_radius, cutoff, rho_max, gravity))
    if radius < 0 or d <= 0 or rho < 0 or g < 0:
        raise ValueError("Require R/rho/G nonnegative and positive integration cutoff")
    return 90 * g * rho * d

def swirl_stress(mu, omega=1, cubic=0):
    """Exact radial-only swirl v_phi=omega*r+cubic*r**3, mu polynomial.

    Linear mu cases are off-axis references. Smooth axis fixtures use even
    scalar coefficients. Returns stress, force density, total-energy work
    divergence, and nonnegative heating; not a full stress-tensor solver.
    """
    mu = poly(mu)
    velocity = poly((0, Q(omega), 0, Q(cubic)))
    strain = add(derivative(velocity), scale(radial_power(velocity, -1), -1))
    stress = multiply(mu, strain)
    force = radial_power(derivative(radial_power(stress, 2)), -2)
    energy_work = radial_power(derivative(radial_power(multiply(velocity, stress), 1)), -1)
    heating = multiply(stress, strain)
    return {"velocity": velocity, "strain": strain, "tau_rphi": stress,
            "force_phi": force, "energy_work_divergence": energy_work,
            "heating": heating}


def axis_equilibrium(h, rho=1, omega=1, p0=5, gamma=Q(7, 5), cubic=0):
    """Exact steady-cell integrals and the documented source-only defect."""
    h, rho, omega, p0, gamma = map(Q, (h, rho, omega, p0, gamma))
    if h <= 0 or rho <= 0 or p0 <= 0 or gamma <= 1:
        raise ValueError("Require positive fixture scales and gamma > 1")
    cubic = Q(cubic)
    velocity = poly((0, omega, 0, cubic))
    pressure = poly((p0, 0, rho * omega * omega / 2, 0,
                     rho * omega * cubic / 2, 0, rho * cubic * cubic / 6))
    kinetic = scale(multiply(velocity, velocity), rho / 2)
    energy = add(scale(pressure, 1 / (gamma - 1)), kinetic)
    mean_m_phi = rho * average(velocity, 0, h, 2)
    ke_true = average(kinetic, 0, h, 1)
    ke_representative = mean_m_phi * mean_m_phi / (2 * rho)
    p_representative = (gamma - 1) * (average(energy, 0, h, 1) - ke_representative)
    radial_measure = integral((Q(0), Q(1)), 0, h)
    pressure_divergence = h * value_at(pressure, h) / radial_measure
    native_source = integral(add(pressure, scale(multiply(velocity, velocity), rho)),
                             0, h) / radial_measure
    inverse_radius_mean = h / radial_measure
    representative_source = (p_representative + mean_m_phi * mean_m_phi / rho) * inverse_radius_mean
    return {"h": h, "kinetic_mean_V": ke_true,
            "kinetic_representative": ke_representative,
            "pressure_mean_V": average(pressure, 0, h, 1),
            "pressure_representative": p_representative,
            "pressure_divergence": pressure_divergence,
            "source_exact_mean_V": native_source,
            "source_representative": representative_source,
            "source_only_defect": representative_source - pressure_divergence}


def fixtures():
    return {
        "schema_version": 1,
        "status": "ANALYTIC_FIXTURE_REFERENCES_ONLY",
        "normalization": "Integral entries per_2pi must be multiplied by full azimuth factor 2*pi",
        "external_off_axis": external_source(1, 3, 2, 2, (Q(-1, 40),), (0, 2), Q(1, 10000)),
        "external_regular_axis": external_source(0, 1, 1, 1, (0, Q(1, 40)), (0, 1), Q(1, 10000)),
        "swirl_rigid_variable_mu_off_axis": swirl_stress((1, 2, 3)),
        "swirl_cubic_variable_mu_off_axis": swirl_stress((1, 2, 3), cubic=Q(1, 4)),
        "swirl_cubic_regular_axis": swirl_stress((1, 0, 3), cubic=Q(1, 4)),
        "axis_general_swirl": axis_equilibrium(Q(1, 16), omega=4, cubic=Q(1, 4)),
        "axis_equilibrium_sequence": [axis_equilibrium(Q(1, n)) for n in (16, 32, 64, 128)],
        "scope": "No production imports, evolution, EOS solve, Poisson, CUDA, or certified force oracle",
    }


if __name__ == "__main__":
    print(json.dumps(fixtures(), default=str, ensure_ascii=False, indent=2))


def _finite_decimal(value, name):
    """Read exact finite decimal input without importing production physics."""
    from decimal import Decimal, DecimalException
    try:
        result = value if isinstance(value, Decimal) else Decimal(str(value))
    except DecimalException as error:
        raise ValueError(f"{name} must be a finite decimal") from error
    if not result.is_finite():
        raise ValueError(f"{name} must be finite")
    return result


def _decimal_rational(value, precision, rounding):
    """Directed conversion of an exact rational; denominator rounding is avoided."""
    from decimal import Decimal, localcontext
    with localcontext() as ctx:
        ctx.prec = precision
        ctx.rounding = rounding
        return Decimal(value.numerator) / Decimal(value.denominator)


def newton_force_modulus(h, rho_max, support_radius, gravity_G, precision=80):
    """Upper bound the force variation of ONE fixed compact bounded source.

    For |t| <= h, K(v)=v/|v|^3 and ||DK||=2/|v|^3. The source inside
    B(x,2h) contributes at most (8+12)*pi*h; outside this ball the segment
    Jacobian gives 64*pi*h*log(max(D/(2h),1)). Therefore
      |g(x+t*e)-g(x)| <= G*rho_max*(20*pi*h + 64*pi*h*log(max(D/(2h),1))).
    D bounds the source distance from the SAME observer x; rho_max bounds
    |rho| throughout that SAME source. This also covers contact/inside points.
    No gravitational softening or source removal is introduced. 22/7 is a
    proven upper bound on pi. Decimal ln is correctly rounded; next_plus
    provides an upward enclosure before the remaining directed arithmetic.
    """
    from decimal import Decimal, ROUND_CEILING, localcontext
    if not isinstance(precision, int) or isinstance(precision, bool) or precision < 16:
        raise ValueError("precision must be an integer >= 16")
    hs = _finite_decimal(h, "h")
    density = _finite_decimal(rho_max, "rho_max")
    distance = _finite_decimal(support_radius, "support_radius")
    constant = _finite_decimal(gravity_G, "gravity_G")
    if hs <= 0 or density < 0 or distance < 0 or constant < 0:
        raise ValueError("Require h > 0 and rho_max/D/G >= 0")
    if density == 0 or constant == 0:
        return Decimal(0)
    ratio = Q(distance) / (2 * Q(hs))
    with localcontext() as ctx:
        ctx.prec = precision
        ctx.rounding = ROUND_CEILING
        logarithm = Decimal(0)
        if ratio > 1:
            upper_ratio = _decimal_rational(ratio, precision, ROUND_CEILING)
            logarithm = upper_ratio.ln().next_plus()
        prefactor = _decimal_rational(Q(constant)*Q(density)*Q(hs)*Q(22,7),
                                      precision, ROUND_CEILING)
        return prefactor * (Decimal(20) + Decimal(64)*logarithm)


def force_difference_interval(phi_minus, phi_plus, h, rho_max,
                              support_radius, gravity_G, precision=80):
    """Conditionally enclose -partial_e Phi from CERTIFIED potential bounds.

    Inputs enclose Phi(x-h*e) and Phi(x+h*e) of the SAME fixed source.
    Since the central difference is the segment-average g_e, its interval
      [(L_minus-U_plus)/(2h), (U_minus-L_plus)/(2h)]
    enlarged by +/- newton_force_modulus encloses g_e(x). An uncertified
    quadrature order difference is NOT a valid input certificate. This leaf
    does not create potential certificates or claim a production force gate.
    Cartesian observers avoid radial-reflection sign ambiguities at the axis.
    """
    from decimal import ROUND_FLOOR, ROUND_CEILING
    if len(phi_minus) != 2 or len(phi_plus) != 2:
        raise ValueError("Each potential enclosure needs two endpoints")
    lm, um = (_finite_decimal(v, "phi_minus") for v in phi_minus)
    lp, up = (_finite_decimal(v, "phi_plus") for v in phi_plus)
    if lm > um or lp > up:
        raise ValueError("Potential enclosure endpoints are reversed")
    hs = _finite_decimal(h, "h")
    radius = newton_force_modulus(hs,rho_max,support_radius,gravity_G,precision)
    lower = (Q(lm)-Q(up))/(2*Q(hs))-Q(radius)
    upper = (Q(um)-Q(lp))/(2*Q(hs))+Q(radius)
    return (_decimal_rational(lower,precision,ROUND_FLOOR),
            _decimal_rational(upper,precision,ROUND_CEILING))
