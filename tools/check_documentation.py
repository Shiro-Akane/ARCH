#!/usr/bin/env python3
"""Check maintained Markdown destinations without rewriting historical evidence.

The repository's existing Tooling lane owns this inexpensive check. Historical
reports, immutable results and source snapshots retain their original basis;
current reader pages must point to existing files and Markdown heading anchors.
No network access, rendering, HDF reads or scientific execution is performed.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit


DEVELOPER_PAGES = frozenset({
    "README.md", "ImplementationOwnership.md", "CommentAndDocumentationStyle.md",
    "ConfigurationContractPlan.zh-CN.md", "CudaReleaseStandard.md",
    "MaterialInputProvenance.zh-CN.md", "PlotfileValidationContract.zh-CN.md",
    "RTAmrDataCorrectnessRepair.zh-CN.md",
})


def maintained(path):
    """Separate current instructions from dated provenance, explicitly by owner."""
    # The two living archive indexes are maintained readers. Historical bodies
    # below them retain their original provenance and are not requalified here.
    if path in ("docs/histroy/README.md", "docs/development/archive/README.md"):
        return True
    parts = Path(path).parts
    if any(part in ("archive", "histroy", "results") for part in parts):
        return False
    if path.startswith(("docs/releases/", "validation/legacy/")):
        return False
    if path.startswith("docs/development/"):
        return Path(path).parent == Path("docs/development") and Path(path).name in DEVELOPER_PAGES
    return path not in ("ARCH_MODERNIZATION_PLAN.md", "AGENTS.md") and not path.startswith(".agents/")


def prose(text):
    # Code examples often contain deliberately invalid URLs or Markdown input.
    result = []
    fence = None
    for line in text.splitlines():
        marker = re.match(r"^\s*(`{3,}|~{3,})", line)
        if marker:
            token = marker[1]
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence):
                fence = None
            continue
        if fence is None:
            result.append(line)
    return "\n".join(result)


def anchors(text):
    """Common GitHub heading anchors, including duplicate and explicit IDs."""
    result, counts = set(), {}
    for heading in re.findall(r"^\s{0,3}#{1,6}\s+(.+?)\s*#*\s*$", prose(text), re.MULTILINE):
        heading = re.sub(r"<[^>]*>", "", heading).lower()
        slug = "".join(c for c in heading if c.isalnum() or c in "_- ").replace(" ", "-")
        count = counts.get(slug, 0)
        counts[slug] = count + 1
        result.add(slug + ("-" + str(count) if count else ""))
    result.update(re.findall(r'<(?:a|span)\s+[^>]*(?:id|name)=["\']([^"\']+)["\']', text))
    return result


def destinations(text):
    text = prose(text)
    # Markdown destination paths in this project are whitespace-free or <...>.
    return re.findall(r"\]\((<[^>\n]+>|[^\s)]+)", text) + re.findall(
        r"^\s*\[[^]\n]+\]:\s*(<[^>\n]+>|\S+)", text, re.MULTILINE)


def check_file(root, relative):
    source = root / relative
    errors, checked = [], 0
    for target in destinations(source.read_text(encoding="utf-8")):
        target = target.removeprefix("<").removesuffix(">")
        url = urlsplit(target)
        if url.scheme or url.netloc:
            continue
        file = (source.parent / unquote(url.path)).resolve() if url.path else source.resolve()
        try:
            file.relative_to(root.resolve())
        except ValueError:
            errors.append(f"{relative}: destination escapes repository: {target}")
            continue
        checked += 1
        if not file.exists():
            errors.append(f"{relative}: missing destination: {target}")
        elif url.fragment and file.suffix.lower() == ".md" and unquote(url.fragment) not in anchors(file.read_text(encoding="utf-8")):
            errors.append(f"{relative}: missing heading: {target}")
    return checked, errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    root = args.root.resolve()
    paths = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard",
                                     "-z", "--", "*.md"], cwd=root).decode().split("\0")
    selected = sorted({p for p in paths if p and maintained(p) and (root / p).is_file()})
    if not selected:
        parser.error("current documentation selection is empty")
    count, errors = 0, []
    for name in selected:
        checked, findings = check_file(root, name)
        count += checked
        errors.extend(findings)
    for error in errors:
        print(error, file=sys.stderr)
    print(f"documentation: {len(selected)} current pages, {count} local destinations, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
