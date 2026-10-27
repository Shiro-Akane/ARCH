# Native Windows Studio desktop build (W3)

For current complete Windows CPU/CUDA/Studio status and one-to-one correspondence, see the [2026-10-27 final record](../histroy/build/WindowsNativeCudaQualification-20261027.md). The W0/W1/W2/W3 observations below retain their dated scope. CUDA uses the separate [MSVC build and trusted tool context](WindowsNativeCuda.md).

[中文](WindowsStudioDesktop.zh-CN.md) · [Shared user guide](Studio.md) · [W0–W3 archive](../histroy/build/WindowsNativeQualification-20261026.zh-CN.md)

W3 is complete for the tested Windows 11 x64 GCC/UCRT64 CPU environment. Native windows, Build/Preview, portable packaging and configuration saving passed. The 2026-10-26 supplement also passed real Core Run/Restart, formal Plotfile readback through the final package, and an independent FP64 transport comparison. The user confirmed that raw values and the curve for that same output displayed normally, then reported a normal window close. OS interactions are human-operated because the WSL Computer Use route is unavailable. GPU/MPI, installers and signing are outside this qualification. Mathematics and other Studio I/O stay frozen; Zenodo and releases remain deferred.

Use the native x64 Node24+ runtime with the [GCC/UCRT64 CPU build](WindowsNativeCpu.md), [same-ABI dependencies](WindowsNativeW0.md) and [W2 services](WindowsStudioPlatform.md). Core, the native toolchain, DLL prerequisites and optional Windows Terminal stay external. The package includes standalone Host Node and h5wasm.

Append to an already configured native CPU Ninja tree:

```powershell
cmake -S . -B build-studio-cpu -DARCH_BUILD_STUDIO=ON "-DARCH_STUDIO_NODE=C:/tools/node/node.exe"
cmake --build build-studio-cpu --target arch-studio-assets -j2
cmake --build build-studio-cpu --target arch-studio-runtime -j2
cmake --build build-studio-cpu --target arch-studio-package -j2
```

CMake uses the native Node installation's npm-cli.js, not npm.ps1. Assets use the lockfile with --ignore-scripts; the runtime target explicitly invokes the official Electron installer. The manifest, both lock entries and installed package must agree. An existing old executable does not skip preparation. Linux uses the same targets and its existing shell entry; Windows generates bin/arch-studio.cmd.

Package on the target OS. Keep the whole studio/.local/desktop-release/arch-studio-bin-win32-x64 directory and run arch-studio-bin.exe. Linux retains its linux-x64 directory and arch-studio wrapper. One copy flow includes production assets, Host, src, h5wasm, licenses and native runtime/node.exe or runtime/node. Electron version comes from the dependency rather than a separate script pin. Preserve upstream Node, Electron/Chromium, ARCH and HDF/h5wasm licenses. The optional electron_config_cache is shared with the official installer; cached official artifacts must pass the locked package's checksums.json. Do not disable TLS or checksum validation.

This is a local portable directory, without signing, installer, auto-update or release actions. The development package version stays0.0.0.

For the trusted Main-only ARCH_STUDIO_WINDOWS_TOOLS configuration and complete PowerShell example, see the [Chinese guide](WindowsStudioDesktop.zh-CN.md). Required keys are cmake, supervisor, ninja, objdump, publisher, rc (absolute local .exe paths), and path (absolute native tool/DLL directories separated by semicolons). Optional terminal identifies Windows Terminal for Run/Restart. No tool settings come from renderer, HTTP or project data. ARCH_STUDIO_NODE is an optional absolute native x64 Node24+ override; an invalid explicit runtime fails visibly. Packages otherwise prefer their bundled Node.

Windows projects default to native Node. Explicit --distro or an absolute Linux/WSL UNC project/source selects the established WSL route. Same-origin navigation, sandbox, context isolation, disabled Node renderer and loopback tokens remain. Native Open/Save returns an in-project .par relative path to the existing Host readers/writers. No second scientific I/O implementation is introduced. Only the five approved A11 Host owners changed for private platform context and path binding. Other I/O owners remain frozen. Native Main waits up to90 seconds for Host readiness; Linux/explicit WSL remains25 seconds, with all Core limits unchanged. Packaging preserves the official standalone Node distribution LICENSE.

Optional Windows Terminal may use an App Execution Alias that is not stat-able; the existing W2 Run owner retains actual launch validation.

Editing and Preview do not require Windows Terminal. The qualified Run/Restart route explicitly sets `tools.terminal` to the available `wt.exe` and uses the existing W2 terminal handoff. In the current restricted test job, omitting it made the independent-console fallback fail with Win32 5; the existing controller rejected the unconfirmed handoff. That fallback was not qualified in this environment. Job constraints were retained.

Desktop dialog selections are converted to canonical forward-slash project-relative paths in the existing arguments owner before invoking unchanged Open/Save As services. Save Working Copy As writes inside the project; Download Copy exports a download. These actions have distinct acceptance evidence.

## Full production-chain verification

A visible window or initial-state Preview does not establish production-output support. Run a small CPU case through the final portable package and its bundled standalone Host Node, restart from an intermediate checkpoint, then read the formal Plotfile through that package. Verify metadata, SHA-bound raw slice/global display LOD/point queries and the same file in the Real Plotfile workspace. Retain the original scientific parser, renderer validators and worker limits.

The representative Sod run advanced eight steps. The existing checkpoint comparator reported `max_abs=0` and `max_rel=0` between the complete and restarted runs. Independent native Windows h5py checks matched the stored FP64 bits for eight DENS samples, one point sample and their coordinates against packaged-reader responses; reads preserved the file SHA. This qualifies the sample, not every physics model or filesystem. The [W0–W3 archive](../histroy/build/WindowsNativeQualification-20261026.zh-CN.md) records distributable scope and summaries. Full logs remain in the local ignored `output/windows-w3-20261026/` directory and are not part of the source distribution.
