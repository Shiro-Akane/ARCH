# Compiled case inspection

`ARCH --inspect-case CASE --config-stdin [--request-id ID]` is the common CPU inspection entry for registered cases. It accepts unsaved `.par` text, calls the real `ProblemGenerator::Setup`, and probes the real `Init` output at a small number of points. It does not allocate a simulation grid, take time steps, or call the scientific output writer. Setup may load an EOS table. Run in a separate Linux/WSL worker, with a 1 GiB address-space / 300 CPU-second limit; Host must enforce a 360-second wall timeout and cancellation/reaping.

Setup time depends on the model; the budget is a ceiling, not a predicted duration. In CPU inspection and initial-preview commands, repeated EOS queries reuse one verified source generation. Initial table loading keeps its full before/after content checks; before publishing success, every consulted source is verified by full content again. A changed source rejects the result (`EOS_SOURCE_CHANGED`). This avoids rehashing the entire table for every iteration of CooperativeHotspots' initialization solve. The optional [preview session](PREVIEW_SESSION_API.md) retains parsed tables and bounded source bytes across requests; exact full-byte comparisons can reuse SHA-256 digests. Setup and Init are still rerun. Ordinary simulation dispatch retains its per-query content checks. AMR workers retain their separate 30 CPU-second / 45 wall-second limits. Host should display Setup progress and keep cancellation available.

This command is independent of `--preview` and `--preview-amr`. Discover all three capabilities via `--list-cases`; registration alone does not enable field or mesh rendering. User-written Setup/Init is executable C++, so the resource guard is not a filesystem or security sandbox.

```sh
ARCH --list-cases
ARCH --inspect-case Sod --config-stdin --request-id inspect-001 < simulation/Sod/Sod.par
ARCH --inspect-case CellularDet --config-stdin < simulation/Cellular/CellularPreview2D.par
```

## Response

The envelope uses `schemaVersion: "1.0"`, `version: "1"`, `kind: "case-inspection"`. Identity contains request ID, registered case ID and SHA-256 of the exact input text. Host separately retains binary/build identity and paths. Limits remain 1 MiB input and 8 MiB response. Empty input checks Core defaults; defaults may not be appropriate for the selected case.

| Field | Meaning |
|---|---|
| capability | Compiled case source path and SHA-256, Setup/read and point-probe availability, current/stale reviewed unit evidence |
| state | Parsed/effective grid, coordinates, AMR, diffusion, registered species; EOS is `setup-managed-not-inspected`, not certified ready |
| parameterMetadata.parameters | Observed input key, actual requested type, default/explicit/effective values, source, raw token, read count, unit and evidence |
| parameterMetadata.unobservedInputKeys | Custom keys not observed during this request; does not prove unused or mismatched input |
| data | `initial-primitive-probe`, 3 / 9 / 27 raw Init samples for 1D / 2D / 3D |
| diagnostics | Structured failure and captured Core messages; partial read metadata retained on failure |

Samples use the tensor product of quarter, midpoint and three-quarter positions on active native axes, with Core `Grid::PhysicalCoordsFromNative` supplying inactive coordinates and Cartesian positions. Velocity components remain native orthonormal components. Each sample includes density, pressure, temperature, three velocities and mass fractions. `thermodynamicInput` and `consumedByConversion` distinguish selected pressure/temperature input from an unused field. No EOS field reconstruction is performed by this command. The samples are a check of Init writes, not a complete field, cell average or AMR rendering input.

`status=ok` means this Setup and the available sample probe finished; it does not certify the entire domain or simulation. Mesh-only custom registrations can finish Setup with `initializationProbe=unavailable` and `data=null`.

Exit codes: 0 finished; 2 invalid CLI/UTF-8/size; 3 configuration error; 4 unregistered/restart unsupported; 5 Setup failure; 6 Init sample failure; 7 response too large. Source consistency failure uses code 6 and stage `source-validation`. A killed worker may have no JSON; Host must handle timeout, signal, cancellation and missing output separately.

## Units and observation coverage

