"""Optional rigorous one-angle full-ring surface/contact reference.

Workflow
--------
1. Freeze explicit actual FP64 leaf bounds, rhoV, source stamps and observers.
2. Check exact rational non-overlap and complete advertised root coverage.
3. Load the already available optional python-flint backend, never install it.
4. Integrate exact regular kernels on [0,pi/2] and [pi/2,pi] using optional
   acb.integral with every root/log forwarding its analytic callback flag.
   Axis observers use analytic limits; contact log corrections are added once.
5. Accumulate ALL actual leaves for EVERY observer within one shared budget;
   require the final real interval widths to meet the caller's original budget.

A mathematical certificate describes ONLY the exact supplied rings/observers.
Core source/observer authentication and physical scientific acceptance remain
separate and false. The reference operates independently of production kernels and does not infer
density from Poisson RHS, soften the force or estimate accuracy from a
Gauss-order difference. Core authentication is a separate responsibility.

For d=|y-x|, div_y((y-x)/d)=2/d:
    Phi = -G rho/2 integral_boundary ((y-x).n)/d dS,
    g   = -G rho   integral_boundary n/d dS.
Each full-circle regular integral is 2*pi*int_0^1 f(pi*t) dt. Analytic contact
corrections returned below already represent the FULL circle, with no extra
2*pi multiplier. Actual CGS G is the exact stored binary64 value.
"""
from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
import hashlib
import importlib
import json
import math
from pathlib import Path
import sys
import time
from typing import Any


MAX_CALLS = 100000
TIMEOUT_SECONDS = 90.0
CGS_G = Fraction(6.67430e-8)
PROFILE = "native-ring-one-angle-kernels-1"


class ReferenceFailure(ValueError):
    """Reject invalid exact input or an unresolved real interval branch."""


class WorkLimit(RuntimeError):
    """The ONE shared callback/time budget was exhausted, without acceptance."""


@dataclass
class Budget:
    """One run budget, shared across all observers, leaves and components.

    Calls count real kernel range evaluations, not individual elementary arb
    operations. A caller must not construct a fresh budget per contribution.
    Wall time includes its preflight/evaluation after this object is created.
    No cap or timeout argument is exposed that could raise the frozen limits.
    """
    started: float
    calls: int = 0

    @classmethod
    def start(cls) -> Budget:
        """Start one monotonic budget for the entire reference request."""
        return cls(time.monotonic())

    def check_time(self) -> None:
        """Check shared wall time during preflight as well as callbacks."""
        if time.monotonic() - self.started >= TIMEOUT_SECONDS:
            raise WorkLimit("global timeout=90 seconds reached")

    def take(self) -> None:
        """Charge a callback before evaluating it; do not reset after failure."""
        self.check_time()
        if self.calls >= MAX_CALLS:
            raise WorkLimit("global max_calls=100000 reached")
        self.calls += 1

    def record(self) -> dict[str, Any]:
        """Report measured local accounting separately from algorithm quality."""
        return {"calls": self.calls, "max_calls": MAX_CALLS,
                "wall_seconds": time.monotonic() - self.started,
                "timeout_seconds": TIMEOUT_SECONDS}


def rational(value: Any) -> Fraction:
    """Keep each supplied finite FP64/decimal/rational value EXACT, not idealized.

    Fraction(float) preserves the actual FP64 bits; decimal strings preserve
    their own explicitly supplied decimal meaning. No input is rounded to a
    guessed root formula. The caller must retain its original record as well.
    """
    if isinstance(value, bool):
        raise ReferenceFailure("Boolean is not a physical numeric value")
    if isinstance(value, float) and not math.isfinite(value):
        raise ReferenceFailure("Nonfinite actual numeric input")
    try:
        return Fraction(value)
    except (TypeError, ValueError, ZeroDivisionError, OverflowError) as exc:
        raise ReferenceFailure("Missing or invalid exact numeric input") from exc


