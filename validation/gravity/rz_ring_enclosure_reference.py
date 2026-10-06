#!/usr/bin/env python3
"""Independent matched-source checks for reliable rectangle enclosures.
Decimal source quadrature is comparison evidence, not the enclosure proof.
No production K/distance/log/source math is imported.
"""
import argparse
from decimal import Decimal as D, localcontext
import json
import math
from pathlib import Path
import subprocess
import rz_ring_offaxis_reference as reference


def probe(binary, mode, arguments):
    q = subprocess.run([str(binary.resolve()), mode, *map(str, arguments)],
                       text=True, capture_output=True, check=True)
    return json.loads(q.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    # Exact stored shared CGS G, not rounded decimal-vs-binary identity.
    reference.G = D.from_float(6.67430e-8)
    logs = []
    values = [1., math.nextafter(1., math.inf), 2., 4., 1.e100,
              float.fromhex("0x1.fffffffffffffp1023")]
    for x in values:
        measured = probe(args.probe, "ring-log-probe", [repr(x)])
        with localcontext() as ctx:
            ctx.prec = 160
            actual = D.from_float(x).ln()
            upper = D.from_float(measured["upper"])
            assert upper >= actual, (x, upper, actual)
            excess = float(upper-actual)
        logs.append({"input_hex": x.hex(), "upper": measured["upper"],
                     "reference_excess": excess})
    cases = [
        dict(name="exterior_radial", r_observer=2., z_observer=0.),
        dict(name="exterior_axial", r_observer=.75, z_observer=2.),
        dict(name="outer_face", r_observer=1., z_observer=0.),
        dict(name="outer_corner", r_observer=1., z_observer=.375),
        dict(name="interior", r_observer=.75, z_observer=0.),
    ]
    rows = []
    for case in cases:
        case.update(r_lower=.5, r_upper=1., z_lower=-.375,
                    z_upper=.375, density=1.)
        sequence = []
        contact = case["name"] not in ("exterior_radial", "exterior_axial")
        for order, precision in [(16, 60), (32, 80)]:
            if contact:
                result = reference.contact_potential_reference(case, order, precision, 1)
            else:
                result = reference.finite_volume_reference(case, order, precision)
            sequence.append({"order": order, "precision": precision,
                             "potential": str(result["potential"])})
        values = [repr(case[k]) for k in ("r_lower", "r_upper", "z_lower",
                  "z_upper", "density", "r_observer", "z_observer")]
        intervals = []
        for boxes in (16, 128):
            measured = probe(args.probe, "ring-enclosure-probe", [*values, boxes])
            assert measured["status"] == 4, measured  # WorkLimit, zero target.
            low, high = D.from_float(measured["lower"]), D.from_float(measured["upper"])
            for q in sequence:
                assert low <= D(q["potential"]) <= high, (case, measured, q)
            intervals.append(measured)
        assert intervals[1]["absolute_error"] < intervals[0]["absolute_error"]
        rows.append({"source": case, "reference_sequence": sequence,
                     "intervals": intervals, "reference_quadrature_certified": False})
        print(case["name"], "contained; target remains WorkLimit", flush=True)
    summary = {"status": "PASS", "source_cases": len(rows), "log_cases": len(logs),
               "G_exact_binary64_hex": (6.67430e-8).hex(),
               "scope": "finite non-axis point-potential source enclosure; not production residual/force gate",
               "proof": "positive rectangular kernel ranges; analytic logarithmic contact majorant; outward FP64 arithmetic",
               "axis_certified_arithmetic": "pending; original analytic branch retained",
               "logs": logs, "rows": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print("RZ_RING_ENCLOSURE_REFERENCE_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
