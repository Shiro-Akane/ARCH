> Historical M0 gap at Phase2C checkpoint. Superseded for the isolated integration by API commit4c0fd5c and PHASE2D_UPSTREAM_PREVIEW_API_AUDIT.md (Stop Gate YES). Original managed CUDA binary still has no Preview API.

# Phase 2D Core Preview Contract Gap — M0 STOP

## v2 target revalidation
The active target is now ARCH_STUDIO_PHASE2D_REAL_IC_PREVIEW_TARGET_v2.md, copied verbatim to PHASE2D_TARGET.md. Full reading and comparison found only wording changes in sections 33 (Diagnostics) and 35 (Metadata unavailable); no new Preview contract, Core approval or change to the M0 Stop Gate was introduced.

Git was checked again: HEAD and studio-phase2c-v0.7.0 both resolve to 332af5675768123cf23fbdf5e3dc3cb6f44a327d. The sealed Phase 2C worktree remains clean. The Phase 2D branch retains the existing documentation-only changes; they were not discarded. Core/CMake/simulation inputs in the audit and managed worktrees remain unchanged against the checkpoint, and no untracked source/helper files were found under src/simulation/cmake/tests/tools. Prior source call-path evidence below remains applicable; no baseline or executable was rerun.

Decision remains M0 STOP. The target is a request to audit and gate implementation, not approval of the proposed thin bridge. M1-M10 remain unstarted; protocol 1.2 and preview=false are unchanged. Await an explicitly approved authoritative init-only contract.

## Decision and baseline
M0 Stop Gate is triggered. No callable approved authoritative init-only Preview interface was found in the audited checkout or existing managed project. Finding C++ Setup/Init implementation is not evidence of a Preview API. Do not proceed to P2D-M1–M10 until Core approves and supplies the missing contract.

- Verified base: studio-phase2c-v0.7.0 -> 332af5675768123cf23fbdf5e3dc3cb6f44a327d.
- Phase 2C branch/worktree was clean before creating this independent documentation-only branch.
- Audit branch: studio/phase2d-real-ic-preview.
- Studio worktree: /home/arch/projects/ARCH-phase2d-real-ic-preview.
- Managed source root remains /home/arch/projects/ARCH-linux, distinct from Studio.
- Managed source/Core/CMake/simulation files were compared with the Phase 2C checkpoint with no differences; unrelated existing Studio edits remain untouched.
- Existing binary: /home/arch/projects/ARCH-linux/build-cuda/bin/ARCH. No binary or helper was executed for this audit; no configure/build/test/simulation was run.

## Current initialization call path
Source references below are relative to the audited repository and use actual line numbers.

1. src/main.cpp:39–58: CLI accepts `ARCH <ProblemType> <ParFile>` and loads a filename through RuntimeParams::Load.
2. src/main.cpp:63–69: creates output/log directories and initializes a persistent log before case initialization.
3. src/main.cpp:81: ProblemRegistry::Get().Create(problem_type).
4. src/core/ProblemRegistry.h:42 and :52: registry supports in-process Register/Create, not a CLI Preview/metadata service.
5. simulation/Sod/Sod.cpp:72 and src/core/UserInterface.h:51–58: Sod registration constructs TypedProblemGenerator<SodProblem>.
6. src/main.cpp:99 -> src/interface/GenericProblem.h:82–84 -> SodProblem::Setup (simulation/Sod/Sod.cpp:28). Actual setup validates dimension/geometry and case values and configures species.
7. src/main.cpp:116 enters DispatchSolver. src/driver/SolverDispatch.cpp:257 writes the backend sidecar; :289 creates the root grid; :291 invokes problem.InitializeData.
8. src/interface/GenericProblem.h:87–94 invokes ProblemHelper::detail::PopulateState and the actual user_model.Init callback (Sod.cpp:60). Shared helpers own block traversal/EOS conversion; they are not a standalone sampled-output API.
9. src/driver/SolverDispatch.cpp:329–342 selects the time-integrator dispatcher. src/driver/Driver.h:903–906 contains the simulation timestep loop; :810–816 defines checkpoint writing and :915 onward schedules runtime output.

ProblemGenerator.h:45–56 exposes Setup and InitializeData(AMRControl, SimConfig, SpeciesManager, context). It does not expose a uniform point-sampling endpoint or machine-readable Preview response. TypedProblemGenerator keeps user_model private; Studio cannot obtain a production point sampler merely by knowing Sod has Init.

## Why the current executable cannot safely provide init-only preview
The CLI has no branch for preview/init-only/sample-only, no stdin config mode and no return-after-initialization contract. After Setup it proceeds into dispatch, where initialization is coupled to normal grid/backend startup and driver execution. Output directory/log/sidecar side effects occur before any hypothetical timestep cutoff.

Using tmax=0, killing a run after startup, reading initial Plotfiles, or stopping after one step would still use the simulation workflow and would not meet the target's no-simulation/no-output guarantee. None was attempted.

The audit covered main/registry/interfaces/shared helpers, driver/dispatch, CMake application and Host/CUDA test target registrations, tests, tools and docs. Searches for init-only/init_only/preview/config-stdin found no Core entry point. Existing standalone tests exercise numerical leaves/backend contracts; no registered helper exposing selected Sod + current config + sampled structured response was identified. The configured bin directory contains ARCH. Test helpers or C++ callbacks are not automatically authorized production interfaces.

## Existing data and transport limits
RuntimeParams.h:117–120 takes a filename. No documented stdin/pipe contract exists. A future approved helper could accept a private temporary config path, but passing one to the present ARCH binary still starts simulation and is not acceptable.

