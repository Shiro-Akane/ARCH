"""Generate isolated Jeans numeric references; no EOS/AMR scientific acceptance."""
from decimal import Decimal, localcontext
import json

PI = "3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086513282306647093844609550582231725359"
CASES = [(1.0, 1.0, 1.0), (4.0, 1.0, 1.0), (1.0, 4.0, 1.0), (1.0, 1.0, 4.0), (1e-320, 1e-300, 1e+20), (1e+300, 1e+300, 1.0), (1e-300, 1e+300, 1e+300), (1e+300, 1e-300, 1e-300)]
def references(precision):
    with localcontext() as context:
        context.prec = precision
        return [
            float((Decimal(PI) * Decimal.from_float(cs2) /
                   (Decimal("6.67430e-8") * Decimal.from_float(rho))).sqrt() /
                  Decimal.from_float(h)).hex()
            for rho, cs2, h in CASES
        ]
if __name__ == "__main__":
    low, high = references(80), references(120)
    if low != high:
        raise RuntimeError("Reference precision did not converge to the same double")
    print(json.dumps([
        dict(density=rho.hex(), soundSpeedSquared=cs2.hex(), maxActiveSpacing=h.hex(),
             expectedCells=expected)
        for (rho, cs2, h), expected in zip(CASES, high)
    ], indent=2))