@dataclass(frozen=True)
class Leaf:
    """One immutable full-azimuth piecewise-constant actual density rectangle."""
    identity: str
    L: Fraction
    H: Fraction
    A: Fraction
    B: Fraction
    rho: Fraction

    @classmethod
    def from_record(cls, record: dict[str, Any]) -> Leaf:
        """Read existing explicit leaf names; never infer rho from Poisson RHS."""
        name = record.get("id")
        if not isinstance(name, str) or not name:
            raise ReferenceFailure("Missing actual leaf id")
        values = [rational(record[k]) for k in
                  ("r_lower", "r_upper", "z_lower", "z_upper", "density")]
        L, H, A, B, rho = values
        if not (0 <= L < H and A < B and rho >= 0):
            raise ReferenceFailure("Invalid full-ring bounds/density")
        return cls(name, *values)


@dataclass(frozen=True)
class Observer:
    """Immutable actual point observer with an opaque preserved Core identity.

    A cell-center or face-fragment-center binding must be supplied/reviewed by
    the Core adapter. This mathematical leaf does not replace it by a midpoint,
    infer a fragment area average, or authenticate a label supplied by a caller.
    """
    identity: str
    R: Fraction
    Z: Fraction

    @classmethod
    def from_record(cls, record: dict[str, Any]) -> Observer:
        """Preserve arbitrary explicit actual point coordinates and its id."""
        name = record.get("id")
        if not isinstance(name, str) or not name:
            raise ReferenceFailure("Missing observer id")
        R, Z = map(rational, (record["r_observer"], record["z_observer"]))
        if R < 0:
            raise ReferenceFailure("Observer radius must be nonnegative")
        return cls(name, R, Z)


def validate_dense_source(source: dict[str, Any], root_bounds: Any,
                          source_identity: dict[str, Any],
                          budget: Budget | None = None) -> tuple[Leaf, ...]:
    """Check exact dense rectangle coverage and preserve actual input stamps.

    Reuses existing source leaf field names. Bounds are the EXPLICIT actual
    canonical bounds, not the old origin+spacing ideal adapter. Containment,
    pairwise positive-area non-overlap and exact area equality prove coverage
    except measure-zero interfaces. Zero-density leaves remain in this proof.
    Stamp validation checks shape/consistency only; it is not Runtime authority.
    """
    if not isinstance(source.get("sourceId"), str) or not source["sourceId"]:
        raise ReferenceFailure("Missing sourceId")
    if not isinstance(source_identity, dict):
        raise ReferenceFailure("Missing actual source identity")
    for key in ("topology", "operator_revision", "boundary_revision",
                "accuracy_revision", "generation"):
        if type(source_identity.get(key)) is not int or source_identity[key] <= 0:
            raise ReferenceFailure("Missing/nonpositive source revision: " + key)
    inputs = source_identity.get("inputs")
    if not isinstance(inputs, list) or not inputs:
        raise ReferenceFailure("Missing actual source dependencies")
    seen_dependencies = set()
    for item in inputs:
        if budget is not None:
            budget.check_time()
        if not isinstance(item, dict):
            raise ReferenceFailure("Malformed actual source dependency")
        if (item.get("epoch") != source_identity["topology"] or
                type(item.get("slot")) is not int or item["slot"] not in (0, 1, 2)):
            raise ReferenceFailure("Source epoch/slot mismatch")
        for key in ("uid", "version", "storage_generation"):
            if type(item.get(key)) is not int or item[key] <= 0:
                raise ReferenceFailure("Missing/nonpositive source dependency")
        dependency = (item["uid"], item["slot"])
        if dependency in seen_dependencies:
            raise ReferenceFailure("Duplicate source uid/slot dependency")
        seen_dependencies.add(dependency)
    rational(source_identity["time"])
    if rational(source_identity["G"]) != CGS_G:
        raise ReferenceFailure("G is not the actual stored shared CGS constant")
    if len(root_bounds) != 4:
        raise ReferenceFailure("Missing explicit actual root bounds")
    L, H, A, B = map(rational, root_bounds)
    if not (0 <= L < H and A < B):
        raise ReferenceFailure("Invalid advertised actual root rectangle")
    records = source.get("leaves")
    if not isinstance(records, list) or not records:
        raise ReferenceFailure("Missing dense source leaves")
    leaves = tuple(Leaf.from_record(record) for record in records)
    if len({leaf.identity for leaf in leaves}) != len(leaves):
        raise ReferenceFailure("Duplicate actual source leaf identity")
    area = Fraction(0)
    for i, leaf in enumerate(leaves):
        if budget is not None:
            budget.check_time()
        if not (L <= leaf.L < leaf.H <= H and A <= leaf.A < leaf.B <= B):
            raise ReferenceFailure("Leaf outside actual root rectangle")
        area += (leaf.H - leaf.L) * (leaf.B - leaf.A)
        for previous in leaves[:i]:
            if budget is not None:
                budget.check_time()
            if (max(previous.L, leaf.L) < min(previous.H, leaf.H) and
                    max(previous.A, leaf.A) < min(previous.B, leaf.B)):
                raise ReferenceFailure("Positive-area source overlap")
    if area != (H - L) * (B - A):
        raise ReferenceFailure("Actual dense source leaves do not cover root")
    return leaves


