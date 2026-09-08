"""Check source-aware balances on the EOS campaign's saved physical endpoints.

This postprocessing reuses the existing checkpoint conservation and provenance
authorities. Nuclear binding data come from the independent NSE data reader;
no production EOS, reaction RHS or time integrator supplies expected energies.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'validation/network'))
import validate_backend_results as runtime
import validation_provenance as provenance
import nse_reference


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    runtime.require_empty_output_root(output)
    output.mkdir(parents=True)
    report_file, recipe = provenance.file_identity(args.report.resolve()), provenance.file_identity(Path(__file__))
    report = json.loads(args.report.read_text())
    identity_args = dict(arch=build / 'bin/ARCH', checkpoint_validator=build / 'arch_cuda_single_level_validation',
                         source_root=ROOT, build_dir=build)
    before = provenance.capture(**identity_args)
    provenance.require_evidence_identity(report, before)
    cases = {case['id']: case for case in report['derived_cases']}
    if len(cases) != 12 or len(report['cases']) != len(cases):
        raise RuntimeError('incomplete normalized-EOS application coverage')
    data = nse_reference.nuclear_data('aprox13')
    binding = np.asarray(data['arrays']['BION'], dtype=np.longdouble) / np.asarray(data['arrays']['AION'], dtype=np.longdouble)
    conversion = np.longdouble(data['energy_conversion'])
    started, records, observed = datetime.now(timezone.utc).isoformat(), [], {}
    for record in report['cases']:
        case = cases[record['id']]
        for backend in ('cpu', 'cuda'):
            lane = record['scientific'][backend]
            parameter, endpoint = Path(lane['parameter_file']), Path(lane['checkpoint'])
            initial, = parameter.parent.glob('*_chk_0000.h5')
            for path in (parameter, initial, endpoint):
                observed[str(path)] = provenance.file_identity(path)
            if observed[str(parameter)]['sha256'] != lane['parameter_sha256'] \
                    or observed[str(endpoint)]['sha256'] != lane['checkpoint_sha256']:
                raise RuntimeError('saved scientific endpoint or parameters changed')
            row = dict(case=case['id'], backend=backend)
            if case['problem'] == 'SmoothAdvection':
                row['conservation'] = runtime.validate_conservation(identity_args['checkpoint_validator'],
                    initial, endpoint, case['conservation_policy'], parameter_file=parameter,
                    parameter_sha256=lane['parameter_sha256'])
            else:
                with h5py.File(initial) as first, h5py.File(endpoint) as last:
                    rho0, rho1 = (file['Data/rho'][:].astype(np.longdouble) for file in (first, last))
                    if not np.array_equal(rho0, rho1) or np.any(rho0 <= 0):
                        raise ArithmeticError('one-zone density changed')
                    if any(np.any(file['Data/' + name][:] != 0.) for file in (first, last)
                           for name in ('mom_u', 'mom_v', 'mom_w')):
                        raise ArithmeticError('one-zone acquired kinetic energy')
                    x0, x1 = (file['Data/X'][:].astype(np.longdouble) for file in (first, last))
                    e0, e1 = (file['Data/eng'][:].astype(np.longdouble) / rho0 for file in (first, last))
                    released = np.sum((x1-x0) * binding[:, None, None], axis=0) * conversion
                    scale = np.maximum(np.maximum(np.abs(e0), np.abs(e1)), np.abs(released))
                    closure = float(np.max(np.abs(e1-e0-released) / scale))
                    if not np.isfinite(closure) or closure > 1e-12:
                        raise ArithmeticError('burn endpoint violates source-aware energy closure')
                    row['binding_energy_closure_relative'] = closure
            records.append(row)
    if any(value != provenance.file_identity(Path(path)) for path, value in observed.items()) \
            or report_file != provenance.file_identity(args.report.resolve()) \
            or recipe != provenance.file_identity(Path(__file__)):
        raise RuntimeError('postprocessing inputs or recipe changed')
    evidence = dict(schema=1, scope='same-physical-time normalized-EOS conservation and burn source balance',
        focused_gate_pass=True, release_qualified=False, application_report=report_file, recipe=recipe,
        command=[sys.executable, '-B', str(Path(__file__).resolve()), *sys.argv[1:]],
        data=data, inputs=observed, cases=records, energy_relative_budget=1e-12,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat())
    provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    print('EOS physical-endpoint balances PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    main()
