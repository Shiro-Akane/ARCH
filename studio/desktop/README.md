# Desktop launcher and packaging

[Build and launch](../../docs/guides/Build.md#build-and-launch-the-desktop) · [Windows tool setup](../../docs/guides/Build.md#windows-tool-configuration) · [User guide](../../docs/guides/Studio.md)

The Electron Main process owns the window, native dialogs and private Host bootstrap. The renderer uses a preload bridge and typed loopback requests; the standalone Node Host owns project operations. Scientific execution remains in ARCH Core.

## Project selection

Launcher fields and command-line arguments select Project, Existing build directory, Binary, Registered model, Source and Configuration. Project-relative source/configuration selections are checked against the selected root. `--source` binds a registered `.cpp` under `simulation/`; Compile/Build updates the executable after source changes.

Launching without arguments opens the empty project chooser, including when the current directory is an ARCH checkout. It starts no Host and loads no model, parameter file or Core runtime. Explicit CLI arguments keep their project-selection behavior.

`--build-dir` explicitly selects an existing in-project Ninja ARCH build. Its source, output and CPU/CUDA bindings are validated. Missing binaries leave Core actions unavailable while the workbench opens.

Studio starts Core at the project root, which is also the base for relative paths inside `.par`. Native dialogs translate in-project selections into the shared forward-slash relative form.

## Runtime and shutdown

Generated source launchers use the Node selected during CMake configuration. Portable packages use their bundled Host runtime unless `ARCH_STUDIO_NODE` selects another standalone runtime. Native Windows requires x64 Node 24+; Linux uses its native Node runtime.

Before starting a native Windows Host, Main reads explicit `ARCH_STUDIO_WINDOWS_TOOLS` JSON, or the file selected by `ARCH_STUDIO_WINDOWS_TOOLS_FILE`. CMake's source launcher supplies the build-local file automatically. The shared validator checks executable paths, DLL directories, MSVC `INCLUDE`/`LIB`/`LIBPATH` and any listed linked runtime DLLs. Missing DLL diagnostics name the required file. GNU objdump supplies the validated PE inspection format.

[WindowsRuntime.cmake](../../cmake/project/WindowsRuntime.cmake) owns the common dependency/compiler environment and CLI `arch-env.cmd`; [WindowsTools.cmake](../../cmake/studio/WindowsTools.cmake) adds desktop tools to Main's manifest. These settings come from the launch environment, never renderer or project parameters. Setup and portable-package overrides are described in the [build guide](../../docs/guides/Build.md#windows-tool-configuration).

Closing the window cancels owned Preview/Configure work. Active Build can finish before Host shutdown. Confirmed Run/Restart jobs have independent process identity, output reservations and a terminal; Stop targets that owned job. Linux uses process groups, while Windows uses a native Job helper.

## Package layout

[Packaging commands](../../docs/guides/Build.md#package-the-desktop) build a native x64 portable directory. Keep its contents together and launch `arch-studio` on Linux or `arch-studio-bin.exe` on Windows.

The package contains Electron, production renderer/Host sources, standalone Host Node and h5wasm. Core, compiler/runtime dependencies and the Run terminal are installed separately. The packager owns its staging directory under `studio/.local`. Generated machine-specific tool paths stay outside the package; a Windows installation selects its own trusted local manifest.

`package.mjs` reads the Electron version from `package.json` and verifies it against the lockfile and installed package. `EnsureElectron.cmake` prepares that runtime. `ARCH_STUDIO_VERSION` sets the package version; its development default is `0.0.0`.

Preserve `LICENSE.arch`, Electron's `LICENSE` and `LICENSES.chromium.html`, `resources/app/runtime/LICENSE.node`, and the dependency notices. Package contents and native startup are checked independently on Linux and Windows.