def input_stamp(source: dict[str, Any], root_bounds: Any,
                identity: dict[str, Any], observers: list[dict[str, Any]]) -> dict[str, Any]:
    """Digest the EXACT supplied input record independently of output estimates.

    Source revisions and actual observers are kept verbatim. This does not
    pretend the old ideal-coordinate adapter certifies current canonical data.
    """
    payload = {"source": source, "root_bounds": root_bounds,
               "source_identity": identity, "observers": observers}
    raw = json.dumps(payload, sort_keys=True, separators=(",", ":"),
                     allow_nan=False).encode("utf-8")
    return {"sha256": hashlib.sha256(raw).hexdigest(), "input": payload,
            "core_binding_qualified": False}


def load_optional_flint(existing_dependency_directory: Path | None = None) -> Any:
    """Load a preexisting backend only; an explicit local directory is optional.

    No download, install, API call, CLI provider, precision escalation or global
    context change is performed. Root's existing flint precision remains active.
    Missing support is a dependency gap, not permission to use scalar estimates.
    """
    if existing_dependency_directory is not None:
        directory = existing_dependency_directory.resolve(strict=True)
        if not (directory / "flint").is_dir():
            raise ReferenceFailure("Explicit local reference dependency lacks flint")
        sys.path.insert(0, str(directory))
    try:
        return importlib.import_module("flint")
    except ImportError as exc:
        raise ReferenceFailure("Optional preexisting python-flint unavailable") from exc


def _exact(arb: Any, value: Fraction) -> Any:
    """Outward real-ball encoding of an exact rational input."""
    return arb(value.numerator) / value.denominator


def _sign(value: Fraction) -> int:
    """Select a branch using exact actual source coordinates, including zero."""
    return (value > 0) - (value < 0)


def _asinh(value: Any) -> Any:
    """Use the backend's enclosing real asinh; no scalar logarithm substitute."""
    return value.asinh()


def _finite(*values: Any) -> None:
    """Reject a nonfinite real range; no midpoint, floor or branch guessing."""
    if not all(value.is_finite() for value in values):
        raise ReferenceFailure("Unresolved/nonfinite real surface interval")


def axis_values(leaf: Leaf, observer: Observer, backend: Any,
                budget: Budget) -> tuple[Any, Any, Any]:
    """Outward analytic single-leaf (Phi,0,g_z) limit at exactly R==0.

    P_s(u)=(u*sqrt(s^2+u^2)+s^2*asinh(u/s))/2; P_0(u)=u*|u|/2.
    Full-source accumulation and its stated precision target remain the caller's
    bounded obligation; this does not grant a complete-source certificate.
    """
    if observer.R:
        raise ReferenceFailure("Axis formula requires actual R==0")
    budget.take()
    arb = backend.arb
    def primitive(s: Fraction, u: Fraction) -> Any:
        """Use the exact removable s=0 branch, not zero times infinity."""
        x, y = _exact(arb, s), _exact(arb, u)
        return (y * _exact(arb, abs(u)) / 2 if not s else
                (y * (x ** 2 + y ** 2).sqrt() + x ** 2 * _asinh(y / x)) / 2)
    def radial_integral(u: Fraction) -> Any:
        """Rationalize sqrt(H^2+u^2)-sqrt(L^2+u^2) without cancellation."""
        hi, lo, y = map(lambda q: _exact(arb, q), (leaf.H, leaf.L, u))
        return _exact(arb, leaf.H ** 2 - leaf.L ** 2) / ((hi ** 2 + y ** 2).sqrt() + (lo ** 2 + y ** 2).sqrt())
    alpha, beta = leaf.B - observer.Z, leaf.A - observer.Z
    scale = 2 * arb.pi() * _exact(arb, CGS_G * leaf.rho)
    phi = -scale * ((primitive(leaf.H, alpha) - primitive(leaf.H, beta)) -
                    (primitive(leaf.L, alpha) - primitive(leaf.L, beta)))
    gz = scale * (radial_integral(beta) - radial_integral(alpha))
    _finite(phi, gz)
    return phi, arb(0), gz



