# Phase 2G-A Continuous Preview Session — verification record

Status: A implementation and acceptance checks PASS. Local checkpoint: studio-phase2g-a-v0.14.0 (resolve the tag for its commit). STOP before B.
Active target: PHASE2G_TARGET.md, A1–A17 only. B/C are not authorized.

## Baseline and integration
Frozen Phase 2F: e97e571ba98641384aee44425a29b83255401856.
Independent project: /home/arch/projects/ARCH-phase2g-continuous-local-workflow.
Branch: studio/phase2g-continuous-local-workflow.
Approved Core patches 97a2b50c then d98f6f6e were integrated in order as e75d2391 and 1c8e659c. No main merge or repeated historical A/B/UI patches.
Fresh CPU Debug binary: build-preview-audit/bin/ARCH; CUDA disabled.
Host-owned profile tracks 75 explicit inputs including session/API/discovery/AMR/EOS/cache changes. dependenciesComplete remains false. No Studio numerical compensation for the authoritative CGS Cv change.

## Automated evidence
Final logs in ignored studio/.local/final-studio.log and final-core.log:
- npm test: 146/146 PASS.
- npm run test:host: 62/62 PASS.
- lint, typecheck, production build, git diff --check: PASS.
- Core: all 18 required scoped groups PASS, 126.43 seconds. ARCH_PREVIEW_SIMULATION_ORACLE=0; no simulation, CUDA baseline or GPU tests.
- Build emits the existing large-bundle warning; this phase does not restructure dependencies.

Core groups: preview_session_contract, preview_verified_resources, preview_exact_sample_cache, tabular_eos_ideal_gas, preview_initial_conversion, preview_api_contract, configuration_api_contract, preview_parameter_reads, preview_parameter_metadata, preview_sampling_limits, preview_cellular_2d, mainline_authority, ui_expansion_contract, refinement_indicator_math, amr_operation_plans, topology_transaction, initialization_probe, case_inspection_contract.

## Implementation evidence against A1–A14
- A1: capability opt-in for Linux session v1; legacy single-shot path retained and covered by original Host regression.
- A2: Host process token/generation binds project, cwd, executable path/SHA, Build ID/profile. Build retires and reaps idle session before compiling.
- A3–A5: fixed --preview-session spawn, shell=false, approved cwd/env; bounded UTF-8 LF frames and continuously drained bounded stderr; ready v1 sequence0 handshake. Generic transport supports all contract commands, but this phase's UI only integrates field Preview.
- A6–A7: readable stages without percentages; complete progress is not success. Only validated final envelope plus existing scientific response validation can publish; request/config/case/sequence/generation/build identity checked.
- A8: 300 ms edit debounce, one active and one replaceable latest pending. Queue integration test executes initial/A/latest only, proves intermediate requests absent and obsolete A never replaces saved result.
- A9: known numeric intermediate input pauses automatic Preview. Real Sod '-' UAT retained prior success/sequence; correcting to .35 resumed. Unknown model types do not receive invented numeric constraints.
- A10: marker candidate remains local; release edits Working Copy once. Display-only transforms excluded from scheduling key.
- A11: old field/Inspector identity retained during updates and failure; matched result replaces field, metadata, state and provenance together.
- A12: cancellation discards pending and terminates process group, TERM then bounded KILL, waits for reap; new generation cannot reuse late stdout. Capability wall budgets are absolute, not extended by progress.
- A13: request-limit normal recycle covered by transport test; next request creates reusable new process.
- A14: outer resource/stage/timing diagnostics exposed without claiming field-result reuse. Every measured field result reports resultReused=false.

Transport tests also cover fragmented UTF-8, malformed/oversized/truncated/wrong-identity frames, progress without final, timeout and TERM-resistant child, cancellation, recoverable Core errors and recycling. Queue tests cover changed Build preventing queued execution and session retirement before Build.

## Performance UAT (production React + real CPU Core)
All values milliseconds, single observed runs, not performance promises. UI wall measures generate dispatch to double-animation-frame publication of Current. It includes Host validation, request/response and polling/render overhead; it excludes preceding form inspection and the 300 ms edit debounce. Core elapsed is not UI latency. Parse measures framed JSON parsing rather than all HTTP/network overhead.

| Request | Host queue | Host elapsed | Core elapsed | Parse | Render to Current | UI wall |
|---|---:|---:|---:|---:|---:|---:|
| Sod512 cold, final measurement | 0.004 | 557.662 | 6.111 | 0.204 | 7.0 | 1107.8 |
| Sod warm .35 | 0.002 | 526.422 | 6.072 | 0.184 | 8.9 | 1027.9 |
| Cellular128x128 first EOS load | 0.003 | 3529.154 | 2939.966 | 3.712 | 18.9 | 4273.3 |
| Cellular position 6.4 to7.0 | 0.002 | 804.745 | 196.388 | 2.972 | 11.4 | 1183.0 |
| Cellular temperature2.0e8 to2.1e8 | 0.002 | 803.368 | 192.359 | 3.279 | 11.4 | 1592.9 |

