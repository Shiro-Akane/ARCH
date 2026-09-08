"""Adapt the recognized SimpleCxx weak-table interface, not its interpolant.

Coordinate derivatives reuse the existing ARCH dual-number primitive and the
upstream bilinear expression. Storage ownership and ODE wiring are separate
gates: this transform alone does not make a table package CUDA-capable.
"""

import re
from collections import Counter


def _replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('generated weak-table coordinate interface is not recognized: ' + old)
    return text.replace(old, new, 1)


def differentiable_coordinates(text):
    """Make the two existing coordinate-consuming functions scalar-generic.

    Table entries and the original polynomial coefficients remain ordinary
    doubles. Only query coordinates/results carry derivatives. Keep expression
    ordering and refuse unfamiliar shapes rather than insert a second formula.
    """
    start = text.index('inline\nReal\nevaluate_linear_2d(')
    end = text.index('\n}', start) + len('\n}')
    original = text[start:end]
    body = _replace_once(original, 'inline\nReal\n', 'template<class Coordinate>\ninline\nCoordinate\n')
    body = _replace_once(body, 'const Real x, const Real y)',
                         'const Coordinate x, const Coordinate y)')
    body = _replace_once(body, '    Real f;', '    Coordinate f;')
    for coordinate in ('x', 'y'):
        body = _replace_once(body,
            f'Real {coordinate}{coordinate} = amrex::Clamp({coordinate}, {coordinate}lo, {coordinate}hi);',
            f'Coordinate {coordinate}{coordinate} = ::timmes::clamp_by_value({coordinate}, {coordinate}lo, {coordinate}hi);')
    text = text[:start] + body + text[end:]

    start = text.index('template<typename R, typename T, typename D>\ninline\nReal\nevaluate_vars(')
    end = text.index('\n}', start) + len('\n}')
    original = text[start:end]
    body = _replace_once(original, 'template<typename R, typename T, typename D>\ninline\nReal\n',
                         'template<typename R, typename T, typename D, class Coordinate>\ninline\nCoordinate\n')
    body = _replace_once(body, 'const Real log_rhoy, const Real log_temp, const int component)',
                         'const Coordinate log_rhoy, const Coordinate log_temp, const int component)')
    body = _replace_once(body, 'Real r = evaluate_linear_2d(', 'Coordinate r = evaluate_linear_2d(')
    return text[:start] + body + text[end:]


GRADIENT_INTERFACE = '''
// Natural-coordinate gradients of the very same clamped log-table interpolant.
// At a knot, use the cell selected by find_index_extrap; outside the domain
// clamp_by_value supplies zero coordinate derivative. No averaged wider stencil.
struct TableValueGradient {
    Real value, d_rhoy, d_temperature;
};
template<typename R, typename T, typename D>
inline
std::array<TableValueGradient, num_vars> tabular_value_gradients(
    const table_t& meta, const R& rhoy_axis, const T& temperature_axis,
    const D& data, Real rhoy, Real temperature)
{
    std::array<TableValueGradient, num_vars> result{};
    if (!std::isfinite(rhoy) || !(rhoy > 0.0_rt)
        || !std::isfinite(temperature) || !(temperature > 0.0_rt)
        || meta.nrhoy < 2 || meta.ntemp < 2 || meta.nvars != num_vars) {
        const Real invalid = std::numeric_limits<Real>::quiet_NaN();
        for (auto& entry : result) entry = {invalid, invalid, invalid};
        return result;
    }
    using Coordinate = ::timmes::Dual<2>;
    const auto log_rhoy = Coordinate::variable(std::log10(rhoy), 0);
    const auto log_temperature = Coordinate::variable(std::log10(temperature), 1);
    const int density_cell = interp_net::find_index_extrap(log_rhoy.value, rhoy_axis);
    const int temperature_cell = interp_net::find_index_extrap(log_temperature.value, temperature_axis);
    for (int component = 1; component <= num_vars; ++component) {
        const auto logarithm = evaluate_vars(density_cell, temperature_cell,
            rhoy_axis, temperature_axis, data, log_rhoy, log_temperature, component);
        Real value = std::pow(10.0_rt, logarithm.value);
        if (component == jtab_nuloss) value = -value;
        // For v=10^f(log10 q), dv/dq = v * df/dlog10(q) / q.
        // Both factors of ln(10) cancel; the signed loss follows the same rule.
        result[component - 1] = {value, value * logarithm.deriv[0] / rhoy,
                                value * logarithm.deriv[1] / temperature};
    }
    return result;
}
'''