# Deterministic root-owned acceptance inventory: proposed cases, NOT results.
CONTACT_CASES = (
    ("axis", 0, 0), ("outside", 4, 2), ("inside", 2, 0),
    ("inner_cylinder", 1, 0), ("outer_cylinder", 3, 0),
    ("cap_interior", 2, 1), ("cap_inner_edge", 1, 1),
    ("cap_outer_edge", 3, 1), ("lower_corner", 1, -1),
)


class IntegralFailure(ReferenceFailure):
    """A bounded certified integration failed to produce the requested interval."""


def _complex_cylinder(backend: Any, s: Fraction, R: Fraction,
                      alpha: Fraction, beta: Fraction, theta: Any,
                      analytic: bool) -> tuple[Any, int]:
    """Exact one-sided cylinder regular kernel with checked complex branches.

    No unchecked complex asinh is used. For a fixed real endpoint u,
    asinh(u/h)=sgn(u)*log((|u|+sqrt(u^2+h^2))/h). Contact factors the h logarithm
    exactly, leaving roots/logs with nonzero positive real arguments at theta=0.
    Every root/log forwards the official integration callback's analytic flag.
    """
    acb, arb = backend.acb, backend.arb
    x, ro = acb(_exact(arb, s)), acb(_exact(arb, R))
    sh = (theta / 2).sin()
    delta = acb(_exact(arb, s - R))
    h2 = delta * delta + 4 * x * ro * sh * sh
    contact = s == R
    h = None if contact else h2.sqrt(analytic=analytic)
    value = acb(0)
    for endpoint, direction in ((alpha, 1), (beta, -1)):
        if not endpoint:
            continue
        absolute = acb(_exact(arb, abs(endpoint)))
        numerator = absolute + (absolute * absolute + h2).sqrt(analytic=analytic)
        denominator = ro if contact else h
        value += direction * _sign(endpoint) * (numerator / denominator).log(analytic=analytic)
    return value, (_sign(alpha) - _sign(beta) if contact else 0)


