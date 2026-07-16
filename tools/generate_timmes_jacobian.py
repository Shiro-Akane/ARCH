#!/usr/bin/env python3
"""Generate a fixed-rate analytic Jacobian from a translated Timmes RHS.

The translated Timmes RHS files are polynomials in molar abundances ``y``.
All ``rate[...]`` entries are treated as constants by this generator.  For
aprox19/aprox21, the small set of abundance-dependent equilibrium-closure
rates is differentiated separately at runtime; see the corresponding network
header.  Keeping those concerns separate makes this generator mechanical and
prevents hand-maintained nuclear formulas.

Usage from the repository root::

    python3 tools/generate_timmes_jacobian.py aprox19
    python3 tools/generate_timmes_jacobian.py aprox21
    python3 tools/generate_timmes_jacobian.py --check aprox19 aprox21

Only Python's standard library is required.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, Mapping, Tuple


# Coefficient expressions contain no abundance variables.  They retain the
# rate algebra and numeric constants needed by each polynomial coefficient.
Coeff = tuple
Monomial = Tuple[str, ...]


def number(value: float) -> Coeff:
    value = float(value)
    if value == 0.0:
        value = 0.0
    return ("number", value)


ZERO = number(0.0)
ONE = number(1.0)


def is_number(expr: Coeff, value: float | None = None) -> bool:
    return expr[0] == "number" and (value is None or expr[1] == value)


def add_coeff(*items: Coeff) -> Coeff:
    flattened = []
    numeric = 0.0
    for item in items:
        if item[0] == "add":
            children = item[1]
        else:
            children = (item,)
        for child in children:
            if is_number(child):
                numeric += child[1]
            elif not is_number(child, 0.0):
                flattened.append(child)
    if numeric != 0.0:
        flattened.append(number(numeric))
    if not flattened:
        return ZERO
    if len(flattened) == 1:
        return flattened[0]
    return ("add", tuple(flattened))


def multiply_coeff(*items: Coeff) -> Coeff:
    flattened = []
    numeric = 1.0
    for item in items:
        if item[0] == "multiply":
            children = item[1]
        else:
            children = (item,)
        for child in children:
            if is_number(child):
                numeric *= child[1]
            else:
                flattened.append(child)
    if numeric == 0.0:
        return ZERO
    result = []
    if numeric != 1.0 or not flattened:
        result.append(number(numeric))
    result.extend(flattened)
    if len(result) == 1:
        return result[0]
    return ("multiply", tuple(result))


def negate_coeff(item: Coeff) -> Coeff:
    return multiply_coeff(number(-1.0), item)


def divide_coeff(numerator: Coeff, denominator: Coeff) -> Coeff:
    if is_number(numerator, 0.0):
        return ZERO
    if is_number(denominator, 1.0):
        return numerator
    if is_number(numerator) and is_number(denominator):
        return number(numerator[1] / denominator[1])
    return ("divide", numerator, denominator)


def derivative_coeff(expr: Coeff, rate_name: str) -> Coeff:
    kind = expr[0]
    if kind in ("number", "name"):
        return ZERO
    if kind == "rate":
        return ONE if expr[1] == rate_name else ZERO
    if kind == "add":
        return add_coeff(*(derivative_coeff(x, rate_name) for x in expr[1]))
    if kind == "multiply":
        terms = []
        factors = expr[1]
        for index, factor in enumerate(factors):
            derivative = derivative_coeff(factor, rate_name)
            if is_number(derivative, 0.0):
                continue
            terms.append(multiply_coeff(
                *factors[:index], derivative, *factors[index + 1:]))
        return add_coeff(*terms)
    if kind == "divide":
        numerator, denominator = expr[1], expr[2]
        d_numerator = derivative_coeff(numerator, rate_name)
        d_denominator = derivative_coeff(denominator, rate_name)
        return divide_coeff(
            add_coeff(
                multiply_coeff(d_numerator, denominator),
                negate_coeff(multiply_coeff(numerator, d_denominator))),
            multiply_coeff(denominator, denominator))
    raise AssertionError(kind)


@dataclass
class Polynomial:
    terms: Dict[Monomial, Coeff]

    @staticmethod
    def coefficient(value: Coeff) -> "Polynomial":
        return Polynomial({(): value} if not is_number(value, 0.0) else {})

    @staticmethod
    def abundance(name: str) -> "Polynomial":
        return Polynomial({(name,): ONE})

    def add(self, other: "Polynomial") -> "Polynomial":
        result = dict(self.terms)
        for monomial, coefficient in other.terms.items():
            combined = add_coeff(result.get(monomial, ZERO), coefficient)
            if is_number(combined, 0.0):
                result.pop(monomial, None)
            else:
                result[monomial] = combined
        return Polynomial(result)

    def negate(self) -> "Polynomial":
        return Polynomial({
            monomial: negate_coeff(coefficient)
            for monomial, coefficient in self.terms.items()
        })

    def multiply(self, other: "Polynomial") -> "Polynomial":
        result: Dict[Monomial, Coeff] = {}
        for left_monomial, left_coefficient in self.terms.items():
            for right_monomial, right_coefficient in other.terms.items():
                monomial = tuple(sorted(left_monomial + right_monomial))
                product = multiply_coeff(left_coefficient, right_coefficient)
                combined = add_coeff(result.get(monomial, ZERO), product)
                if is_number(combined, 0.0):
                    result.pop(monomial, None)
                else:
                    result[monomial] = combined
        return Polynomial(result)

    def divide(self, other: "Polynomial") -> "Polynomial":
        if set(other.terms) != {()}:
            raise ValueError("RHS division by an abundance-dependent expression")
        denominator = other.terms[()]
        return Polynomial({
            monomial: divide_coeff(coefficient, denominator)
            for monomial, coefficient in self.terms.items()
        })

    def derivative(self, abundance: str) -> "Polynomial":
        result: Dict[Monomial, Coeff] = {}
        for monomial, coefficient in self.terms.items():
            multiplicity = monomial.count(abundance)
            if multiplicity == 0:
                continue
            reduced = list(monomial)
            reduced.remove(abundance)
            derivative = multiply_coeff(number(multiplicity), coefficient)
            key = tuple(reduced)
            result[key] = add_coeff(result.get(key, ZERO), derivative)
        return Polynomial(result)

    def rate_derivative(self, rate_name: str) -> "Polynomial":
        result = {}
        for monomial, coefficient in self.terms.items():
            derivative = derivative_coeff(coefficient, rate_name)
            if not is_number(derivative, 0.0):
                result[monomial] = derivative
        return Polynomial(result)


class ExpressionReader:
    def __init__(self) -> None:
        self.environment: Dict[str, Polynomial] = {}

    def read(self, node: ast.AST) -> Polynomial:
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
            return Polynomial.coefficient(number(float(node.value)))
        if isinstance(node, ast.Name):
            if node.id.startswith("y__"):
                return Polynomial.abundance(node.id[3:])
            if node.id.startswith("rate__"):
                return Polynomial.coefficient(("rate", node.id[6:]))
            if node.id in self.environment:
                return self.environment[node.id]
            # Named numeric constants such as sixth and c54 are coefficients.
            return Polynomial.coefficient(("name", node.id))
        if isinstance(node, ast.UnaryOp):
            value = self.read(node.operand)
            if isinstance(node.op, ast.USub):
                return value.negate()
            if isinstance(node.op, ast.UAdd):
                return value
        if isinstance(node, ast.BinOp):
            left = self.read(node.left)
            right = self.read(node.right)
            if isinstance(node.op, ast.Add):
                return left.add(right)
            if isinstance(node.op, ast.Sub):
                return left.add(right.negate())
            if isinstance(node.op, ast.Mult):
                return left.multiply(right)
            if isinstance(node.op, ast.Div):
                return left.divide(right)
        raise ValueError(f"unsupported RHS syntax: {ast.dump(node)}")

    def parse(self, expression: str) -> Polynomial:
        expression = re.sub(r"\by\[(\w+)\]", r"y__\1", expression)
        expression = re.sub(r"\brate\[(\w+)\]", r"rate__\1", expression)
        expression = re.sub(r"\bqray\[(\w+)\]", r"qray__\1", expression)
        return self.read(ast.parse(expression, mode="eval").body)


def format_number(value: float) -> str:
    rendered = format(value, ".17g")
    if "e" not in rendered and "." not in rendered:
        rendered += ".0"
    return rendered


def render_coefficient(expr: Coeff) -> str:
    kind = expr[0]
    if kind == "number":
        return format_number(expr[1])
    if kind == "rate":
        return f"rate[{expr[1]}]"
    if kind == "name":
        return expr[1]
    if kind == "add":
        return "(" + " + ".join(render_coefficient(x) for x in expr[1]) + ")"
    if kind == "multiply":
        return "(" + " * ".join(render_coefficient(x) for x in expr[1]) + ")"
    if kind == "divide":
        return f"({render_coefficient(expr[1])} / {render_coefficient(expr[2])})"
    raise AssertionError(kind)


def render_polynomial(poly: Polynomial) -> str:
    terms = []
    for monomial, coefficient in poly.terms.items():
        factors = [render_coefficient(coefficient)]
        factors.extend(f"y[{name}]" for name in monomial)
        terms.append("(" + " * ".join(factors) + ")")
    return " + ".join(terms) if terms else "0.0"


def read_species(header: str) -> list[str]:
    match = re.search(
        r"enum\s+Species\s*:\s*int\s*\{(.*?)\};", header, re.DOTALL)
    if not match:
        raise ValueError("could not locate Species enum")
    body = re.sub(r"//.*", "", match.group(1))
    return [item.strip() for item in body.split(",") if item.strip()]


def read_rhs(rhs: str, species: Iterable[str]) -> Mapping[str, Polynomial]:
    reader = ExpressionReader()
    assignment = re.compile(r"^\s*(a\d+|qray\[(\w+)\])\s*=\s*(.*?)\s*;\s*$")
    for line_number, line in enumerate(rhs.splitlines(), start=1):
        match = assignment.match(line)
        if not match:
            continue
        target = match.group(1)
        if target.startswith("qray"):
            target = f"qray__{match.group(2)}"
        try:
            reader.environment[target] = reader.parse(match.group(3))
        except Exception as error:
            raise ValueError(f"line {line_number}: {line.strip()}: {error}") from error
    result = {}
    for name in species:
        key = f"qray__{name}"
        if key not in reader.environment:
            raise ValueError(f"RHS did not assign qray[{name}]")
        result[name] = reader.environment[key]
    return result


def read_abundance_dependent_rates(common_header: str) -> list[str]:
    marker = "inline void form_extended_equilibrium"
    start = common_header.find(marker)
    if start < 0:
        raise ValueError("could not locate form_extended_equilibrium")
    body = common_header[start:]
    result = []
    for rate_name in re.findall(r"r\[Ids::(\w+)\]\s*=", body):
        if rate_name not in result:
            result.append(rate_name)
    return result


def generate(network: str, root: Path) -> tuple[Path, str]:
    directory = root / "src" / "physics" / "network" / network
    header_path = next(directory.glob("Net*.h"))
    rhs_path = directory / "TimmesRhs.inc"
    output_path = directory / "TimmesJacobian.inc"
    species = read_species(header_path.read_text(encoding="utf-8"))
    rhs = read_rhs(rhs_path.read_text(encoding="utf-8"), species)
    common_path = directory.parent / "timmes_common" / "AproxRateAssembly.h"
    dependent_rates = read_abundance_dependent_rates(
        common_path.read_text(encoding="utf-8"))
    dimension = len(species)

    lines = [
        "// Generated mechanically from TimmesRhs.inc by",
        f"// tools/generate_timmes_jacobian.py {network}",
        "// All rate[] entries are held fixed in this partial Jacobian.",
        "// Keep the large sparse expression out of its caller's register set.",
        "#if defined(__CUDACC__)",
        "#  define TIMMES_JACOBIAN_NOINLINE __noinline__",
        "#elif defined(__GNUC__) || defined(__clang__)",
        "#  define TIMMES_JACOBIAN_NOINLINE __attribute__((noinline))",
        "#else",
        "#  define TIMMES_JACOBIAN_NOINLINE",
        "#endif",
        f"TIMMES_HD TIMMES_JACOBIAN_NOINLINE inline void jacobian_{network}_molar_fixed_rates(",
        "    const double* y, const double* rate, double* jac)",
        "{",
        f"    for (int i = 0; i < {dimension} * {dimension}; ++i) jac[i] = 0.0;",
    ]
    nonzero = 0
    for row in species:
        for column in species:
            derivative = rhs[row].derivative(column)
            if not derivative.terms:
                continue
            lines.append(
                f"    jac[({row}) * {dimension} + ({column})] = "
                f"{render_polynomial(derivative)};"
            )
            nonzero += 1
    lines.extend([
        "}",
        "",
        "// Add only the chain-rule contribution from abundance-dependent",
        "// rates produced by form_extended_equilibrium().  differentiated_rate",
        "// contains the closure evaluated with one active Dual<1> abundance.",
        f"TIMMES_HD TIMMES_JACOBIAN_NOINLINE inline void add_{network}_rate_derivative_column(",
        "    const double* y, const double* rate,",
        "    const timmes::Dual<1>* differentiated_rate,",
        "    int column, double* jac)",
        "{",
    ])
    correction_rows = 0
    for row in species:
        corrections = []
        for rate_name in dependent_rates:
            derivative = rhs[row].rate_derivative(rate_name)
            if not derivative.terms:
                continue
            corrections.append(
                f"({render_polynomial(derivative)} * "
                f"differentiated_rate[{rate_name}].deriv[0])"
            )
        if corrections:
            lines.append(
                f"    jac[({row}) * {dimension} + column] += "
                + " + ".join(corrections) + ";"
            )
            correction_rows += 1
    lines.extend([
        "}",
        "#undef TIMMES_JACOBIAN_NOINLINE",
        "",
        f"// fixed-rate nonzero entries: {nonzero} / {dimension * dimension}",
        f"// closure-correction rows: {correction_rows}",
        "",
    ])
    return output_path, "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("networks", nargs="+", choices=("aprox19", "aprox21"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    failed = False
    for network in args.networks:
        output_path, content = generate(network, root)
        if args.check:
            existing = output_path.read_text(encoding="utf-8") if output_path.exists() else ""
            if existing != content:
                print(f"out of date: {output_path}", file=sys.stderr)
                failed = True
            else:
                print(f"up to date: {output_path}")
        else:
            output_path.write_text(content, encoding="utf-8")
            print(f"generated: {output_path}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
