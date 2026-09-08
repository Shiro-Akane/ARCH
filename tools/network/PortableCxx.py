"""Storage/annotation adaptation for generated SimpleCxx numerical headers.

No reaction formula is emitted here. The upstream numerical body is compiled
by both Host and CUDA. Immutable arrays become allocation-free lookup views so
device code never dereferences a Host global or requires a second physics copy.
"""

import re


def split_jacobian_rows(text):
    """Move each generated matrix row into a bounded shared call, verbatim.

    A row is a mathematical responsibility, not an arbitrary line/byte chunk.
    No extra files or backend-specific formula copies are produced. Refuse an
    unfamiliar upstream body rather than dropping statements or reordering rows.
    """
    signature = re.search(
        r'template\s*<\s*class\s+MatrixType\s*>\s*'
        r'(?:ARCH_HOST_DEVICE\s+inline|ARCH_HEAVY_INLINE)\s*'
        r'void\s+jac_nuc\s*\((?P<args>[^{}]+?)\)\s*\{', text)
    if signature is None:
        if re.search(r'\bvoid\s+jac_nuc\s*\(', text):
            raise ValueError('generated jac_nuc signature is not recognized')
        return text
    end = text.find('}', signature.end())
    if end < 0:
        raise ValueError('generated jac_nuc has no closing brace')
    body = text[signature.end():end]
    entry = re.compile(r'scratch\s*=\s*[^;{}]+;\s*'
                       r'jac\.set\(\s*(?P<row>\w+)\s*,\s*\w+\s*,\s*scratch\s*\)\s*;')
    matches = list(entry.finditer(body))
    remainder = entry.sub('', body)
    remainder, declarations = re.subn(r'\bReal\s+scratch\s*;', '', remainder)
    remainder = re.sub(r'//[^\n]*|/\*.*?\*/', '', remainder, flags=re.DOTALL)
    if not matches or declarations != 1 or remainder.strip():
        raise ValueError('generated jac_nuc row body is not recognized')
    rows, seen = [], set()
    for match in matches:
        row = match.group('row')
        if not rows or rows[-1][0] != row:
            if row in seen:
                raise ValueError('generated jac_nuc rows are not contiguous')
            seen.add(row)
            rows.append((row, []))
        rows[-1][1].append(match.group())
    arguments = signature.group('args')
    expected = ('const burn_t& state, MatrixType& jac, '
                'const Array1D<Real, 1, NumSpec>& Y, '
                'const Array1D<Real, 1, NumRates>& screened_rates')
    if re.sub(r'\s+', '', arguments) != re.sub(r'\s+', '', expected):
        raise ValueError('generated jac_nuc arguments are not recognized')
    functions, calls = [], []
    for row, entries in rows:
        name = f'jac_nuc_row_{row}'
        functions.append('template<class MatrixType>\nARCH_HEAVY_INLINE\n'
                         f'void {name}({arguments}) {{\n    Real scratch;\n    '
                         + '\n    '.join(entries) + '\n}\n')
        calls.append(f'    {name}(state, jac, Y, screened_rates);')
    wrapper = ('template<class MatrixType>\nARCH_HEAVY_INLINE\n'
               f'void jac_nuc({arguments}) {{\n' + '\n'.join(calls) + '\n}')
    return text[:signature.start()] + '\n'.join(functions) + wrapper + text[end + 1:]


def bounded_math_calls(text):
    """Preserve expressions; keep major generated stages out of every caller.

    These are mathematical responsibilities, independent of network size/ID.
    Reaction evaluators also need a call boundary: a large rate catalogue must
    not be inlined wholesale into each aggregate evaluation. Expressions and
    constants are untouched. The annotation is
    owned by ARCH portability, so ordinary CPU compilation retains its policy.
    """
    stages = ("actual_rhs", "actual_jac", "rhs_nuc", "jac_nuc",
              "evaluate_rates", "evaluate_screening", "fill_reaclib_rates",
              "fill_derived_rates", "fill_approx_rates", "actual_log_screen",
              "actual_screen", "fill_plasma_state", r"rate_\w+")
    text = re.sub(r'ARCH_HOST_DEVICE inline(?=\s+void\s+(?:'
                  + '|'.join(stages) + r')\s*\()', 'ARCH_HEAVY_INLINE', text)
    # This value-returning helper also contains transcendental expressions.
    # Leaving it inline expands pow into every charge-pair setup even after
    # the screening law itself has a call boundary. Preserve its body exactly.
    return re.sub(r'ARCH_HOST_DEVICE inline(?=\s+screen_factors_t\s+'
                  r'calculate_screen_factor\s*\()', 'ARCH_HEAVY_INLINE', text)


