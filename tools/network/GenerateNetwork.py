#!/usr/bin/env python3
"""Safely generate and register an ARCH custom pynucastro SimpleCxx network."""
from __future__ import annotations
import argparse, ast, hashlib, importlib.util, json, math, os, re, shutil, sys, tempfile
from datetime import datetime, timezone
from fractions import Fraction
from pathlib import Path
from PortableAdapter import adapt as make_portable_adapter

# Package contract identifier used for reuse and CMake registration.
# This is generated metadata, not an ARCH release number or a user setting.
GENERATOR_VERSION = 4
ID_RE = re.compile(r"^[a-z][a-z0-9_]{0,47}$")
RESERVED = {"custom", "none", "null", "ideal", "helmholtz", "tabular"}

def fail(message): raise SystemExit(f"GenerateNetwork: {message}")

def load_recipe(path):
    spec = importlib.util.spec_from_file_location("arch_custom_network_recipe", path)
    if spec is None or spec.loader is None: fail(f"cannot load recipe: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def validate_id(value):
    if not isinstance(value, str) or not ID_RE.fullmatch(value):
        fail("NETWORK_ID must match ^[a-z][a-z0-9_]{0,47}$")
    if value in RESERVED or value.startswith(("aprox", "iso")):
        fail(f"NETWORK_ID '{value}' is reserved; custom generation can never replace aprox*/iso* networks")
    return value

def default_network(recipe, pyna):
    if not hasattr(recipe, "NUCLEI"):
        fail("recipe must define build_network(pynucastro) or NUCLEI")
    nuclei = [pyna.Nucleus.from_cache(name) for name in recipe.NUCLEI]
    requested_names = [n.short_spec_name for n in nuclei]
    if len(set(requested_names)) != len(requested_names):
        fail("NUCLEI must not contain duplicate nuclei")
    library = pyna.ReacLibLibrary().linking_nuclei(
        nuclei, with_reverse=bool(getattr(recipe, "WITH_REVERSE", True)),
        print_warning=bool(getattr(recipe, "PRINT_RATE_WARNINGS", True)))
    network = pyna.SimpleCxxNetwork(
        libraries=library, inert_nuclei=nuclei,
        do_screening=bool(getattr(recipe, "DO_SCREENING", True)))
    produced_names = {n.short_spec_name for n in network.unique_nuclei}
    missing = [name for name in requested_names if name not in produced_names]
    if missing:
        fail("pynucastro dropped requested nuclei: " + ", ".join(missing))
    return network

def rewrite_local_includes(generated_dir):
    names = {p.name for p in generated_dir.glob("*.H")}
    pattern = re.compile(r"^(\s*#include\s*)<([^>]+)>(\s*)$", re.MULTILINE)
    for path in generated_dir.glob("*.H"):
        text = path.read_text(encoding="utf-8")
        text = pattern.sub(lambda m: f'{m.group(1)}"{m.group(2)}"{m.group(3)}'
                           if m.group(2) in names else m.group(0), text)
        path.write_text(text, encoding="utf-8")

def prune_literal_zero_jacobian(generated_dir):
    """Remove entries that pynucastro proves structurally zero in generated C++."""
    path = generated_dir / "actual_rhs.H"
    if not path.is_file():
        fail("pynucastro output is missing generated/actual_rhs.H")
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"^[ \t]*jac\.set\(\s*[A-Za-z_]\w*\s*,\s*[A-Za-z_]\w*\s*,"
        r"\s*0\.0\s*\);[ \t]*\r?\n?",
        re.MULTILINE,
    )
    text, count = pattern.subn("", text)
    path.write_text(text, encoding="utf-8")
    return count

def cpp_array(values): return ", ".join(f"{float(v):.17g}" for v in values)
def quoted_array(values): return ", ".join(json.dumps(str(v)) for v in values)

def validated_nuclei(network):
    nuclei = list(network.unique_nuclei)
    if not nuclei: fail("pynucastro produced a network with zero nuclei")
    incomplete = [n.short_spec_name for n in nuclei
                  if n.mass is None or n.A_nuc is None]
    if incomplete:
        fail("pynucastro has incomplete mass data for: " +
             ", ".join(incomplete))
    return nuclei