UserTypes.h:22–32 defines internal primitive rho, velocity components, pressure, optional temperature and mass fractions. These are C++ scientific values, not an external fields/units/coordinates schema. The current entry point and Sod Setup write human-readable stdout. No versioned sample payload, field enumeration/unit contract, request/revision identity, or init-only cancellation boundary was found. Existing plotfile metadata must not be repurposed as proof of a Preview contract.

## Minimum Core/local interface required — proposal, not approval
Core must approve an init-only CLI mode or dedicated thin helper linked to the same registered case and authoritative Setup/Init implementation. It must:

- Load the exact submitted config using the existing Core parser and validation.
- Resolve the actual registered Sod case, run shared Setup and authoritative initialization/sampling without copying physics.
- Define 1D coordinate locations (e.g. uniform cell centers versus endpoints), domain policy, EOS/composition conversion and returned field semantics.
- Return density and pressure under Core-declared names; optional additional fields only when genuinely supplied. Unknown units/metadata remain absent/null.
- Stop before simulation driver/timestep entry; prohibit output directory, logger, plotfile, checkpoint and sidecar creation for this mode.
- Reject unsupported dimension/case/config and provide structured diagnostics. No fallback to simulation.
- Provide a bounded machine-readable output channel: structured stdout only with diagnostics on stderr, or a private structured result file. Existing human logs must not be mixed with JSON or scraped by regex.
- Supply build/registration evidence for the helper and establish its relationship to the managed ARCH build. A separately built helper needs its own fingerprint/build provenance; it cannot silently inherit ARCH binary provenance.
- Include preview-only contract tests proving shared initialization use, no timestep, no scientific outputs, failure/cancellation cleanup and bounded response.

No implementation of this proposed bridge is authorized or included in this report.

## Suggested request fields
Browser -> Host (subject to Core contract approval):

```json
{
  "projectId": "session-id",
  "profileId": "host-owned-sod-preview-profile",
  "configText": "exact serialized unsaved Working Copy",
  "configRevision": "sha256-of-exact-UTF8-config",
  "requestedSampleCount": 512
}
```

Host validates the revision against bytes, assigns requestId, validates last successful Build readiness, resolves fixed caseId=Sod and an approved program/argv/environment. Browser cannot choose paths, commands, output/temp filenames or process IDs. Default 512/max 4096 samples and <=1 MiB config are proposed bounds, not an existing Core guarantee.

Prefer stdin/pipe input. If the approved Core runner is filename-only, Host uses a unique private request directory and no-follow config file, never the project .par; clean it on success/failure/cancel. This is not Save and must not alter the current config association.

## Suggested response schema
A proposed structured envelope (not an existing ARCH API):

```text
schemaVersion
identity {
  requestId, projectId, profileId, caseId,
  configRevision, buildId, binarySha256,
  runnerSha256 (if a separate helper is used)
}
generatedAt
dimension: 1
coordinate { name, unit: string|null, values: number[] }
sampling { kind, count, valueLocation: "init-sample" }
fields[] { key, displayName?, unit: string|null, values[], min, max }
diagnostics[] { severity, code?, message, parameterKey? }
```

Host/adapter must validate schema, all identities, finite/monotonic coordinates, equal lengths, duplicate keys, finite values and consistent min/max; proposed max response 8 MiB with bounded field count/logs. These values are init samples, not simulation cells or a theoretical Sod solution certification.

## Required provenance and readiness
Bind each request/result to exact config bytes/revision, request ID, selected case/profile, last successful Build ID and actual binary fingerprint. Preserve managed source root, selected source/tracked fingerprints and profile fingerprint from Phase 2C. If a separate helper is approved, establish and validate helper provenance too.

Current Phase 2C mapping is configured and full dependency freshness is unknown. Neither becomes verified merely because a preview succeeds. Require matching binary/profile/known tracked inputs and a successful manifest; changed tracked inputs disable Preview pending Build. Repository dirty alone must not disable it. UI must disclose use of the last successful tracked build with incomplete dependency authority.

## Cancellation requirements
One Host-owned Preview at a time. Only its request ID can be cancelled; no arbitrary PID API. Use a controlled Linux process group, graceful termination followed by bounded forced termination if necessary, bounded timeout (proposed 30–60 seconds pending Core costs) and deterministic private-temp cleanup. Core must confirm shutdown does not publish partial samples as success or write scientific output.

Frontend revision/build checks must invalidate old results even if cancellation races completion. Edits during generation, failed/cancelled requests and build changes retain the last successful Preview as stale; they never clear or save the Working Copy. A cancelled result cannot become current. These are requirements for later M1–M10, not implemented behavior.

## No-physics-duplication constraint
Do not port Sod equations to TypeScript, parse source to emulate Init, promote Mock/test providers, reconstruct IC from Plotfiles or copy Setup/Init into a second implementation. An approved bridge must call/link the same Core-owned scientific implementation and preserve its ownership. Core must choose the minimal safe sampling seam; Studio will only transport, validate, retain provenance and render returned samples.

## Stop status / next decision
P2D-M0 audit complete: STOP GATE. Await Core approval of the minimal init-only contract and its implementation ownership. M1–M10 were not started. Protocol remains 1.2 and preview capability remains false. No scientific Core, CMake, parser, frontend or Host implementation was changed. No Phase 2D success tag, build, simulation, scientific output or automatic push.

Only this gap report, the copied target and an audit STATUS entry are added in the independent Phase 2D worktree. No full regression was rerun for documentation-only changes; git diff --check is the relevant check. This is an audit deliverable, not Phase 2D completion.
