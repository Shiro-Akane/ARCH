"""Independent Na23/Ne23 Urca trajectory, not another production network.

Read original Suzuki table DATA and nuclear masses from installed pynucastro.
SciPy's regular-grid interpolation and DOP853/Radau integrate the constrained
two-isotope balance plus temperature and signed loss quadrature. No ARCH
generated header, interpolator, Jacobian, solver or constants header is used.
The deferred SimpleCxx nuclear-data conversion is read from its upstream
template, not silently replaced with unrelated current constants.

Usage: python weak_reference.py --rho 4e9 --temperature 5e8 --interval 10 --cv 1e8
Requires numpy, scipy and pynucastro. Prints JSON, never rewrites test fixtures.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, localcontext
import json
from pathlib import Path
import re
import sys

import numpy as np
import mpmath as mp
import pynucastro as pyna
from pynucastro.constants import constants
from scipy.integrate import solve_ivp
from scipy.interpolate import RegularGridInterpolator

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from validation_provenance import file_identity as identity


class TrajectoryBudgetError(ArithmeticError):
    def __init__(self, comparison):
        super().__init__("Production trajectory exceeds independent scientific budget")
        self.comparison = comparison


def tables():
    root = Path(pyna.__file__).resolve().parent
    directory = root / "library/tabular/suzuki"
    # Dataset/stoichiometry selection belongs to this independent Urca witness,
    # not production dispatch or an inference from a generated network ID.
    paths = [directory / "suzuki-23ne-23na_betadecay.dat",
             directory / "suzuki-23na-23ne_electroncapture.dat"]
    interpolators, bounds = [], []
    for path in paths:
        data = np.loadtxt(path, comments="!")
        rhoy, temperature = np.unique(data[:, 0]), np.unique(data[:, 1])
        coordinates = data[:, :2].reshape(len(rhoy), len(temperature), 2)
        if not (np.array_equal(coordinates[:, 0, 0], rhoy)
                and np.array_equal(coordinates[0, :, 1], temperature)
                and np.all(coordinates[:, :, 0] == rhoy[:, None])
                and np.all(coordinates[:, :, 1] == temperature[None, :])):
            raise ValueError("Suzuki data do not form the declared rectangular grid")
        # Columns 5/6/7: log10(rate / s^-1), log10(neutrino loss / erg s^-1),
        # log10(gamma energy / erg s^-1), per reacting nucleus.
        values = data[:, 5:8].reshape(len(rhoy), len(temperature), 3)
        interpolators.append(RegularGridInterpolator((rhoy, temperature), values))
        bounds.append((np.array([rhoy[0], temperature[0]]),
                       np.array([rhoy[-1], temperature[-1]])))
    template = root / "templates/simple-cxx-network/fundamental_constants.H.template"
    text = template.read_text()
    def scalar(name):
        match = re.search(r'\b' + name + r'\s*=\s*([\d.eE+-]+)\s*;', text)
        if not match:
            raise ValueError("Unrecognized upstream nuclear-data constant: " + name)
        return float(match[1])
    na, c = scalar("n_A"), scalar("c_light")
    masses = [pyna.Nucleus(name).A_nuc * constants.m_u_C18 for name in ("ne23", "na23")]
    with localcontext() as context:
        context.prec = 60
        # The independent constrained X_Na=1-X_Ne balance allows a direct mass
        # difference. No production baryon-gauge or compensated-sum code needed.
        q = float(-Decimal(na) * Decimal(c)**2
                  * (Decimal(masses[0]) - Decimal(masses[1])) / Decimal(23))
    return interpolators, bounds, na, q, {
        "pynucastro_version": pyna.__version__, "tables": [identity(path) for path in paths],
        "nuclear_conversion": identity(template), "masses_g": masses,
        "avogadro": na, "light_speed_cm_s": c, "energy_per_ne23_fraction": q}


class HelmClosure:
    """Reuse the independent monomial-fit EOS, differentiating the constrained
    two-isotope composition with mpmath rather than copying ARCH derivatives.
    """
    def __init__(self, rho):
        sys.path.insert(0, str(ROOT / "validation/eos"))
        from helm_reference import HelmReference, TABLE, IMAX, JMAX
        values = np.fromfile(TABLE, sep=" ")
        points = IMAX * JMAX
        if values.size != 21 * points:
            raise ValueError("incomplete independent Helm table")
        with mp.workdps(40):
            self.model = HelmReference(values[:9*points].reshape(JMAX, IMAX, 9),
                values[13*points:17*points].reshape(JMAX, IMAX, 4))
            self.rho = mp.mpf(rho)
            self.model.ytot = mp.mpf(1) / 23
        self.identity = identity(TABLE)

    def state(self, ne, temperature):
        self.model.ye = (11 - ne) / 23
        return self.model.state(self.rho, temperature)

    def derivatives(self, ne, temperature):
        with mp.workdps(40):
            ne, temperature = mp.mpf(ne), mp.mpf(temperature)
            cv = self.state(ne, temperature)["cv"]
            energy_ne = mp.diff(lambda n: self.state(n, temperature)["E"], ne)
            return float(cv), float(energy_ne)

    def energy(self, ne, temperature):
        with mp.workdps(40):
            return self.state(mp.mpf(ne), mp.mpf(temperature))["E"]

    def energy_change(self, ne, temperature, initial_temperature):
        with mp.workdps(40):
            return float(self.energy(ne, temperature) - self.energy(0.5, initial_temperature))


def reference(rho, temperature, interval, cv=None, *, eos="constant_cv"):
    interpolators, bounds, na, q, inputs = tables()
    if eos not in ("constant_cv", "helmholtz"):
        raise ValueError("unknown independent thermal closure")
    helm = HelmClosure(rho) if eos == "helmholtz" else None
    if helm is None and (cv is None or not np.isfinite(cv) or cv <= 0.0):
        raise ValueError("constant heat capacity must be finite and positive")
    scale = float(helm.energy(0.5, temperature)) if helm else cv * temperature
    if helm:
        inputs["helm_table"] = helm.identity
    def rhs(_time, state):
        ne, theta, _source = state
        t = theta * temperature
        if not (np.isfinite(t) and t > 0 and 0 <= ne <= 1):
            raise ValueError("Independent Urca trajectory left its physical domain")
        ye = (10 * ne + 11 * (1 - ne)) / 23
        coordinates = np.array([np.log10(rho * ye), np.log10(t)])
        values = [10.0 ** interpolation(np.clip(coordinates, *domain))[0]
                  for interpolation, domain in zip(interpolators, bounds)]
        decay, capture = values
        derivative = capture[0] * (1 - ne) - decay[0] * ne
        source = na / 23 * (ne * (decay[2] - decay[1])
                            + (1 - ne) * (capture[2] - capture[1]))
        if helm:
            capacity, energy_ne = helm.derivatives(ne, t)
            thermal_rate = ((q - energy_ne) * derivative + source) / (capacity * temperature)
        else:
            thermal_rate = (q * derivative + source) / scale
        return [derivative, thermal_rate, source / scale]

    trajectories = []
    for method in ("DOP853", "Radau"):
        solution = solve_ivp(rhs, (0, interval), [0.5, 1.0, 0.0], method=method,
                             rtol=5.e-13, atol=1.e-14, max_step=interval / 128)
        if not solution.success or solution.t[-1] != interval:
            raise ArithmeticError("Independent integrator did not reach the requested time")
        end = solution.y[:, -1]
        thermal = (helm.energy_change(float(end[0]), float(end[1]*temperature), temperature)
                   if helm else scale * (end[1] - 1))
        closure = abs(thermal - q * (end[0] - 0.5) - scale * end[2]) / scale
        if not np.all(np.isfinite(end)) or closure > 2.e-11:
            raise ArithmeticError("Independent trajectory failed its energy identity")
        trajectories.append({"method": method, "rhs_calls": solution.nfev,
            "normalized_state": end.tolist(), "energy_identity_error": closure,
            "state": [end[0], 1 - end[0], end[1] * temperature, end[2] * scale]})
    agreement = float(np.max(np.abs(np.array(trajectories[0]["normalized_state"])
                                   - trajectories[1]["normalized_state"])))
    if agreement > 2.e-11:
        raise ArithmeticError(f"Independent integrators disagree: {agreement}")
    controls = {"rho": rho, "temperature": temperature, "interval": interval}
    if helm:
        controls.update(eos="helmholtz", energy_scale=scale)
    else:
        controls["cv"] = cv
    return {"oracle": "original Suzuki data; SciPy grid interpolation; constrained Urca balance",
            "controls": controls,
            "inputs": inputs, "trajectories": trajectories,
            "normalized_integrator_agreement": agreement, "release_qualified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("rho", "temperature", "interval"):
        parser.add_argument("--" + name, type=float, required=True)
    parser.add_argument("--cv", type=float)
    parser.add_argument("--eos", choices=("constant_cv", "helmholtz"), default="constant_cv")
    parser.add_argument("--compare", type=Path,
                        help="compare a current weak-trajectory CSV log with explicit control metadata")
    args = parser.parse_args()
    controls = {name: getattr(args, name) for name in ("rho", "temperature", "interval")}
    if not all(np.isfinite(value) and value > 0 for value in controls.values()):
        parser.error("all physical controls must be finite and positive")
    result = reference(**controls, cv=args.cv, eos=args.eos)
    if args.compare:
        result["comparison"] = compare_trajectory(result, args.compare)
    print(json.dumps(result, indent=2, sort_keys=True))


def compare_trajectory(reference_result, path):
    """Strict input identity and a declared dimensionless scientific budget.

    This 1e-7 budget controls fractions, T/T_initial and source/E_initial.
    E_initial is cv*T_initial for the constant-cv control, or the independent
    Helmholtz energy. The C++ energy-balance check is relative to heating, not
    this large normalization energy.
    It is distinct from (and does not replace) the C++ 2e-10 backend parity gate.
    Production local tolerances are not automatically global error guarantees.
    """
    lines = path.read_text().splitlines()
    controls = reference_result["controls"]
    helm = controls.get("eos") == "helmholtz"
    prefix = 'controls_helm,' if helm else 'controls,'
    other_prefix = 'controls,' if helm else 'controls_helm,'
    if any(line.startswith(other_prefix) for line in lines):
        raise ValueError('Trajectory contains a different EOS control')
    metadata = [line.split(',')[1:] for line in lines if line.startswith(prefix)]
    if len(metadata) != 1 or len(metadata[0]) != 5:
        raise ValueError("Trajectory lacks unique physical-control metadata")
    observed = list(map(float, metadata[0]))
    if observed[:3] != [controls[name] for name in ("rho", "temperature", "interval")]:
        raise ValueError("Trajectory controls differ from the independent reference")
    if helm:
        if not np.isfinite(observed[3]) or abs(observed[3]/controls['energy_scale'] - 1) > 4096*np.finfo(float).eps:
            raise ValueError("Trajectory energy normalization failed the independent EOS check")
    elif observed[3] != controls['cv']:
        raise ValueError("Trajectory controls differ from the independent reference")
    if not np.isfinite(observed[4]) or observed[4] <= 0:
        raise ValueError("Trajectory local tolerance is invalid")
    if lines.count('WEAK_TRAJECTORY_PARITY_PASS') != 1 or any('FAIL' in line for line in lines):
        raise ValueError("Trajectory did not pass its complete backend/closure controls")
    expected = np.array(reference_result["trajectories"][0]["state"])
    energy_scale = controls['energy_scale'] if helm else controls['cv'] * controls['temperature']
    scale = np.array([1.0, 1.0, controls["temperature"], energy_scale])
    rows = []
    seen = set()
    for line in lines:
        if not re.match(r'^\d+,\d+,', line):
            continue
        values = line.split(',')
        if len(values) != 9:
            raise ValueError("Trajectory state has the wrong Urca extent")
        method, backend, attempts, rejects = map(int, values[:4])
        key = method, backend
        if key in seen or method not in (0, 1, 2) or backend not in (0, 1):
            raise ValueError("Duplicate or unknown trajectory method/backend")
        seen.add(key)
        closure, *state = map(float, values[4:])
        if not (attempts > 0 and 0 <= rejects <= attempts and np.isfinite(closure)
                and 0 <= closure < 2.e-8 and np.all(np.isfinite(state))):
            raise ValueError("Trajectory has incomplete/nonfinite solver evidence")
        error = float(np.max(np.abs(np.array(state) - expected) / scale))
        rows.append({"method": method, "backend": backend, "attempts": attempts,
                     "rejections": rejects, "normalized_error": error, "pass": error <= 1.e-7})
    if len(seen) != 6:
        raise ValueError("Trajectory does not cover all three ODEs on both backends")
    result = {"log": identity(path), "local_rtol": observed[4], "normalized_budget": 1.e-7,
              "normalization": scale.tolist(), "rows": rows,
              "pass": all(row["pass"] for row in rows)}
    # Print failed diagnostic metrics, but do not return a successful gate.
    if not result["pass"]:
        print(json.dumps({"comparison": result}, indent=2, sort_keys=True))
        raise TrajectoryBudgetError(result)
    return result


if __name__ == "__main__":
    main()