def generated_energy_metadata(generated_dir, species_count):
    """Read the upstream energy authority, never reconstruct nuclear masses.

    SimpleCxx emits atomic masses using its own mass-unit convention. Converting
    Nucleus.mass (MeV) again with another constants edition made the RHS and
    Jacobian/accepted-step energy use different masses. Retain exact emitted
    binary64 values and the emitted conversion expression's evaluation order.
    Only literal arithmetic and references to declared constants are accepted.
    """
    def clean(name):
        return re.sub(r'//[^\n]*|/\*.*?\*/', '',
                      (generated_dir / name).read_text(encoding='utf-8'), flags=re.DOTALL)

    arrays = re.findall(r'\bArray1D\s*<\s*Real\s*,[^>]+>\s+mion\s*\{([^{}]*)\}',
                        clean('actual_network.H'))
    if len(arrays) != 1:
        raise ValueError('generated nuclear-mass storage is not recognized')
    literals = [ast.literal_eval(token.strip().removesuffix('_rt'))
                for token in arrays[0].split(',') if token.strip()]
    if any(type(value) not in (int, float) for value in literals):
        raise ValueError('generated nuclear masses must be numeric literals')
    masses = [float(value) for value in literals]
    if len(masses) != species_count or any(not math.isfinite(x) or x <= 0 for x in masses):
        raise ValueError('generated nuclear masses have invalid values or extent')
    declarations = dict(re.findall(r'\bconstexpr\s+Real\s+(\w+)\s*=\s*([^;]+);',
                                   clean('fundamental_constants.H')))

    def evaluate(node, stack=()):
        if isinstance(node, ast.Constant) and type(node.value) in (int, float):
            return float(node.value)
        if isinstance(node, ast.Name) and node.id in declarations and node.id not in stack:
            return evaluate(ast.parse(declarations[node.id].replace('_rt', ''), mode='eval').body,
                            stack + (node.id,))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            value = evaluate(node.operand, stack)
            return -value if isinstance(node.op, ast.USub) else value
        if isinstance(node, ast.BinOp):
            left, right = evaluate(node.left, stack), evaluate(node.right, stack)
            if isinstance(node.op, ast.Add): return left + right
            if isinstance(node.op, ast.Sub): return left - right
            if isinstance(node.op, ast.Mult): return left * right
            if isinstance(node.op, ast.Div): return left / right
        raise ValueError('generated energy conversion is not literal constant arithmetic')

    conversion = evaluate(ast.Name(id='enuc_conv2'))
    if not math.isfinite(conversion) or conversion >= 0:
        raise ValueError('generated nuclear-energy conversion is invalid')
    return masses, conversion


def stabilize_generated_energy_sum(generated_dir):
    """Accumulate the upstream nuclear energy in the conserved-baryon gauge.

    Reaction and screening expressions are untouched. Fail on an unfamiliar
    energy body rather than silently retaining two accumulation conventions.
    """
    path = generated_dir / 'actual_rhs.H'
    text = path.read_text(encoding='utf-8')
    pattern = re.compile(
        r'enuc\s*=\s*0\.0_rt\s*;\s*'
        r'for\s*\(\s*int\s+n\s*=\s*1\s*;\s*n\s*<=\s*NumSpec\s*;\s*\+\+n\s*\)\s*\{\s*'
        r'enuc\s*\+=\s*dydt\(n\)\s*\*\s*network::mion\(n\)\s*;\s*\}\s*'
        r'enuc\s*\*=\s*C::enuc_conv2\s*;')
    replacement = '''arch::math::CompensatedSum nuclear_mass;
    for (int n = 1; n <= NumSpec; ++n) {
        nuclear_mass.add_product(dydt(n), network::energy_mion(n));
    }
    enuc = nuclear_mass.value() * C::enuc_conv2;'''
    text, count = pattern.subn(replacement, text)
    if count != 1:
        raise ValueError('generated nuclear-energy accumulation is not recognized')
    path.write_text(text, encoding='utf-8')


