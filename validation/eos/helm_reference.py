"""Independent high-precision checks of the checked-in Helmholtz table.

Validation-only: a monomial polynomial is fitted to endpoint derivative DATA
by linear algebra, not ARCH's Hermite basis/interpolator. Thermodynamic values
follow P=rho^2 dF/drho, E=F-T dF/dT and cv=-T d2F/dT2. Ions, radiation and the
documented uniform-background Coulomb fit complete the model. Constants use
SI definitions / CODATA 2022 independently of the production constants header.
Source/model: https://cococubed.com/code_pages/eos.shtml

Requires mpmath/numpy/scipy. Prints references and table identity; never overwrites a
fixture. Decimal nuclear/table input is rounded to binary64 before reference
arithmetic. Scalar physical references retain the documented decimal grid.
Derivative checks also fit the independently rounded binary64 grid: this
separates coordinate quantization from evaluation error in cancelling terms.
Neither grid is sampled from ARCH. Both 60/80-digit results must round alike.
"""

from __future__ import annotations

from functools import lru_cache
import hashlib
import json
from pathlib import Path

import mpmath as mp
import numpy as np
from scipy.integrate import solve_ivp


ROOT = Path(__file__).resolve().parents[2]
TABLE = ROOT / "EOS_toolkit/tables/helmholtz/helm_table.dat"
IMAX, JMAX = 541, 201
DERIVATIVES = ((0, 0), (1, 0), (0, 1), (2, 0), (0, 2),
               (1, 1), (2, 1), (1, 2), (2, 2))


def endpoint_matrix(order):
    """Constrain monomial coefficients using values/derivatives at 0 and 1."""
    rows = []
    for side in (0, 1):
        for derivative in range(order):
            rows.append([mp.factorial(k) / mp.factorial(k - derivative)
                         * mp.mpf(side)**(k - derivative) if k >= derivative else 0
                         for k in range(2 * order)])
    return mp.matrix(rows)


