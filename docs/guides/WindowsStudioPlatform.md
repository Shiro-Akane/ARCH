# Windows Studio platform services: W2

For current complete Windows CPU/CUDA/Studio status and one-to-one correspondence, see the [2026-10-27 final record](../histroy/build/WindowsNativeCudaQualification-20261027.md). The W0/W1/W2/W3 observations below retain their dated scope. CUDA uses the separate [MSVC build and trusted tool context](WindowsNativeCuda.md).

[简体中文](WindowsStudioPlatform.zh-CN.md) · [Native CPU CLI](WindowsNativeCpu.md)

W2 adds native process operations beneath the existing Host state machines.
Process supervision, Configure completion, repeat configuration and cancellation
have execution evidence. Approved A2 Build evidence and manifest publication now
use the same state machine. Approved A3–A6 source/config/Plotfile operations and
Run/Restart have native evidence below. Approved A7–A10 add a distinct Windows
Preview resource contract and scoped recorded-build admission through the same
Host owners. W3 native window software integration, A11 and portable packaging are
covered by the [desktop build guide](WindowsStudioDesktop.md). OS dialog interaction
has separate accepted W3 evidence for configuration saving and real Core-output display; it is not part of these W2 receipts. Daily operation uses the shared [Studio guide](Studio.md).

## Shared ownership and building

`studio/host/hostProcess.ts` owns process operations only. Linux retains process
groups; Windows uses `arch_windows_process` from the same CMake graph. Node's
standard library does not expose Job Objects, assignment at process creation or
process-creation identity handles. This small native target supplies those
operations without another workflow, solver, GUI or data reader/writer.

The Windows 10+ Job-list creation attribute contains the target before its thread
is resumed. There is no uncontained fallback. Only standard streams and, for Run, explicitly allowlisted output leases and
directory pins are inherited. Job and verified parent handles are not. Job close reclaims descendants, and
normal leader exit also drains remaining descendants. Configure/Build cancellation targets the
owned helper. Run Stop uses a control pipe and requires helper status 130 after
Job drain; unknown termination is not reported stopped. No `taskkill` or PID enumeration is used. Linux group cancellation
continues after leader exit, while descendants keep the group alive.

The parent's PID and creation FILETIME must match before its held handle is used
for lifetime supervision. Detection has scheduling latency; zero target
instructions after parent death are not promised. Containment already exists.
Simultaneous parent/tool exit retains failure rather than inferring success.
Child status is forwarded; helper faults return 125 with an
`ARCH process supervisor:` diagnostic. A tool may also return 125, so the status
alone does not distinguish attribution. Configure reports command or supervisor
failure and retains its log.

Use the CPU guide's `$ucrt`, `$build`, `$install` and selected DLL PATH:

```powershell
& "$ucrt/bin/cmake.exe" --build $build --target arch_windows_process arch_windows_build_publish --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Process supervisor build failed' }
& "$ucrt/bin/cmake.exe" --install $build --component Runtime --prefix $install
if ($LASTEXITCODE -ne 0) { throw 'Runtime installation failed' }
```

The build executables are `$build/arch_windows_process.exe` and
`$build/arch_windows_build_publish.exe`; Runtime installs them
under `$install/bin`. Installation retains other existing Runtime copies and is
not a standalone desktop package. No release or installer was created.

Trusted Host initialization may provide `ConfigureProfile.windowsTools` with
absolute local `.exe` paths for `cmake` and `supervisor`, plus fixed `path`.
Build additionally requires fixed `ninja`, `objdump`, `rc` (GNU windres) and
`publisher` paths. `localCpuProfile(root, windowsTools)` supplies the existing
Configure/Profile; `openProject` passes the same trusted context to BuildRunner.
These are not HTTP settings. Browsers cannot select commands, argv, environment
or shell. Without explicit Host setup, native Configure refuses to start. UNC
and device tool paths remain outside this qualification.

## Paths and capability boundaries

Launch selection converts current-host paths into existing `/`-separated project
identities before original `selectedPath` admission. Escapes, drive-relative
`C:relative` and Windows device paths are rejected. Windows inputs may use either
slash spelling. Linux/WSL paths retain their host identity; changing slashes does
not relocate a project or confer access.

