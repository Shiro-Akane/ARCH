"""Check archived fixed-science records and reproduce a compact factual summary.

This checks the recorded gates, not an independent physics oracle. Timings in
these records overlap builds and must never be interpreted as speedup results.
"""
import argparse
import json
import math
from pathlib import Path


PHASES = (
    'contracts batch-contracts canonical independent first-law nse coupled '
    'coupled-all-transport amr curved lifecycle restart coupled-restart '
    'coupled-all-transport-restart tails'
).split()
MODULES = {f'coupled_{ode}_{diff}_all_transport_b{blocks}'
           for ode in ('be_nr', 'bd', 'ros4')
           for diff in ('rkl1', 'rkl2') for blocks in (8, 32, 128)}


def check(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def finite(value):
    check(isinstance(value, (int, float)) and not isinstance(value, bool)
          and math.isfinite(value), f'Invalid numeric evidence: {value!r}')
    return value


def summarize_scale(report, expected=MODULES):
    check(report['status'] == 'passed' and report['identity_verified'],
          'Scale matrix did not pass with verified identity')
    check(report['release_qualified'] is False and report['pilot'] is True,
          'Numerical-only evidence was mislabeled as formal timing')
    cases = report['cases']
    check(len(cases) == len(expected) and {c['id'] for c in cases} == expected,
          'Missing, duplicated or unexpected coupled case')
    lanes, comparisons = report['lanes'], report['comparisons']
    check(len(lanes) == 2 * len(expected) and len(comparisons) == len(expected),
          'Incomplete CPU/CUDA lane or comparison count')
    check({(lane['case'], lane['backend']) for lane in lanes}
          == {(case, backend) for case in expected for backend in ('cpu', 'cuda')},
          'Missing or duplicated CPU/CUDA lane')
    rows = []
    for lane in lanes:
        check(lane['status'] == 'passed' and lane['returncode'] == 0
              and not lane['timed_out'] and lane['threads'] == 8,
              f"Failed or unexpected lane: {lane['case']}/{lane['backend']}")
        balance, point = lane['source_balance'], lane['pointwise_quality']
        budget = finite(balance['budget'])
        check(budget == 1e-12 and point['species_sum_budget'] == 1e-12,
              'The original source/pointwise budgets changed')
        for key in ('energy_relative', 'mass_relative', 'charge_absolute'):
            check(0 <= finite(balance[key]) <= budget, f'{key} exceeds original budget')
        check(0 <= finite(point['species_sum_absolute']) <= 1e-12,
              'Pointwise composition closure failed')
        check(finite(lane['metadata']['time']) == 1e-10,
              'Lane did not reach the complete original terminal time')
        rows.append(dict(case=lane['case'], backend=lane['backend'],
                         steps=lane['metadata']['step'],
                         final_blocks=balance['after']['blocks'],
                         energy_relative=balance['energy_relative'],
                         mass_relative=balance['mass_relative'],
                         charge_absolute=balance['charge_absolute'],
                         species_sum_absolute=point['species_sum_absolute']))
    directories = {lane['directory']: lane for lane in lanes}
    covered = set()
    for comparison in comparisons:
        reference = directories[comparison['reference']]
        candidate = directories[comparison['candidate']]
        check(reference['case'] == candidate['case']
              and reference['backend'] == 'cpu' and candidate['backend'] == 'cuda',
              'Comparison did not pair the same case across backends')
        check(reference['case'] not in covered, 'Duplicated comparison')
        covered.add(reference['case'])
        fields = comparison['fields']
        check(comparison['workload_aligned'] and fields['passed']
              and fields['status'] == 'pass', 'Field or workload comparison failed')
        finite(fields['max_field_normalized'])
        finite(fields['max_enuc_normalized'])
    check(covered == expected, 'Comparison coverage incomplete')
    return dict(status='passed', formal_timing=False, cases=len(cases),
                lanes=len(lanes), comparisons=len(comparisons), rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--science-root', type=Path,
                        default=Path(__file__).resolve().parent / 'science')
    parser.add_argument('--critical-only', type=Path,
                        help='Check just the separately recorded critical case, not the full suite')
    args = parser.parse_args()
    if args.critical_only:
        result = summarize_scale(read(args.critical_only),
                                 {'coupled_bd_rkl2_all_transport_b128'})
    else:
        root = args.science_root
        phases = {name: read(root / 'validation' / f'{name}.json') for name in PHASES}
        check(all(r['status'] == 'passed' and r['identity_verified_after_run']
                  for r in phases.values()), 'An original phase did not pass')
        log = (root / 'build' / 'fix-contracts.log').read_text(encoding='utf-8')
        check('valid=21 invalid_no_scatter=24' in log
              and '100% tests passed' in log
              and not any(word in log for word in ('***Skipped', 'Not Run', 'tests did not run')),
              'Real 21-case CUDA contract evidence is absent or skipped')
        result = summarize_scale(read(root / 'details' / 'coupled-scale-v1' / 'evidence.json'))
        result['original_phases'] = {name: 'passed' for name in phases}
        result['critical'] = summarize_scale(
            read(root / 'details' / 'coupled-critical-b128-v1' / 'evidence.json'),
            {'coupled_bd_rkl2_all_transport_b128'})
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
