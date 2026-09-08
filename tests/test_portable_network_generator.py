"""Fast storage-adapter regressions; no pynucastro, CUDA, or compiler needed.

Fixtures model the recognized SimpleCxx header contract. These check conversion
and metadata identity, not scientific accuracy of a generated reaction network.
"""

from pathlib import Path
import re
import sys
import tempfile
from fractions import Fraction
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/network"))
import GenerateNetwork
import PortableAdapter
import PortableCxx
import WeakTables
import WeakStorage


class WeakStorageContract(unittest.TestCase):
    @staticmethod
    def table():
        return '''const int num_tables = 1;
namespace rate_tables {
    // An independent 2 x 2 x 3 storage layout, not physical rate data.
    inline table_t j_parent_child_meta{.ntemp=2, .nrhoy=2, .nvars=3, .nheader=5};
    inline Array1D<Real, 1, 2> j_parent_child_rhoy{7.0, 9.0};
    inline Array1D<Real, 1, 2> j_parent_child_temp{8.0, 10.0};
    inline Array3D<Real, 1, 2, 1, 2, 1, num_vars> j_parent_child_data{
        -500.0, 1.2345678901234567, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
}
'''

    def test_storage_moves_literal_tokens_and_preserves_column_major_order(self):
        text, symbols, size = WeakStorage.lower_table_storage(self.table())
        self.assertEqual(size, 16)
        self.assertEqual(symbols, ['j_parent_child_meta', 'j_parent_child_rhoy',
                                   'j_parent_child_temp', 'j_parent_child_data'])
        self.assertIn('7.0, 9.0, 8.0, 10.0, -500.0, 1.2345678901234567, 2, 3', text)
        self.assertIn('tables.slice(0, 2)', text)
        self.assertIn('tables.slice(2, 2)', text)
        self.assertIn('tables.slice(4, 12)', text)
        self.assertIn('WeakValuesView{pointer, 2, 2, 3}', text)
        self.assertNotIn('Array3D', text)

    def test_unknown_or_inconsistent_weak_storage_fails_closed(self):
        for text in (self.table().replace('num_tables = 1', 'num_tables = 2'),
                     self.table().replace('7.0, 9.0', '7.0'),
                     self.table().replace('7.0, 9.0', '7.0, nan'),
                     self.table().replace('.ntemp=2', '.ntemp=3'),
                     self.table().replace('\n}', '\nint unrecognized = 1;\n}')):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    WeakStorage.lower_table_storage(text)


PROPERTIES = """#ifndef NETWORK_PROPERTIES_H
#define NETWORK_PROPERTIES_H
constexpr int NumSpecTotal = 2;
constexpr Real aion[NumSpecTotal] = {4.0_rt, 12.0_rt};
constexpr Real aion_inv[NumSpecTotal] = {0.25_rt, 0.083333333333333329_rt};
constexpr Real zion[NumSpecTotal] = {2.0_rt, 6.0_rt};
#endif
"""
NETWORK = """#ifndef ACTUAL_NETWORK_H
#define ACTUAL_NETWORK_H
namespace network {
inline Array1D<Real, 1, NumSpecTotal> mion{
    6.6446571948428542e-24_rt, // deliberately differs from reconverting Nucleus.mass
    1.9926468799200001e-23_rt
};
}
#endif
"""
RHS = """#ifndef ACTUAL_RHS_H
#define ACTUAL_RHS_H
template<class Matrix>
inline
void actual_jac(burn_t& state, Matrix& jac) {
    jac.zero();
    const Real scratch = state.rho * 1.2345678901234567e-17_rt;
    jac.set(He4, He4, scratch);
    jac.set(C12, He4, 3.0_rt * scratch);
    jac.set(He4, He4, scratch);
}
template<class T>
inline
void ener_gener_rate(T const& dydt, Real& enuc) {
    enuc = 0.0_rt;
    for (int n = 1; n <= NumSpec; ++n) {
        enuc += dydt(n) * network::mion(n);
    }
    enuc *= C::enuc_conv2;
}
#endif
"""
BRIDGE = """#ifndef AMREX_BRIDGE_H
#define AMREX_BRIDGE_H
namespace amrex {
    template<auto I, auto N, class F>
    inline
    constexpr void constexpr_for (F const& f)
    {
        if constexpr (I < N) {
            f(std::integral_constant<decltype(I), I>());
            constexpr_for<I+1, N>(f);
        }
    }
}
#endif
"""