def _complex_cap(backend: Any, L: Fraction, H: Fraction, R: Fraction,
                 t: Fraction, theta: Any, half: int,
                 analytic: bool) -> tuple[Any, int]:
    """Frozen globally regular disk kernel on either [0,pi/2] or [pi/2,pi].

    First-half contact remainder subtracts kappa*R*cos(theta)*log(2*sinhalf).
    Its Q+/Q- factorization is globally valid on that half without guessing the
    sign of r-R*cos(theta). On the second half all Q+ are strictly positive.
    Edge distance d_R=2R*sinhalf is the one-sided analytic extension. All sqrt
    and log operations are branch-checked by forwarding analytic to acb.
    """
    acb, arb = backend.acb, backend.arb
    lo, hi, ro = [acb(_exact(arb, q)) for q in (L, H, R)]
    gapL, gapH = [acb(_exact(arb, q - R)) for q in (L, H)]
    axial = acb(_exact(arb, t))
    sh, ch = (theta / 2).sin(), (theta / 2).cos()
    a = ro * theta.cos()
    uL, uH = lo - a, hi - a
    # Contact endpoints use the exact one-sided analytic distance, never sqrt(0).
    dL = (2 * ro * sh if not t and L == R else
          (gapL * gapL + 4 * lo * ro * sh * sh + axial * axial).sqrt(analytic=analytic))
    dH = (2 * ro * sh if not t and H == R else
          (gapH * gapH + 4 * hi * ro * sh * sh + axial * axial).sqrt(analytic=analytic))
    if half == 1:
        return dH - dL + a * ((uH + dH) / (uL + dL)).log(analytic=analytic), 0
    if t:
        # Fixed representation choice, not a guessed sign of a complex ball.
        ratio = ((dL - uL) / (dH - uH) if R > H else
                 (uH + dH) / (uL + dL))
        return dH - dL + a * ratio.log(analytic=analytic), 0
    if L < R < H:
        regular = ((uH + dH) / ro).log(analytic=analytic)
        regular += ((dL - uL) / ro).log(analytic=analytic)
        regular -= 2 * ch.log(analytic=analytic)
        return dH - dL + a * regular, 2
    if R == L:
        regular = ((uH + dH) / ro).log(analytic=analytic)
        regular -= (1 + sh).log(analytic=analytic)
        return dH - dL + a * regular, 1
    if R == H:
        regular = ((1 + sh) * (dL - uL) / ro).log(analytic=analytic)
        regular -= 2 * ch.log(analytic=analytic)
        return dH - dL + a * regular, 1
    ratio = ((dL - uL) / (dH - uH) if R > H else
             (uH + dH) / (uL + dL))
    return dH - dL + a * ratio.log(analytic=analytic), 0


def _complex_regular_component(backend: Any, leaf: Leaf, observer: Observer,
                               theta: Any, half: int, component: int,
                               analytic: bool) -> Any:
    """Combine actual signed surfaces for ONE physical component, without writes.

    Components 0,1,2 mean Phi,g_r,g_z. The frozen physical density is retained
    for every actual leaf; no equal-density union or interface omission occurs.
    Angular integration is over 0..pi and its answer is doubled exactly once.
    """
    acb, arb = backend.acb, backend.arb
    value = acb(0)
    R, Z = observer.R, observer.Z
    ro = acb(_exact(arb, R))
    factor = acb(_exact(arb, CGS_G * leaf.rho))
    if component != 2:
        for s, sigma in ((leaf.L, -1), (leaf.H, 1)):
            if not s:
                continue
            kernel, _ = _complex_cylinder(backend, s, R, leaf.B - Z,
                                           leaf.A - Z, theta, analytic)
            scale = sigma * acb(_exact(arb, s))
            if component == 1:
                value -= factor * scale * theta.cos() * kernel
            else:
                value -= factor * scale * (acb(_exact(arb, s)) - ro * theta.cos()) * kernel / 2
    if component != 1:
        for z, sigma in ((leaf.A, -1), (leaf.B, 1)):
            t = z - Z
            if component == 0 and not t:
                continue  # Exact zero cap potential BEFORE evaluating D.
            kernel, _ = _complex_cap(backend, leaf.L, leaf.H, R, t,
                                     theta, half, analytic)
            if component == 2:
                value -= factor * sigma * kernel
            else:
                value -= factor * sigma * acb(_exact(arb, t)) * kernel / 2
    return value


def _half_contact_primitive(backend: Any) -> Any:
    """Enclose I(pi/2)=log(sqrt(2))-pi/4-1/2 exactly.

    I'(q)=cos(q)*log(2*sin(q/2)), I(0)=0. This finite endpoint formula avoids
    evaluating zero*sin-log and gives the first-half disk log correction.
    These real arb sqrt/log operations have no complex analytic callback flag.
    """
    arb = backend.arb
    return arb(2).sqrt().log() - arb.pi() / 4 - arb(1) / 2


