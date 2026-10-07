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

from dataclasses import dataclass, field
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
MAX_GEOMETRY_MEMO_ENTRIES = 65536


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
    The ordinary request retains its frozen 90s/100000 profile. The separate
    full-domain diagnostic uses a manager-frozen aggregate resource profile;
    Fixed matched-resolution requests use the same math and their own immutable
    resource policies; no profile changes reference precision or physical acceptance.
    No user-facing cap or per-contribution reset is exposed.
    """
    started: float
    calls: int = 0
    _full_domain_diagnostic: bool = False
    _matched_resolution: int = 0
    _full_domain_extended: bool = False
    # Exact mathematical geometry only; owned by this ONE cumulative request.
    # Neither source/field authority nor source density is retained here.
    _geometry_memo: _LeafGeometryMemo | None = field(default=None, init=False,
                                                    repr=False, compare=False)

    @property
    def resource_profile(self) -> str:
        """Read one frozen internal policy; arbitrary/conflicting profiles fail."""
        if (type(self._full_domain_diagnostic) is not bool
                or type(self._matched_resolution) is not int
                or type(self._full_domain_extended) is not bool):
            raise ValueError("Invalid frozen reference resource profile")
        if (self._matched_resolution not in (0, 1, 2)
                or sum((bool(self._matched_resolution), self._full_domain_diagnostic,
                        self._full_domain_extended)) > 1):
            raise ValueError("Invalid/conflicting frozen reference resource profile")
        if self._full_domain_extended:
            return "full-domain-extended-1"
        if self._matched_resolution:
            return "matched-resolution-" + str(self._matched_resolution)
        return "full-domain-diagnostic-1" if self._full_domain_diagnostic else "bounded-request-1"

    @property
    def matched_resolution_level(self) -> int:
        """Return the internal fixed-layout selector, never a physics control."""
        self.resource_profile  # Validate the complete profile combination.
        return self._matched_resolution

    @property
    def max_calls(self) -> int:
        """Read the ONE request's immutable-by-contract callback profile."""
        profile = self.resource_profile
        if profile == "full-domain-extended-1": return 10000000
        if profile == "matched-resolution-1": return 6000000
        if profile == "matched-resolution-2": return 22000000
        return 1800000 if self._full_domain_diagnostic else MAX_CALLS

    @property
    def timeout_seconds(self) -> float:
        """Read the same aggregate wall profile used by every contribution."""
        profile = self.resource_profile
        if profile == "full-domain-extended-1": return 600.0
        if profile == "matched-resolution-1": return 600.0
        if profile == "matched-resolution-2": return 1800.0
        return 240.0 if self._full_domain_diagnostic else TIMEOUT_SECONDS

    @classmethod
    def full_domain_diagnostic(cls) -> Budget:
        """Start the distinct complete-domain diagnostic, never a science grant.

        One cumulative 240s/1800000-callback budget includes dense validation,
        symmetry proofs and every source/observer/component. Earlier requests'
        costs stay in their separate historical records; they are not reset.
        """
        return cls(time.monotonic(), _full_domain_diagnostic=True)

    @classmethod
    def full_domain_extended(cls) -> Budget:
        """Start ONE explicit maintainer-only generic 600s/10000000 request.

        Validation, all observers, callbacks and output share this clock. The
        dense source needs its existing complete authentication/coverage, not
        a fixed cell count. Precision, widths and every kernel stay unchanged.
        The external campaign owner must include earlier costs in its own
        frozen cap; choosing this profile does not reset a campaign budget.
        """
        return cls(time.monotonic(), _full_domain_extended=True)

    @classmethod
    def matched_resolution(cls, level: int) -> Budget:
        """Start ONE distinct fixed 2048/8192-cell complete diagnostic request.

        Shared kernel formulas, 70 dps and all interval widths are unchanged.
        The external serial owner enforces the two-request 2400s batch cap;
        unused allowance is never transferred and old request costs persist.
        """
        if type(level) is not int or level not in (1, 2):
            raise ValueError("Matched reference resource profile requires fixed level 1 or 2")
        return cls(time.monotonic(), _matched_resolution=level)

    @classmethod
    def start(cls) -> Budget:
        """Start one monotonic budget for the entire reference request."""
        return cls(time.monotonic())

    def check_time(self) -> None:
        """Check shared wall time during preflight as well as callbacks."""
        if time.monotonic() - self.started >= self.timeout_seconds:
            raise WorkLimit(f"global timeout={self.timeout_seconds:g} seconds reached")

    def take(self) -> None:
        """Charge a callback before evaluating it; do not reset after failure."""
        self.check_time()
        if self.calls >= self.max_calls:
            raise WorkLimit(f"global max_calls={self.max_calls} reached")
        self.calls += 1

    def record(self) -> dict[str, Any]:
        """Report measured local accounting separately from algorithm quality."""
        result = {"calls": self.calls, "max_calls": self.max_calls,
                  "wall_seconds": time.monotonic() - self.started,
                  "timeout_seconds": self.timeout_seconds,
                  "resource_profile": self.resource_profile}
        if self._geometry_memo is not None:
            result["geometry_memo"] = self._geometry_memo.record()
        return result

    def _memo_for(self, backend: Any) -> _LeafGeometryMemo:
        """Borrow ONE run-local memo across calls sharing this actual Budget.

        The original clock/calls/profile stay unchanged. Backend or precision
        drift rejects reuse; a source/layout/density change is never inferred
        from a previous request because every contribution is requalified.
        """
        self.check_time()
        if self._geometry_memo is None:
            self._geometry_memo = _LeafGeometryMemo(backend, self)
        self._geometry_memo.require_context(backend)
        return self._geometry_memo


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
    """Immutable full-ring rectangle with nonnegative integration density.

    from_record preserves the actual physical source. The extended reference
    can also construct internal positive-magnitude mathematical components;
    their signed coefficients never become physical input records.
    """
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


