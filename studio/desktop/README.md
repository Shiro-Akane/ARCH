# Desktop entry: Linux / WSL and native Windows

The desktop opens an independent Electron window with production assets and a managed local Host. Linux uses `arch-studio`; Windows uses `arch-studio.cmd` from a checkout and `arch-studio-bin.exe` from a package. A checkout needs native standalone Node 24+; a portable package includes the native Host Node runtime. Linux graphical libraries and WSLg (under WSL) are prerequisites. Linux Run/Restart use `xterm` and `flock`; native Windows uses the W2 Job/terminal services. These do not provide server/background scheduling.

W3 native Windows CPU Studio qualification is complete for the tested Windows11 x64 GCC/UCRT64 environment: A11, real project/window software integration and portable packaging passed. The user performed Save Working Copy As and its copy passed independent byte/read validation. OS interactions were human-operated because the WSL Computer Use route was unavailable. GPU/MPI, installers and signing remain outside this qualification.

From a source checkout after building Studio:

```bash
studio/desktop/arch-studio --project "$PWD" --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod.par
studio/desktop/arch-studio --source "$PWD/simulation/Sod/Sod.cpp" --config simulation/Sod/Sod.par
```

With CMake `ARCH_BUILD_STUDIO=ON`, the equivalent launcher is `build-studio-cpu/bin/arch-studio`. Relative binary/config paths resolve against the selected project. `--source` selects a regular `.cpp` under `simulation/`; an uncompiled or edited source remains pending until explicit Configure/Build produces its exact registered source and digest. The source view is read-only; edit C++ with your editor. Missing executables open the workbench with Core actions disabled. Stale identities and unsupported cases remain visible errors. Configure/Build are explicit actions with a small output panel.

Use `--build-dir build-release-cpu` (or select Existing build directory in the launcher) to adopt an existing in-project Ninja ARCH tree explicitly. Its canonical source/build/output identities and CPU/CUDA setting are checked; Configure preserves the cache mode and adds no new definitions. Selecting this tree does not assert build freshness or enable Preview before a matching successful Studio Build.

Node is selected from `ARCH_STUDIO_NODE`, otherwise the packaged Host runtime and PATH (plus existing user/system candidates on Linux). An explicit runtime must be absolute and supported; it is never replaced silently. Native Windows requires standalone x64 Node 24+ `.exe`, verified by platform, architecture and version. Electron or a WSL shim cannot serve as the bundled Host Node.

Closing the desktop cancels owned Preview/Configure requests and stops its Host. Active Build is allowed to finish. Confirmed Run/Restart jobs have their own terminal, records and process identity and survive Host exit. Stop targets the owned job. Output-directory advisory reservations coordinate Studio-managed runs; they are not approval to overwrite existing data. See the [workflow guide](../../docs/guides/Studio.zh-CN.md).

## Packaging

From `studio/`, run `npm run desktop:package`, or use `cmake --build build-studio-cpu --target arch-studio-package`. Package on the target OS with native x64 Node. Keep the complete `.local/desktop-release/arch-studio-bin-linux-x64/` or `arch-studio-bin-win32-x64/` directory together; start `arch-studio` on Linux or `arch-studio-bin.exe` on Windows. Host Node and h5wasm are bundled. The scientific Core, native toolchain/dependencies, OS graphical libraries and Run/Restart terminal remain external prerequisites. The packager recreates its owned staging tree and copies only production assets and required Host sources, not tests or historical docs.

The default package version remains development `0.0.0`. Set `ARCH_STUDIO_VERSION` only after the release version has been frozen. Preserve `LICENSE.arch`, Electron's `LICENSE`/`LICENSES.chromium.html`, `resources/app/runtime/LICENSE.node`, and the dependency notices; ARCH's MIT license does not replace third-party licenses. Packaging is not signing, an installer or a release tag.

The package script derives Electron from `package.json` and checks both lock entries and the installed package before staging. CMake explicitly prepares that exact runtime with the official installer, even if an older executable already exists. Updating a dependency pin therefore does not leave a separate hard-coded packaging version.

## Native Windows prerequisites and entry

Use GCC/UCRT64 and same-ABI dependencies from the [native CPU guide](../../docs/guides/WindowsNativeCpu.md). The Core and W2 helpers stay outside the Electron package. Build `arch-studio-assets` and `arch-studio-runtime` with `ARCH_BUILD_STUDIO=ON` and `ARCH_STUDIO_NODE` set to native `node.exe`. CMake invokes npm through its native `npm-cli.js`, without depending on shell execution policy for `npm.ps1`.

Main reads `ARCH_STUDIO_WINDOWS_TOOLS` as a JSON object with required `cmake`, `supervisor`, `ninja`, `objdump`, `publisher`, `rc` absolute local `.exe` paths and a `path` string of absolute native tool/DLL directories separated by semicolons. `terminal` is an optional absolute Windows Terminal `.exe`; Run/Restart needs it. These are trusted startup settings; they are not renderer fields, HTTP parameters or project data. Missing or invalid settings fail visibly. Never put credentials in this configuration.

```powershell
# From the ARCH root, after setting the trusted tool context:
& .\build-studio-cpu\bin\arch-studio.cmd --project "$PWD" --build-dir build-studio-cpu --binary build-studio-cpu/bin/ARCH.exe --case Sod --config simulation/Sod/Sod.par
```

Windows drive paths default to the native Host. Explicit `--distro` or an absolute Linux/WSL UNC project selects the existing WSL route and its Linux Node/Core requirements. Native Open/Save As selections remain `.par` paths inside the managed project and still use the existing Host readers/writers. Closing follows the same drain policy on both platforms. Earlier platform/checkpoint instructions remain in the [historical archive](../../docs/histroy/studio/desktop-checkpoint-history.md).

Environment setup and troubleshooting: [Linux/WSL and Windows requirements](../../docs/guides/StudioEnvironment.zh-CN.md).