def _integrated_correction(backend: Any, leaf: Leaf,
                           observer: Observer, component: int) -> Any:
    """Return the exact FULL-circle integrated log correction ONCE.

    Cylinder: int_full cos*H = int_full cos*Hreg + k*pi;
              int_full(s-Rcos)*H = int_full(s-Rcos)*Hreg - k*pi*R.
    Contact disk: only first half was regularized, so int_full D receives
    -2*kappa*R*I(pi/2). Its potential prefactor t is identically zero.
    """
    arb = backend.arb
    pi, ro = arb.pi(), _exact(arb, observer.R)
    factor = _exact(arb, CGS_G * leaf.rho)
    value = arb(0)
    if component != 2:
        for s, sigma in ((leaf.L, -1), (leaf.H, 1)):
            if not s or s != observer.R:
                continue
            k = _sign(leaf.B - observer.Z) - _sign(leaf.A - observer.Z)
            scale = sigma * _exact(arb, s)
            value += (-factor * scale * k * pi if component == 1 else
                      factor * scale * k * pi * ro / 2)
    if component == 2:
        kappa = (2 if leaf.L < observer.R < leaf.H else
                 int(observer.R == leaf.L or observer.R == leaf.H))
        for z, sigma in ((leaf.A, -1), (leaf.B, 1)):
            if z == observer.Z and kappa:
                # g_z=-G*rho*sigma*int D, hence positive 2*G*rho*sigma*kR*I.
                value += 2 * factor * sigma * kappa * ro * _half_contact_primitive(backend)
    return value


def integrate_leaf(leaf: Leaf, observer: Observer, backend: Any,
                   target_widths: tuple[Fraction, Fraction, Fraction],
                   budget: Budget) -> tuple[Any, Any, Any]:
    """Integrate ALL exact leaf surfaces rigorously with one global budget.

    acb.integral uses checked analytic continuations and enclosing real balls.
    An unprovable callback branch returns nan so its own bounded subdivision
    can resolve it. Budget exhaustion is sticky and propagated after the call;
    it is never converted into a guessed value. No depth cap is increased.
    The final all-source interval width, not requested integrator tolerances,
    decides whether the caller obtained its exact original allowance.
    """
    arb, acb = backend.arb, backend.acb
    if not leaf.rho:
        return arb(0), arb(0), arb(0)
    if not observer.R:
        return axis_values(leaf, observer, backend, budget)
    values = []
    pi = acb(arb.pi())
    for component, allowance in enumerate(target_widths):
        total = acb(0)
        for half in (0, 1):
            failure = None
            def callback(theta: Any, analytic: bool) -> Any:
                """Charge real work and forward analytic to EVERY root/log kernel."""
                nonlocal failure
                if failure is not None:
                    return acb("nan")
                try:
                    budget.take()
                except WorkLimit as exc:
                    failure = exc
                    return acb("nan")
                value = _complex_regular_component(backend, leaf, observer,
                                                    theta, half, component, analytic)
                return value if value.is_finite() else acb("nan")
            # Each callback consumes the same budget, so per-call API limits
            # cannot multiply the global cap across components or leaves.
            remaining = MAX_CALLS - budget.calls
            if remaining <= 0:
                raise WorkLimit("global max_calls=100000 reached")
            # acb's absolute goal applies to EACH accepted subinterval; it is
            # not a global sum-error promise. Use a conservative FIXED work
            # request allocation; callback count alone proves no error bound.
            # The relative default uses CURRENT precision (no ctx mutation),
            # avoiding the special zero-relative sentinel exponent. Neither
            # internal goal replaces the mandatory ORIGINAL final-width gate.
            tolerance = _exact(arb, allowance / (16 * MAX_CALLS))
            part = acb.integral(callback, pi * half / 2, pi * (half + 1) / 2,
                                abs_tol=tolerance, rel_tol=None,
                                eval_limit=remaining, depth_limit=24)
            if failure is not None:
                raise failure
            if time.monotonic() - budget.started >= TIMEOUT_SECONDS:
                raise WorkLimit("global timeout=90 seconds reached")
            if not part.is_finite() or not part.imag.contains(0):
                raise IntegralFailure("No finite real certified angular integral")
            total += 2 * part
        real = total.real + _integrated_correction(backend, leaf, observer, component)
        _finite(real)
        values.append(real)
    return tuple(values)


