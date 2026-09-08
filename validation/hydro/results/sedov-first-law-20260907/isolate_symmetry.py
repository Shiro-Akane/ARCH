"""Diagnostic first-step isolation; not a release-acceptance replacement."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'validation/hydro'))
import sedov_reference as sedov
runtime, provenance = sedov.runtime, sedov.provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--terminal', action='store_true', help='compare all method controls at t=0.1')
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    runtime.require_empty_output_root(output)
    output.mkdir(parents=True)
    arch = build / 'bin/ARCH'
    identity = provenance.capture_focused(artifacts={'arch': arch}, source_root=ROOT, build_dir=build)
    records = []
    for reconstruction, resolved in (('pcm', 'pcm'), ('muscl', 'plm'), ('ppm', 'ppm')):
        for method in ('Euler', 'RK2', 'RK3'):
            case = dict(id=reconstruction + '_' + method.lower(), problem='Sedov',
                input='validation/hydro/inputs/sod_ppm_n64.par',
                overrides=dict(nblockx1='8', max_blocks='16', center_x='.5',
                    deposit_radius=str(1/128), ambient_density='1', ambient_pressure='1e-5',
                    explosion_energy='1', reconstruct=reconstruction, time_integrator=method),
                plan_policy=dict(eos='ideal', reconstruction=resolved, time=method.lower()))
            for steps in ((0,) if args.terminal else (1, 2, 4)):
                lane = (runtime.run_arch_terminal_lane(arch, ROOT, case, 'cpu', .1, output / 'runs')
                        if args.terminal else runtime.run_arch_lane(arch, ROOT, case, 'cpu', steps, output / 'runs'))
                state, time = sedov.load_state(lane['checkpoint'])
                mirror = state[::-1].copy()
                mirror[:, 1] *= -1
                error = np.max(np.abs(state - mirror), axis=0)
                record = dict(case=case['id'], steps=steps, time=time, symmetry_absolute=error.tolist(),
                              checkpoint=str(lane['checkpoint']))
                records.append(record)
                print(json.dumps(record), flush=True)
    provenance.require_unchanged(identity, provenance.capture_focused(
        artifacts={'arch': arch}, source_root=ROOT, build_dir=build))
    (output / 'diagnostic.json').write_text(json.dumps(dict(identity=identity, records=records,
        release_qualified=False), indent=2) + '\n')


if __name__ == '__main__':
    main()