# Recognized upstream coordinate signatures; structural tests below preserve
# the expression text, while compiled scientific tests use the real package.
TABLE_COORDINATES = """inline
Real
evaluate_linear_2d(const Real fip1jp1, const Real fip1j, const Real fijp1, const Real fij,
                   const Real xhi, const Real xlo, const Real yhi, const Real ylo,
                   const Real x, const Real y)
{
    Real f;
    Real dx = xhi - xlo, dy = yhi - ylo;
    Real A = (fip1jp1 - fip1j - fijp1 + fij) / (dx * dy);
    Real B = (fip1j - fij) / dx, C = (fijp1 - fij) / dy, E = fij;
    Real xx = amrex::Clamp(x, xlo, xhi);
    Real yy = amrex::Clamp(y, ylo, yhi);
    f = A * (xx - xlo) * (yy - ylo) + B * (xx - xlo) + C * (yy - ylo) + E;
    return f;
}
template<typename R, typename T, typename D>
inline
Real
evaluate_vars(const int i, const int j, const R& rho, const T& temp, const D& data,
              const Real log_rhoy, const Real log_temp, const int component)
{
    Real r = evaluate_linear_2d(data(j+1,i+1,component), data(j,i+1,component),
        data(j+1,i,component), data(j,i,component), rho(i+1), rho(i), temp(j+1), temp(j),
        log_rhoy, log_temp);
    return r;
}
"""


