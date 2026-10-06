# Linux / WSL desktop entry

`arch-studio` opens an independent Electron window with production assets and a managed local Host. A source checkout needs Node 24+; the portable package includes its Host Node runtime. Linux graphical libraries and WSLg (under WSL) are prerequisites. Run/Restart use an independent `xterm` and Linux `flock`; they do not provide server/background scheduling.

From a source checkout after building Studio:

```bash
studio/desktop/arch-studio --project "$PWD" --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod.par
studio/desktop/arch-studio --source "$PWD/simulation/Sod/Sod.cpp" --config simulation/Sod/Sod.par
```

With CMake `ARCH_BUILD_STUDIO=ON`, the equivalent launcher is `build-studio-cpu/bin/arch-studio`. Relative binary/config paths resolve against the selected project. `--source` identifies registered source; it does not compile new C++ automatically. Missing builds, stale identities and unsupported cases remain visible errors. Configure/Build are explicit actions with a small output panel.

Node is selected from `ARCH_STUDIO_NODE`, otherwise the packaged Host runtime, PATH and the existing user/system candidates. `ARCH_STUDIO_NODE` must be an absolute supported Linux executable. An invalid explicit runtime is never replaced silently.

Closing the desktop cancels owned Preview/Configure requests and stops its Host. Active Build is allowed to finish. Confirmed Run/Restart jobs have their own terminal, records and process identity and survive Host exit. Stop targets the owned job. Output-directory advisory reservations coordinate Studio-managed runs; they are not approval to overwrite existing data. See the [workflow guide](../../docs/guides/Studio.zh-CN.md).

## Packaging

From `studio/`, run `npm run desktop:package`, or use `cmake --build build-studio-cpu --target arch-studio-package`. Keep the complete `.local/desktop-release/arch-studio-bin-linux-x64/` directory together and start its `arch-studio`. The Host Node executable and h5wasm are bundled. The scientific Core, Linux graphical libraries and Run/Restart terminal remain external prerequisites. The packager recreates its owned staging tree and copies only production assets and required Host sources, not tests or historical docs.

The default package version remains development `0.0.0`. Set `ARCH_STUDIO_VERSION` only after the release version has been frozen. Preserve `LICENSE.arch`, Electron's `LICENSE`/`LICENSES.chromium.html`, and the dependency notices; ARCH's MIT license does not replace third-party licenses. Packaging is not signing, an installer or a release tag.

Native Windows packaging is outside this Linux delivery. Earlier platform/checkpoint instructions remain in the [historical archive](../docs/archive/desktop-checkpoint-history.md).

Environment setup and troubleshooting: [Linux/WSL requirements](../../docs/guides/StudioEnvironment.zh-CN.md).