def install_conserved_energy_gauge(generated_dir, network, nuclei):
    """Remove a common baryon rest mass before potentially cancelling sums.

    For every admitted reaction sum(A*dY/dt)=0. Thus m_i may be replaced by
    m_i-A_i*m0 without changing nuclear heating. Choose m0 from the same emitted
    mass data, not another constants edition or a privileged isotope. Validate
    the invariant from reaction stoichiometry, never from a sampled state.
    The one emitted accessor is used by RHS, Jacobian and accepted-step energy.
    """
    masses, conversion = generated_energy_metadata(generated_dir, len(nuclei))
    numbers = [n.A for n in nuclei]
    if any(type(a) is not int or a <= 0 for a in numbers):
        raise ValueError('nuclear baryon numbers must be positive integers')
    for rate in network.rates:
        if sum(n.A for n in rate.reactants) != sum(n.A for n in rate.products):
            raise ValueError('reaction does not conserve baryon number; energy gauge is invalid')
    origin = min(mass / number for mass, number in zip(masses, numbers))
    # Correctly round exactly the same fused subtraction emitted below.
    weights = [float(Fraction(mass) - number * Fraction(origin))
               for mass, number in zip(masses, numbers)]
    path = generated_dir / 'actual_network.H'
    text = path.read_text(encoding='utf-8')
    initializer = re.compile(r'\bArray1D\s*<\s*Real\s*,[^>]+>\s+mion\s*\{[^{}]*\}\s*;')
    matches = list(initializer.finditer(text))
    if len(matches) != 1 or re.search(r'\b(?:energy_mion|baryon_mass_origin)\b', text):
        raise ValueError('generated energy-gauge insertion point is not recognized')
    helper = f'''
    // Equivalent energy reference: every generated reaction conserves baryons.
    inline constexpr Real baryon_mass_origin = {origin:.17g};
    inline Real energy_mion(int n) {{
        return std::fma(-aion[n - 1], baryon_mass_origin, mion(n));
    }}
'''
    end = matches[0].end()
    path.write_text(text[:end] + helper + text[end:], encoding='utf-8')
    return weights, conversion


