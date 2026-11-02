"""Verify every original file in the lossless expanded-run archives."""
import argparse
import hashlib
import json
import tarfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--evidence", type=Path, required=True)
args = parser.parse_args()
index = json.loads((args.evidence / "expanded-archive-index.json").read_text())
file_count = raw_bytes = 0
for item in index["archives"]:
    path = args.evidence / "expanded-archives" / item["archive"]
    assert path.stat().st_size == item["archive_bytes"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == item["archive_sha256"]
    seen = set()
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            if member.isdir():
                continue
            assert member.isfile()
            expected = item["files"][member.name]
            assert member.size == expected["bytes"]
            digest = hashlib.sha256()
            with archive.extractfile(member) as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            assert digest.hexdigest() == expected["sha256"], member.name
            seen.add(member.name)
            file_count += 1
            raw_bytes += member.size
    assert seen == set(item["files"])
print(json.dumps({"verified": True, "archives": len(index["archives"]),
                  "raw_files": file_count, "raw_bytes": raw_bytes}))