| Route | Current scope |
| --- | --- |
| Native CPU CLI | Retains W1 evidence; no new complete CLI/science qualification |
| Process primitives | Native wide argv, streams, status, Job cleanup, parent lifetime and creation identity checked |
| Configure cancellation | Same state machine; actual native CMake work cancelled and reaped |
| Configure completion | Approved A1 path comparisons allow the actual ARCH production graph to configure and repeat with input SHA evidence; this is not complete compiler/link dependency evidence |
| Build | A2/A8 qualify Configure → ARCH Build → manifest → reopen with scoped recorded inputs current; full freshness stays unknown |
| Preview | A7/A9 use explicit Windows commit/user-CPU limits, strict capabilities and the existing Host session; Linux keeps its original resource contract |
| Run/Restart | A4a/A5 reuse the original workflow; terminal handoff, Host exit, output exclusion, process identity and Stop tested; real Sod Run/CHK Restart compared at zero thresholds |
| Save/source/Plotfile reads | Approved A3/A4b/A5 OS boundaries use original owners and h5wasm; A6 admits the existing exact Windows main-image label |

Job process-memory limits constrain committed memory, not Linux `RLIMIT_AS`.
A7 explicitly approves this Windows-specific meaning, together with user-mode
CPU time; it does not claim equivalence to Linux limits. Linux budgets, mathematics,
scientific thresholds, scientific schemas and external HDF libraries stay fixed.
Additional frozen I/O exceptions beyond A1–A9 still require review.

A1 shares one CMake path comparator: Windows normalizes separators/dot segments
and drive-letter case while directory/file case remains exact. Linux keeps raw
string equality. Actual CMake caches can write `c:` for `C:`. Repeat Configure
uses the same rule and still rejects foreign trees, generators and duplicate
binding keys. Fingerprints, read limits, missing files and escape checks remain.

## Validation and reproduction

Native execution used the official Windows x64 Node 24.21.0 portable archive,
checked against its published SHA-256, without changing the system PATH.
In PowerShell with the selected ABI DLL PATH:

```powershell
$node = 'C:/path/to/node.exe'
$env:ARCH_TEST_CMAKE = "$ucrt/bin/cmake.exe"
$env:ARCH_TEST_SUPERVISOR = "$build/arch_windows_process.exe"
& $node --test --test-concurrency=1 "$repo/studio/tests/host-process.test.ts" `
  "$repo/studio/tests/host-cmake-paths.test.ts"
