#!/usr/bin/env python3
"""Summarize per-CUDA-translation-unit peak compiler RSS samples."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import re
import sys


SOURCE_RE = re.compile(
    r"(?:--orig_src_(?:file|path)_name\s+|-c\s+)[\"']?([^\"'\s]+\.cu)"
)


def parse_samples(path: Path) -> list[dict[str, object]]:
    sources: dict[tuple[int, int], str] = {}
    samples: dict[tuple[int, tuple[int, int]], int] = {}
    timestamp: int | None = None
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
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
            sources[group] = match.group(1)
        sample_key = (timestamp, group)
        samples[sample_key] = samples.get(sample_key, 0) + rss_kib

    peaks: dict[tuple[int, int], int] = {}
    for (_, group), rss_kib in samples.items():
        peaks[group] = max(peaks.get(group, 0), rss_kib)
    rows = [
        {
            "translation_unit": sources[group],
            "peak_rss_kib": peaks[group],
            "peak_rss_mib": round(peaks[group] / 1024.0, 3),
            "session_id": group[0],
            "process_group": group[1],
        }
        for group in sources
        if group in peaks
    ]
    rows.sort(key=lambda item: (-int(item["peak_rss_kib"]),
                                str(item["translation_unit"])))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("samples", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--session-id", type=int,
        help="only report compiler process groups from this shell session")
    parser.add_argument(
        "--source-prefix",
        help="only report translation units below this source path prefix")
    args = parser.parse_args()
    rows = parse_samples(args.samples)
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
        "session_id", "process_group"]
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