def write_adapter(stage, network_id, network):
    cls = f"NetCustom_{network_id}"
    detail = f"arch_custom_{network_id}_detail"
    generated = f"arch_pynucastro_{network_id}"
    nuclei = validated_nuclei(network)
    names = [n.short_spec_name.lower() for n in nuclei]
    aion, zion = [n.A for n in nuclei], [n.Z for n in nuclei]
    masses, conversion = install_conserved_energy_gauge(stage / 'generated', network, nuclei)
    header_name, source_name = f"{cls}.h", f"{cls}.cpp"

    header = f'''/** Generated by tools/network/GenerateNetwork.py. Do not edit. */
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>
#include "data/GlobalDefs.h"
#include "physics/species/Species.h"
#include "numerics/burnsolver/NetworkDerivative.h"

namespace {detail} {{
void eval_rhs(const double*, double, double*, double&);
void eval_jacobian_entries(const double*, double, std::vector<int>&,
                           std::vector<int>&, std::vector<double>&, double*);
void eval_temperature_derivative(const double*, double, double*, double&);
}}

struct {cls} {{
    static constexpr int NUM_SPECIES = {len(nuclei)};
    static constexpr int ODE_NEQ = NUM_SPECIES + 1;
    static constexpr bool SUPPORTS_NSE = false;
    static constexpr const char* NETWORK_NAME = "custom:{network_id}";
    inline static constexpr std::array<const char*, NUM_SPECIES> SPECIES_NAMES{{{quoted_array(names)}}};
    inline static constexpr std::array<double, NUM_SPECIES> AION{{{cpp_array(aion)}}};
    inline static constexpr std::array<double, NUM_SPECIES> ZION{{{cpp_array(zion)}}};
    inline static constexpr std::array<double, NUM_SPECIES> ENERGY_WEIGHTS{{{cpp_array(masses)}}};
    static constexpr double ENERGY_CONVERSION = {conversion:.17g};

    static double aion(int index) {{ return AION[index]; }}
    static double energy_weight(int index) {{ return ENERGY_WEIGHTS[index]; }}
    static std::string get_network_name() {{ return NETWORK_NAME; }}
    static void RegisterSpecies(SpeciesManager& specs) {{
        for (int i = 0; i < NUM_SPECIES; ++i)
            specs.add_species(SPECIES_NAMES[i], AION[i], ZION[i], 5.0/3.0, 0.0);
    }}
    static void SetupInitialFractions(SimConfig& config, const SpeciesManager& specs,
                                      std::vector<double>& output) {{
        output.assign(specs.count(), config.physics.burn.smallx);
        double sum = 0.0;
        for (int i = 0; i < specs.count(); ++i) {{
            std::string target = "x" + specs.get_name(i);
            std::transform(target.begin(), target.end(), target.begin(),
                [](unsigned char c) {{ return static_cast<char>(std::tolower(c)); }});
            for (const auto& entry : config.custom_params) {{
                std::string key = entry.first;
                std::transform(key.begin(), key.end(), key.begin(),
                    [](unsigned char c) {{ return static_cast<char>(std::tolower(c)); }});
                if (key == target) {{ output[i] += entry.second; break; }}
            }}
            sum += output[i];
        }}
        if (!(sum > 0.0)) throw std::runtime_error("custom network initial composition has zero sum");
        for (double& value : output) value /= sum;
    }}
    static void eval_rhs(const double* state, double rho, double, double* rhs, double& enuc) {{
        {detail}::eval_rhs(state, rho, rhs, enuc);
    }}
    template <typename MatrixType>
    static void eval_jacobian(const double* state, double rho, double, MatrixType& jac,
                              double* denuc_dX = nullptr) {{
        std::vector<int> rows, columns;
        std::vector<double> values;
        {detail}::eval_jacobian_entries(state, rho, rows, columns, values, denuc_dX);
        for (std::size_t k = 0; k < values.size(); ++k)
            jac.set(rows[k], columns[k], values[k]);
    }}
    static void eval_temperature_derivative(const double* state, double rho, double,
                                             double* drhs_dT, double& denuc_dT) {{
        {detail}::eval_temperature_derivative(state, rho, drhs_dT, denuc_dT);
    }}
}};
'''
    (stage/header_name).write_text(header, encoding="utf-8")
    screen_on = "#define SCREENING 1\n" if network.do_screening else ""
    screen_off = "#undef SCREENING\n" if network.do_screening else ""
    source = f'''#include "{header_name}"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace {generated} {{
{screen_on}#include "generated/actual_rhs.H"
{screen_off}}}

namespace {detail} {{
using Network = {cls};
static {generated}::burn_t make_state(const double* state, double rho) {{
    {generated}::burn_t burn{{}};
    burn.rho = rho; burn.T = state[Network::NUM_SPECIES];
    for (int i=0; i<Network::NUM_SPECIES; ++i) burn.xn[i] = state[i];
    return burn;
}}
void eval_rhs(const double* state, double rho, double* rhs, double& enuc) {{
    auto burn = make_state(state, rho);
    {generated}::Array1D<double, 1, {generated}::NumSpec> dydt{{}};
    double enu_weak = 0.0;
    {generated}::actual_rhs(burn, dydt, enu_weak);
    for (int i=0; i<Network::NUM_SPECIES; ++i) rhs[i] = dydt(i+1)*Network::AION[i];
    {generated}::ener_gener_rate(dydt, enuc);
    enuc += enu_weak;
}}
struct JacobianSink {{
    std::vector<int>& rows; std::vector<int>& columns; std::vector<double>& values;
    void zero() {{ rows.clear(); columns.clear(); values.clear(); }}
    void set(int row, int column, double value) {{
        rows.push_back(row); columns.push_back(column);
        values.push_back(value*Network::AION[row-1]/Network::AION[column-1]);
    }}
}};
void eval_jacobian_entries(const double* state, double rho, std::vector<int>& rows,
                           std::vector<int>& columns, std::vector<double>& values,
                           double* denuc_dX) {{
    auto burn = make_state(state, rho);
    JacobianSink sink{{rows, columns, values}};
    {generated}::actual_jac(burn, sink);
    if (denuc_dX) {{
        std::fill(denuc_dX, denuc_dX+Network::NUM_SPECIES, 0.0);
        for (std::size_t k=0; k<values.size(); ++k) {{
            int row=rows[k]-1, column=columns[k]-1;
            denuc_dX[column] += Network::ENERGY_CONVERSION * values[k] /
                Network::AION[row] * Network::ENERGY_WEIGHTS[row];
        }}
    }}
}}
struct CompleteRhs {{
    ARCH_HOST_DEVICE void operator()(const double* state, double rho,
                                    double* rhs, double& energy) const {{
        eval_rhs(state, rho, rhs, energy);
    }}
}};
void eval_temperature_derivative(const double* state, double rho,
                                 double* drhs_dT, double& denuc_dT) {{
    arch::burnmath::temperature_derivative<Network::NUM_SPECIES + 1>(
        state, rho, drhs_dT, denuc_dT, CompleteRhs{{}});
}}
}}
'''
    (stage/source_name).write_text(source, encoding="utf-8")
    return cls, header_name, source_name