def balanced_constexpr_for(text):
    """Lower the recognized SimpleCxx bridge loop without changing call order.

    The original template recursively instantiates every remaining index. A
    large network therefore exhausts NVCC's template depth before reaching its
    mathematical body. Ordered half-open subranges have logarithmic depth;
    only the singleton leaf calls the original integral_constant callback.
    """
    signature = (
        r'template\s*<\s*auto\s+I\s*,\s*auto\s+N\s*,\s*class\s+F\s*>\s*'
        r'(?:ARCH_HOST_DEVICE\s+)?inline\s+constexpr\s+void\s+'
        r'constexpr_for\s*\(\s*F\s+const\s*&\s*f\s*\)\s*\{')
    original = re.compile(
        '(' + signature + r')\s*'
        r'if\s+constexpr\s*\(\s*I\s*<\s*N\s*\)\s*\{\s*'
        r'f\s*\(\s*std::integral_constant\s*<\s*decltype\(I\)\s*,\s*I\s*>\s*\(\s*\)\s*\)\s*;\s*'
        r'constexpr_for\s*<\s*I\s*\+\s*1\s*,\s*N\s*>\s*\(\s*f\s*\)\s*;\s*'
        r'\}\s*\}')
    matches = list(original.finditer(text))
    if len(matches) != 1 or len(re.findall(signature, text)) != 1:
        raise ValueError('generated constexpr_for bridge layout is not recognized; refusing changed loop semantics')
    match = matches[0]
    body = '''
        if constexpr (I < N) {
            if constexpr (I + 1 < N) {
                // Ordered subdivision bounds template depth without changing
                // index order or the original I+1 integral promotion.
                constexpr decltype(I + 1) middle = I + (N - I) / 2;
                constexpr_for<I, middle>(f);
                constexpr_for<middle, N>(f);
            } else {
                f(std::integral_constant<decltype(I), I>());
            }
        }
    }'''
    return text[:match.start()] + match.group(1) + body + text[match.end():]


def _lookup(name, low, entries):
    cases = '\n'.join(f'        case {index}: return {value};'
                      for index, value in enumerate(entries))
    return f'''struct {name}_lookup {{
    ARCH_HOST_DEVICE static constexpr int size() {{ return {len(entries)}; }}
    ARCH_HOST_DEVICE static constexpr int lo() {{ return {low}; }}
    ARCH_HOST_DEVICE static constexpr int hi() {{ return lo() + size() - 1; }}
    // Small immutable lookups stay inlineable on both backends.
    ARCH_HOST_DEVICE constexpr Real operator()(int index) const {{
        switch (index - lo()) {{
{cases}
        default: assert(false); return 0.0_rt;
        }}
    }}
    ARCH_HOST_DEVICE constexpr Real operator[](int index) const {{ return (*this)(index); }}
}};
ARCH_HOST_DEVICE inline constexpr {name}_lookup {name}_values() {{ return {{}}; }}'''


def reuse_value_rate_storage(text):
    """Avoid a second rate-stage instantiation for a value-only species Jacobian.

    Only the recognized three-use local buffer is narrowed. Rate expressions,
    call arguments/order and derivative-enabled consumers are untouched. An
    unfamiliar upstream consumer simply retains its original storage/type.
    """
    function = re.search(r'\bvoid\s+actual_jac\s*\([^{}]*\)\s*\{(?P<body>.*?)^}',
                         text, re.MULTILINE | re.DOTALL)
    if function is None:
        return text
    body = function.group('body')
    expected = re.compile(
        r'\brate_derivs_t\s+rate_eval\s*;\s*'
        r'constexpr\s+int\s+do_T_derivatives\s*=\s*0\s*;\s*'
        r'evaluate_rates\s*<\s*do_T_derivatives\s*,\s*rate_derivs_t\s*>'
        r'\s*\(\s*state\s*,\s*rate_eval\s*\)\s*;')
    # The only remaining access must be the value array handed to jac_nuc.
    if (len(re.findall(r'\brate_eval\b', body)) != 3
            or len(re.findall(r'\brate_derivs_t\b', body)) != 2
            or len(re.findall(r'\brate_eval\s*\.\s*screened_rates\b', body)) != 1
            or len(expected.findall(body)) != 1):
        return text
    changed = expected.sub(lambda match: match.group().replace('rate_derivs_t', 'rate_t'), body)
    return text[:function.start('body')] + changed + text[function.end('body'):]


def qualify_include_guards(text, network_id):
    # Namespaces do not scope macros. Every headerized custom package needs
    # its own guards, including Host-only weak packages in a CPU catalogue.
    for guard in re.findall(r'^#ifndef\s+(\w+)\s*$', text, re.MULTILINE):
        text = re.sub(r'\b' + re.escape(guard) + r'\b',
                      f'ARCH_GENERATED_{network_id.upper()}_{guard}', text)
    return text