if ($LASTEXITCODE -ne 0) { throw 'Native process checks failed' }
```

All 15 native checks passed without skips, covering actual Configure/repeat,
wrong bindings, SHA, size, version and traversal controls. Helper termination reaped descendants
while preserving an unrelated process; parent death reclaimed its Job. An
independent worker survived Host exit and reclaimed its own task on termination.
These were the initial primitive checks; current Run handoff evidence is below.

A1 Linux evidence/Configure/Build checks passed 33 cases without skips. Actual
production configuration recorded 577 inputs initially and 491 on each of two
cached configurations. The real sets and raw reply hashes are preserved rather
than forcing different phases into one identity. CMake/Ninja fingerprints were
also checked. TypeScript and changed-file lint passed. Real Electron windows, complete
GUI workflows, all filesystems and science models remain outside this evidence.
Zenodo and release remain deferred.

## A2 Build evidence and publication

The existing runner and manifest content owner remain shared. Fixed CMake/Ninja,
GNU drivers, selected linker, subprograms, specs and native DLL plugin retain
actual hashes. When CMake omits RC id/version, only the explicitly bound GNU
windres is probed with `--version`; its actual version and driver hash are kept.
Missing C/C++ identities still reject. GNUld depfiles support native drive paths
and separators without dropping Make escapes, target binding or ADS/expansion/
wildcard rejection.

With `ARCH_EMIT_LINK_DEPENDENCIES=ON`, GNU windres emits a real preprocessing
depfile, fixing the stock CMake rule's empty RC records. The unchanged Ninja
parser still rejects zero counts, missing entries and STALE records.
`ARCH_RETAIN_LTO_LINK_INPUTS=ON` retains actual LTO link inputs while keeping IPO
and numerical flags. The Host sets TMPDIR/TEMP/TMP to the build-owned
`.studio-link-inputs/<build-id>`. This GNUld depfile lists original IR objects/libraries but omits retained LTRANS
objects: retention does not establish their inclusion in the snapshot. Coverage
remains unknown. Deleting recorded inputs invalidates evidence; missing objects
are never omitted to claim a match.

ELF runtime v1 stays intact. Internal PE v2 records actual objdump, tools/helpers,
imports, ordered search inputs, candidates and before/after identities. System and
API-set boundaries, unparsed delay imports, dynamic loading and unmodeled loader
selection remain explicit unknowns with `dependenciesComplete=false`. This is
build provenance, not a science schema or exhaustive observed DLL loading.
Actual cc1plus `objdump -p` output was 35.6 MB, including unwind tables: PE has a
64 MiB per-inspection and 256 MiB total-output budget; ELF keeps its 1 MiB cap.
The graph retains 32 roots, 256 nodes, 8192 edges and 256 candidates per edge.

`arch_windows_build_publish` accepts at most 4 MiB of opaque manifest bytes from
the same content owner. It pins non-reparse ancestors, exclusively creates an
owner/SYSTEM-private temp, flushes and checks close, then atomically renames by
the retained source identity handle within the same directory. There is no
path-rename fallback. The Native API's simple-name/NULL-root contract is described
in [Microsoft's FILE_RENAME_INFORMATION reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information).
Tests use different CWDs explicitly. Held readers may deny replacement: failure
preserves the old complete file and removes only the owned temp. Postcommit
errors report committed rather than promising rollback. Power-loss durability,
complete ACL emulation, business CAS and races before initial pinning are not
claimed.

## A2 reproduction and qualification

With the native tools and selected DLL PATH above:

```powershell
$env:ARCH_TEST_NINJA = "$ucrt/bin/ninja.exe"
$env:ARCH_TEST_CXX = "$ucrt/bin/g++.exe"
$env:ARCH_TEST_RC = "$ucrt/bin/windres.exe"
$env:ARCH_TEST_OBJDUMP = "$ucrt/bin/objdump.exe"
$env:ARCH_TEST_PUBLISHER = "$build/arch_windows_build_publish.exe"
& $node --test --test-concurrency=1 "$repo/studio/tests/host-link-windows.test.ts" `
  "$repo/studio/tests/host-windows-runtime.test.ts" `
  "$repo/studio/tests/host-windows-build.test.ts"
