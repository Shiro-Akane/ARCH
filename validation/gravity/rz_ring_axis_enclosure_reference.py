#!/usr/bin/env python3
"""Independent Decimal primitive check for full-ring analytic axis intervals.
Producer uses factored section + four terms; reference uses the Newton
axis primitive. No production interval/axis/K code is imported.
"""
import argparse
from decimal import Decimal as D, localcontext
import json
import math
from pathlib import Path
import random
import subprocess
from rz_ring_axis_reference import PI


def reference(rl, rh, zl, zh, density, zo, precision):
    with localcontext() as ctx:
        ctx.prec = precision
        rl, rh, zl, zh, density, zo = map(D.from_float, (rl, rh, zl, zh, density, zo))
        def primitive(r, u):
            if r == 0:
                return u * abs(u) / 2
            x = u / r
            a = (abs(x) + (1 + x*x).sqrt()).ln()
            if x < 0:
                a = -a
            return (u * (r*r + u*u).sqrt() + r*r*a) / 2
        low, high = zl-zo, zh-zo
        integral = (primitive(rh, high)-primitive(rh, low)
                    -primitive(rl, high)+primitive(rl, low))
        return -(2*PI*D.from_float(6.67430e-8)*density)*integral


def call(binary, mode, arguments):
    q = subprocess.run([str(binary.resolve()), mode, *map(str, arguments)],
                       capture_output=True, text=True, check=True)
    return json.loads(q.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    logs = []
    xs = [1., math.nextafter(1., math.inf), 2., 3., 1.e100,
          float.fromhex("0x1.fffffffffffffp1023")]
    rng = random.Random(7102026)
    xs += [math.ldexp(rng.uniform(1., 2.), rng.randrange(0, 1022))
           for _ in range(24)]
    for x in xs:
        measured = call(args.probe, "ring-log-probe", [repr(x)])
        with localcontext() as ctx:
            ctx.prec = 120
            exact = D.from_float(x).ln()
            assert D.from_float(measured["lower"]) <= exact <= D.from_float(measured["upper"])
        logs.append({"input_hex": x.hex(), **measured})
    cases = [(rl, 1., -.375, .375, 1., zo)
             for rl in (0., .5) for zo in (0., .375, 2., 100., -100., 1.e6, -1.e6, 1.e12, -1.e12, 1.e20, -1.e20)]
    cases += [(rl, 1., -.23, .71, 1., zo) for rl in (0., .5) for zo in (1.e6, -1.e6, 1.e12, -1.e12)]
    # Adjacent stored endpoints and nonrepresentable exact midpoint radius.
    cases += [(1., math.nextafter(1., math.inf), -.375, .375, 1., zo)
              for zo in (0., .375, 2., -2., 1.e6, -1.e6, 1.e12, -1.e12, 1.e20, -1.e20)]
    log1ps = []
    for x in (0., math.ulp(0.), 1.e-300, 1.e-20, 1.e-8, .5, 1.,
              math.nextafter(1., math.inf), 2., 1.e100):
        bound = call(args.probe, "ring-log1p-probe", [repr(x)])
        for precision in (400, 480):
            with localcontext() as ctx:
                ctx.prec = precision
                exact = (1 + D.from_float(x)).ln()
                assert D.from_float(bound["lower"]) <= exact <= D.from_float(bound["upper"])
        log1ps.append({"input_hex": x.hex(), **bound})
    rows = []
    for values in cases:
        rl, rh, zl, zh, density, zo = values
        measured = call(args.probe, "ring-enclosure-probe",
                        [rl, rh, zl, zh, density, 0., zo, 128, "1e-10"])
        refs = [reference(*values, p) for p in (160, 200)]
        low, high = D.from_float(measured["lower"]), D.from_float(measured["upper"])
        for v in refs:
            assert low <= v <= high, (values, measured, str(v))
        assert measured["boxes"] == 0 and measured["range_evaluations"] == 0
        assert measured["status"] == 0, ("unchanged internal target rejected", values, measured)
        rows.append({"source_exact_hex": [v.hex() for v in values],
                     "reference_decimal_160": str(refs[0]),
                     "reference_decimal_200": str(refs[1]), "enclosure": measured})
        print("axis", rl, zo, "contained; status", measured["status"], flush=True)
    result = {"status": "PASS", "axis_cases": len(rows), "log_cases": len(logs),
              "precision": [160, 200], "log1p_cases": len(log1ps), "log1p_precision": [400, 480], "G_exact_binary64_hex": (6.67430e-8).hex(),
              "reference": "independent integrated 3D Newton axis primitive",
              "scope": "stored source axis point potential and arithmetic enclosure; not production RZ/force/residual science",
              "logs": logs, "log1ps": log1ps, "rows": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("RZ_AXIS_ENCLOSURE_DECIMAL_REFERENCE_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
