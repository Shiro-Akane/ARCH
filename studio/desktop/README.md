# Desktop launcher and packaging

[Build and launch](../../docs/guides/Build.md#build-and-launch-the-desktop) · [Windows tool setup](../../docs/guides/Build.md#windows-tool-configuration) · [User guide](../../docs/guides/Studio.md)

The Electron Main process owns the window, native dialogs and private Host bootstrap. The renderer uses a preload bridge and typed loopback requests; the standalone Node Host owns project operations. Scientific execution remains in ARCH Core.

## Project selection

Launcher fields and command-line arguments select Project, Existing build directory, Binary, Registered model, Source and Configuration. Project-relative source/configuration selections are checked against the selected root. `--source` binds a registered `.cpp` under `simulation/`; Compile/Build updates the executable after source changes.

`--build-dir` explicitly selects an existing in-project Ninja ARCH build. Its source, output and CPU/CUDA bindings are validated. Missing binaries leave Core actions unavailable while the workbench opens.

Studio starts Core at the project root, which is also the base for relative paths inside `.par`. Native dialogs translate in-project selections into the shared forward-slash relative form.

## Runtime and shutdown

`ARCH_STUDIO_NODE` selects an explicit standalone runtime. Otherwise the launcher uses its packaged Host runtime or suitable system candidates. Native Windows requires x64 Node 24+; Linux uses its native Node runtime.

Main reads `ARCH_STUDIO_WINDOWS_TOOLS` before starting a native Windows Host. The existing tool validator checks executable paths, DLL search directories and optional MSVC `INCLUDE`/`LIB`/`LIBPATH`. These settings come from the launcher environment. Installation examples are maintained in the [build guide](../../docs/guides/Build.md#windows-tool-configuration).

Closing the window cancels owned Preview/Configure work. Active Build can finish before Host shutdown. Confirmed Run/Restart jobs have independent process identity, output reservations and a terminal; Stop targets that owned job. Linux uses process groups, while Windows uses a native Job helper.

## Package layout

[Packaging commands](../../docs/guides/Build.md#package-the-desktop) build a native x64 portable directory. Keep its contents together and launch `arch-studio` on Linux or `arch-studio-bin.exe` on Windows.

The package contains Electron, production renderer/Host sources, standalone Host Node and h5wasm. Core, compiler/runtime dependencies and the Run terminal are installed separately. The packager owns its staging directory under `studio/.local`.

`package.mjs` reads the Electron version from `package.json` and verifies it against the lockfile and installed package. `EnsureElectron.cmake` prepares that runtime. `ARCH_STUDIO_VERSION` sets the package version; its development default is `0.0.0`.

Preserve `LICENSE.arch`, Electron's `LICENSE` and `LICENSES.chromium.html`, `resources/app/runtime/LICENSE.node`, and the dependency notices. Package contents and native startup are checked independently on Linux and Windows.