def _validate_nonoverlapping_rectangles(leaves: tuple[Leaf, ...], budget: Budget | None) -> None:
    """Reject positive-area overlap using exact Fraction rectangle events.

    Workflow: compress strictly positive open z segments, end intervals before
    starting intervals at the SAME exact r, then range-add each active z span.
    The tree's minimum/maximum count certify occupancy, never physical area or
    source density. Touching edges/corners have no positive-area intersection.
    Containment and the original exact root-area sum remain the caller's proof.
    This one sweep uses O(N log N) exact comparisons/integer updates and O(N)
    memory; every event checks the SAME caller budget, without charging kernels
    or creating a fresh deadline. No floats, epsilon, snapping or ideal mesh.
    """
    if budget is not None:
        budget.check_time()
    z_coordinates = sorted({coordinate for leaf in leaves for coordinate in (leaf.A, leaf.B)})
    segments = len(z_coordinates) - 1
    if segments < 1 or any(a >= b for a, b in zip(z_coordinates, z_coordinates[1:])):
        raise ReferenceFailure("Invalid exact source overlap segment partition")
    z_index = {coordinate: index for index, coordinate in enumerate(z_coordinates)}
    events = []
    for leaf in leaves:
        if budget is not None:
            budget.check_time()
        lower, upper = z_index[leaf.A], z_index[leaf.B]
        # Sorting delta -1 before +1 admits exact contact, but still checks each
        # start separately so overlapping same-r starts cannot hide in a batch.
        events.append((leaf.L, 1, lower, upper))
        events.append((leaf.H, -1, lower, upper))
    events.sort()
    if budget is not None:
        budget.check_time()
    minimum, maximum, lazy = ([0] * (4 * segments) for _ in range(3))

    def range_add(node, left, right, lower, upper, delta):
        """Add one count on [lower,upper); ancestor lazy counts stay exact."""
        if lower <= left and right <= upper:
            minimum[node] += delta
            maximum[node] += delta
            lazy[node] += delta
            return
        midpoint = (left + right) // 2
        if lower < midpoint:
            range_add(2 * node, left, midpoint, lower, upper, delta)
        if upper > midpoint:
            range_add(2 * node + 1, midpoint, right, lower, upper, delta)
        minimum[node] = lazy[node] + min(minimum[2 * node], minimum[2 * node + 1])
        maximum[node] = lazy[node] + max(maximum[2 * node], maximum[2 * node + 1])

    for _, delta, lower, upper in events:
        if budget is not None:
            budget.check_time()
        range_add(1, 0, segments, lower, upper, delta)
        if minimum[1] < 0:
            raise ReferenceFailure("Exact source overlap sweep has negative occupancy")
        if maximum[1] > 1:
            raise ReferenceFailure("Positive-area source overlap")
    if minimum[1] != 0 or maximum[1] != 0:
        raise ReferenceFailure("Exact source overlap sweep has unclosed occupancy")


