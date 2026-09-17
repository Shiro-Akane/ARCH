"""Add last-ARCH-launch labels to a frozen diagnostic's Host copy timings.

Generated diagnostic only. No extra CUDA calls, synchronization, shape changes,
physics or status transformations. Timings remain Host API waits, not kernel time.
"""
import argparse
import hashlib
import json
from pathlib import Path

ORIGINAL_SHA = 'd23b05a86535567f89508dc2d17ff3f57dcaec65a5743eeef80222a19386a5a8'


def transform(original):
    if hashlib.sha256(original).hexdigest() != ORIGINAL_SHA:
        raise ValueError('unreviewed diagnostic base')
    newline = b'\r\n' if original.count(b'\r\n') == original.count(b'\n') else b'\n'
    call = b'    const auto start=Clock::now(); const auto result=native(destination,source,bytes,kind,stream);'
    labeled = newline.join((b'    std::string preceding;',
        b'    { auto& r=registry(); std::lock_guard<std::mutex> lock(r.mutex); preceding=r.last_kernel[stream]; }', call))
    key = b'add("memcpy_async:"+std::to_string(static_cast<int>(kind)),start,end,bytes);'
    labeled_key = b'add("memcpy_async:"+std::to_string(static_cast<int>(kind))+":after:"+preceding,start,end,bytes);'
    changed = original
    for before, after in ((call, labeled), (key, labeled_key)):
        if changed.count(before) != 1:
            raise ValueError('ambiguous diagnostic insertion')
        changed = changed.replace(before, after, 1)
    if changed.replace(labeled_key, key, 1).replace(labeled, call, 1) != original:
        raise ValueError('change outside two reversible Host-label regions')
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    changed = transform(args.original.read_bytes())
    args.output.mkdir(parents=True, exist_ok=False)
    with (args.output / 'cuda_progress_after_launch.cpp').open('xb') as stream:
        stream.write(changed)
    with (args.output / 'overlay-record.json').open('x') as stream:
        json.dump(dict(status='prepared_not_compiled_not_run', original_sha256=ORIGINAL_SHA,
                       sha256=hashlib.sha256(changed).hexdigest(),
                       changed_regions=2, added_cuda_calls=False, release_qualified=False,
                       scope='Host API waits after last ARCH launch, not GPU execution time'), stream, indent=2)
        stream.write('\n')
    print(hashlib.sha256(changed).hexdigest())


if __name__ == '__main__':
    main()
