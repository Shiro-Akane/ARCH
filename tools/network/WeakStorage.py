"""Lower recognized embedded SimpleCxx weak data to explicitly borrowed storage.

Reaction/interpolation expressions are retained. This file owns storage layout
and argument plumbing; WeakTables owns derivatives. Neither emits another RHS.
"""
import math
import re

from PortableCxx import portable_headers
from WeakTables import _replace_once

VIEW = '::arch::network::WeakTableStorageView'


def lower_table_storage(text):
    block = re.search(r'^namespace rate_tables\s*\{\n(.*?)^\}', text, re.M | re.S)
    if not block:
        raise ValueError('embedded weak-table storage namespace is not recognized')
    body = block.group(1)
    metas = list(re.finditer(
        r'inline\s+table_t\s+(\w+)_meta\s*\{\s*\.ntemp=(\d+),\s*\.nrhoy=(\d+),'
        r'\s*\.nvars=(\d+),\s*\.nheader=(\d+)\s*\};', body))
    count = re.search(r'const int num_tables\s*=\s*(\d+)\s*;', text)
    if not count or len(metas) != int(count.group(1)) or not metas:
        raise ValueError('embedded weak-table metadata count mismatch')
    values, accessors, names, consumed = [], [], [], []
    for meta in metas:
        name = meta.group(1)
        nt, nr, nv, nh = map(int, meta.groups()[1:])
        if min(nt, nr) < 2 or nv != 3:
            raise ValueError('embedded weak-table dimensions are not recognized')
        names.append(name + '_meta')
        consumed.append(meta.group())
        accessors.append(f'inline\ntable_t {name}_meta(const {VIEW}&) {{ return {{{nt}, {nr}, {nv}, {nh}}}; }}')
        shapes = {
            'rhoy': (f'Array1D<Real, 1, {nr}>', nr, f'::arch::network::WeakAxisView{{pointer, {nr}}}'),
            'temp': (f'Array1D<Real, 1, {nt}>', nt, f'::arch::network::WeakAxisView{{pointer, {nt}}}'),
            'data': (f'Array3D<Real, 1, {nt}, 1, {nr}, 1, num_vars>', nt * nr * nv,
                     f'::arch::network::WeakValuesView{{pointer, {nt}, {nr}, {nv}}}')}
        for suffix, (shape, length, result) in shapes.items():
            symbol = name + '_' + suffix
            expression = r'inline\s+' + re.escape(shape).replace(r'\ ', r'\s*')
            matches = list(re.finditer(expression + r'\s+' + symbol + r'\s*\{([^{}]*)\};', body))
            if len(matches) != 1:
                raise ValueError('embedded weak array shape is not recognized: ' + symbol)
            array = matches[0]
            entries = [v.strip() for v in re.sub(r'//[^\n]*', '', array.group(1)).split(',') if v.strip()]
            if len(entries) != length or not all(math.isfinite(float(v.removesuffix('_rt'))) for v in entries):
                raise ValueError('embedded weak array extent or finite-value contract failed: ' + symbol)
            names.append(symbol)
            consumed.append(array.group())
            accessors.append(f'inline\nauto {symbol}(const {VIEW}& tables) {{\n'
                f'    const auto* pointer = tables.slice({len(values)}, {length});\n'
                f'    return {result};\n}}')
            values.extend(entries)
    remainder = body
    for fragment in consumed:
        remainder = _replace_once(remainder, fragment, '')
    if re.sub(r'//[^\n]*|/\*.*?\*/', '', remainder, flags=re.S).strip():
        raise ValueError('unrecognized declarations in embedded weak storage')
    # Literal tokens are moved, not rounded/recomputed. Keep this Host-owned
    # data outside the portable small-constant lookup transformation.
    rows = [', '.join(values[i:i + 8]) for i in range(0, len(values), 8)]
    storage = ('namespace rate_tables {\n// ARCH_EXPLICIT_WEAK_STORAGE\n'
        'inline constexpr double weak_table_values[]{\n    '
        + ',\n    '.join(rows) + '\n};\n' + '\n'.join(accessors) + '\n}')
    return text[:block.start()] + storage + text[block.end():], names, len(values)


