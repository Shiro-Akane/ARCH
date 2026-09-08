#!/usr/bin/env python3
"""Summarize compiler-command metrics or historical CUDA RSS samples.

GNU time command maxima and sampled process-group RSS sums are different
measurements; the output labels them explicitly. Neither is a build-wide peak
or proof of runtime capacity. Keep the original guarded build log as evidence.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import re
import shlex
import sys


SOURCE_RE = re.compile(
    r"(?:--orig_src_(?:file|path)_name\s+|-c\s+)[\"']?([^\"'\s]+\.cu)"
)
COMMAND_PREFIX = "ARCH_COMPILE_METRIC "
COMMAND_RE = re.compile(
    r"ARCH_COMPILE_METRIC elapsed_seconds=(\d+(?:\.\d+)?) "
    r"peak_rss_kib=(\d+) exit_code=(\d+) command=(.+)"
)


def parse_commands(lines: list[str]) -> list[dict[str, object]]:
    """Read one GNU time record per compile invocation, including failed ones.

    `%C` is not a lossless argv encoding. Reject ambiguous/unquoted source paths
    instead of inventing a translation-unit identity from a truncated token.
    """
    rows = []
    for line in lines:
        if not line.startswith(COMMAND_PREFIX):
            continue
        match = COMMAND_RE.fullmatch(line)
        if match is None:
            raise ValueError("malformed compiler-command metric")
        seconds, rss, code, command = match.groups()
        if not math.isfinite(float(seconds)):
            raise ValueError("nonfinite compiler elapsed time")
        arguments = shlex.split(command)
        if arguments.count("-c") != 1:
            raise ValueError("compiler metric must identify exactly one -c source")
        index = arguments.index("-c") + 1
        if index >= len(arguments) or Path(arguments[index]).suffix.lower() not in (
                ".c", ".cc", ".cpp", ".cxx", ".cu", ".c++"):
            raise ValueError("missing or ambiguous compiler source path")
        rows.append({
            "translation_unit": arguments[index],
            "peak_rss_kib": int(rss),
            "peak_rss_mib": round(int(rss) / 1024.0, 3),
            "measurement": "gnu_time_command_maxrss",
            "elapsed_seconds": float(seconds), "exit_code": int(code),
            "session_id": "", "process_group": "",
        })
    return rows


def parse_process_samples(lines: list[str]) -> list[dict[str, object]]:
    sources: dict[tuple[int, int], set[str]] = {}
    samples: dict[tuple[int, tuple[int, int]], int] = {}
    timestamp: int | None = None
    for raw in lines:
        line = raw.strip()
        if line.isdigit():
            timestamp = int(line)
            continue
        if timestamp is None or not line:
            continue
        fields = line.split(None, 9)
        if len(fields) != 10:
            continue
        try:
            pgid = int(fields[2])
            sid = int(fields[3])
            rss_kib = int(fields[5])
        except ValueError:
            continue
        group = (sid, pgid)
        match = SOURCE_RE.search(fields[9])
        if match:
            sources.setdefault(group, set()).add(match.group(1))
        sample_key = (timestamp, group)
        samples[sample_key] = samples.get(sample_key, 0) + rss_kib

    peaks: dict[tuple[int, int], int] = {}
    for (_, group), rss_kib in samples.items():
        peaks[group] = max(peaks.get(group, 0), rss_kib)
    if any(len(names) != 1 for names in sources.values()):
        raise ValueError("a sampled process group spans multiple translation units; "
                         "use per-command instrumentation for unambiguous attribution")
    return [
        {
            "translation_unit": next(iter(sources[group])),
            "peak_rss_kib": peaks[group],
            "peak_rss_mib": round(peaks[group] / 1024.0, 3),
            "measurement": "sampled_process_group_rss_sum",
            "elapsed_seconds": "", "exit_code": "",
            "session_id": group[0],
            "process_group": group[1],
        }
        for group in sources
        if group in peaks
    ]


def parse_samples(path: Path, input_format: str = "auto") -> list[dict[str, object]]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if input_format == "auto":
        input_format = "commands" if any(line.startswith(COMMAND_PREFIX) for line in lines) else "ps"
    if input_format not in ("commands", "ps"):
        raise ValueError("unknown compiler metric format")
    rows = parse_commands(lines) if input_format == "commands" else parse_process_samples(lines)
    rows.sort(key=lambda item: (-int(item["peak_rss_kib"]),
                                str(item["translation_unit"])))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samples", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--format", choices=("auto", "commands", "ps"), default="auto")
    parser.add_argument(
        "--session-id", type=int,
        help="only report compiler process groups from this shell session")
    parser.add_argument(
        "--source-prefix",
        help="only report translation units below this source path prefix")
    args = parser.parse_args()
    rows = parse_samples(args.samples, args.format)
    if args.session_id is not None:
        rows = [row for row in rows if row["session_id"] == args.session_id]
    if args.source_prefix:
        prefix = str(Path(args.source_prefix))
        rows = [
            row for row in rows
            if str(row["translation_unit"]).startswith(prefix)
        ]
    fields = [
        "translation_unit", "peak_rss_kib", "peak_rss_mib",
        "measurement", "elapsed_seconds", "exit_code", "session_id", "process_group"]
    if not rows:
        parser.error("no uniquely attributed compiler measurements match the requested input/filter")
    stream = args.output.open("w", newline="", encoding="utf-8") \
        if args.output else sys.stdout
    try:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    finally:
        if args.output:
            stream.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
