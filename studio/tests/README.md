# Studio and Host tests

Use Node 24+ and install the native tools from the [build guide](../../docs/guides/Build.md). From `studio/`:

```bash
npm ci
npm run lint
npm run build
npm test
```

`npm run build` includes TypeScript checking. `npm run typecheck` checks types without producing assets. The complete test suite includes `host-*.test.ts`; `npm run test:host` selects a diagnostic subset.

## Platform prerequisites

Linux Host fixtures perform real small builds using `/usr/bin/cmake`, `/usr/bin/ninja`, `/usr/bin/c++` and `ld.mold`. Install these tools before running the full suite. Windows native tests use the shared helpers and a private tool context:

| Environment variable | Native test input |
| --- | --- |
| `ARCH_TEST_CMAKE`, `ARCH_TEST_NINJA` | Absolute paths to native CMake and Ninja |
| `ARCH_TEST_CXX`, `ARCH_TEST_RC`, `ARCH_TEST_OBJDUMP` | GCC/UCRT64 C++ compiler, resource compiler and objdump |
| `ARCH_TEST_SUPERVISOR`, `ARCH_TEST_PUBLISHER` | Built `arch_windows_process.exe` and `arch_windows_build_publish.exe` |
| `ARCH_TEST_TERMINAL` | Windows Terminal executable for terminal handoff tests |
| `PATH` | Matching compiler/runtime DLL directories and required Windows tools |

CUDA profile tests additionally use `ARCH_TEST_NVCC`, `ARCH_TEST_MSVC` and `ARCH_TEST_CUDA_ROOT`. Prepare their MSVC environment as described in [Windows CUDA installation](../../docs/guides/Build.md#windows-cuda-installation). Native process, build, configuration, handoff, desktop and Plotfile groups require their corresponding real tools; use the installed program paths rather than WSL paths.

## Selection and coverage

`run-platform.mjs` enumerates root `*.test.ts` files with TAP output and one file worker. On Linux, six declared Windows-only files are reported as `NOT_APPLICABLE` on stderr, including generated native tool environments. Shared and mixed files, including `host-windows-run.test.ts`, still run. Unknown files are selected by default; an empty inventory or stale declared filename fails. Mixed files register platform-specific cases only on the applicable OS.

The tests cover configuration/workspace state, Preview and cancellation, renderer behavior, Plotfile queries, controlled builds and Run/Restart lifecycle. Native Electron window, file-dialog and packaged-runtime checks supplement this suite; their coverage is listed in [platform support](../../docs/releases/PlatformSupport.md).

Run preparation tests bind Core's `--check-run` response to saved bytes and the selected executable, reject unavailable providers and contradictory readiness, and recheck the backend and output targets at confirmation. Native integration also exercises the generated Windows environment against a real Core; POSIX executable fixtures do not substitute for that check.

Hosted CI runs the complete applicable Linux suite, lint and typed production build. Its result checker requires a nonempty report with no failed or skipped tests. The [workflow guide](../../.github/workflows/README.md) describes change selection and diagnostic artifacts.

## Fixtures and outputs

[fixtures/](fixtures/README.md) contains bounded regression inputs, including a pinned 14 KiB CPU Sod Plotfile. CI hydrates that LFS fixture through the fixed-pointer cache action and verifies size and SHA-256 before Node execution. Local testing uses the matching hydrated file.

`tools/compareRestartCheckpoints.mjs` compares selected checkpoint files. `tools/import_cellular.py` imports its recorded sample dataset for development. Generated scientific files belong in a local output directory.

Core tests live in [tests/](../../tests/README.md), and independent scientific references and runners in [Validation](../../validation/README.md). Portable desktop packages include production assets, Host imports and required runtime source; they exclude this test tree.
