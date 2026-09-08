"""Turn the generated Host adapter into a single Host/Device numerical body."""

import re
from PortableCxx import host_headers, jacobian_structure, portable_headers


def adapt(stage, network_id, cls, header_name, source_name, *, host_weak_math=False):
    device_callable = portable_headers(stage / 'generated', network_id)
    if not device_callable and not host_weak_math:
        return False
    if host_weak_math and 'tabular_value_gradients(' not in (stage / 'generated/table_rates.H').read_text():
        raise ValueError('Host weak mathematical header requires recognized coordinate derivatives')
    if not device_callable:
        host_headers(stage / 'generated', network_id)
    annotation = 'ARCH_HOST_DEVICE' if device_callable else 'ARCH_HOST'
    heavy = 'ARCH_HEAVY_INLINE' if device_callable else 'inline'
    detail = f'arch_custom_{network_id}_detail'
    generated = f'arch_pynucastro_{network_id}'
    header_path = stage / header_name
    source_path = stage / source_name
    header = header_path.read_text(encoding='utf-8')
    source = source_path.read_text(encoding='utf-8')
    header = header.replace('#include <algorithm>', '#include "core/ArchPortability.h"\n#include <algorithm>', 1)
    declarations = f'''namespace {detail} {{
{heavy} void eval_rhs(const double*, double, double*, double&);
template<class Matrix>
{heavy} void eval_jacobian(const double*, double, Matrix&, double*);
{heavy} void eval_temperature_derivative(const double*, double, double*, double&);
}}'''
    header = re.sub(r'namespace ' + detail + r' \{.*?\n\}', declarations, header, count=1, flags=re.DOTALL)
    start = header.index('        std::vector<int> rows, columns;')
    end = header.index('\n    }', start)
    header = header[:start] + f'        {detail}::eval_jacobian(state, rho, jac, denuc_dX);' + header[end:]
    for function in ('eval_rhs', 'eval_jacobian', 'eval_temperature_derivative'):
        header = header.replace('    static void ' + function, '    ' + annotation + ' static void ' + function)
    marker = '''    static double aion(int index) { return AION[index]; }
    static double energy_weight(int index) { return ENERGY_WEIGHTS[index]; }
'''
    if header.count(marker) != 1:
        raise ValueError('generated scalar network interface is not recognized')
    header = header.replace(marker, f'''    {annotation} static double aion(int index);
    {annotation} static double energy_weight(int index);
    template<class Sink> static void enumerate_jacobian_structure(Sink& sink);
''', 1)
    math_name = cls + '.math.h'
    header += f'\n#include "{math_name}"\n'

    # The numerical source is moved, not copied: the .cpp only includes the
    # same public header used by device translation units.
    source = source.replace(f'#include "{header_name}"', '#pragma once', 1)
    source = source.replace(f'namespace {generated} {{',
        '#pragma push_macro("SCREENING")\n#undef SCREENING\n' + f'namespace {generated} {{', 1)
    source = source.replace(f'namespace {detail} {{',
        '#pragma pop_macro("SCREENING")\n' + f'namespace {detail} {{', 1)
    source = source.replace('static ' + generated + '::burn_t make_state',
                            annotation + ' inline ' + generated + '::burn_t make_state')
    source = source.replace('\nvoid eval_rhs(', '\n' + heavy + ' void eval_rhs(')
    source = source.replace('\nvoid eval_temperature_derivative(',
                            '\n' + heavy + ' void eval_temperature_derivative(')
    start = source.index('struct JacobianSink {')
    end = source.index('struct CompleteRhs {', start)
    source = source[:start] + f'''template<class Matrix>
struct JacobianSink {{
    Matrix& matrix;
    double* energy;
    {annotation} void zero() {{
        if (energy) for (int index = 0; index < Network::NUM_SPECIES; ++index) energy[index] = 0.0;
    }}
    {heavy} void set(int row, int column, double value) {{
        const double mass_value = value * Network::aion(row - 1) / Network::aion(column - 1);
        matrix.set(row, column, mass_value);
        if (energy) energy[column - 1] += Network::ENERGY_CONVERSION * mass_value
            / Network::aion(row - 1) * Network::energy_weight(row - 1);
    }}
}};
template<class Matrix>
{heavy} void eval_jacobian(const double* state, double rho,
                                          Matrix& matrix, double* energy) {{
    auto burn = make_state(state, rho);
    JacobianSink<Matrix> sink{{matrix, energy}};
    sink.zero();
    {generated}::actual_jac(burn, sink);
}}
''' + source[end:]
    source = re.sub(r'Network::AION\[([^]]+)\]', r'Network::aion(\1)', source)
    # Metadata has one generated origin. The device reads the same immutable
    # lookup used by the reaction code; no startup upload or managed global.
    aion_storage = 'aion_values()' if device_callable else 'aion'
    source += f'''\n{annotation} inline double {cls}::aion(int index) {{
    return {generated}::{aion_storage}[index];
}}
{annotation} inline double {cls}::energy_weight(int index) {{
    return {generated}::network::energy_mion(index + 1);
}}
template<class Sink>
inline void {cls}::enumerate_jacobian_structure(Sink& sink) {{
'''
    for row, column in jacobian_structure(stage / 'generated'):
        source += f'    sink.set({generated}::Species::{row}, {generated}::Species::{column}, 0.0);\n'
    source += '}\n'
    # Upstream headers use standard-library headers inside their generated
    # namespace. Preinclude them at global scope before entering that namespace.
    source = source.replace('#include <algorithm>', '#include <array>\n#include <algorithm>', 1)
    if not device_callable:
        # A shared mathematical header is not a device-storage capability.
        # The existing complete-RHS functor must remain Host-only as well.
        source = source.replace('ARCH_HOST_DEVICE', 'ARCH_HOST')
    header_path.write_text(header, encoding='utf-8')
    (stage / math_name).write_text(source, encoding='utf-8')
    source_path.write_text(f'#include "{header_name}"\n', encoding='utf-8')
    return device_callable