if ($LASTEXITCODE -ne 0) { throw 'Native Build evidence checks failed' }
```

In a native test tree configured with `BUILD_TESTING=ON` as in the CPU guide:

```powershell
& "$ucrt/bin/cmake.exe" --build $build --target arch_windows_build_publish_test --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Publisher test build failed' }
& "$ucrt/bin/ctest.exe" --test-dir $build -R '^windows_build_manifest_publication$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Publisher controls failed' }
```

Executed evidence: 15 A1 native regressions passed; 11 A2 Node checks passed without
skips for actual Configure/Build/reopen, input changes/missing links, failed builds
preserving the old manifest, malformed evidence and wrong tool bindings. Thirteen
native publication controls passed. Two actual ARCH production Builds and reopen
produced validated records with 491 CMake inputs, 912 compiler input files, 48 PE
runtime nodes and zero missing link inputs. Matching records remain freshness-
unknown rather than upgrading incomplete system/loader evidence into readiness.
Linux same-scope checks passed 62 with 10 Windows-only skips; TypeScript/lint passed.
That A2 receipt does not reissue every scientific qualification; A7–A10 closure is
described below.

GCC 16.2 failed to reopen its resolution file under a Chinese retained-LTO temp
path. This phase qualifies ASCII source/build paths, including spaces, while
keeping LTO and missing-input checks. Unicode install/run paths retain W1's
bounded evidence; Unicode LTO builds are unqualified. Keep these build trees in
ASCII paths pending a toolchain solution. Current source/config/Plotfile and Run
qualification is below. A7–A10 Preview qualification follows; Zenodo and release remain deferred.


## A3–A6 operations in the existing owners

`windowsFilePublish.ts` shares the native OS operations in
`arch_windows_build_publish`; the historical target name stays. There is no
second config serializer or HDF library. `WindowsDirectoryPins.h` supplies the
same directory identity primitive to file operations and Run leases.

| Operation | Retained contract and native boundary |
| --- | --- |
| Source A3a | 256 KiB, strict UTF-8, original BOM/CRLF bytes, hashes and path admission; Windows checks opened/read/reopened file identity, Linux keeps `/proc` checks |
| Save/SaveAs A3b | Original parser and optimistic fingerprints; private exclusive temp, flush/checked close, same-directory publication by held handle; SaveAs never overwrites |
| Plotfile A4b | Original h5wasm, scientific parsing and 64 MiB cap; non-reparse directory pins and a read-sharing-only lease deny writes/replacement during parsing |
| Host wiring A5 | Trusted tool context passes through configuration and isolated/project metadata boundaries internally; no HTTP execution settings |
| Source label A6 | Adds only `Windows mapped main executable`; keeps the Linux label, 64 hex SHA, main-executable-only scope, null consistency, formal-record hashes and unknown-label rejection |

Config writes require a successful `windows-file-1` helper capability probe.
Save preserves the same-owner target's DACL ACEs and protection. Missing/NULL
DACLs, other owners, readonly and reparse targets reject conservatively. This is
not full security-descriptor/SACL emulation or business CAS. Kernel
delete-on-close covers prepared temps. The small disarm-to-rename window,
postcommit forced termination and power loss retain uncertain outcomes; no
universal crash cleanup/rollback promise is made. Postcommit errors require
readback or report committed. See [Microsoft's disposition extension](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information_ex).

The 15-second lease timer bounds acquisition only. A lease remains held until
parsing finishes and explicit release; a real 15.5-second hold still denied
writes. The isolated reader retains its 15-second wall, 64 KiB response and
256 MiB Node heap limits. Heap limits do not cap WASM or process RSS and do not
replace Core Preview budgets.

## Run, terminals and trusted startup

Output leases share Known Folder `LocalAppData/ARCHStudioRunLocks-v1` across
Hosts, independently of TEMP. Protected directories/files permit the current
user and SYSTEM. Lock files are not deleted to release ownership. Core inherits
only allowlisted leases; the helper drains its Job before a terminal state.
Recovery requires PID, creation FILETIME and liveness, not saved state alone.

The default native console requires a separate console and Job breakaway. An
outer Job that forbids breakaway causes explicit failure. The local WSL executor
reproduced Win32 5; actual handoff used installed Windows Terminal 1.24.12741.0
via a trusted absolute `windowsTools.terminal` path to `wt.exe`. Building helpers
does not require Terminal. `-w new new-tab` launches the original worker;
project/tool bootstrap travels as base64url while fixed executable/script paths
reject Terminal semicolon delimiters. See [Terminal command-line arguments](https://learn.microsoft.com/en-us/windows/terminal/command-line-arguments).

These settings belong to Host initialization, not project data, job.json or
HTTP. Current desktop/development CLI entry points do not automatically discover
these tools. Qualification requires explicit trusted Host setup and does not
establish ordinary Windows Electron startup. The existing API shares one recipe:

```typescript
const profiles = localCpuProfile(root, tools); // tools: WindowsHostTools
const reader = await openProject({
  project: root, buildProfile: profiles.build.id,
  trustedBuildProfile: profiles.build, configureProfile: profiles.configure,
  config: 'simulation/Sod/Sod_beginner.par',
});
```

Resolve `root` with `realpath` first. Supply the fixed ABI tools above and the
CPU guide's C/C++ compilers, dependency prefix and HighFive source through trusted
Configure definitions. Never derive executables from HTTP or PAR contents.
Unknown Build freshness still blocks current-Build claims; the actual Run checks
used an explicitly selected executable with a verified SHA.

## A3–A6 reproduction and evidence

With the previous tool variables and selected ABI DLL PATH:

```powershell
$env:ARCH_TEST_TERMINAL = "$env:LOCALAPPDATA/Microsoft/WindowsApps/wt.exe"
& $node --test --test-concurrency=1 --test-reporter=tap `
  "$repo/studio/tests/host-process.test.ts" "$repo/studio/tests/host-cmake-paths.test.ts" `
  "$repo/studio/tests/host-windows-run.test.ts" "$repo/studio/tests/host-windows-handoff.test.ts" `
  "$repo/studio/tests/host-source-stability.test.ts" "$repo/studio/tests/host-windows-config.test.ts" `
  "$repo/studio/tests/host-windows-plotfile.test.ts"