def validate_dense_source(source: dict[str, Any], root_bounds: Any,
                          source_identity: dict[str, Any],
                          budget: Budget | None = None) -> tuple[Leaf, ...]:
    """Check exact dense rectangle coverage and preserve actual input stamps.

    Reuses existing source leaf field names. Bounds are the EXPLICIT actual
    canonical bounds, not the old origin+spacing ideal adapter. Containment,
    exact positive-area non-overlap and exact area equality prove coverage
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
    _validate_nonoverlapping_rectangles(leaves, budget)
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
            remaining = budget.max_calls - budget.calls
            if remaining <= 0:
                raise WorkLimit(f"global max_calls={budget.max_calls} reached")
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
            if time.monotonic() - budget.started >= budget.timeout_seconds:
                raise WorkLimit(f"global timeout={budget.timeout_seconds:g} seconds reached")
            if not part.is_finite() or not part.imag.contains(0):
                raise IntegralFailure("No finite real certified angular integral")
            total += 2 * part
        real = total.real + _integrated_correction(backend, leaf, observer, component)
        _finite(real)
        values.append(real)
    return tuple(values)



def _unit_geometry(leaf: Leaf, observer: Observer,
                   precision: tuple[Any, ...],
                   widths: tuple[Fraction, Fraction, Fraction]) -> tuple[Any, Leaf, Observer, int]:
    """Map exact axial translation/reflection to a unit-density integral key.

    Set Z=0 and retain EXACT a=A-Z,b=B-Z; no binary64 subtraction, mesh
    inference or density union occurs. (a,b)->(-b,-a) preserves Phi/g_r and
    reverses g_z, also on axis/contact by the continuous Newton force limit.
    The key contains G,R,L,H,these relative endpoints, the actual method and
    precision, and the actual qualifying unit-width request. IDs remain solely
    in the caller's original record; no normalized identity is exported.
    """
    if len(widths) != 3 or any(width <= 0 for width in widths):
        raise ReferenceFailure("Invalid qualified unit-density widths")
    L,H,A,B,rho,R,Z = map(rational, (leaf.L,leaf.H,leaf.A,leaf.B,leaf.rho,observer.R,observer.Z))
    if not (0 <= L < H and A < B and rho >= 0 and R >= 0):
        raise ReferenceFailure("Invalid exact geometry memo contribution")
    relative = (A-Z,B-Z)
    reflected = (-relative[1],-relative[0])
    sign = -1 if reflected < relative else 1
    a,b = reflected if sign == -1 else relative
    geometry = (CGS_G,R,L,H,a,b,precision)
    key = geometry + (tuple(widths),)
    return key, Leaf("unit-geometry-only",L,H,a,b,Fraction(1)), Observer("unit-origin-only",R,Fraction(0)), sign


def _widths_meet(values: tuple[Any, Any, Any],
                 widths: tuple[Fraction, Fraction, Fraction]) -> bool:
    """Check actual outward rational endpoints, including scaling roundoff.

    A requested tolerance or a memo admission label is never an interval proof.
    Endpoint extraction uses the same exact fmpq serialization as the existing
    reference, with no formatted midpoint or float certification.
    """
    return all(value.is_finite() and
               Fraction(str(value.upper().fmpq()))-Fraction(str(value.lower().fmpq())) <= width
               for value,width in zip(values,widths))


class _LeafGeometryMemo:
    """Bounded exact mathematical integral reuse, owned by ONE shared Budget.

    Workflow: normalize exact translation/reflection, locate a qualified
    unit-density enclosure, recheck its actual current unit/weighted widths,
    otherwise evaluate/refine through the UNCHANGED integrate_leaf and SAME
    cumulative budget. Multiply all three enclosing balls by actual rho;
    reflection changes only g_z. Final all-source acceptance stays the caller's
    ORIGINAL test. Two bounded maps hold at most65536 geometric entries;
    saturation drops admissions, never contributions. No source/field cache,
    backend installation, precision escalation or request-budget reset exists.
    """
    def __init__(self, backend: Any, budget: Budget, *, capacity: int | None = None):
        """Capture actual backend/precision/profile; zero capacity is test-only bypass."""
        capacity = MAX_GEOMETRY_MEMO_ENTRIES if capacity is None else capacity
        if type(capacity) is not int or not 0 <= capacity <= 65536:
            raise ReferenceFailure("Invalid bounded geometry memo capacity")
        self.backend, self.budget, self.capacity = backend, budget, capacity
        self.precision = self._precision(backend)
        self.policy = (budget.started,budget.resource_profile,budget.max_calls,budget.timeout_seconds)
        self.entries: dict[Any, tuple[Any, Any, Any]] = {}
        self.latest: dict[Any, Any] = {}
        self.hits = self.misses = self.refinements = self.admissions = 0
        self.integration_requests = self.capacity_refusals = self.bypasses = 0
        self.width_rejections = 0

    @staticmethod
    def _precision(backend: Any) -> tuple[Any, ...]:
        """Read exact method/optional-backend precision metadata without changing it."""
        try:
            dps,bits,version = backend.ctx.dps,backend.ctx.prec,backend.__version__
        except AttributeError as exc:
            raise ReferenceFailure("Missing actual optional-backend precision") from exc
        if type(dps) is not int or type(bits) is not int or dps <= 0 or bits <= 0 or not isinstance(version,str):
            raise ReferenceFailure("Invalid actual optional-backend precision")
        return PROFILE,version,dps,bits

    def require_context(self, backend: Any) -> None:
        """Reject backend/precision/resource drift; check wall time even on a hit."""
        self.budget.check_time()
        actual = (self.budget.started,self.budget.resource_profile,
                  self.budget.max_calls,self.budget.timeout_seconds)
        if backend is not self.backend or self._precision(backend) != self.precision or actual != self.policy:
            raise ReferenceFailure("Geometry memo backend/precision/budget drift")

    def _scaled(self, values: tuple[Any, Any, Any], rho: Fraction,
                sign: int) -> tuple[Any, Any, Any]:
        """Outward multiply actual rho and reflect axial force, never midpoint-rescale."""
        factor = _exact(self.backend.arb,rho)
        return values[0]*factor,values[1]*factor,values[2]*factor*sign

    def _admit(self, key: Any, values: tuple[Any, Any, Any]) -> None:
        """Replace a refined geometry entry or decline a saturated admission."""
        geometry = key[:-1]
        previous = self.latest.get(geometry)
        if previous is None and len(self.entries) >= self.capacity:
            self.capacity_refusals += 1
            return
        if previous is not None:
            del self.entries[previous]
        self.entries[key] = values
        self.latest[geometry] = key
        self.admissions += 1

    def integrate(self, leaf: Leaf, observer: Observer,
                  target_widths: tuple[Fraction, Fraction, Fraction],
                  unit_widths: tuple[Fraction, Fraction, Fraction]) -> tuple[Any, Any, Any]:
        """Return one actual weighted contribution or fail within the SAME budget.

        unit_widths initially=min original allowance/(N*maxrho) componentwise
        for this current source, even when another call shares the budget. The
        extra min(target/rho) is exact and supports tighter later requests.
        If cached or fresh actual width is insufficient, halve only the internal
        goal and reintegrate at unchanged precision/depth/cumulative limits.
        A finite lower precision floor therefore yields WorkLimit, never PASS.
        """
        self.require_context(self.backend)
        if len(target_widths) != 3 or any(width <= 0 for width in target_widths):
            raise ReferenceFailure("Invalid original contribution widths")
        rho = rational(leaf.rho)
        if rho < 0:
            raise ReferenceFailure("Negative actual density in geometry memo")
        if not rho:
            return self.backend.arb(0),self.backend.arb(0),self.backend.arb(0)
        if not self.capacity:
            self.bypasses += 1
            # Unmemoized diagnostic retains the original full-density kernel.
            values = integrate_leaf(leaf,observer,self.backend,target_widths,self.budget)
            if not _widths_meet(values,target_widths):
                raise IntegralFailure("Unmemoized actual contribution exceeds original width")
            return values
        goals = tuple(min(unit,target/rho) for unit,target in zip(unit_widths,target_widths))
        key,normalized,origin,sign = _unit_geometry(leaf,observer,self.precision,goals)
        previous = self.latest.get(key[:-1])
        if previous is not None:
            values = self.entries[previous]
            scaled = self._scaled(values,rho,sign)
            if _widths_meet(values,goals) and _widths_meet(scaled,target_widths):
                self.hits += 1
                self.require_context(self.backend)
                return scaled
            self.width_rejections += 1
            self.refinements += 1
        else:
            self.misses += 1
        while True:
            self.require_context(self.backend)
            self.integration_requests += 1
            values = integrate_leaf(normalized,origin,self.backend,goals,self.budget)
            self.require_context(self.backend)
            scaled = self._scaled(values,rho,sign)
            if _widths_meet(values,goals) and _widths_meet(scaled,target_widths):
                key = key[:-1]+(goals,)
                self._admit(key,values)
                return scaled
            self.width_rejections += 1
            self.refinements += 1
            goals = tuple(width/2 for width in goals)

    def record(self) -> dict[str, Any]:
        """Report work/admission observations separately from any certificate."""
        return {"profile":"run-local-exact-unit-ring-geometry-1",
                "capacity":self.capacity,"entries":len(self.entries),
                "hits":self.hits,"misses":self.misses,"refinements":self.refinements,
                "integration_requests":self.integration_requests,"admissions":self.admissions,
                "width_rejections":self.width_rejections,"capacity_refusals":self.capacity_refusals,
                "unmemoized_bypasses":self.bypasses,"statistics_only":True}


def _ball_record(ball: Any) -> dict[str, str]:
    """Serialize outward exact endpoint rationals, not formatted midpoints.

    arb.lower/upper return enclosing endpoint balls. Their exact fmpq endpoint
    values are inherited from the existing checked local flint wrapper pattern.
    The human ball string is diagnostic only; acceptance uses the actual ball.
    """
    return {"lower_rational": str(ball.lower().fmpq()),
            "upper_rational": str(ball.upper().fmpq()),
            "ball": str(ball)}


def _exact_density_contrast(leaves: tuple[Leaf, ...], root_bounds: Any,
                            budget: Budget) -> tuple[tuple[tuple[Leaf, int], ...], dict[str, Any]]:
    """Decompose an ALREADY validated dense source without changing its identity.

    Workflow: retain every supplied positive/zero physical leaf; aggregate its
    EXACT full-ring volume/pi by actual Fraction density; select maximum-volume
    density (smallest density breaks exact ties); construct a mathematical
    background rectangle and nonzero signed contrasts. The physical identity
        rho(x) = c*1_Omega(x) + sum_i (rho_i-c)*1_Omega_i(x)
    follows from the caller's original disjoint full-root coverage proof. No
    tolerance, density clipping, field/RHS inference or geometry rounding occurs.
    Negative coefficients live OUTSIDE positive-magnitude Leaf integration.
    This helper is not a substitute for validate_dense_source or Core authority.
    """
    budget.check_time()
    if not leaves or len(root_bounds) != 4:
        raise ReferenceFailure("Missing validated contrast source/root")
    L,H,A,B=map(rational,root_bounds)
    if not (0 <= L < H and A < B):
        raise ReferenceFailure("Invalid exact contrast root")
    volumes: dict[Fraction, Fraction]={}
    volume_total=mass_total=Fraction(0)
    for leaf in leaves:
        budget.check_time()
        if not (L <= leaf.L < leaf.H <= H and A <= leaf.A < leaf.B <= B
                and leaf.rho >= 0):
            raise ReferenceFailure("Invalid physical leaf in exact density contrast")
        volume=(leaf.H**2-leaf.L**2)*(leaf.B-leaf.A)
        volumes[leaf.rho]=volumes.get(leaf.rho,Fraction(0))+volume
        volume_total+=volume;mass_total+=leaf.rho*volume
    root_volume=(H**2-L**2)*(B-A)
    if volume_total != root_volume:
        raise ReferenceFailure("Exact contrast source volume differs from validated root")
    background=min(volumes,key=lambda rho:(-volumes[rho],rho))
    terms=[]
    if background:
        terms.append((Leaf("mathematical-background",L,H,A,B,background),1))
    positive=negative=zeros=0;represented_mass=background*root_volume
    for leaf in leaves:
        budget.check_time()
        delta=leaf.rho-background
        if not delta:
            zeros+=1
            continue  # ONLY exact Fraction equality permits omission.
        sign=1 if delta > 0 else -1
        terms.append((Leaf("mathematical-contrast:"+leaf.identity,
                           leaf.L,leaf.H,leaf.A,leaf.B,abs(delta)),sign))
        positive+=sign > 0;negative+=sign < 0
        represented_mass+=delta*(leaf.H**2-leaf.L**2)*(leaf.B-leaf.A)
    if represented_mass != mass_total:
        raise ReferenceFailure("Exact density contrast changed the source mass")
    metadata=dict(profile="exact-volume-modal-density-contrast-1",
        background_density_exact=str(background),
        background_selected_volume_over_pi_exact=str(volumes[background]),
        original_leaf_count=len(leaves),integration_term_count=len(terms),
        positive_contrast_terms=positive,negative_contrast_terms=negative,
        zero_contrast_leaves=zeros,root_bounds_exact=list(map(str,(L,H,A,B))),
        root_volume_over_pi_exact=str(root_volume),
        original_mass_over_pi_exact=str(mass_total),
        represented_mass_over_pi_exact=str(represented_mass),
        exact_dense_coverage="original-validated-disjoint-full-root",
        science_accepted=False,core_binding_qualified=False)
    return tuple(terms),metadata


def _integrate_density_contrast(terms: tuple[tuple[Leaf, int], ...],
                                observer: Observer, allowances: tuple[Fraction, Fraction, Fraction],
                                memo: _LeafGeometryMemo) -> list[Any]:
    """Outward sum exact signed terms under ONE unchanged shared budget.

    Each nonzero term gets allowance/(4*N), reserving accumulation room.
    Its unit-density work goal is local/abs(coefficient), never the physical
    source's global maximum density. The original memo checks actual unit and
    weighted widths; this owner checks the signed weighted widths again. The
    original final all-source width gate remains mandatory after outward sum.
    Negation is exact ball negation, outside the positive-magnitude memo.
    """
    if memo.budget.resource_profile != "full-domain-extended-1":
        raise ReferenceFailure("Density contrast integration requires the explicit extended profile")
    total=[memo.backend.arb(0),memo.backend.arb(0),memo.backend.arb(0)]
    if not terms:
        memo.require_context(memo.backend)
        return total
    local=tuple(width/(4*len(terms)) for width in allowances)
    for leaf,sign in terms:
        memo.require_context(memo.backend)
        if type(sign) is not int or sign not in (-1,1) or leaf.rho <= 0:
            raise ReferenceFailure("Invalid signed mathematical density term")
        unit=tuple(width/leaf.rho for width in local)
        values=memo.integrate(leaf,observer,local,unit)
        signed=tuple(value if sign == 1 else -value for value in values)
        if not _widths_meet(values,local) or not _widths_meet(signed,local):
            raise IntegralFailure("Exact contrast weighted contribution exceeds original allocation")
        for index in range(3):
            total[index]+=signed[index]
    return total


def evaluate_reference(source: dict[str, Any], root_bounds: Any,
                       source_identity: dict[str, Any],
                       observer_records: list[dict[str, Any]],
                       target_widths: dict[str, Any],
                       existing_dependency_directory: Path | None = None,
                       _shared_budget: Budget | None = None) -> dict[str, Any]:
    """Request a complete mathematical source reference with original allowances.

    The caller supplies original absolute interval WIDTH allowances for Phi,
    g_r and g_z. No numeric tolerance is invented and no precision is changed.
    Every observer and contributing actual leaf must succeed. Math certification
    is independent of Core source/observer authentication and physical acceptance;
    the latter two remain false even when the rigorous intervals meet the request.
    """
    # An internal diagnostic may include dense validation/coalescing in this
    # same budget. It cannot reset calls, extend limits or restart its clock.
    budget = Budget.start() if _shared_budget is None else _shared_budget
    if not isinstance(budget, Budget):
        raise TypeError("Invalid shared reference budget")
    stamp = None
    decomposition = None
    terms = None
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
        if budget.resource_profile == "full-domain-extended-1":
            terms,decomposition=_exact_density_contrast(leaves,root_bounds,budget)
        backend = load_optional_flint(existing_dependency_directory)
        # Deterministic strict error allocation is an integration request only;
        # outward all-leaf accumulation is checked against the ORIGINAL widths.
        local = tuple(value / len(leaves) for value in allowances)
        maxrho = max(leaf.rho for leaf in leaves)
        # Allocate from THIS full source, not a cached source's count/density.
        # All-zero sources still retain every exact leaf/identity without division.
        unit = tuple(value/maxrho for value in local) if maxrho else local
        memo = budget._memo_for(backend)
        for observer in observers:
            total = [backend.arb(0), backend.arb(0), backend.arb(0)]
            if terms is None:
                for leaf in leaves:
                    values = memo.integrate(leaf, observer, local, unit)
                    for index in range(3):
                        total[index] += values[index]
            else:
                total=_integrate_density_contrast(terms,observer,allowances,memo)
            if time.monotonic() - budget.started >= budget.timeout_seconds:
                raise WorkLimit(f"global timeout={budget.timeout_seconds:g} seconds reached")
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
                **({"density_decomposition": decomposition} if decomposition is not None else {}),
                "backend": {"python_flint_version": backend.__version__, "ctx_dps": backend.ctx.dps},
                "scope": "Exact supplied dense piecewise-constant full rings and supplied point observers only",
                "remaining_gap": "Actual canonical Core source/observer authentication and Runtime/scientific consumer acceptance"}
    except (ReferenceFailure, WorkLimit, KeyError, TypeError, ValueError, ArithmeticError) as exc:
        return {"profile": "native-ring-one-angle-integrator-1",
                "status": "WorkLimit" if isinstance(exc, WorkLimit) else "Unverified",
                "certified": False, "science_accepted": False,
                "core_binding_qualified": False, "identity": stamp,
                "rows": rows, "budget": budget.record(), "failure": str(exc),
                **({"density_decomposition": decomposition} if decomposition is not None else {}),
                "scope": "No complete all-observer mathematical certificate; earlier rows do not promote a subset"}