The probe records executed `SimConfig::Get` / `GetCustomParam` reads. It also records the Core Timmes composition reader, which accesses the numeric map directly. Composition input evidence uses `valueStage=input-before-floor-and-normalization` and describes the contribution before the existing floor and normalization; the sample's mass fractions are the resulting output. Repeated incompatible reads remain ambiguous. Fractional, exponent/decimal-form or out-of-range tokens requested as integer are rejected before conversion in inspection mode; normal production reads retain their existing behavior.

`ProblemGenerator::InspectSetup` and `InspectInitialPrimitive` are the interception boundary. The observer is opt-in and per request. The production grid initialization method and time stepping do not install it. C++ branches not executed by these samples and arbitrary direct map accesses inside user models are not traced. Raw doubles do not retain arithmetic or comparison provenance.

Units have two concrete evidence sources:

- **Core composition reader:** mass-fraction input is dimensionless, reported as `1`.
- **Reviewed built-in case expressions:** centralized in `CaseUnitEvidence.cpp`, guarded by the exact compiled case `.cpp` SHA-256. CMake stamps the source at compilation; editing it invalidates the old unit conclusions. Core must review changed expressions before updating the expected hash. Header dependencies and build options remain part of Host's full build manifest; a case-source hash alone is not full build freshness.

The 14 current built-in models have reviewed units for their numeric inputs: Sod, CellularDet, Gaussian, Sedov, RT, SmoothAdvection, GravityBox, JeansWave, ExternalGravity, DiffusionMode, BurnOneZone, BurnGradient, CooperativeHotspots, SNIaCoupled. Returned parameters are still only those actually read in the current configuration. Examples: `x_pos`/`radiusPerturb` use cm; CellularDet `noiseAmplitude` is dimensionless; RT `amplitude` uses cm/s; Sedov `explosion_energy` uses erg/cm², erg/cm or erg for 1D, 2D or 3D.

`unitEvidence.status`: `known`, `dimensionless`, `not-applicable`, `uncovered`, `conflict`. `unit=null` for uncovered/conflicting inputs. Strings and switches need no unit label. `automaticExpressionInference=false`: reviewed evidence must not be presented as automatic inference for arbitrary C++. A regression demonstrates identical read/sink observations from `rho=a` and `rho=a*b` with `b=1`; boundary observation cannot uniquely determine the two input units. New/changed cases remain inspectable and report evidence coverage; they do not require a frontend-specific adapter or a user declaration form.

Graphical bindings are separate. This inspection response has no draggable markers; the existing Sod marker still comes from `--preview`. Units alone do not establish position/radius/shape relationships.

## Rendering support

Initial field responses add `fields[].logDomain` with `positiveCount`, `zeroCount`, `negativeCount`, `nonFiniteCount`, `canLog`, `minPositive`, `maxPositive`. The probe uses the same statistics for consumed sinks in `data.logDomains`. Source values are unchanged. The field counts describe the complete returned sample array; Studio recomputes for a selected viewport if needed. Axis Log validity uses the returned axis coordinates independently. No positive values means an empty Log view, not a negative-value configuration error.

## Maintenance and validation

| Owner | Responsibility |
|---|---|
| ApplicationContract.h | CLI names, response kinds/versions and worker budgets |
| StateSnapshot.cpp | Shared Core state serialization for field/mesh/inspection responses |
| ParameterMetadata.cpp | One serializer for observed input values and unit evidence |
| CaseUnitEvidence.cpp | Source-guarded reviewed model units; no parser, copied defaults or numerical model |
| ResourceEstimates.cpp / InitialMesh.h | Allocation-free level estimates / real bounded initial hierarchy |
| WorkerLimits.cpp | Shared Linux worker resource guard |
| ../core/files/InspectionSources.h / EOSDispatcher::InspectionScope | One verified EOS source generation per inspection request, with final content verification |
| ValueDomain.h | Zero/negative/nonfinite separation for Log presentation |

`case_inspection_contract` checks all compiled built-in cases, source stamps, numeric unit coverage, composition reads, defaults/explicit values, 1D/2D/3D Sedov units, curvilinear coordinate conversion, strict integers, partial errors and no scientific output. `initialization_probe` checks the ProblemGenerator boundary, observer restoration, ambiguous dimensional inference, stale evidence, unit conflicts and Log statistics. Existing field, configuration and AMR regression groups remain required.
