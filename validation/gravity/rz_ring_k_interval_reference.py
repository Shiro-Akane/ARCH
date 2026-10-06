#!/usr/bin/env python3
"""Independent Decimal AGM reference for ring K intervals; no production imports.
Exact binary64 inputs, Machin pi, two precision levels. Containment evidence
does not certify source quadrature, coordinates or a physical force error.
"""
import argparse
from decimal import Decimal as D, localcontext
import json
import math
from pathlib import Path
import random
import subprocess


def atan_reciprocal(n, precision):
    x = D(1) / D(n)
    term, total, k = x, x, 1
    threshold = D(10) ** (-precision - 5)
    while True:
        term *= -x * x
        add = term / (2 * k + 1)
        total += add
        if abs(add) < threshold:
            return total
        k += 1


def reference(root, precision):
    with localcontext() as ctx:
        ctx.prec = precision
        pi = 16 * atan_reciprocal(5, precision) - 4 * atan_reciprocal(239, precision)
        a, b = D(1), D.from_float(root)
        for _ in range(80):
            a, b = (a + b) / 2, (a * b).sqrt()
            if abs(a - b) <= D(10) ** (-precision + 5) * a:
                return pi / (2 * a)
    raise RuntimeError("reference iteration limit")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    roots = [1., math.nextafter(1., 0.), .5, .01,
             math.nextafter(0., 1.), float.fromhex("0x1p-1022")]
    roots += [10. ** (-n) for n in range(1, 324, 7)]
    rng = random.Random(7102026)
    roots += [math.ldexp(rng.uniform(.5, 1.), -rng.randrange(0, 1073))
              for _ in range(32)]
    results = []
    for root in sorted(set(roots)):
        q = subprocess.run([str(args.probe.resolve()), "ring-k-probe", repr(root)],
                           capture_output=True, text=True, check=True)
        measured = json.loads(q.stdout)
        assert measured["status"] == 0, (root, measured)
        low, high = D.from_float(measured["lower"]), D.from_float(measured["upper"])
        a, b = reference(root, 160), reference(root, 240)
        with localcontext() as ctx:
            ctx.prec = 260
            reference_guard = abs(b) * D("1e-150")
            assert abs(a - b) < reference_guard, (root, "reference disagreement")
            assert low <= b - reference_guard and b + reference_guard <= high, (root, measured, str(b))
            width = float((high - low) / abs(b))
        results.append({"root_hex": root.hex(), "lower": measured["lower"],
                        "upper": measured["upper"], "iterations": measured["iterations"],
                        "relative_interval_width": width})
    summary = {"status": "PASS", "samples": len(results),
               "precision": [160, 240], "pi_reference": "independent Machin atan series",
               "max_relative_width": max(x["relative_interval_width"] for x in results),
               "max_iterations": max(x["iterations"] for x in results),
               "scope": "K of exact stored complementary root, not source quadrature",
               "results": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print("RZ_RING_K_DECIMAL_REFERENCE_PASS samples=" + str(len(results))
          + " max_relative_width=" + str(summary["max_relative_width"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
