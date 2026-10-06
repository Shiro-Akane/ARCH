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
