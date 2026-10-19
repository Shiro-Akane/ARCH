"""Identity only for the two required CI assets; never fetch data here.

The composite action owns a narrow authenticated checkout on cache miss. This
helper reads the tested commit's canonical pointer and verifies local bytes on
both cache paths. Workstation use remains offline and does not change filters.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
ASSETS = {
    # Preserve the established table cache key; no new 60 MB cache miss just
    # because the same verification now serves Studio's small fixture too.
    "helmholtz": ("EOS_toolkit/tables/helmholtz/helm_table.dat", "arch-helmholtz-lfs-v1-"),
    "studio-sod": ("studio/tests/fixtures/sod-1d.h5", "arch-studio-sod-lfs-v1-"),
}


def parse_pointer(pointer):
    identity = re.fullmatch(
        rb"version https://git-lfs.github.com/spec/v1\noid sha256:([0-9a-f]{64})\nsize ([1-9][0-9]*)\n",
        pointer,
    )
    if identity is None:
        raise ValueError("Required asset has no canonical LFS pointer")
    return {"sha256": identity[1].decode("ascii"), "size": int(identity[2]),
            "pointer_sha256": hashlib.sha256(pointer).hexdigest()}


def identify(asset, root=ROOT):
    if asset not in ASSETS:
        raise ValueError("Unknown required CI asset")
    path, prefix = ASSETS[asset]
    pointer = subprocess.check_output(["git", "show", "HEAD:" + path], cwd=root)
    result = parse_pointer(pointer)
    return {"asset": asset, "path": path, **result,
            "cache_key": prefix + result["pointer_sha256"]}


def verify(record, root=ROOT):
    path = root / record["path"]
    if path.is_symlink() or not path.is_file():
        raise ValueError("Required materialized asset is not a regular file: " + record["path"])
    if path.stat().st_size != record["size"]:
        raise ValueError("Asset is still a pointer or has the wrong size: " + record["path"])
    digest = hashlib.sha256()
    # Stream large EOS tables; this fixed 1 MiB buffer is an IO work choice,
    # independent of table shape, units and scientific accuracy.
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != record["sha256"]:
        raise ValueError("Materialized asset SHA-256 mismatch: " + record["path"])
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("pointer", "verify"))
    parser.add_argument("--asset", choices=ASSETS, required=True)
    args = parser.parse_args(argv)
    record = identify(args.asset)
    if args.mode == "verify":
        verify(record)
    elif os.environ.get("GITHUB_OUTPUT"):
        with Path(os.environ["GITHUB_OUTPUT"]).open("a") as output:
            output.write("path=" + record["path"] + "\ncache_key=" + record["cache_key"] + "\n")
    print(json.dumps(record, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