Cellular cold tableLoads=1, tableHits=2, retainedFileBytes=60242514, Init calls=16384. Warm edits tableLoads=0/tableHits=3 but still Init calls=16384. Position edit reports fileContentMatches=2/fileHashes=0. These are verified-resource reuse statistics, not reuse of the previous field result.
Sod fast .30/.31/.32 edits collapsed to final .32; same process sequence advanced1 to2. The controlled Host queue test additionally proves one active plus one latest pending under overlapping computation.

## Production desktop regression evidence
Production URL http://127.0.0.1:4189/, separate Host127.0.0.1:4180; no dev server. Desktop1280x720 and1920x1080 inspected; controls accessible via panel scrolling. No mobile feature work.

- Real Cellular128x128,256x256 and nonsquare64x32; shock_dir0/1. Sample500 maps i52,j7,x1=21,x2=3, with raw VELY and VELX consistent with selected orientation.
- Spatial and field Log controls remain independent; zero-domain Log gives explicit warning; returning Linear restores display. Zoom/pan/Fit preserve selected raw sample and restore full domain.
- shock_dir2 returns explicit unsupported error while old64x32 image and Inspector remain; correction0 recovers Current.
- Strong cancellation test: clicked Cancel during actual256x256 setup, confirmed owned PID1455 gone, retained prior64x32 image. Next valid edit started new session and reached Current.
- Save As wrote studio/.local/Cellular-UAT.par with SHA256 0082136c26ed7356f7d06246f722e1a6631b0902a1b5bed2648c078e3700710e. Explicit Save of temperature2.3e8 wrote SHA256 5f3d25da0ce576d9e93c13644be81ac83e1d1e33a4583c95649308b0779eafd3. Reloaded Saved state. Core reference config untouched.
- Actual controlled Build succeeded2026-09-22T06:38:30.646Z, Build ID prefix eaea1d3d. Prior idle Preview PID3656 disappeared; old image marked build-changed/unverified and Build did not auto Preview.
- Mock manual Generate produces Current. Archived Cellular real-result workspace loads its existing 736-cell profile.
- Real Plotfile imported existing tests/fixtures/sod-1d.h5; parsed time0.15, Cartesian1D and DENS/ENER/PRES/VELX; DENS displays64 samples, min0.125/max1.
- Host path preflight for eos_table_path reports actual managed cwd and readable Helmholtz table. Missing studio/.local/missing-uat-table.dat reports Input file does not exist; old image retained. Restoring original path recovers Current without saving.
- Parameter catalog shows90 keys/89 alias-shared controls; omitted gamma/regrid_interval remain Schema Default, not written. Inspector separates Working Copy/inspection/model effective sources.
- Sod actual drag .5 to .602112676056338 produced one edit and automatic successful sequence5, Init calls512, resultReused=false. One Ctrl+Z restored .5/Saved; automatic sequence6 restored Current and matching metadata revision cd54c4000a8bd3917f5b3fb658c2614f91bddc7ebd84e7e076f6c3bb79489284. No Save was invoked.

## Checkpoint audit and STOP
- Cold Sod remeasurement uses fresh processToken a721b8dc-c5f0-4bc3-ba76-826ed7538e97, generation5, sequence1, retainedTables0, Init calls512. Full timing columns above are measured. The initial cold run is superseded.
- A15 timing boundary is dispatch-to-Current. It does not claim keystroke-to-Current latency or count Core elapsed as UI latency; debounce and inspection precede dispatch.
- A16 combines real fast-edit/cancellation UAT with instrumented Host overlap tests; no claim that the short Sod browser run alone exercises every pending-queue race.
- A17 full automated regressions and targeted real production desktop interactions passed. This report describes agent-operated UAT, not independent human approval.
- No uncommitted changes in scientific Core, simulation, CMake or root STATUS. Only the two explicitly authorized Core increments precede the Studio A commit.
- node_modules, dist, .local and UAT temporary files excluded from Git. Existing Phase2F tag remains unchanged.
- Known limits: explicit dependency list is not complete; binary freshness remains truthfully unknown beyond tracked inputs. CPU Debug measurements are single-run local observations. Existing production bundle-size warning remains.
- No B case-discovery/AMR UI, desktop wrapper or Phase3. No automatic push. STOP pending user authorization for B.
