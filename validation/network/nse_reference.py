"""Independent high-precision NSE reference; prints data, never updates tests.

Reproduction: Python 3 + mpmath + numpy + scipy, run from any directory.
This is validation-only, not another production network/NSE implementation.
Equations (1)--(3): https://cococubed.com/code_pages/nse.shtml
Constants: SI definitions / CODATA 2022 (NIST allascii.txt).

Only immutable nuclear DATA are read from network headers. No ARCH solver,
interpolator, constants header or compiled implementation is imported. A
double-precision trust-region fit supplies an initial guess; mpmath then solves
the *unscaled* mass and charge equations with arbitrary-precision arithmetic.
Two independently rounded precisions must agree before results are emitted.
The network's deferred binding-energy conversion remains its data convention.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

import mpmath as mp
import numpy as np
from scipy.optimize import least_squares
from scipy.special import logsumexp


ROOT = Path(__file__).resolve().parents[2]
NETWORKS = {"aprox13": "NetAprox13", "aprox19": "NetAprox19",
            "aprox21": "NetAprox21", "iso7": "NetIso7"}


def numeric_array(source: str, name: str) -> list[float]:
    match = re.search(r"\b" + name + r"\s*\{([^{}]+)\}", source)
    if not match:
        raise ValueError(f"missing literal data array: {name}")
    body = re.sub(r"//[^\n]*", "", match[1])
    # First round decimal source literals as C++ binary64 input data do.
    return [float(value.strip()) for value in body.split(",") if value.strip()]


def nuclear_data(name: str) -> dict:
    path = ROOT / f"src/physics/network/{name}/{NETWORKS[name]}.h"
    source = path.read_text()
    arrays = {field: numeric_array(source, field)
              for field in ("AION", "ZION", "BION", "SPIN")}
    if len({len(values) for values in arrays.values()}) != 1:
        raise ValueError("nuclear data array lengths disagree")
    count = len(arrays["AION"])
    old = [(i + 1.0) / (0.5 * count * (count + 1.0)) for i in range(count)]
    ye = 0.0
    for x, a, z in zip(old, arrays["AION"], arrays["ZION"]):
        ye += x * z / a
    conversion_path = ROOT / "src/physics/network/timmes_common/NuclearConstants.h"
    conversion_source = conversion_path.read_text()
    scalars = {}
    for key in ("ev2erg", "avo"):
        match = re.search(r"\b" + key + r"\s*=\s*([\d.eE+-]+)\s*;", conversion_source)
        if not match:
            raise ValueError(f"unrecognized network conversion authority: {key}")
        scalars[key] = float(match[1])
    return dict(arrays=arrays, old=old, ye=ye,
                energy_conversion=(scalars["ev2erg"] * 1.0e6) * scalars["avo"],
                data_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                conversion_sha256=hashlib.sha256(conversion_path.read_bytes()).hexdigest())


def equilibrium(data: dict, digits: int) -> dict:
    with mp.workdps(digits):
        a, z, binding, spin = ([mp.mpf(value) for value in data["arrays"][field]]
                              for field in ("AION", "ZION", "BION", "SPIN"))
        temperature, density = mp.mpf("5e9"), mp.mpf("1e7")
        kb, na = mp.mpf("1.380649e-16"), mp.mpf("6.02214076e23")
        h, mu = mp.mpf("6.62607015e-27"), mp.mpf("1.66053906892e-24")
        mev = mp.mpf("1.602176634e-6")
        active = [i for i in range(len(a)) if spin[i] > 0]
        logc = [mp.log(a[i] * spin[i] / (na * density))
                + mp.mpf("1.5") * mp.log(2 * mp.pi * kb * temperature * a[i] * mu / h**2)
                + binding[i] * mev / (kb * temperature) for i in active]
        charge_degenerate = len({z[i] / a[i] for i in active}) == 1
        av = np.array([float(a[i]) for i in active])
        zv = np.array([float(z[i]) for i in active])
        base = np.array([float(value) for value in logc])
        ye = mp.mpf(data["ye"])

        def residual_guess(potential):
            logs = base + av * potential[0]
            if not charge_degenerate:
                logs += zv * potential[1]
            normalization = logsumexp(logs)
            residual = [normalization]
            if not charge_degenerate:
                residual.append(np.dot(zv / av, np.exp(logs - normalization)) - float(ye))
            return residual

        initial = [-max(base / av)] + ([] if charge_degenerate else [0.0])
        guess = least_squares(residual_guess, initial, xtol=1e-14, ftol=1e-14,
                              gtol=1e-14, max_nfev=1000)
        if not guess.success or np.max(np.abs(residual_guess(guess.x))) > 1e-9:
            raise ArithmeticError("independent trust-region initial guess failed")

        def abundances(common, charge=mp.mpf(0)):
            return [mp.exp(base_i + a[i] * common + z[i] * charge)
                    for i, base_i in zip(active, logc)]

        def mass(common):
            return mp.fsum(abundances(common)) - 1

        def constraints(common, charge):
            x = abundances(common, charge)
            return (mp.fsum(x) - 1,
                    mp.fsum(z[i] / a[i] * value for i, value in zip(active, x)) - ye)

        tolerance = mp.power(10, -digits + 15)
        if charge_degenerate:
            potential = mp.findroot(mass, mp.mpf(guess.x[0]),
                                    tol=tolerance**2, maxsteps=100)
            x_active = abundances(potential)
        else:
            potential = mp.findroot(constraints, tuple(mp.mpf(v) for v in guess.x),
                                    tol=tolerance**2, maxsteps=100)
            x_active = abundances(*potential)
        if abs(mp.fsum(x_active) - 1) > tolerance:
            raise ArithmeticError(f"independent mass constraint failed: {mp.fsum(x_active) - 1}")
        charge = mp.fsum(z[i] / a[i] * value for i, value in zip(active, x_active))
        # Degenerate networks cannot encode sub-ulp input normalization noise.
        if abs(charge - ye) > (mp.mpf("2e-16") if charge_degenerate else tolerance):
            raise ArithmeticError("independent charge constraint failed")
        x = [mp.mpf(0)] * len(a)
        for i, value in zip(active, x_active):
            x[i] = value
        conversion = mp.mpf(data["energy_conversion"])

        def energy(old):
            return conversion * mp.fsum((new - mp.mpf(before)) * b / mass_number
                                         for new, before, b, mass_number
                                         in zip(x, old, binding, a))

        old = data["old"]
        boundary_old = [-1e-12] + old[1:]
        return dict(x=[float(v).hex() for v in x],
                    enuc=float(energy(old)).hex(),
                    boundary_enuc=float(energy(boundary_old)).hex(),
                    ye=data["ye"].hex(),
                    energy_conversion=data["energy_conversion"].hex())


def main() -> None:
    result = dict(oracle="direct Saha mass/charge roots; mpmath 70/90 digits",
                  constants="SI definitions / CODATA 2022; network conversion deferred",
                  temperature=5e9, density=1e7, networks={})
    for name in NETWORKS:
        data = nuclear_data(name)
        low, high = equilibrium(data, 70), equilibrium(data, 90)
        if low != high:
            raise ArithmeticError(f"{name}: binary64 references differ with oracle precision")
        result["networks"][name] = dict(data_sha256=data["data_sha256"],
                                        conversion_sha256=data["conversion_sha256"], **high)
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
