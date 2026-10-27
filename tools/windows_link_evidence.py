"""Native Windows recipe parsing; scientific acceptance stays in shared readers.

These records identify configured files, not the complete process loader graph.
No build, DLL loading, or arbitrary shell command is performed here.
"""
from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path
import re
import subprocess
from typing import Callable, Any


def _same_path(left: Path, right: Path) -> bool:
    return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


def ninja_link_recipe(command: list[str], build_dir: Path, output: Path) -> str:
    # Ninja expands its own rspfile_content, even after a successful build has
    # removed the temporary response file. This is the configured recipe only.
    result = subprocess.run(command + ["-t", "compdb", "-x"], check=True,
                            capture_output=True, text=True, encoding="utf-8")
    if len(result.stdout.encode("utf-8")) > 32 * 1024 * 1024:
        raise RuntimeError("Windows link recipe exceeds its read budget")
    entries = json.loads(result.stdout)
    if not isinstance(entries, list):
        raise RuntimeError("invalid Windows Ninja recipe inventory")
    selected = []
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("output"), str):
            raise RuntimeError("invalid Windows Ninja output record")
        if not isinstance(entry.get("directory"), str):
            raise RuntimeError("invalid Windows Ninja directory record")
        directory = Path(entry["directory"])
        if not directory.is_absolute() or not _same_path(directory, build_dir):
            raise RuntimeError("Windows Ninja recipe directory differs from selected build")
        candidate = Path(entry["output"])
        if not candidate.is_absolute():
            candidate = directory / candidate
        if _same_path(candidate, output):
            selected.append(entry.get("command"))
    if len(selected) != 1 or not isinstance(selected[0], str) or not selected[0].strip():
        raise RuntimeError("Windows Ninja recipe does not bind one exact ARCH.exe output")
    arguments = command_arguments(selected[0])
    outputs = [arg[5:] for arg in arguments if arg.lower().startswith("/out:")]
    if len(outputs) != 1:
        raise RuntimeError("Windows link must name exactly one /OUT artifact")
    linked = Path(outputs[0])
    if not linked.is_absolute():
        linked = build_dir / linked
    if not _same_path(linked, output):
        raise RuntimeError("Windows linker output differs from selected ARCH.exe")
    return selected[0]


def command_arguments(command: str) -> list[str]:
    if os.name != "nt":
        raise RuntimeError("native Windows command parser required")
    # Strip only CMake/Ninja's generated outer CMD wrapper. The contents are
    # parsed as data; operators are never executed by the validator.
    wrapper = re.fullmatch(r'\s*(?:"[^"\r\n]*[\\/]cmd\.exe"|[^\s"\r\n]*cmd\.exe)'
                           r'\s+/C\s+"(.*)"\s*', command, re.I | re.S)
    if wrapper:
        command = wrapper.group(1)
    elif re.search(r"(?:^|[\\/])cmd\.exe\s", command, re.I):
        raise RuntimeError("ambiguous Windows link command wrapper")
    from ctypes import wintypes
    shell = ctypes.WinDLL("shell32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    shell.CommandLineToArgvW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(wintypes.LPWSTR)
    kernel.LocalFree.argtypes = [wintypes.HLOCAL]
    kernel.LocalFree.restype = wintypes.HLOCAL
    count = ctypes.c_int()
    argv = shell.CommandLineToArgvW("arch-link-evidence " + command, ctypes.byref(count))
    if not argv:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        result = [argv[i] for i in range(1, count.value)]
    finally:
        kernel.LocalFree(ctypes.cast(argv, wintypes.HLOCAL))
    if any(arg.startswith("@") for arg in result):
        raise RuntimeError("unexpanded Windows link response file")
    return result


def provider_name(filename: str) -> str | None:
    # Preserve the native GCC CPU profile's static .a spelling as well as the
    # MSVC static SuiteSparse/import-library spellings actually configured here.
    match = re.fullmatch(r'lib(klu|amd|btf|colamd|suitesparseconfig)\.a', filename, re.I)
    if not match:
        match = re.fullmatch(r'(klu|amd|btf|colamd|suitesparseconfig)_static\.lib', filename, re.I)
    if not match:
        match = re.fullmatch(r'(cudss|cublas|cublasLt)\.lib', filename, re.I)
    if match:
        return match.group(1).lower()
    if filename.lower() == "arch_cuda_sparse_provider.lib":
        return "arch_cuda_sparse_provider"
    return None


def configured_provider_dlls(options: dict[str, str],
                             identity: Callable[[Path], dict[str, Any]]) -> dict[str, Any]:
    bindings = (
        ("cudss", "CuDSS_LIBRARY", "CuDSS_RUNTIME_LIBRARY"),
        ("cublas", "CUDA_cublas_LIBRARY", "CUDA_cublas_RUNTIME_LIBRARY"),
        ("cublasLt", "CUDA_cublasLt_LIBRARY", "CUDA_cublasLt_RUNTIME_LIBRARY"),
    )
    libraries, imports = [], []
    for name, import_key, dll_key in bindings:
        imported, runtime = options.get(import_key, ""), options.get(dll_key, "")
        if (not imported or not runtime or not Path(imported).is_absolute()
                or not Path(runtime).is_absolute()):
            raise RuntimeError("Windows runtime sparse artifacts require absolute configured import/DLL paths")
        before = identity(Path(imported))
        if Path(imported).stat().st_size > 16 * 1024 * 1024:
            raise RuntimeError("Windows sparse import library exceeds read budget")
        data = Path(imported).read_bytes()
        if before != identity(Path(imported)):
            raise RuntimeError("Windows sparse import library changed during inspection")
        names = {item.decode("ascii").lower() for item in re.findall(
            rb'(?:cudss|cublas(?:Lt)?)64_[0-9]+\.dll(?=\x00)', data, re.I)}
        expected = re.compile(re.escape(name) + r"64_[0-9]+\.dll", re.I)
        paired = {item for item in names if expected.fullmatch(item)}
        if len(paired) != 1 or Path(runtime).name.lower() not in paired:
            raise RuntimeError("configured Windows DLL differs from its import-library binding")
        imports.append({"provider": name, **before})
        libraries.append({"provider": name, "kind": "configured-windows-dll",
                          **identity(Path(runtime))})
    return {"runtime_library_scope": "configured-windows-provider-dlls",
            "runtime_libraries": libraries, "runtime_import_bindings": imports}