class HelmReference:
    def __init__(self, free_energy, eta, *, binary64_grid=False):
        self.free_energy, self.eta_table = free_energy, eta
        self.binary64_grid = binary64_grid
        self.kb, self.na = mp.mpf("1.380649e-16"), mp.mpf("6.02214076e23")
        self.h, self.c = mp.mpf("6.62607015e-27"), mp.mpf("2.99792458e10")
        self.alpha = mp.mpf("7.2973525643e-3")
        self.radiation = 8 * mp.pi**5 * self.kb**4 / (15 * self.h**3 * self.c**3)
        self.charge_squared = self.alpha * self.h * self.c / (2 * mp.pi)
        # Same two declared species as the owner/parity regression (A=1,4; Z=1,2).
        self.ytot, self.ye = mp.mpf(7) / 16, mp.mpf(5) / 8

    @lru_cache(maxsize=32)
    def polynomial(self, i, j, order, precision):
        # mpmath.diff temporarily raises precision. Its tiny derivative steps
        # must never reuse coefficients/grid coordinates rounded at a lower
        # working precision, especially at a shared table endpoint.
        def node(index, lower):
            value = mp.power(10, lower + mp.mpf(index) / 20)
            return mp.mpf(float(value)) if self.binary64_grid else value
        dn, tn = node(i, -12), node(j, 3)
        dd, dt = node(i + 1, -12) - dn, node(j + 1, 3) - tn
        data = self.free_energy if order == 3 else self.eta_table
        derivatives = DERIVATIVES if order == 3 else ((0, 0), (1, 0), (0, 1), (1, 1))
        constraints = mp.matrix(2 * order, 2 * order)
        for di in (0, 1):
            for tj in (0, 1):
                for field, (dr, dtemp) in enumerate(derivatives):
                    constraints[order * di + dr, order * tj + dtemp] = (
                        mp.mpf(float(data[j + tj, i + di, field])) * dd**dr * dt**dtemp)
        matrix = endpoint_matrix(order)
        coefficients = matrix**-1 * constraints * (matrix.T)**-1
        # Check all endpoint constraints independently of point evaluation.
        if mp.norm(matrix * coefficients * matrix.T - constraints, p="inf") > (
                mp.norm(constraints, p="inf") * mp.power(10, -mp.mp.dps + 8)):
            raise ArithmeticError("endpoint polynomial constraints failed")
        return coefficients, dn, tn, dd, dt

    def interpolate(self, rho, temperature, dr=0, dtemp=0, order=3):
        density = rho * self.ye
        i = max(0, min(IMAX - 2, int(mp.floor((mp.log10(density) + 12) * 20))))
        j = max(0, min(JMAX - 2, int(mp.floor((mp.log10(temperature) - 3) * 20))))
        coef, dn, tn, dd, dt = self.polynomial(i, j, order, mp.mp.prec)
        x, y = (density - dn) / dd, (temperature - tn) / dt
        return mp.fsum(coef[k, l] * mp.factorial(k) / mp.factorial(k - dr)
                       * mp.factorial(l) / mp.factorial(l - dtemp)
                       * x**(k - dr) * y**(l - dtemp) / dd**dr / dt**dtemp
                       for k in range(dr, 2 * order) for l in range(dtemp, 2 * order))

    def coulomb_energy(self, rho, temperature):
        nion = rho * self.ytot * self.na
        zbar = self.ye / self.ytot
        coupling = zbar**2 * self.charge_squared * (4 * mp.pi * nion / 3)**(mp.mpf(1) / 3) / (self.kb * temperature)
        coefficient = self.na * self.ytot * self.kb * temperature
        if coupling >= 1:
            return coefficient * (-mp.mpf("0.898004") * coupling
                                  + mp.mpf("0.96786") * coupling**mp.mpf("0.25")
                                  + mp.mpf("0.220703") / coupling**mp.mpf("0.25")
                                  - mp.mpf("0.86097"))
        return coefficient * (-3 * mp.mpf("0.288675") * coupling**mp.mpf("1.5")
                              + mp.mpf("0.29561") * coupling**mp.mpf("1.9885"))

    def state(self, rho, temperature):
        free = self.interpolate(rho, temperature)
        df_t = self.interpolate(rho, temperature, dtemp=1)
        pele = (rho * self.ye)**2 * self.interpolate(rho, temperature, dr=1)
        eele = self.ye * (free - temperature * df_t)
        cvele = -self.ye * temperature * self.interpolate(rho, temperature, dtemp=2)
        pion = rho * self.ytot * self.na * self.kb * temperature
        erad = self.radiation * temperature**4 / rho
        pressure = pele + pion + erad * rho / 3
        energy = eele + mp.mpf("1.5") * pion / rho + erad
        cv = cvele + mp.mpf("1.5") * self.ytot * self.na * self.kb + 4 * erad / temperature
        ecoul = self.coulomb_energy(rho, temperature)
        if pressure + rho * ecoul / 3 > 0 and energy + ecoul > 0:
            pressure += rho * ecoul / 3
            energy += ecoul
            cv += mp.diff(lambda t: self.coulomb_energy(rho, t), temperature)
        return dict(P=pressure, E=energy, cv=cv, pele=pele,
                    xne=rho * self.ye * self.na,
                    eta=self.interpolate(rho, temperature, order=2))

    def pressure(self, rho, temperature):
        return self.state(rho, temperature)["P"]

    def differentials(self, rho, temperature):
        """Independent derivatives of the endpoint-fit polynomial/model.

        Composition coordinates are y=sum(X/A), z=Ye. mpmath differentiates
        this reference, not ARCH's derivative expressions or finite stencils.
        """
        y, z = self.ytot, self.ye
        def composition(a, b, quantity):
            try:
                self.ytot, self.ye = a, b
                return self.state(rho, temperature)[quantity]
            finally:
                self.ytot, self.ye = y, z
        return dict(
            pressure_density=mp.diff(lambda r: self.pressure(r, temperature), rho),
            pressure_temperature=mp.diff(lambda t: self.pressure(rho, t), temperature),
            energy_y=mp.diff(lambda a: composition(a, z, "E"), y),
            energy_z=mp.diff(lambda b: composition(y, b, "E"), z),
            energy_yy=mp.diff(lambda a: composition(a, z, "E"), y, 2),
            energy_yz=mp.diff(lambda a, b: composition(a, b, "E"), (y, z), (1, 1)),
            energy_zz=mp.diff(lambda b: composition(y, b, "E"), z, 2),
            cv_y=mp.diff(lambda a: composition(a, z, "cv"), y),
            cv_z=mp.diff(lambda b: composition(y, b, "cv"), z),
            # The quintic is C2, not C3: at a T node use the selected right
            # cell's one-sided third derivative, as the production locator does.
            cv_temperature=mp.diff(lambda t: self.state(rho, t)["cv"], temperature, direction=1))


