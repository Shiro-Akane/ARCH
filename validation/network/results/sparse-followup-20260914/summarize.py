"""Summarize preserved focused successes and timeout without promoting timings."""
import json
from pathlib import Path


def summarize(directory):
    result = []
    for phase, expected in [('be-original-v2', 2), ('long-bd-ros-v1', 8),
                            ('long-be-small-v1', 2), ('long-duration-v2', 0)]:
        report = json.loads((directory / 'records' / (phase + '.json')).read_text())
        if phase == 'long-duration-v2':
            assert report['status']=='failed' and 'TimeoutExpired' in report['error']
            assert not report['runs']
            result.append(dict(phase=phase, status='failed-timeout', error=report['error']))
            continue
        assert report['status']=='passed' and len(report['runs']) == expected
        assert report['provider_sha256']=='24bb9be952ef34c68cd4ea65de498f0cd0de8bb70dc5419d6ef9d33ecf6b4fcd'
        for run in report['runs']:
            assert run['passed']
            metrics = [[float(v) for v in line.split(',')[1:]]
                       for line in run['metrics'] if line.startswith('metrics,')]
            assert len(metrics)==1 and len(metrics[0])==8
            row = metrics[0]
            assert row[3]<=2e-10 and row[4]<=2e-8 and row[5]>64*2.220446049250313e-16
            result.append(dict(phase=phase, status='passed', name=run['name'],
                steps=report['steps'], duration=report['duration'],
                field_scaled_error=row[3], limiter_relative_error=row[4],
                max_species_evolution=row[5], pool_cells=int(row[6]),
                workspace_bytes_per_lane=int(row[7])))
    return result


if __name__=='__main__':
    print(json.dumps(summarize(Path(__file__).resolve().parent), indent=2))
