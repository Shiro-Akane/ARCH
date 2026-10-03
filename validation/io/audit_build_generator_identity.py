#!/usr/bin/env python3
"""Read-only Host build-generator identity audit; never executes cache programs."""
import argparse
import hashlib
import json
import os
import stat
from pathlib import Path

def fingerprint(filename):
    path = Path(filename)
    if not path.is_absolute():
        raise ValueError("Generator tool path must be absolute")
    resolved = path.resolve(strict=True)
    with resolved.open("rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not os.access(resolved, os.X_OK) or before.st_size > 128 * 1024 * 1024:
            raise ValueError("Generator tool unavailable or over budget")
        sha = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(chunk)
        after = os.fstat(stream.fileno())
    if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
        raise ValueError("Generator tool changed while reading")
    if path.resolve(strict=True) != resolved:
        raise ValueError("Generator symlink target changed while reading")
    current = resolved.stat()
    if (current.st_size, current.st_mtime_ns, current.st_ino) != (before.st_size, before.st_mtime_ns, before.st_ino):
        raise ValueError("Generator path changed while reading")
    return dict(path=str(path), resolvedPath=str(resolved), sha256=sha.hexdigest(), size=before.st_size)

def audit(source, build, host_cmake="/usr/bin/cmake"):
    source, build = Path(source).resolve(strict=True), Path(build).resolve(strict=True)
    cache = build / "CMakeCache.txt"
    if cache.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("CMake cache exceeds budget")
    with cache.open("rb") as stream:
        content = stream.read(2 * 1024 * 1024 + 1)
    if len(content) > 2 * 1024 * 1024:
        raise ValueError("CMake cache exceeds budget")
    values = {}
    for line in content.decode("utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        declaration, value = line.split("=", 1)
        key = declaration.split(":", 1)[0]
        if key in values:
            raise ValueError("Duplicate CMake cache key: " + key)
        values[key] = value
    for key, expected in [("CMAKE_HOME_DIRECTORY", str(source)), ("CMAKE_CACHEFILE_DIR", str(build))]:
        if values.get(key) != expected:
            raise ValueError("CMake source/build binding differs: " + key)
    if values.get("CMAKE_GENERATOR") != "Ninja":
        raise ValueError("Only the current Host Ninja profile is audited")
    cmake = fingerprint(host_cmake)
    cached_cmake = fingerprint(values.get("CMAKE_COMMAND", ""))
    if cmake != cached_cmake:
        raise ValueError("Cache CMake differs from the Host-owned CMake")
    ninja = fingerprint(values.get("CMAKE_MAKE_PROGRAM", ""))
    with cache.open("rb") as stream:
        current = stream.read(2 * 1024 * 1024 + 1)
    if current != content:
        raise ValueError("CMake cache changed while reading")
    return dict(kind="build-generator-identity-audit", version=1, sourceRoot=str(source),
                buildDirectory=str(build), cacheSha256=hashlib.sha256(content).hexdigest(),
                generator="Ninja", tools={"cmake": cmake, "ninja": ninja},
                observedExecution=False, dependenciesComplete=False,
                missingCoverage=["integration-with-build-manifest", "before-after-build-stability",
                                 "generator-runtime-dependencies", "complete-dependency-closure"])

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True)
    p.add_argument("--build", required=True)
    args = p.parse_args()
    try:
        print(json.dumps(audit(args.source, args.build), indent=2))
        return 0
    except (OSError, ValueError, UnicodeError) as error:
        print(json.dumps({"status": "invalid", "error": str(error)}))
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
