"""CUDA instrumentation boundary shared by runtime validation campaigns.

The actual ARCH binary remains the artifact under qualification. Instrumentation
only prefixes its command and requires a complete, error-free tool report;
checkpoint/physics acceptance stays in the ordinary validators.
"""
from pathlib import Path
import re

import validation_provenance as provenance


def check_report(text: str, tool: str) -> list[str]:
    expected = {
        "memcheck": ["ERROR SUMMARY: 0 errors", "LEAK SUMMARY: 0 bytes leaked in 0 allocations"],
        "racecheck": ["RACECHECK SUMMARY: 0 hazards displayed (0 errors, 0 warnings)"],
    }
    if tool not in expected:
        raise ValueError("unsupported CUDA sanitizer tool")
    if len(re.findall(r"^========= Process ID:\s+\d+\s*$", text, re.M)) != 1:
        raise RuntimeError("sanitizer report lacks one instrumented process")
    summaries = [line.removeprefix("========= ") for line in text.splitlines()
                 if "SUMMARY:" in line]
    diagnostics = "\n".join(line for line in text.splitlines() if "SUMMARY:" not in line)
    if sorted(summaries) != sorted(expected[tool]) or re.search(
            r"^=========\s+(?:Warning|Error|Fatal)\b", diagnostics, re.M | re.I):
        raise RuntimeError(f"incomplete or unsuccessful {tool} report: {summaries}")
    return summaries


class CudaSanitizer:
    def __init__(self, executable: Path, tool: str):
        if tool not in ("memcheck", "racecheck"):
            raise ValueError("unsupported CUDA sanitizer tool")
        self.executable = executable.resolve()
        self.tool = tool
        self.identity = provenance.file_identity(self.executable)

    def command(self, application: list[str], lane: Path) -> list[str]:
        report = lane / "sanitizer.log"
        if report.exists():
            raise RuntimeError("refusing to reuse a sanitizer report")
        prefix = [str(self.executable), "--tool", self.tool, "--print-session-details",
                  "--require-cuda-init", "yes", "--error-exitcode", "86",
                  "--log-file", str(report.resolve())]
        if self.tool == "memcheck":
            prefix += ["--leak-check", "full"]
        return prefix + application

    def evidence(self, lane: Path) -> dict:
        if self.identity != provenance.file_identity(self.executable):
            raise RuntimeError("CUDA sanitizer changed during execution")
        report = lane / "sanitizer.log"
        summaries = check_report(report.read_text(encoding="utf-8"), self.tool)
        return {"tool": self.tool, "executable": self.identity,
                "report": provenance.file_identity(report), "summaries": summaries}


def add_arguments(parser) -> None:
    parser.add_argument("--cuda-sanitizer", type=Path,
                        help="instrument actual CUDA ARCH lanes; CPU lanes remain ordinary references")
    parser.add_argument("--sanitizer-tool", choices=("memcheck", "racecheck"), default="memcheck")


def from_arguments(args) -> CudaSanitizer | None:
    return CudaSanitizer(args.cuda_sanitizer, args.sanitizer_tool) if args.cuda_sanitizer else None