def main():
    parser=argparse.ArgumentParser(description="Generate an isolated ARCH custom network.")
    parser.add_argument("recipe", type=Path, help="Python recipe defining the network")
    parser.add_argument("--replace", action="store_true",
                        help="replace an existing generated package with the same network ID")
    parser.add_argument("--check", action="store_true",
                        help="validate the recipe and network without writing a package")
    parser.add_argument("--custom-root", type=Path, default=None, help=argparse.SUPPRESS)
    args=parser.parse_args()
    recipe_path=args.recipe.resolve()
    if not recipe_path.is_file(): fail(f"recipe does not exist: {recipe_path}")
    recipe=load_recipe(recipe_path)
    network_id=validate_id(getattr(recipe, "NETWORK_ID", None))
    repo_root=Path(__file__).resolve().parents[2]
    custom_root=(args.custom_root.resolve() if args.custom_root else
                 repo_root/"src"/"physics"/"network"/"custom")
    target=custom_root/network_id
    if target.parent.resolve()!=custom_root.resolve(): fail("target escaped custom root")
    try: import pynucastro as pyna
    except ImportError:
        fail("pynucastro is required in the active Python environment; "
             "see tools/network/README.md for setup and generation instructions")
    recipe_hash=hashlib.sha256(recipe_path.read_bytes()).hexdigest()
    generator_hash = hashlib.sha256(b"".join(
        (Path(__file__).parent / name).read_bytes()
        for name in ("GenerateNetwork.py", "PortableCxx.py", "PortableAdapter.py", "WeakTables.py", "WeakStorage.py"))).hexdigest()
    if target.exists() and not args.replace and not args.check:
        manifest_path=target/"manifest.json"
        if manifest_path.is_file():
            m=json.loads(manifest_path.read_text())
            if (m.get("recipe_sha256")==recipe_hash and m.get("pynucastro_version")==pyna.__version__
                    and m.get("generator_version")==GENERATOR_VERSION
                    and m.get("generator_sha256")==generator_hash):
                print(f"custom:{network_id} is already up to date; no files changed"); return
        fail(f"custom:{network_id} exists; choose another ID or pass --replace")
    network=(recipe.build_network(pyna) if hasattr(recipe, "build_network")
             else default_network(recipe, pyna))
    if not isinstance(network, pyna.SimpleCxxNetwork):
        fail("build_network() must return pynucastro.SimpleCxxNetwork")
    validated_nuclei(network)
    if args.check:
        print(f"validated custom:{network_id}: {len(network.unique_nuclei)} nuclei, "
              f"{len(network.rates)} rates, pynucastro {pyna.__version__}"); return
    custom_root.mkdir(parents=True, exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix=f".{network_id}.", dir=custom_root))
    backup = None
    try:
        generated_dir=stage/"generated"; generated_dir.mkdir()
        network.write_network(odir=generated_dir)
        rewrite_local_includes(generated_dir)
        stabilize_generated_energy_sum(generated_dir)
        pruned_zero_jacobian_entries = prune_literal_zero_jacobian(generated_dir)
        if pruned_zero_jacobian_entries == 0:
            print(
                "GenerateNetwork: warning: no literal-zero Jacobian entries "
                "matched; review generated/actual_rhs.H if this network was "
                "expected to be sparse",
                file=sys.stderr,
            )
        cls, header, source=write_adapter(stage, network_id, network)
        from WeakTables import enable_coordinate_derivatives, connect_weak_derivatives
        weak_coordinate_derivatives = enable_coordinate_derivatives(stage, header)
        device_callable = make_portable_adapter(stage, network_id, cls, header, source,
                                                host_weak_math=weak_coordinate_derivatives)
        if weak_coordinate_derivatives:
            connect_weak_derivatives(stage, network_id, network)
            from WeakStorage import promote_weak_storage
            promote_weak_storage(stage, network_id)
            # The same view is bound by CPU entry points and CUDA dense /
            # sparse owners. Promotion validates the complete table layout;
            # failure aborts staged publication instead of advertising a route.
            device_callable = True
        manifest={"schema_version":1, "generator_version":GENERATOR_VERSION,
            "generator_sha256": generator_hash,
            "network_id":network_id, "runtime_name":f"custom:{network_id}",
            "class_name":cls, "header":header, "source":source,
            "pynucastro_version":pyna.__version__, "recipe":str(recipe_path),
            "recipe_sha256":recipe_hash, "species_count":len(network.unique_nuclei),
            "species":[n.short_spec_name.lower() for n in network.unique_nuclei],
            "rate_count":len(network.rates), "supports_nse":False,
            "device_callable_math": device_callable,
            "weak_table_coordinate_derivatives": weak_coordinate_derivatives,
            "weak_composition_jacobian": weak_coordinate_derivatives,
            "auxiliary_equations": int(weak_coordinate_derivatives),
            "explicit_weak_storage": weak_coordinate_derivatives,
            "literal_zero_jacobian_entries_pruned":pruned_zero_jacobian_entries,
            "temperature_jacobian":"shared fourth-order complete-RHS difference; relative epsilon^(1/5) step; forward boundary stencil",
            "energy_authority":"generated mion and C::enuc_conv2; conserved-baryon mass offset, shared energy_mion accessor"}
        (stage/"manifest.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")
        (stage/"network.cmake").write_text(
            f'set(ARCH_CUSTOM_NETWORK_ID "{network_id}")\n'
            f'set(ARCH_CUSTOM_NETWORK_TYPE "{cls}")\n'
            f'set(ARCH_CUSTOM_NETWORK_HEADER "${{CMAKE_CURRENT_LIST_DIR}}/{header}")\n'
            f'set(ARCH_CUSTOM_NETWORK_SOURCE "${{CMAKE_CURRENT_LIST_DIR}}/{source}")\n')
        if target.exists():
            backup_root=custom_root/".backup"; backup_root.mkdir(exist_ok=True)
            backup=backup_root/f"{network_id}-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}"
            os.replace(target, backup); print(f"preserved previous network at {backup}")
        try:
            os.replace(stage, target)
        except Exception:
            if backup is not None and not target.exists():
                os.replace(backup, target)
            raise
    except Exception:
        if stage.exists(): shutil.rmtree(stage)
        raise
    print(f"generated custom:{network_id}: {len(network.unique_nuclei)} nuclei, "
          f"{len(network.rates)} rates; pruned {pruned_zero_jacobian_entries} "
          "literal-zero Jacobian entries; rerun CMake to register it")

if __name__=="__main__": main()