def enable_coordinate_derivatives(stage, header_name):
    path = stage / 'generated/table_rates.H'
    text = path.read_text(encoding='utf-8')
    count = re.search(r'const int num_tables\s*=\s*(\d+)\s*;', text)
    if count is None:
        raise ValueError('generated weak-table count is not recognized')
    if int(count.group(1)) == 0:
        return False
    if 'tabular_value_gradients' in text:
        raise ValueError('generated weak-table derivative interface already exists')
    transformed = differentiable_coordinates(text)
    end = transformed.rfind('#endif')
    if end < 0 or transformed[end:].strip() != '#endif':
        raise ValueError('generated weak-table header guard is not recognized')
    transformed = transformed[:end] + GRADIENT_INTERFACE + transformed[end:]
    header_path = stage / header_name
    header = header_path.read_text(encoding='utf-8')
    header = _replace_once(header, '#include <array>',
        '#include <array>\n#include <limits>\n#include "physics/network/timmes_common/Dual.h"')
    # Validate every edit before publishing either member of this staged package.
    path.write_text(transformed, encoding='utf-8')
    header_path.write_text(header, encoding='utf-8')
    return True


def connect_weak_derivatives(stage, network_id, network):
    """Supply the missing weak composition/energy terms in the shared adapter.

    Upstream actual_jac differentiates abundance algebra at frozen tabular
    rates. Add its rho*Ye chain rule, not a second reaction RHS. The generated
    stoichiometry determines affected rows; no runtime isotope-name selection.
    """
    cls = f'NetCustom_{network_id}'
    generated = f'arch_pynucastro_{network_id}'
    detail = f'arch_custom_{network_id}_detail'
    indices = {nucleus: index for index, nucleus in enumerate(network.unique_nuclei)}
    table_text = (stage / 'generated/table_rates.H').read_text(encoding='utf-8')
    count = re.search(r'const int num_tables\s*=\s*(\d+)\s*;', table_text)
    if count is None or int(count.group(1)) != len(network.tabular_rates):
        raise ValueError('weak rate inventory differs from the generated table count')
    rows = set()
    contributions = []
    for rate in network.tabular_rates:
        if len(rate.reactants) != 1 or len(rate.products) != 1:
            raise ValueError('tabular weak rate is not a recognized single-parent transition')
        name = rate.table_index_name
        if not re.fullmatch(r'[A-Za-z_]\w*', name):
            raise ValueError('unsafe generated weak-table symbol')
        if len(re.findall(r'\btable_t\s+' + re.escape(name) + r'_meta\b', table_text)) != 1:
            raise ValueError('weak rate metadata is absent or ambiguous in the generated table')
        parent = indices[rate.reactants[0]]
        stoichiometry = Counter(rate.products)
        stoichiometry.subtract(rate.reactants)
        writes = []
        for nucleus, multiplicity in stoichiometry.items():
            if not multiplicity:
                continue
            row = indices[nucleus]
            rows.add(row)
            writes.append(f'            sink.species_jacobian({row}, column, '
                          f'{multiplicity}.0 * Network::aion({row}) * flux_gradient);')
        contributions.append(f'''    {{
        const auto values = {generated}::tabular_value_gradients(
            {generated}::rate_tables::{name}_meta,
            {generated}::rate_tables::{name}_rhoy,
            {generated}::rate_tables::{name}_temp,
            {generated}::rate_tables::{name}_data, rho * burn.y_e, burn.T);
        const auto& rate = values[{generated}::jtab_rate - 1];
        const auto& neutrino = values[{generated}::jtab_nuloss - 1];
        const auto& photon = values[{generated}::jtab_gamma - 1];
        const double parent_inverse_mass = {generated}::aion_inv[{parent}];
        const double parent_y = state[{parent}] * parent_inverse_mass;
        for (int column = 0; column < Network::NUM_SPECIES; ++column) {{
            const double rhoy_gradient = rho * {generated}::zion[column]
                * {generated}::aion_inv[column];
            const double flux_gradient = parent_y * rate.d_rhoy * rhoy_gradient;
{chr(10).join(writes)}
            const double explicit_parent = column == {parent}
                ? parent_inverse_mass * (neutrino.value + photon.value) : 0.0;
            sink.loss_gradient(column, {generated}::C::n_A * (explicit_parent
                + parent_y * (neutrino.d_rhoy + photon.d_rhoy) * rhoy_gradient));
        }}
        sink.loss_gradient(Network::NUM_SPECIES,
            {generated}::C::n_A * parent_y * (neutrino.d_temperature + photon.d_temperature));
    }}''')
    if not contributions:
        raise ValueError('weak derivative adapter requires declared tabular rates')

    header_path = stage / f'{cls}.h'
    math_path = stage / f'{cls}.math.h'
    header = header_path.read_text(encoding='utf-8')
    math = math_path.read_text(encoding='utf-8')
    header = _replace_once(header,
        'static constexpr int ODE_NEQ = NUM_SPECIES + 1;',
        'static constexpr int NONCONSERVATIVE_ENERGY_INDEX = NUM_SPECIES + 1;\n'
        '    static constexpr int ODE_NEQ = NUM_SPECIES + 2;')
    header = _replace_once(header,
        'inline void eval_rhs(const double*, double, double*, double&);',
        'inline void eval_rhs(const double*, double, double*, double&, double* = nullptr);\n'
        'inline void eval_nonconservative_gradient(const double*, double, double*);')
    header = _replace_once(header,
        'double* rhs, double& enuc) {\n        ' + detail + '::eval_rhs(state, rho, rhs, enuc);',
        'double* rhs, double& enuc, double* nonconservative = nullptr) {\n        '
        + detail + '::eval_rhs(state, rho, rhs, enuc, nonconservative);')
    marker = '    template <typename MatrixType>'
    header = _replace_once(header, marker, f'''    ARCH_HOST static double eval_nonconservative_energy(const double* state, double rho, double) {{
        double rhs[NUM_SPECIES], total, source;
        {detail}::eval_rhs(state, rho, rhs, total, &source);
        return source;
    }}
    ARCH_HOST static void eval_nonconservative_gradient(const double* state, double rho, double, double* gradient) {{
        {detail}::eval_nonconservative_gradient(state, rho, gradient);
    }}
''' + marker)
    math = _replace_once(math,
        'inline void eval_rhs(const double* state, double rho, double* rhs, double& enuc)',
        'inline void eval_rhs(const double* state, double rho, double* rhs, double& enuc, double* nonconservative)')
    math = _replace_once(math, f'    {generated}::actual_rhs(burn, dydt, enu_weak);',
        f'    {generated}::actual_rhs(burn, dydt, enu_weak);\n    if (nonconservative) *nonconservative = enu_weak;')
    body = '''template<class Sink>
inline void visit_weak_derivatives(const double* state, double rho, Sink& sink) {
    auto burn = make_state(state, rho);
''' + f'    {generated}::compute_ye(burn);\n' + '\n'.join(contributions) + '''
}
template<class Matrix>
struct WeakJacobianSink {
    Matrix& matrix;
    double* energy;
    void species_jacobian(int row, int column, double value) {
        matrix.set(row + 1, column + 1, matrix(row + 1, column + 1) + value);
        if (energy) energy[column] += Network::ENERGY_CONVERSION * value
            / Network::aion(row) * Network::energy_weight(row);
    }
    void loss_gradient(int column, double value) {
        if (energy && column < Network::NUM_SPECIES) energy[column] += value;
    }
};
struct WeakLossGradientSink {
    double* gradient;
    void species_jacobian(int, int, double) {}
    void loss_gradient(int column, double value) { gradient[column] += value; }
};
inline void eval_nonconservative_gradient(const double* state, double rho, double* gradient) {
    for (int i = 0; i <= Network::NUM_SPECIES; ++i) gradient[i] = 0.0;
    WeakLossGradientSink sink{gradient};
    visit_weak_derivatives(state, rho, sink);
}
'''
    math = _replace_once(math, 'template<class Matrix>\nstruct JacobianSink {',
                         body + 'template<class Matrix>\nstruct JacobianSink {')
    math = _replace_once(math, f'    {generated}::actual_jac(burn, sink);',
        f'    {generated}::actual_jac(burn, sink);\n'
        '    WeakJacobianSink<Matrix> weak{matrix, energy};\n'
        '    visit_weak_derivatives(state, rho, weak);')
    # These additional rows depend on every charge-bearing abundance via Ye.
    # Recording all columns is conservative symbolic structure, not state sampling.
    marker = f'inline void {cls}::enumerate_jacobian_structure(Sink& sink) {{'
    extra = ''.join(f'    for (int column = 1; column <= NUM_SPECIES; ++column) sink.set({row + 1}, column, 0.0);\n'
                    for row in sorted(rows))
    math = _replace_once(math, marker, marker + '\n' + extra)
    header_path.write_text(header, encoding='utf-8')
    math_path.write_text(math, encoding='utf-8')