def host_headers(generated_dir, network_id):
    texts = {p.name: p.read_text(encoding='utf-8') for p in generated_dir.glob('*.H')}
    if 'amrex_bridge.H' in texts:
        texts['amrex_bridge.H'] = balanced_constexpr_for(texts['amrex_bridge.H'])
    for name, text in texts.items():
        (generated_dir / name).write_text(qualify_include_guards(text, network_id), encoding='utf-8')


def portable_headers(generated_dir, network_id, *, prepared=False):
    """Adapt recognized immutable storage; reject unsupported table ownership.

    Nonzero weak-rate arrays require the explicit storage lowering and backend
    owner/view contract. Unlowered arrays are never silently rewritten as tiny
    lookup constants or marked device-capable here.
    """
    texts = {p.name: p.read_text(encoding='utf-8') for p in generated_dir.glob('*.H')}
    table_count = re.search(r'const int num_tables\s*=\s*(\d+)\s*;', texts['table_rates.H'])
    if table_count is None or (int(table_count.group(1))
            and '// ARCH_EXPLICIT_WEAK_STORAGE' not in texts['table_rates.H']):
        return False
    if not prepared and 'amrex_bridge.H' in texts:
        # Validate before writing any header: an upstream loop-shape change
        # must not leave a partly converted package advertised as portable.
        texts['amrex_bridge.H'] = balanced_constexpr_for(texts['amrex_bridge.H'])
    replacements = set()
    array = re.compile(
        r'(?P<prefix>\b(?:inline\s+)?(?:constexpr\s+)?)'
        r'(?:Array1D<Real,\s*(?P<low>[^,>]+),\s*[^>]+>\s+(?P<name>\w+)\s*'
        r'|Real\s+(?P<cname>\w+)\s*\[[^]]+\]\s*=\s*)'
        r'\{(?P<values>[^{}]*)\}\s*;', re.MULTILINE)
    def convert(match):
        name = match.group('name') or match.group('cname')
        values = re.sub(r'//[^\n]*', '', match.group('values'))
        entries = [value.strip() for value in values.split(',') if value.strip()]
        if not entries:
            raise ValueError(f'empty generated constant {name}')
        replacements.add(name)
        return _lookup(name, match.group('low') or '0', entries)
    for name, value in texts.items():
        texts[name] = array.sub(convert, value)
    required = {'aion', 'aion_inv', 'zion', 'mion'}
    if not required.issubset(replacements):
        raise ValueError('generated immutable storage layout is not recognized')
    for name, value in texts.items():
        for constant in replacements:
            value = re.sub(r'\b' + re.escape(constant) + r'\b', constant + '_values()', value)
        # Generated names are namespaced, but upstream include guards are not.
        # Qualify guards as well so several custom networks can coexist in a TU.
        if not prepared:
            value = qualify_include_guards(value, network_id)
        # Template functions use inline on its own line. The few one-line
        # function definitions are handled too; namespaces and data are not.
        value = re.sub(r'(?m)^(\s*)(\[\[nodiscard\]\]\s+)?inline\s*$',
                       lambda m: m.group(1) + (m.group(2) or '') + 'ARCH_HOST_DEVICE inline', value)
        value = re.sub(r'(?m)^(\s*)inline(?=\s+(?:void|Real)\s+\w+\s*\()',
                       r'\1ARCH_HOST_DEVICE inline', value)
        # Literal operators are constexpr but lacked an explicit CUDA contract.
        value = value.replace('    constexpr Real\n    operator',
                              '    ARCH_HOST_DEVICE constexpr Real\n    operator')
        # Runtime file IO remains Host-only even in otherwise portable packages.
        value = re.sub(r'ARCH_HOST_DEVICE inline(\s+void\s+(?:init_tabular|init_tab_info)\b)', r'inline\1', value)
        value = bounded_math_calls(value)
        if name == 'actual_rhs.H':
            value = reuse_value_rate_storage(value)
            value = split_jacobian_rows(value)
        (generated_dir / name).write_text(value, encoding='utf-8')
    return True


def jacobian_structure(generated_dir):
    """Extract declared writes, never numerical values at a sampled state."""
    text = (generated_dir / 'actual_rhs.H').read_text(encoding='utf-8')
    pairs = re.findall(r'\bjac\.set\(\s*(\w+)\s*,\s*(\w+)\s*,', text)
    calls = re.findall(r'\bjac\.set\s*\(', text)
    if len(pairs) != len(calls):
        raise ValueError('generated Jacobian contains an unrecognized structural write; refusing incomplete device structure')
    return list(dict.fromkeys(pairs))