def promote_weak_storage(stage, network_id):
    """Bind the already headerized common weak math; do not enable a registry.

    The caller advertises device capability only after its backend owner exists.
    Unsupported generator layouts fail before the staged package is published.
    """
    cls = 'NetCustom_' + network_id
    generated = 'arch_pynucastro_' + network_id
    detail = 'arch_custom_' + network_id + '_detail'
    directory = stage / 'generated'
    table_path = directory / 'table_rates.H'
    rhs_path = directory / 'actual_rhs.H'
    header_path = stage / (cls + '.h')
    math_path = stage / (cls + '.math.h')
    table, names, size = lower_table_storage(table_path.read_text())
    rhs, header, body = (p.read_text() for p in (rhs_path, header_path, math_path))
    for symbol in names:
        rhs = re.sub(r'\b' + symbol + r'\b', symbol + '(tables)', rhs)
        body = re.sub(r'\b' + symbol + r'\b', symbol + '(tables)', body)
    for function in ('evaluate_rates', 'actual_rhs', 'actual_jac'):
        rhs, count = re.subn(r'(\bvoid\s+' + function + r'\s*\([^{}]*?)(\)\s*\{)',
            r'\1, const ' + VIEW + r'& tables\2', rhs)
        if count != 1:
            raise ValueError('weak argument binding failed: ' + function)
    rhs, count = re.subn(r'(evaluate_rates<[^;]+>\(state, rate_eval)\);', r'\1, tables);', rhs)
    if count != 2:
        raise ValueError('weak rate consumers are not recognized')
    header = _replace_once(header, '#include <array>',
        '#include <array>\n#include "physics/network/WeakTableView.h"')
    header = _replace_once(header, f'struct {cls} {{', f'''struct {cls} {{
    {VIEW} tables{{}};
    static constexpr std::size_t TABLE_VALUE_COUNT = {size};
    static {VIEW} host_table_storage();
    static {cls} host_view() {{ return {{host_table_storage()}}; }}
    ARCH_HOST_DEVICE bool valid() const {{
        return tables.data != nullptr && tables.size == TABLE_VALUE_COUNT;
    }}''')
    # Public methods become const view methods. Nuclear metadata stays static.
    for function in ('eval_rhs', 'eval_jacobian', 'eval_temperature_derivative',
                     'eval_nonconservative_energy', 'eval_nonconservative_gradient'):
        # This scalar accessor owns discarded full-RHS outputs. Keep their
        # lifetime inside the same heavy-call boundary used by network leaves;
        # CUDA whole-kernel expansion otherwise coalesces a temporary output
        # with a caller's still-live input in the optimized trajectory witness.
        annotation = ('ARCH_HEAVY_INLINE' if function == 'eval_nonconservative_energy'
                      else 'ARCH_HOST_DEVICE')
        header, count = re.subn(r'ARCH_HOST static (void|double) ' + function + r'(\([^{}]*?\)) \{',
            annotation + r' \1 ' + function + r'\2 const {', header)
        if count != 1:
            raise ValueError('weak view entry is not recognized: ' + function)
    replacements = {
        'inline void eval_rhs(const double*, double, double*, double&, double* = nullptr);':
            f'ARCH_HEAVY_INLINE void eval_rhs(const double*, double, double*, double&, double*, const {VIEW}&);',
        'inline void eval_jacobian(const double*, double, Matrix&, double*);':
            f'ARCH_HEAVY_INLINE void eval_jacobian(const double*, double, Matrix&, double*, const {VIEW}&);',
        'inline void eval_temperature_derivative(const double*, double, double*, double&);':
            f'ARCH_HEAVY_INLINE void eval_temperature_derivative(const double*, double, double*, double&, const {VIEW}&);',
        'inline void eval_nonconservative_gradient(const double*, double, double*);':
            f'ARCH_HEAVY_INLINE void eval_nonconservative_gradient(const double*, double, double*, const {VIEW}&);'}
    for old, new in replacements.items():
        header = _replace_once(header, old, new)
    for args in ('state, rho, rhs, enuc, nonconservative', 'state, rho, rhs, total, &source',
                 'state, rho, gradient', 'state, rho, jac, denuc_dX', 'state, rho, drhs_dT, denuc_dT'):
        header = _replace_once(header, '(' + args + ');', '(' + args + ', tables);')
    for function in ('eval_rhs', 'eval_jacobian', 'eval_temperature_derivative',
                     'eval_nonconservative_gradient', 'visit_weak_derivatives'):
        body, count = re.subn(r'(inline void ' + function + r'\([^{}]*?)(\) \{)',
            r'\1, const ' + VIEW + r'& tables\2', body)
        if count != 1:
            raise ValueError('weak common body binding failed: ' + function)
    for old, new in (
        ('::actual_rhs(burn, dydt, enu_weak);', '::actual_rhs(burn, dydt, enu_weak, tables);'),
        ('::actual_jac(burn, sink);', '::actual_jac(burn, sink, tables);'),
        ('visit_weak_derivatives(state, rho, sink);', 'visit_weak_derivatives(state, rho, sink, tables);'),
        ('visit_weak_derivatives(state, rho, weak);', 'visit_weak_derivatives(state, rho, weak, tables);'),
        ('struct CompleteRhs {', f'struct CompleteRhs {{\n    {VIEW} tables;'),
        ('eval_rhs(state, rho, rhs, energy);', 'eval_rhs(state, rho, rhs, energy, nullptr, tables);'),
        ('CompleteRhs{}', 'CompleteRhs{tables}'),
        (generated + '::aion[index]', generated + '::aion_values()[index]')):
        body = _replace_once(body, old, new)
    # Only numerical entries are promoted. Registration, Host ownership and
    # symbolic pattern enumeration never acquire device responsibilities.
    header = re.sub(r'\bARCH_HOST\b', 'ARCH_HOST_DEVICE', header)
    body = re.sub(r'\bARCH_HOST\b', 'ARCH_HOST_DEVICE', body)
    body = re.sub(r'(?m)^(\s*)inline void (?!' + cls + '::)', r'\1ARCH_HEAVY_INLINE void ', body)
    body = re.sub(r'(?m)^(\s*)void (species_jacobian|loss_gradient)\(',
                  r'\1ARCH_HOST_DEVICE void \2(', body)
    for scalar in ('aion_inv', 'zion'):
        body = re.sub(r'\b' + generated + '::' + scalar + r'\[',
                      generated + '::' + scalar + '_values()[', body)
    body += f'''\ninline {VIEW} {cls}::host_table_storage() {{
    return {{{generated}::rate_tables::weak_table_values, TABLE_VALUE_COUNT}};
}}
'''
    table_path.write_text(table)
    rhs_path.write_text(rhs)
    header_path.write_text(header)
    math_path.write_text(body)
    if not portable_headers(directory, network_id, prepared=True):
        raise ValueError('explicit weak storage was not accepted by the portable header adapter')
    return size
