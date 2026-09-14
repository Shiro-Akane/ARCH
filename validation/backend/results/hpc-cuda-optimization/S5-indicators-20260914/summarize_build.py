"""Extract observed per-compiler-process metrics, not a 16 GiB Debug attestation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex


METRIC = re.compile(r'^ARCH_COMPILE_METRIC elapsed_seconds=([0-9.]+) '
                    r'peak_rss_kib=(\d+) exit_code=(\d+) command=(.*)$', re.MULTILINE)


def summarize(path):
    content = path.read_bytes()
    records = []
    for elapsed, rss, code, command in METRIC.findall(content.decode('utf-8')):
        args = shlex.split(command)
        inputs = [a for a in args if a.endswith(('.cu', '.cpp', '.c', '.cc', '.cxx'))]
        if len(inputs) != 1:
            raise RuntimeError('ambiguous compiler input: ' + command)
        records.append(dict(source=inputs[0], elapsed_seconds=float(elapsed),
            peak_rss_kib=int(rss), exit_code=int(code), command=command))
    if not records:
        raise RuntimeError('no actual completed compiler-process metrics')
    return dict(log=str(path.resolve()), log_sha256=hashlib.sha256(content).hexdigest(),
        scope='one compiler invocation and its children; overlapping invocations must not be summed as total peak',
        qualification='observations only; neither clean-build completion nor 16 GiB Debug qualification',
        completed_invocations=len(records), failed_invocations=sum(r['exit_code'] != 0 for r in records),
        records=records, largest_peak_rss=sorted(records, key=lambda r:r['peak_rss_kib'], reverse=True)[:10])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    report = summarize(args.log)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(json.dumps({key:report[key] for key in ('completed_invocations', 'failed_invocations')}))
    for row in report['largest_peak_rss'][:5]:
        print(row['peak_rss_kib'], row['elapsed_seconds'], row['source'])
