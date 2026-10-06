#!/usr/bin/env python3
"""Static Linux ELF dependency evidence; no inspected executable is run.

Cache-selected paths are candidates, not proof of actual loader selection.
RPATH/RUNPATH, dlopen, hwcaps, preload and environment selection are not modeled.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import stat
import subprocess
import tempfile

MAX_FILE = 128 * 1024 * 1024
MAX_OUTPUT = 1024 * 1024
MAX_NODES = 256
READELF = "/usr/bin/readelf"
LDCONFIG = "/usr/sbin/ldconfig"

def fingerprint(filename):
    path = Path(filename)
    if not path.is_absolute():
        raise ValueError("Require an absolute local path")
    resolved = path.resolve(strict=True)
    with os.fdopen(os.open(resolved, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK), "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > MAX_FILE:
            raise ValueError("Nonregular or oversized input")
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
        after = os.fstat(stream.fileno())
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
    if identity(before) != identity(after) or identity(before) != identity(resolved.stat()) or path.resolve(strict=True) != resolved:
        raise ValueError("Input changed during fingerprint")
    return dict(path=str(path), resolvedPath=str(resolved), sha256=digest.hexdigest(), size=before.st_size)

def capture(program, args):
    """Only fixed read-only inspectors run, with bounded file output."""
    if program not in (READELF, LDCONFIG):
        raise ValueError("Inspector is not fixed")
    def limit():
        resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_OUTPUT, MAX_OUTPUT))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    with tempfile.TemporaryFile() as output:
        completed = subprocess.run([program, *args], stdin=subprocess.DEVNULL, stdout=output,
            stderr=subprocess.STDOUT, env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
            timeout=10, preexec_fn=limit, check=False)
        output.seek(0)
        raw = output.read(MAX_OUTPUT + 1)
    if completed.returncode or len(raw) > MAX_OUTPUT:
        raise ValueError("Read-only inspector failed or exceeded output budget")
    return raw.decode("utf-8", errors="strict")

def parse_elf(text):
    if not re.search(r"Class:\s+ELF64\b", text) or not re.search(r"Machine:\s+Advanced Micro Devices X86-64\b", text):
        raise ValueError("Only current Linux x86-64 ELF is covered")
    needed = re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]*)\]", text)
    if len(needed) > 128 or len(set(needed)) != len(needed) or any(not n or "/" in n or "\0" in n for n in needed):
        raise ValueError("Invalid or excessive DT_NEEDED declarations")
    interp = re.findall(r"\[Requesting program interpreter: ([^\]]+)\]", text)
    if len(interp) > 1 or any(not Path(n).is_absolute() for n in interp):
        raise ValueError("Invalid ELF interpreter")
    paths = re.findall(r"\((?:RPATH|RUNPATH)\)\s+.*?\[([^\]]*)\]", text)
    return dict(needed=needed, interpreter=interp[0] if interp else None, searchPaths=paths)

def parse_cache(text):
    result = {}
    for line in text.splitlines():
        match = re.fullmatch(r"\s*(\S+) \(([^)]*)\) => (/.+?)\s*", line)
        if match and "x86-64" in match[2].split(","):
            result.setdefault(match[1], set()).add(match[3])
    return {k: sorted(v) for k, v in result.items()}

def audit(tools):
    if not tools or len(tools) > 32:
        raise ValueError("Require 1..32 tool roots")
    inspectors = [fingerprint(READELF), fingerprint(LDCONFIG)]
    cache_before = fingerprint("/etc/ld.so.cache")
    cache = parse_cache(capture(LDCONFIG, ["-p"]))
    nodes, edges, unresolved = {}, [], []
    pending = list(tools)
    while pending:
        candidate = pending.pop(0)
        before = fingerprint(candidate)
        key = before["resolvedPath"]
        if key in nodes:
            continue
        if len(nodes) >= MAX_NODES:
            raise ValueError("ELF dependency graph exceeds node budget")
        details = parse_elf(capture(READELF, ["-h", "-l", "-d", "--", key]))
        if fingerprint(candidate) != before:
            raise ValueError("ELF changed during inspection")
        nodes[key] = {**before, **details}
        if details["interpreter"]:
            edges.append(dict(parent=key, kind="interpreter", requested=details["interpreter"],
                              candidate=details["interpreter"]))
            pending.append(details["interpreter"])
        for name in details["needed"]:
            choices = cache.get(name, [])
            if details["searchPaths"] or len(choices) != 1:
                unresolved.append(dict(parent=key, requested=name,
                    reason="unmodeled-rpath" if details["searchPaths"] else "missing-or-ambiguous-cache-candidate",
                    candidates=choices))
            else:
                edges.append(dict(parent=key, kind="DT_NEEDED", requested=name, candidate=choices[0],
                                  resolution="ldconfig-default-candidate-not-loaded-proof"))
                pending.append(choices[0])
    for node in nodes.values():
        if fingerprint(node["path"]) != {k: node[k] for k in ("path", "resolvedPath", "sha256", "size")}:
            raise ValueError("Graph input changed before completion")
    if fingerprint("/etc/ld.so.cache") != cache_before or [fingerprint(READELF), fingerprint(LDCONFIG)] != inspectors:
        raise ValueError("Cache or inspector changed during audit")
    return dict(kind="static-build-tool-elf-dependencies", version=1, status="partial",
        roots=list(tools), inspectors=inspectors, loaderCache=cache_before,
        nodes=list(nodes.values()), edges=edges, unresolved=unresolved,
        inspectedProgramExecuted=False, dependenciesComplete=False,
        missingCoverage=["actual-loader-selection", "dlopen-and-plugins", "loader-environment-preload-hwcaps",
                         "non-ELF-tool-data", "across-build-stability", "build-manifest-integration",
                         "complete-dependency-closure"])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", action="append", required=True)
    args = parser.parse_args()
    try:
        result = audit(args.tool)
        print(json.dumps(result, indent=2, allow_nan=False))
        return 0
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(json.dumps(dict(status="invalid", error=str(error))))
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