class PortableNetworkGeneratorTests(unittest.TestCase):
    def test_small_lookups_stay_inlineable_and_keep_literal_values(self):
        values = ['1.2345678901234567e-17_rt', '4.0_rt', '12.0_rt']
        lookup = PortableCxx._lookup('sample', 1, values)
        self.assertIn('ARCH_HOST_DEVICE constexpr Real operator()(int index)', lookup)
        self.assertNotIn('ARCH_HEAVY_INLINE', lookup)
        self.assertIn('ARCH_HOST_DEVICE constexpr Real operator[](int index)', lookup)
        for index, value in enumerate(values):
            self.assertEqual(lookup.count(f'case {index}: return {value};'), 1)

    def test_value_only_species_jacobian_reuses_rate_storage_without_formula_edits(self):
        original = '''template<class MatrixType>
ARCH_HEAVY_INLINE void actual_jac(burn_t& state, MatrixType& jac)
{
    Array1D<Real, 1, NumSpec> Y;
    for (int i = 1; i <= NumSpec; ++i) { Y(i) = state.xn[i-1]; }
    jac.zero();
    rate_derivs_t rate_eval;
    constexpr int do_T_derivatives = 0;
    evaluate_rates<do_T_derivatives, rate_derivs_t>(state, rate_eval);
    jac_nuc(state, jac, Y, rate_eval.screened_rates);
}
'''
        changed = PortableCxx.reuse_value_rate_storage(original)
        self.assertEqual(changed, original.replace('rate_derivs_t', 'rate_t'))
        self.assertEqual(changed, PortableCxx.reuse_value_rate_storage(changed))
        for unknown in (original.replace('do_T_derivatives = 0', 'do_T_derivatives = 1'),
                        original.replace('rate_eval.screened_rates', 'rate_eval.dscreened_rates_dT'),
                        original.replace('state, rate_eval);', 'state, rate_eval, tables);'),
                        original.replace('    jac_nuc(', '    consume(rate_eval);\n    jac_nuc(')):
            with self.subTest(body=unknown):
                self.assertEqual(PortableCxx.reuse_value_rate_storage(unknown), unknown)

    def test_rhs_keeps_upstream_body_without_unqualified_row_wrappers(self):
        # The per-isotope RHS split did not establish a compile/RSS benefit.
        # Keep the original aggregate body and only the shared heavy-call
        # annotation; Jacobian row boundaries remain independently covered.
        stage = self.fixture()
        expression = 'ydot_nuc(He4) = -Y(He4) * state.rho + screened_rates(k_sample);'
        path = stage / "generated/actual_rhs.H"
        path.write_text(RHS + '\ninline\nvoid rhs_nuc(State state, Result& ydot_nuc, '
                        'Fractions Y, Rates screened_rates) {\n' + expression + '\n}\n')
        self.assertTrue(PortableCxx.portable_headers(stage / "generated", "alpha"))
        result = path.read_text()
        self.assertIn('ARCH_HEAVY_INLINE\nvoid rhs_nuc(', result)
        self.assertEqual(result.count(expression), 1)
        self.assertNotIn('rhs_nuc_row_', result)

    def test_jacobian_rows_move_verbatim_with_original_write_order(self):
        signature = ("template<class MatrixType>\nARCH_HEAVY_INLINE\nvoid jac_nuc("
                     "const burn_t& state, MatrixType& jac, const Array1D<Real, 1, NumSpec>& Y, "
                     "const Array1D<Real, 1, NumRates>& screened_rates) {\nReal scratch;\n")
        entries = ["scratch = -Y(He4) * state.rho;\njac.set(He4, He4, scratch);",
                   "scratch = Y(C12) + 1.234e-17_rt;\njac.set(He4, C12, scratch);",
                   "scratch = screened_rates(k_sample) * Y(He4);\njac.set(C12, He4, scratch);"]
        original = signature + '\n'.join(entries) + '\n}\n'
        transformed = PortableCxx.split_jacobian_rows(original)
        for expression in entries:
            self.assertEqual(transformed.count(expression), 1)
        self.assertLess(transformed.index(entries[0]), transformed.index(entries[1]))
        self.assertLess(transformed.index(entries[1]), transformed.index(entries[2]))
        self.assertEqual(re.findall(r'jac_nuc_row_(\w+)\(state, jac, Y, screened_rates\);', transformed),
                         ['He4', 'C12'])
        for invalid in (original.replace('Real scratch;', 'Real scratch; state.rho = 0;'),
                        original.replace('jac.set(C12, He4', 'jac.set(He4, He4')
                            .replace(entries[1], entries[1].replace('jac.set(He4', 'jac.set(C12')),
                        original.replace('const burn_t& state', 'const burn_t& unknown')):
            with self.assertRaises(ValueError):
                PortableCxx.split_jacobian_rows(invalid)

    def test_heavy_stages_change_annotation_only_and_are_idempotent(self):
        original = ("ARCH_HOST_DEVICE inline\nvoid actual_jac(burn_t& s, Matrix& jac) {\n"
                    "  jac.set(He4, He4, 1.2345678901234567e-17_rt);\n}\n"
                    "ARCH_HOST_DEVICE inline Real scalar_rate(Real T) { return T*T; }\n")
        changed = PortableCxx.bounded_math_calls(original)
        self.assertEqual(changed, original.replace("ARCH_HOST_DEVICE inline\nvoid actual_jac",
                                                  "ARCH_HEAVY_INLINE\nvoid actual_jac"))
        self.assertEqual(changed, PortableCxx.bounded_math_calls(changed))
        reaction = "ARCH_HOST_DEVICE inline\nvoid rate_sample_reaclib(Real& r) { r = 2.0; }"
        self.assertEqual(PortableCxx.bounded_math_calls(reaction),
                         reaction.replace("ARCH_HOST_DEVICE inline", "ARCH_HEAVY_INLINE"))
        screening = "ARCH_HOST_DEVICE inline void actual_log_screen(State s, Factors f, Real& r) { r = chugunov2007(s, f); }"
        self.assertEqual(PortableCxx.bounded_math_calls(screening),
                         screening.replace("ARCH_HOST_DEVICE inline", "ARCH_HEAVY_INLINE"))
        factors = ("ARCH_HOST_DEVICE inline\nscreen_factors_t calculate_screen_factor(Real z) "
                   "{ return {std::pow(z, 1.0_rt / 3.0_rt)}; }")
        changed = PortableCxx.bounded_math_calls(factors)
        self.assertEqual(changed, factors.replace("ARCH_HOST_DEVICE inline", "ARCH_HEAVY_INLINE"))
        self.assertEqual(changed, PortableCxx.bounded_math_calls(changed))

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def fixture(self, name="alpha", tables=0):
        stage = self.root / name
        generated = stage / "generated"
        generated.mkdir(parents=True)
        for filename, content in {
            "network_properties.H": PROPERTIES,
            "actual_network.H": NETWORK,
            "actual_rhs.H": RHS,
            "amrex_bridge.H": BRIDGE,
            "table_rates.H": f"#ifndef TABLE_RATES_H\n#define TABLE_RATES_H\nconst int num_tables = {tables};\n#endif\n",
            "fundamental_constants.H": "#ifndef CONSTANTS_H\n#define CONSTANTS_H\nconstexpr Real enuc_conv2 = -6.02214129e23 * 2.99792458e10 * 2.99792458e10;\n#endif\n",
        }.items():
            (generated / filename).write_text(content, encoding="utf-8")
        return stage

    @staticmethod
    def snapshot(stage):
        return {path.relative_to(stage).as_posix(): path.read_bytes()
                for path in stage.rglob("*") if path.is_file()}

    @staticmethod
    def adapter(stage, name="alpha", screening=False):
        # Only generation-time metadata, with no dependency on pynucastro.
        nuclei = [SimpleNamespace(A=4, Z=2, mass=3727.3794, A_nuc=4.001506,
                                  short_spec_name="he4"),
                  SimpleNamespace(A=12, Z=6, mass=11174.862, A_nuc=12.0,
                                  short_spec_name="c12")]
        GenerateNetwork.stabilize_generated_energy_sum(stage / 'generated')
        return GenerateNetwork.write_adapter(stage, name,
            SimpleNamespace(unique_nuclei=nuclei, do_screening=screening, rates=[]))

    def test_nonzero_weak_table_packages_remain_unchanged_cpu_only(self):
        stage = self.fixture(tables=2)
        cls, header, source = self.adapter(stage)
        text = (stage / header).read_text()
        # Shared ODE energy accounting consumes the same scalar interface for
        # CPU-only packages; portability must not create a missing API.
        self.assertIn('static double aion(int index) { return AION[index]; }', text)
        self.assertIn('static double energy_weight(int index) { return ENERGY_WEIGHTS[index]; }', text)
        before = self.snapshot(stage)
        self.assertFalse(PortableAdapter.adapt(stage, "alpha", cls, header, source))
        self.assertEqual(before, self.snapshot(stage))
        self.assertFalse((stage / f"{cls}.math.h").exists())

    def test_weak_coordinate_transform_preserves_one_interpolation_expression(self):
        result = WeakTables.differentiable_coordinates(TABLE_COORDINATES)
        for line in TABLE_COORDINATES.splitlines():
            if line.strip().startswith(('Real dx', 'Real A', 'Real B', 'f =')):
                self.assertEqual(result.count(line), 1)
        self.assertEqual(result.count('evaluate_linear_2d('), 2)  # definition + call
        self.assertEqual(result.count('::timmes::clamp_by_value('), 2)
        self.assertIn('class Coordinate', result)
        self.assertNotIn('amrex::Clamp', result)
        with self.assertRaises(ValueError):
            WeakTables.differentiable_coordinates(TABLE_COORDINATES.replace('Real f;', 'auto f = 0.0;'))

    def test_weak_coordinate_api_does_not_declare_device_ownership(self):
        stage = self.fixture(tables=2)
        cls, header, source = self.adapter(stage)
        path = stage / 'generated/table_rates.H'
        path.write_text(path.read_text().replace('#endif', TABLE_COORDINATES + '#endif'))
        self.assertTrue(WeakTables.enable_coordinate_derivatives(stage, header))
        self.assertIn('tabular_value_gradients(', path.read_text())
        self.assertIn('physics/network/timmes_common/Dual.h', (stage / header).read_text())
        before = self.snapshot(stage)
        self.assertFalse(PortableAdapter.adapt(stage, 'alpha', cls, header, source))
        self.assertEqual(before, self.snapshot(stage))
        with self.assertRaises(ValueError):
            WeakTables.enable_coordinate_derivatives(stage, header)
        self.assertEqual(before, self.snapshot(stage))

    def test_weak_coordinate_unknown_shape_fails_without_partial_write(self):
        stage = self.fixture(tables=2)
        _, header, _ = self.adapter(stage)
        before = self.snapshot(stage)
        with self.assertRaises(ValueError):
            WeakTables.enable_coordinate_derivatives(stage, header)
        self.assertEqual(before, self.snapshot(stage))

    def test_table_free_coordinate_transform_is_a_noop(self):
        stage = self.fixture()
        _, header, _ = self.adapter(stage)
        before = self.snapshot(stage)
        self.assertFalse(WeakTables.enable_coordinate_derivatives(stage, header))
        self.assertEqual(before, self.snapshot(stage))

    def weak_header_fixture(self, name='alpha'):
        stage = self.fixture(name=name, tables=2)
        cls, header, source = self.adapter(stage, name=name)
        table = stage / 'generated/table_rates.H'
        metadata = ('inline table_t j_parent_daughter_meta{};\n'
                    'inline table_t j_daughter_parent_meta{};\n')
        table.write_text(table.read_text().replace('#endif', metadata + TABLE_COORDINATES + '#endif'))
        self.assertTrue(WeakTables.enable_coordinate_derivatives(stage, header))
        self.assertFalse(PortableAdapter.adapt(stage, name, cls, header, source, host_weak_math=True))
        return stage, cls, header, source

    @staticmethod
    def weak_inventory():
        # Symbolic interface metadata only, not a physical reaction fixture.
        return SimpleNamespace(unique_nuclei=['parent', 'daughter'], tabular_rates=[
            SimpleNamespace(table_index_name='j_parent_daughter', reactants=['parent'], products=['daughter']),
            SimpleNamespace(table_index_name='j_daughter_parent', reactants=['daughter'], products=['parent'])])

    def test_weak_derivatives_have_one_rhs_and_one_symbolic_correction_body(self):
        stage, cls, header, source = self.weak_header_fixture()
        WeakTables.connect_weak_derivatives(stage, 'alpha', self.weak_inventory())
        public = (stage / header).read_text()
        math = (stage / f'{cls}.math.h').read_text()
        self.assertEqual((stage / source).read_text(), f'#include "{header}"\n')
        self.assertEqual(math.count('::actual_rhs(burn, dydt, enu_weak);'), 1)
        self.assertEqual(math.count('void visit_weak_derivatives('), 1)
        self.assertIn('if (nonconservative) *nonconservative = enu_weak;', math)
        self.assertIn('matrix(row + 1, column + 1) + value', math)
        self.assertIn('sink.set(1, column, 0.0)', math)
        self.assertIn('sink.set(2, column, 0.0)', math)
        self.assertIn('eval_nonconservative_gradient', public)
        self.assertIn('NONCONSERVATIVE_ENERGY_INDEX = NUM_SPECIES + 1', public)
        self.assertIn('ODE_NEQ = NUM_SPECIES + 2', public)
        self.assertIn('temperature_derivative<Network::NUM_SPECIES + 1>', math)
        self.assertNotIn('ARCH_HOST_DEVICE', public + math)
        self.assertNotIn('std::vector<int> rows, columns;', public + math)

    def test_weak_derivative_inventory_mismatch_preserves_staged_files(self):
        stage, _, _, _ = self.weak_header_fixture()
        before = self.snapshot(stage)
        inventory = self.weak_inventory()
        inventory.tabular_rates[0].table_index_name = 'j_absent'
        with self.assertRaises(ValueError):
            WeakTables.connect_weak_derivatives(stage, 'alpha', inventory)
        self.assertEqual(before, self.snapshot(stage))
        inventory.tabular_rates.pop()
        with self.assertRaises(ValueError):
            WeakTables.connect_weak_derivatives(stage, 'alpha', inventory)
        self.assertEqual(before, self.snapshot(stage))

    def test_cpu_weak_headers_also_scope_guards_and_bound_loop_depth(self):
        for name in ('alpha', 'beta'):
            stage, _, _, _ = self.weak_header_fixture(name)
            for header in (stage / 'generated').glob('*.H'):
                guards = re.findall(r'^#ifndef\s+(\w+)', header.read_text(), re.MULTILINE)
                self.assertTrue(all(guard.startswith(f'ARCH_GENERATED_{name.upper()}_') for guard in guards))
            bridge = (stage / 'generated/amrex_bridge.H').read_text()
            self.assertIn('constexpr_for<I, middle>', bridge)
            self.assertNotIn('ARCH_HOST_DEVICE', bridge)

    def test_portable_scalar_accessors_replace_host_bodies_once(self):
        stage = self.fixture()
        cls, header, source = self.adapter(stage)
        self.assertTrue(PortableAdapter.adapt(stage, "alpha", cls, header, source))
        text = (stage / header).read_text()
        math = (stage / f"{cls}.math.h").read_text()
        for name in ('aion', 'energy_weight'):
            self.assertEqual(text.count(f'static double {name}(int index)'), 1)
            self.assertIn(f'ARCH_HOST_DEVICE static double {name}(int index);', text)
            self.assertEqual(math.count(f'double {cls}::{name}(int index)'), 1)
        self.assertNotIn('return AION[index];', text)
        self.assertNotIn('return ENERGY_WEIGHTS[index];', text)

    def test_configuration_record_does_not_import_runtime_parser(self):
        # SetupInitialFractions consumes SimConfig, not parameter-file IO. This
        # boundary applies to the CPU adapter too, before device adaptation.
        stage = self.fixture()
        cls, header, source = self.adapter(stage)
        text = (stage / header).read_text()
        self.assertIn('#include "data/GlobalDefs.h"', text)
        self.assertNotIn('RuntimeParams.h', text)
        self.assertTrue(PortableAdapter.adapt(stage, "alpha", cls, header, source))
        for name in (header, source, f"{cls}.math.h"):
            self.assertNotIn('RuntimeParams.h', (stage / name).read_text())

    def test_unrecognized_table_count_is_not_declared_device_callable(self):
        stage = self.fixture()
        (stage / "generated/table_rates.H").write_text("// unknown upstream table contract\n")
        before = self.snapshot(stage)
        self.assertFalse(PortableCxx.portable_headers(stage / "generated", "alpha"))
        self.assertEqual(before, self.snapshot(stage))

    def test_lookup_preserves_exact_constant_tokens_and_index_origins(self):
        stage = self.fixture()
        self.assertTrue(PortableCxx.portable_headers(stage / "generated", "alpha"))
        properties = (stage / "generated/network_properties.H").read_text()
        network = (stage / "generated/actual_network.H").read_text()
        for literal in ("4.0_rt", "12.0_rt", "0.25_rt", "0.083333333333333329_rt"):
            self.assertIn("return " + literal + ";", properties)
        for literal in ("6.6446571948428542e-24_rt", "1.9926468799200001e-23_rt"):
            self.assertIn("return " + literal + ";", network)
        self.assertIn("lo() { return 0; }", properties)
        self.assertIn("lo() { return 1; }", network)
        self.assertNotRegex(properties, r"Real\s+(?:aion|aion_inv|zion)\s*\[")
        self.assertNotRegex(network, r"Array1D<Real,.*?\bmion\s*\{")

    def test_bridge_loop_keeps_callback_type_and_ordered_logarithmic_subranges(self):
        stage = self.fixture()
        self.assertTrue(PortableCxx.portable_headers(stage / "generated", "alpha"))
        bridge = (stage / "generated/amrex_bridge.H").read_text()
        self.assertIn("ARCH_HOST_DEVICE inline\n    constexpr void constexpr_for", bridge)
        self.assertIn("if constexpr (I < N)", bridge)
        self.assertIn("if constexpr (I + 1 < N)", bridge)
        self.assertIn("constexpr decltype(I + 1) middle = I + (N - I) / 2;", bridge)
        self.assertEqual(bridge.count("f(std::integral_constant<decltype(I), I>());"), 1)
        calls = re.findall(r"constexpr_for<\s*(\w+)\s*,\s*(\w+)\s*>\(f\);", bridge)
        self.assertEqual(calls, [("I", "middle"), ("middle", "N")])
        self.assertNotRegex(bridge, r"constexpr_for<\s*I\s*\+\s*1\s*,")

        # Interpret only the emitted interval schedule, not any reaction math.
        # This is an input-contract test; actual C++/CUDA execution is separate.
        def visit(first, end, schedule=calls, depth=1):
            if first >= end:
                return [], depth
            if first + 1 >= end:
                return [first], depth
            names = {"I": first, "N": end, "middle": first + (end - first) // 2}
            values, maximum_depth = [], depth
            for low, high in schedule:
                result, nested_depth = visit(names[low], names[high], schedule, depth + 1)
                values.extend(result)
                maximum_depth = max(maximum_depth, nested_depth)
            return values, maximum_depth

        for first, end in ((0, 0), (5, 3), (7, 8), (0, 2), (1, 201),
                           (0, 201), (-3, 202), (17, 4101)):
            with self.subTest(first=first, end=end):
                values, depth = visit(first, end)
                self.assertEqual(values, list(range(first, end)))
                self.assertEqual(len(values), len(set(values)))
                self.assertLessEqual(depth, max(1, (max(0, end - first) - 1).bit_length() + 1))
        # Deliberate bad schedules must not satisfy the order/once contract.
        self.assertNotEqual(visit(1, 201, list(reversed(calls)))[0], list(range(1, 201)))
        self.assertNotEqual(visit(1, 201, [calls[0], calls[0]])[0], list(range(1, 201)))

    def test_unknown_or_duplicate_bridge_loop_fails_before_any_output_write(self):
        for name, invalid in (
            ("step_two", BRIDGE.replace("constexpr_for<I+1, N>", "constexpr_for<I+2, N>")),
            ("changed_type", BRIDGE.replace("decltype(I), I", "int, I")),
            ("extra_callback", BRIDGE.replace("constexpr_for<I+1, N>(f);", "f(I); constexpr_for<I+1, N>(f);")),
            ("duplicate", BRIDGE + BRIDGE),
        ):
            with self.subTest(name=name):
                stage = self.fixture(name)
                (stage / "generated/amrex_bridge.H").write_text(invalid)
                before = self.snapshot(stage)
                with self.assertRaisesRegex(ValueError, "constexpr_for bridge layout"):
                    PortableCxx.portable_headers(stage / "generated", name)
                self.assertEqual(self.snapshot(stage), before)

    def test_already_annotated_bridge_is_handled_without_changing_surrounding_code(self):
        original = BRIDGE.replace("    inline\n", "    ARCH_HOST_DEVICE inline\n")
        transformed = PortableCxx.balanced_constexpr_for(original)
        self.assertEqual(transformed.count("ARCH_HOST_DEVICE"), 1)
        self.assertTrue(transformed.startswith(original.split("    template<", 1)[0]))
        self.assertTrue(transformed.endswith("\n}\n#endif\n"))

    def test_independent_packages_have_disjoint_include_guards(self):
        guard_sets = []
        for name in ("alpha", "beta"):
            stage = self.fixture(name)
            self.assertTrue(PortableCxx.portable_headers(stage / "generated", name))
            guards = set()
            for path in (stage / "generated").glob("*.H"):
                text = path.read_text()
                definitions = set(re.findall(r"^#define\s+(\w+)", text, re.MULTILINE))
                for guard in re.findall(r"^#ifndef\s+(\w+)", text, re.MULTILINE):
                    self.assertTrue(guard.startswith(f"ARCH_GENERATED_{name.upper()}_"))
                    self.assertIn(guard, definitions)
                    guards.add(guard)
            guard_sets.append(guards)
        self.assertTrue(guard_sets[0])
        self.assertFalse(guard_sets[0] & guard_sets[1])

    def test_reaction_expressions_and_scalar_constants_are_not_rederived(self):
        stage = self.fixture()
        self.assertTrue(PortableCxx.portable_headers(stage / "generated", "alpha"))
        rhs = (stage / "generated/actual_rhs.H").read_text()
        self.assertIn("const Real scratch = state.rho * 1.2345678901234567e-17_rt;", rhs)
        self.assertIn("jac.set(C12, He4, 3.0_rt * scratch);", rhs)
        self.assertIn("dydt(n) * network::mion_values()(n)", rhs)
        constants = (stage / "generated/fundamental_constants.H").read_text()
        self.assertIn("-6.02214129e23 * 2.99792458e10 * 2.99792458e10", constants)

    def test_jacobian_structure_comes_from_declared_writes_not_sampled_values(self):
        stage = self.fixture()
        self.assertEqual(PortableCxx.jacobian_structure(stage / "generated"),
                         [("He4", "He4"), ("C12", "He4")])
        # This term may vanish in a sample state; it remains structural.
        path = stage / "generated/actual_rhs.H"
        path.write_text(path.read_text().replace("3.0_rt * scratch", "state.rho * scratch"))
        self.assertEqual(PortableCxx.jacobian_structure(stage / "generated"),
                         [("He4", "He4"), ("C12", "He4")])

    def test_only_literal_zero_writes_are_pruned(self):
        stage = self.fixture()
        path = stage / "generated/actual_rhs.H"
        path.write_text(RHS.replace("    jac.zero();", "    jac.zero();\n    jac.set(He4, C12, 0.0);"))
        self.assertEqual(GenerateNetwork.prune_literal_zero_jacobian(stage / "generated"), 1)
        self.assertEqual(PortableCxx.jacobian_structure(stage / "generated"),
                         [("He4", "He4"), ("C12", "He4")])

    def test_unknown_jacobian_indices_fail_closed_instead_of_losing_nonzeros(self):
        stage = self.fixture()
        path = stage / "generated/actual_rhs.H"
        for indices in ("Species::He4, C12", "row + 1, C12", "He4, columns[index]"):
            with self.subTest(indices=indices):
                path.write_text(RHS + f"\njac.set({indices}, scratch);\n")
                with self.assertRaisesRegex(ValueError, "unrecognized structural write"):
                    PortableCxx.jacobian_structure(stage / "generated")

    def test_screened_and_unscreened_packages_restore_caller_macro(self):
        math_headers = []
        for name, screening in (("alpha", True), ("beta", False)):
            stage = self.fixture(name)
            cls, header, source = self.adapter(stage, name, screening)
            self.assertTrue(PortableAdapter.adapt(stage, name, cls, header, source))
            math_headers.append((stage / f"{cls}.math.h").read_text())
        for initial in (None, "27"):
            with self.subTest(initial=initial):
                macro = initial
                stack = []
                selected = []
                # Interpret only the generated SCREENING wrapper directives;
                # the reaction bodies are not reimplemented or compiled here.
                for text in math_headers:
                    self.assertEqual(text.count('#pragma push_macro("SCREENING")'), 1)
                    self.assertEqual(text.count('#pragma pop_macro("SCREENING")'), 1)
                    for line in text.splitlines():
                        if line == '#pragma push_macro("SCREENING")':
                            stack.append(macro)
                        elif line == '#pragma pop_macro("SCREENING")':
                            self.assertTrue(stack)
                            macro = stack.pop()
                        elif line == '#undef SCREENING':
                            macro = None
                        elif line.startswith('#define SCREENING '):
                            macro = line.split(maxsplit=2)[2]
                        elif line == '#include "generated/actual_rhs.H"':
                            selected.append(macro)
                    self.assertEqual(macro, initial, "package leaked its SCREENING selection")
                    self.assertFalse(stack)
                self.assertEqual(selected, ["1", None])

    def test_adapter_moves_one_math_body_and_reuses_upstream_energy_metadata(self):
        stage = self.fixture()
        cls, header_name, source_name = self.adapter(stage)
        original_header = (stage / header_name).read_text()
        original_weights = re.search(r"ENERGY_WEIGHTS\{([^}]+)\}", original_header).group(1)
        expected_weights = [value.strip() for value in original_weights.split(",")]
        original_masses = [6.6446571948428542e-24, 1.9926468799200001e-23]
        origin = min(m / a for m, a in zip(original_masses, [4, 12]))
        self.assertEqual([float(value).hex() for value in expected_weights],
                         [float(Fraction(m) - a * Fraction(origin)).hex()
                          for m, a in zip(original_masses, [4, 12])])
        self.assertTrue(PortableAdapter.adapt(stage, "alpha", cls, header_name, source_name))
        header = (stage / header_name).read_text()
        math = (stage / f"{cls}.math.h").read_text()
        self.assertEqual((stage / source_name).read_text(), f'#include "{header_name}"\n')
        self.assertIn(f'#include "{cls}.math.h"', header)
        self.assertEqual(math.count("ARCH_HEAVY_INLINE void eval_rhs("), 1)
        self.assertNotIn("std::vector<double>", math)
        self.assertNotIn("Network::AION[", math)
        self.assertIn("matrix.set(row, column, mass_value);", math)
        energy_body = math.split(f"double {cls}::energy_weight(int index)", 1)[1].split("template<class Sink>", 1)[0]
        self.assertIn("return arch_pynucastro_alpha::network::energy_mion(index + 1);", energy_body)
        self.assertNotIn("switch", energy_body)
        self.assertIn("ENERGY_WEIGHTS{" + original_weights + "}", header)
        self.assertIn("arch_pynucastro_alpha::Species::C12", math)

    def test_energy_conversion_preserves_upstream_expression_order(self):
        stage = self.fixture()
        path = stage / 'generated/fundamental_constants.H'
        path.write_text('constexpr Real c_light = 2.99792458e10;\n'
                        'constexpr Real n_A = 6.02214129e23;\n'
                        'constexpr Real enuc_conv2 = -n_A * c_light * c_light;\n')
        masses, conversion = GenerateNetwork.generated_energy_metadata(stage / 'generated', 2)
        self.assertEqual(conversion.hex(), (-6.02214129e23 * 2.99792458e10 * 2.99792458e10).hex())
        self.assertEqual(len(masses), 2)
        with self.assertRaisesRegex(ValueError, 'extent'):
            GenerateNetwork.generated_energy_metadata(stage / 'generated', 3)
        for invalid in ('std::pow(c_light, 2)', 'unknown', 'enuc_conv2', '1.0 / 0.0'):
            path.write_text(f'constexpr Real enuc_conv2 = {invalid};\n')
            with self.assertRaises((ValueError, SyntaxError, ZeroDivisionError)):
                GenerateNetwork.generated_energy_metadata(stage / 'generated', 2)

    def test_energy_gauge_is_invariant_for_conserved_reactions_and_rejects_mass_leaks(self):
        stage = self.fixture()
        helium, carbon = SimpleNamespace(A=4), SimpleNamespace(A=12)
        rates = [SimpleNamespace(reactants=[helium] * 3, products=[carbon])]
        masses, _ = GenerateNetwork.generated_energy_metadata(stage / 'generated', 2)
        weights, _ = GenerateNetwork.install_conserved_energy_gauge(
            stage / 'generated', SimpleNamespace(rates=rates), [helium, carbon])
        # Independent exact-rational reaction Q from emitted masses. Fused
        # mass subtraction loses no information for this dyadic witness.
        self.assertEqual(-3 * Fraction(masses[0]) + Fraction(masses[1]),
                         -3 * Fraction(weights[0]) + Fraction(weights[1]))
        native = (stage / 'generated/actual_network.H').read_text()
        self.assertEqual(native.count('inline Real energy_mion('), 1)
        self.assertIn('std::fma(-aion[n - 1], baryon_mass_origin, mion(n))', native)
        self.assertTrue(PortableCxx.portable_headers(stage / 'generated', 'alpha'))
        portable = (stage / 'generated/actual_network.H').read_text()
        self.assertIn('std::fma(-aion_values()[n - 1], baryon_mass_origin, mion_values()(n))', portable)
        invalid = self.fixture('invalid')
        before = self.snapshot(invalid)
        for baryons in (3, 0, 4.0):
            bad = SimpleNamespace(A=baryons)
            rate = SimpleNamespace(reactants=[bad], products=[carbon])
            with self.assertRaisesRegex(ValueError, 'baryon'):
                GenerateNetwork.install_conserved_energy_gauge(
                    invalid / 'generated', SimpleNamespace(rates=[rate]), [bad, carbon])
            self.assertEqual(before, self.snapshot(invalid))

    def test_generated_energy_dot_keeps_upstream_mass_data_and_one_sum_authority(self):
        stage = self.fixture()
        path = stage / 'generated/actual_rhs.H'
        original = '''template<class T>
void ener_gener_rate(T const& dydt, Real& enuc) {
    enuc = 0.0_rt;
    for (int n = 1; n <= NumSpec; ++n) {
        enuc += dydt(n) * network::mion(n);
    }
    enuc *= C::enuc_conv2;
}
'''
        path.write_text(original)
        GenerateNetwork.stabilize_generated_energy_sum(stage / 'generated')
        result = path.read_text()
        self.assertEqual(result.count('arch::math::CompensatedSum nuclear_mass;'), 1)
        self.assertIn('nuclear_mass.add_product(dydt(n), network::energy_mion(n));', result)
        self.assertIn('nuclear_mass.value() * C::enuc_conv2', result)
        for invalid in (original.replace('network::mion(n)', 'unrecognized_mass(n)'), original + original):
            path.write_text(invalid)
            with self.assertRaisesRegex(ValueError, 'accumulation'):
                GenerateNetwork.stabilize_generated_energy_sum(stage / 'generated')
            self.assertEqual(path.read_text(), invalid)

    def test_complete_temperature_rhs_delegates_to_shared_difference_policy(self):
        stage = self.fixture()
        cls, header, source = self.adapter(stage)
        for portable in (False, True):
            if portable:
                self.assertTrue(PortableAdapter.adapt(stage, 'alpha', cls, header, source))
            text = (stage / (f'{cls}.math.h' if portable else source)).read_text()
            self.assertEqual(text.count('arch::burnmath::temperature_derivative<Network::NUM_SPECIES + 1>'), 1)
            self.assertIn('eval_rhs(state, rho, rhs, energy);', text)
            self.assertNotIn('(er-el)*inv', text)


if __name__ == "__main__":
    unittest.main()