def _ball_record(ball: Any) -> dict[str, str]:
    """Serialize outward exact endpoint rationals, not formatted midpoints.

    arb.lower/upper return enclosing endpoint balls. Their exact fmpq endpoint
    values are inherited from the existing checked local flint wrapper pattern.
    The human ball string is diagnostic only; acceptance uses the actual ball.
    """
    return {"lower_rational": str(ball.lower().fmpq()),
            "upper_rational": str(ball.upper().fmpq()),
            "ball": str(ball)}


def evaluate_reference(source: dict[str, Any], root_bounds: Any,
                       source_identity: dict[str, Any],
                       observer_records: list[dict[str, Any]],
                       target_widths: dict[str, Any],
                       existing_dependency_directory: Path | None = None) -> dict[str, Any]:
    """Request a complete mathematical source reference with original allowances.

    The caller supplies original absolute interval WIDTH allowances for Phi,
    g_r and g_z. No numeric tolerance is invented and no precision is changed.
    Every observer and contributing actual leaf must succeed. Math certification
    is independent of Core source/observer authentication and physical acceptance;
    the latter two remain false even when the rigorous intervals meet the request.
    """
    budget = Budget.start()
    stamp = None
    rows = []
    try:
        stamp = input_stamp(source, root_bounds, source_identity, observer_records)
        budget.check_time()
        leaves = validate_dense_source(source, root_bounds, source_identity, budget)
        if not isinstance(observer_records, list) or not observer_records:
            raise ReferenceFailure("Missing actual observers")
        observers = tuple(Observer.from_record(record) for record in observer_records)
        if len({observer.identity for observer in observers}) != len(observers):
            raise ReferenceFailure("Duplicate actual observer identity")
        allowances = tuple(rational(target_widths[key]) for key in ("Phi", "g_r", "g_z"))
        if any(value <= 0 for value in allowances):
            raise ReferenceFailure("Original interval width allowances must be positive")
        backend = load_optional_flint(existing_dependency_directory)
        # Deterministic strict error allocation is an integration request only;
        # outward all-leaf accumulation is checked against the ORIGINAL widths.
        local = tuple(value / len(leaves) for value in allowances)
        for observer in observers:
            total = [backend.arb(0), backend.arb(0), backend.arb(0)]
            for leaf in leaves:
                values = integrate_leaf(leaf, observer, backend, local, budget)
                for index in range(3):
                    total[index] += values[index]
            if time.monotonic() - budget.started >= TIMEOUT_SECONDS:
                raise WorkLimit("global timeout=90 seconds reached")
            finite = all(value.is_finite() for value in total)
            meets = finite and all(2 * value.rad() <= _exact(backend.arb, allowance)
                                   for value, allowance in zip(total, allowances))
            rows.append({"observer_id": observer.identity,
                         "observer": {"R_exact": str(observer.R), "Z_exact": str(observer.Z)},
                         "intervals": {key: _ball_record(value) for key, value in
                                       zip(("Phi", "g_r", "g_z"), total)},
                         "math_certificate_meets_target": meets})
            if not meets:
                raise IntegralFailure("Rigorous all-source interval width exceeds original request")
        return {"profile": "native-ring-one-angle-integrator-1",
                "status": "MathematicalIntervalsCertified", "certified": True,
                "science_accepted": False, "core_binding_qualified": False,
                "identity": stamp, "target_widths_exact": dict(zip(("Phi", "g_r", "g_z"),
                                                                     map(str, allowances))),
                "rows": rows, "budget": budget.record(),
                "backend": {"python_flint_version": backend.__version__, "ctx_dps": backend.ctx.dps},
                "scope": "Exact supplied dense piecewise-constant full rings and supplied point observers only",
                "remaining_gap": "Actual canonical Core source/observer authentication and Runtime/scientific consumer acceptance"}
    except (ReferenceFailure, WorkLimit, KeyError, TypeError, ValueError, ArithmeticError) as exc:
        return {"profile": "native-ring-one-angle-integrator-1",
                "status": "WorkLimit" if isinstance(exc, WorkLimit) else "Unverified",
                "certified": False, "science_accepted": False,
                "core_binding_qualified": False, "identity": stamp,
                "rows": rows, "budget": budget.record(), "failure": str(exc),
                "scope": "No complete all-observer mathematical certificate; earlier rows do not promote a subset"}