def references(free_energy, eta, digits):
    with mp.workdps(digits):
        model = HelmReference(free_energy, eta)
        rounded_grid = HelmReference(free_energy, eta, binary64_grid=True)
        rho, temperature = mp.mpf("1e6"), mp.mpf("1e8")
        state = model.state(rho, temperature)
        derivatives = model.differentials(rho, temperature)
        state["dp_drho"] = derivatives["pressure_density"]
        state["dp_dT"] = derivatives["pressure_temperature"]
        state["sound_speed"] = mp.sqrt(state["dp_drho"] + state["dp_dT"]**2 * temperature / (rho**2 * state["cv"]))
        energy_density = mp.diff(lambda r: model.state(r, temperature)["E"], rho)
        maxwell_error = abs(rho**2 * energy_density - state["P"] + temperature * state["dp_dT"])
        if maxwell_error > abs(state["P"]) * mp.power(10, -digits + 12):
            raise ArithmeticError("reference energy/pressure violates the first law")
        dp_drho_e = state["dp_drho"] - state["dp_dT"] * energy_density / state["cv"]
        dp_de_rho = state["dp_dT"] / state["cv"]
        # Probe field order matches the public EOS surface, not an iterative
        # implementation. Exact inverses recover the prescribed thermodynamic state.
        probe = [mp.mpf("1.4"), state["cv"], state["P"], state["E"], temperature,
                 state["P"], state["sound_speed"], dp_drho_e, dp_de_rho,
                 rho * (state["E"] + mp.mpf(7) / 16)]
        probe += [state[name] for name in ("P", "E", "cv", "sound_speed", "dp_drho",
                                           "dp_dT", "pele", "xne", "eta")]
        lower = model.pressure(mp.mpf("1e-12") / model.ye, mp.mpf("1e3"))
        upper = model.pressure(mp.mpf("1e15") / model.ye, mp.mpf("1e13"))
        witnesses = []
        for density, temp in (("1e9", "1e6"), ("1e9", "1e8"),
                              ("1e-6", "1e9")):
            point = model.state(mp.mpf(density), mp.mpf(temp))
            witnesses.append(dict(rho=float(density).hex(), T=float(temp).hex(),
                                  **{name: float(value).hex() for name, value in point.items()},
                                  derivatives={name: float(value).hex() for name, value in
                                      model.differentials(mp.mpf(density), mp.mpf(temp)).items()},
                                  binary64_grid_derivatives={name: float(value).hex() for name, value in
                                      rounded_grid.differentials(mp.mpf(float(density)), mp.mpf(float(temp))).items()}))
        return dict(state={name: float(value).hex() for name, value in state.items()},
                    derivatives={name: float(value).hex() for name, value in derivatives.items()},
                    binary64_grid_derivatives={name: float(value).hex() for name, value in
                        rounded_grid.differentials(rho, temperature).items()},
                    probe=[float(value).hex() for value in probe],
                    witnesses=witnesses,
                    lower_bound_P=float(lower).hex(), upper_bound_P=float(upper).hex())


def isentrope_reference(free_energy, eta):
    """Independent DOP853 in pressure coordinates, not ARCH's RK4/root loop.

    Use the exact derivative of the independently fitted free energy for
    the entropy path as well as the change from ln(rho) to ln(P).
    """
    with mp.workdps(60):
        model = HelmReference(free_energy, eta)
        def derivative(_, logarithms):
            rho, temp = (mp.exp(mp.mpf(float(value))) for value in logarithms)
            state = model.state(rho, temp)
            pr = mp.diff(lambda r: model.pressure(r, temp), rho)
            pt = mp.diff(lambda t: model.pressure(rho, t), temp)
            slope = pt / (rho * state["cv"])
            density_slope = state["P"] / (rho * pr + temp * pt * slope)
            return [float(density_slope), float(slope * density_slope)]
        solution = solve_ivp(derivative, (0.0, float(mp.log(mp.mpf("1.001")))),
                             np.log([1e6, 1e8]), method="DOP853", rtol=2.3e-14,
                             atol=1e-15)
        if not solution.success:
            raise ArithmeticError("independent isentrope integration failed")
        rho, temp = (mp.exp(mp.mpf(float(value))) for value in solution.y[:, -1])
        state = model.state(rho, temp)
        pr = mp.diff(lambda r: model.pressure(r, temp), rho)
        pt = mp.diff(lambda t: model.pressure(rho, t), temp)
        cs = mp.sqrt(pr + pt**2 * temp / (rho**2 * state["cv"]))
        if abs(state["P"] / (mp.mpf("1.001") * model.pressure(mp.mpf("1e6"), mp.mpf("1e8"))) - 1) > mp.mpf("1e-13"):
            raise ArithmeticError("independent isentrope target pressure failed")
        return dict(rho=float(rho).hex(), temperature=float(temp).hex(),
                    pressure=float(state["P"]).hex(), sound_speed=float(cs).hex())


def main():
    values = np.fromfile(TABLE, sep=" ")
    points = IMAX * JMAX
    if values.size != 21 * points:
        raise ValueError("table extent is not the documented four-block layout")
    free_energy = values[:9 * points].reshape(JMAX, IMAX, 9)
    eta = values[13 * points:17 * points].reshape(JMAX, IMAX, 4)
    low, high = references(free_energy, eta, 60), references(free_energy, eta, 80)
    if low != high:
        raise ArithmeticError("independent EOS references differ with precision")
    print(json.dumps(dict(oracle="monomial endpoint fit; mpmath 60/80 digits",
                          table_sha256=hashlib.sha256(TABLE.read_bytes()).hexdigest(),
                          isentrope=isentrope_reference(free_energy, eta),
                          **high), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