if ($LASTEXITCODE -ne 0) { throw 'Native platform checks failed' }
& $node --test --test-concurrency=1 "$repo/studio/tests/plotfile-windows-identity.test.ts"
if ($LASTEXITCODE -ne 0) { throw 'Platform source identity checks failed' }
& "$ucrt/bin/cmake.exe" --build $build --target arch_windows_build_publish_test --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'File operation test build failed' }
& "$ucrt/bin/ctest.exe" --test-dir $build `
  -R '^windows_(build_manifest_publication|config_and_read_operations)$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'File operation controls failed' }
```

The native platform group passed 43 checks, and Build/PE/link/A6 passed 12,
without skips. Two CTests passed 22 file-operation controls. Final Linux I/O/Run
passed 121 with five Windows-only skips; Build/Configure/entry checks passed 63
with nine skips. After the startup-release fix, 41 isolated-reader/HTTP/source checks
passed again. The shared Linux CMake graph and four Core/I/O CTests passed.
TypeScript/lint passed. In a native Unicode/space-containing
runtime directory, Sod Run and a step-1 CHK restart through step 8 succeeded.
The original comparator used four zero thresholds and returned max_abs=max_rel=0.
A6 read the already completed outputs without rerunning to hide the original
label rejection. This qualifies the engineering path, not every science model.

At the A3–A6 checkpoint, native `--inspect-case` still returned the Linux-only
budget error. A7–A10 below replace that blocker with the explicitly approved
Windows contract. Real Electron windows/installers, UNC, Unicode LTO builds,
other filesystems and the full scientific matrix remain outside the evidence.
Zenodo and release remain deferred.


## A7–A10 Preview resources and recorded-build admission

`WorkerLimits.cpp` remains the resource owner. Its Windows branch creates a
private nested resource Job inside the existing process-supervision Job. It
limits each process to **1024 MiB committed memory** and uses Job-wide **user-mode
CPU**. Reserving address space is permitted without committing it; this is not
`RLIMIT_AS`, RSS or working-set accounting. Child CPU would consume the same
resource Job allowance. No parent Job is modified, no breakaway is enabled and
closing the resource handle does not kill the current process. OpenMP remains
one thread for Preview. Configure, Build, Run and metadata workers do not acquire
these Preview budgets.

Sessions renew 300 user-CPU seconds for field/case requests and 30 for mesh and
resource queries. One-shot workers instead retain a cumulative process ceiling,
including startup, and repeated calls only tighten it. Windows periodically
checks CPU limits: native 1-second negative controls terminated with actual kernel
quota status after several seconds, so exact subsecond enforcement is not
promised. The existing 360/45-second Host wall limits, frames, sequence numbers,
queue bounds, cancellation and owned-process cleanup remain separate checks.
See [Microsoft Job limit semantics](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information).

Windows session, inspection and AMR capabilities identify the exact policy:

```json
{"kind":"windows-job-commit-user-v1","memoryKind":"committed","processCommitMiB":1024,"cpuClock":"user","cpuScope":"resource-job","budgetRenewal":"per-request"}
```

