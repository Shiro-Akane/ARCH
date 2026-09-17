#!/usr/bin/env python3
"""Copy and verify the pinned HighFive dependency; do not remove its old path."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from mainline_retirement_inventory import BASE, HIGHFIVE, OUT, save

PIN = '0d0259e823a0e8aee2f036ba738c703ac4a0721c'
DEST = BASE / 'ARCH-dependencies' / ('highfive-' + PIN)


def manifest(root):
    result = {}
    for directory, dirs, files in os.walk(root, followlinks=False):
        for name in list(dirs) + files:
            p = Path(directory) / name
            key = str(p.relative_to(root))
            if p.is_symlink():
                result[key] = {'link': os.readlink(p)}
                if name in dirs:
                    dirs.remove(name)
            elif p.is_file():
                result[key] = {'bytes': p.stat().st_size,
                               'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
    return result


def main():
    assert HIGHFIVE.resolve() == HIGHFIVE and not DEST.exists()
    assert subprocess.check_output(['git', '-C', str(HIGHFIVE), 'rev-parse', 'HEAD'], text=True).strip() == PIN
    before = manifest(HIGHFIVE)
    DEST.parent.mkdir(exist_ok=True)
    shutil.copytree(HIGHFIVE, DEST, symlinks=True)
    assert manifest(HIGHFIVE) == before == manifest(DEST)
    command = ['/usr/bin/g++-11', '-std=c++17', '-fsyntax-only',
               '-I' + str(DEST / 'include'), '-I' + str(BASE / '.envs/arch/include'),
               '/tmp/highfive-migration-smoke.cpp']
    r = subprocess.run(command, text=True, capture_output=True)
    save(OUT / 'dependency-migration.json', {
        'old_path': str(HIGHFIVE), 'new_path': str(DEST), 'git_pin': PIN,
        'all_files_and_symlinks_equal': True, 'entries': len(before),
        'compile_command': command, 'compile_exit_code': r.returncode,
        'compile_stdout': r.stdout, 'compile_stderr': r.stderr,
        'old_path_removed': False, 'manifest': before})
    assert r.returncode == 0, r.stderr
    print(json.dumps({'destination': str(DEST), 'entries_verified': len(before), 'compile_exit_code': r.returncode}))


if __name__ == '__main__':
    main()