They omit `processAddressSpaceMiB`. Host refuses absent, unknown or weakened
Windows policies. The pure predicate is shared from the existing
`workflowValidation.ts`; A9 changes only registry platform metadata admission.
A10 updates only the Core registry platform macro and per-case platform/policy
metadata. Source digests, reviewed unit maps and all scientific data validation
remain in their original owners. The small value builder is inline in the existing
PreviewSession header, so standalone inspection probes do not link session runtime.
Linux payloads retain their existing form.

A8 adds optional `observedInputs` to the transient `BuildSnapshot`, with
`scope:"observed-build-inputs-v1"` and `state:"current"|"changed"|"unknown"`.
Trusted Windows CPU profiles opt in; Linux and existing profiles do not acquire
it automatically. `BuildRunner` alone derives it from the existing fingerprints,
configuration/compiler/link/runtime evidence and strict stability/error gates.
GNU windres keeps its exact RC identity. Preview calls the same owner's
`requireBuildEvidence`, then rechecks the still-current state after capability
negotiation. Concurrent publication of unknown evidence cannot publish readiness.

**Observed current does not mean complete dependency freshness.**
`binaryState:"freshness-unknown"`, `dependenciesComplete:false`, all recorded
system/API-set boundary edges and dynamic-loader/LTO limitations remain. Every
recorded physical candidate is still hashed. Missing/changed evidence or an
unparsed delay import blocks scoped readiness. No manifest version, persisted
schema, completeness flag or BinaryBuildState token is added. Old profiles and
manifests retain their compatibility behavior; enabling the new scope changes
the existing profile fingerprint and requires a matching successful Build.

Build ARCH and its existing helpers with the same ABI toolchain and existing
IPO/LTO options. No new runtime library or alternative scientific serializer is
required. The standalone native resource harness is built with:

```powershell
Push-Location $repo
try {
  & "$ucrt/bin/g++.exe" -std=c++20 -fopenmp -O2 -Wall -Wextra -Wpedantic -Werror `
    -Isrc tests/api/preview/test_windows_worker_limits.cpp src/api/resources/WorkerLimits.cpp `
    -o "$build/windows-worker-limits.exe"
  if ($LASTEXITCODE -ne 0) { throw 'Resource harness build failed' }
  foreach ($mode in @('info','startup-info','reserve2gib','commit-over-1gib','single-info',
      'renewal','tighter-parent-memory','cpu-over','tighter-parent-cpu')) {
    & "$build/windows-worker-limits.exe" $mode
    if ($LASTEXITCODE -ne 0) { throw "Resource check failed: $mode" }
  }
  & $node --test --test-concurrency=1 studio/tests/build-observed-contract.test.ts `
    studio/tests/host-preview-windows-platform.test.ts
  if ($LASTEXITCODE -ne 0) { throw 'Metadata/platform contract checks failed' }
} finally { Pop-Location }
```

Use the selected ABI DLL PATH and `ARCH_TEST_SUPERVISOR` above. Resource checks
include actual CPU termination, twelve renewed requests, commit refusal,
reserve/commit distinction and stricter parent CPU/memory. They do not allocate
or touch a multi-gigabyte data array. Real production Build evidence is checked
independently from these small primitives; successful compilation alone does not
qualify Preview, GUI packaging or every physical model.


A7–A10 native acceptance includes the final production Build/reopen, nine resource
modes, twelve recorded-evidence/concurrency controls and an unchanged standalone
inspection probe. The final native Host HTTP path passed registry, field Preview,
case inspection, resource estimates and initial AMR. Three Sod edits matched
one-shot Core data exactly in the persistent session. Native platform regressions
passed 56 checks with one Linux-only skip; the additional lifecycle/metadata group
passed 14 with one skip (groups overlap). Linux Host groups passed 67/61 checks
with 9/4 Windows-only skips, and five focused Core CTests passed. This is bounded
Host/initialization evidence, not a complete science-model or Electron qualification.
The final production record retained all 552 runtime boundary edges; independent
hashing checked 1,313 recorded file identities without mismatch.

One first capability query after rebuilding timed out at the retained 3-second
limit while independent file rereads ran concurrently. It failed closed; the
uncontended repeat passed under the same limit. The evidence retains that failure
and does not promise startup below 3 seconds on arbitrary systems or load.
