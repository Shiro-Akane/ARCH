# Phase 2D — Real initial-condition Preview completion report

Implementation and scoped agent UAT completed. This is not a claim of scientific-theory validation or project-owner approval.

## Baseline and upstream integration
Phase2C checkpoint studio-phase2c-v0.7.0 /332af5675768123cf23fbdf5e3dc3cb6f44a327d.
Independent integration branch studio/phase2d-api-integration, worktree /home/arch/projects/ARCH-phase2d-api-integration. Original Phase2D worktree and dirty managed /home/arch/projects/ARCH-linux were preserved. No main merge/rebase/reset/stash.
Fetched origin/review/studio-v0.4.2 matches API commit4c0fd5c1242d9a48d2a75d7a2c546abb34f83627; parent and requested merge-base defc0ff7ccbf5304b63e90cd4792575f49a74006. No-commit cherry-pick conflicted only in studio/README.md; current Studio documentation retained and API explanation transplanted. Core/API/CMake/scientific sources exactly match approved upstream; no local scientific rewrite.

## M0 authoritative contract — Stop Gate YES
See PHASE2D_UPSTREAM_PREVIEW_API_AUDIT.md. Unified ARCH CLI --preview branches before output/log/backend/Driver setup. RuntimeParams::LoadText uses the same parser. Registry Sod -> Setup -> SampleInitialPrimitive -> actual Sod Init -> shared InitialConservedState and EOS. No frontend physics, no Plotfile-derived initial state.
Core schema1.0, CPU, Sod1D Cartesian, stdin rawUTF8<=1MiB, sample count2..4096/default512, exact-byte configRevision SHA256. Fields DENS/PRES/TEMP/VELX/ENER/EINT from Core; null units stay unavailable. Structured bounded diagnostics/errors. CPU initialization works independently of requested simulation CUDA backend.
Core CTest preview_initial_conversion PASS. preview_api_contract10 PASS,1 explicitly skipped: optional ordinary simulation/Plotfile comparison. No simulation was run. README and actual call paths agree for required Sod path.

## M1–M4 Host and provenance
Host protocol1.3, separate from Core schema1.0. New fixed arch-preview-cpu-integration Build Profile points to this integration root/build-preview-audit/bin/ARCH, targetARCH, CPU Debug, CUDA/KLU OFF, fixed parallelism4. Original arch-existing-cuda-release untouched and does not advertise Preview. No browser-controlled executable/args/cwd/env/PID, no generic exec/configure route.
Preview Profile sod-initial-cpu invokes only --preview Sod --config-stdin --samples <bounded count> --request-id <Host UUID>. Exact unsaved serializer text goes to stdin; no temp config or implicit Save. Request/project/case/config/profile/build ID and executable SHA256 are carried and checked.
New successful Manifest8a36507b-4edb-4aca-861c-fa492bd076e9 includes25 explicit inputs, source root/Git HEAD/repository dirty, profile fingerprint, binary before/after, timestamps and tracked stability. Actual build Git HEAD was332af567 plus integrated uncommitted API inputs; hashes record that tree faithfully, not a claim that HEAD alone contains API. Executable SHA25671d39b59f04400739a3b9d3027c4d0db2e403dd50f4315e31e1844f8c683537d,44512624bytes. Standard Host no-op Build captured unchanged before/after input and binary fingerprints after the focused CPU compile.
Readiness requires matching successful Manifest/profile/inputs/binary and inactive Build. Full dependency completeness remains unknown; UI discloses that it uses last successful tracked build. Mapping is configured, not a general Core registry-verification claim. Old Phase2C11-input Manifest cannot stand for new API binary. Additional API/interface/parser/conversion inputs are tracked.
One active Preview, Build/Preview mutual exclusion. Owned process group SIGTERM then SIGKILL after1s;30s timeout. Strict requestUUID cancel, no arbitrary PID. Completion rechecks build/source/binary; changed inputs discard result. stdout<=8MiB, stderr bounded64KiB, strict finite schema/identity/dimension/coordinates/field lengths/extrema/duplicates validation. Origin/Host/protocol exact; JSON request<=1MiB including overhead.

## M5–M7 UI
RealInitPreviewProvider transports opaque serialized Working Copy. Existing .par parser/round-trip code unchanged. Separate Mock/Plotfile/RealIC semantics. Dynamic authoritative field selector, shared LineRenderer, curve click/index sample Inspector, request/config/build provenance, Core diagnostics. Missing units and metadata are explicit.
Config Dirty and Preview Current are independent. Exact content/project guard prevents obsolete completion becoming Current. Current also requires matching ready build/binary/request. Editing, generating, failing or cancelling retains last successful curve. Preview never saves config.
UAT fixes: accept boolean Preview capability in protocol1.3 handshake; remove obsolete disconnected caption; readable scoped control colors; do not display unrelated prior Host diagnostics; supply actual min/max to shared LineVis including finite constant-domain padding. The range correction also benefits Plotfile rendering without changing its provider or values.

## M8 automated security/race coverage
Unknown profile/project and injected program/args/cwd/env/shell/case; missing Manifest, changed profile/input/binary; protocol1.2 rejected; bounded config; spawn failure/nonzero exit; invalid JSON, wrong schema/request/revision/build identity; unsupported dimension, wrong lengths, nonfinite coordinates/fields, duplicate fields, extrema; oversized output; timeout and process-group cancellation; Build mutual exclusion; changed source during execution; obsolete config/project revision. Files remain unchanged and last success retained. Fixtures are test-only, never promoted to production science.

## M9 real Sod UAT
User explicitly authorized browser UAT after automatic review initially cited an earlier, separate Manual UAT prohibition. That initial block caused no browser actions; it was cleared by the new authorization.
Production UI on127.0.0.1:4185 connected to protocol1.3 Host on4180. Verified Open Project Config, real Sod512-sample curve, DENS/PRES selection, point click and sample index, actual values/units absence/provenance. Original x_pos0.5 -> unsaved0.3 produced stale old graph then Dirty+Current with changed data; invalid x_pos2 returned the Core interior-interface error and retained graph/Working Copy. No Save or Save As performed.
A separate ignored Sod1D config reused authoritative Helmholtz EOS loading to expose a long-enough actual request for UI cancellation/race. No Core change, no simulation and no project config write. UI Cancel produced Host statecancelled(request5b330357-7959-42b2-949b-063cea29ba86), retained prior requesta6d650a0-7212-4779-8cfd-bfc3cdf4dc2e, and left no ARCH Preview processes. Editing x_pos0.35->0.45 during generation displayed result-discarded and retained prior graph; subsequent update became Current.
Large ideal-Sod pressure1e24 now displays the correct0..1e24 ordinate domain and Inspector1e24; this is transport/rendering evidence, not a scientific parameter recommendation. Curve click selected sample173,x1=0.33886719 correctly.
1280x720 and1920x1080 workstation layouts inspected;800x720 narrow desktop width had no horizontal overflow and accessible controls/Inspector. No mobile-specific UX added.
Disk simulation/Sod/Sod.par SHA256 remainedcd54c4000a8bd3917f5b3fb658c2614f91bddc7ebd84e7e076f6c3bb79489284; output directory empty. CMake initially created that empty directory by its existing rule; Preview created no scientific files. API checks also independently compared before/after file inventories and raw config bytes.

## M10 final regression
After all UAT corrections: npm test94/94 PASS; npm run test:host39/39 PASS; lint PASS; typecheck PASS; production build PASS; unstaged/staged diff checks PASS. Host tests are the required repeated subset. Existing Plotfile/HDF5 fixtures, invalid-file recovery, .par round-trip, history/scrub/retention, config lifecycle/security and Build tests remain passing. Known large-bundle warning remains from existing visualization/HDF5 dependencies; no packaging refactor.
No node_modules/dist/.local/build products/test screenshots in checkpoint. Build artifacts and local evidence remain ignored; no dist forced intoGit.

## Limits / deferred
Fixed integration profile uses an explicit local absolute root, not an auto-portable deployment. Different host requires intentional trusted profile setup. Original managed CUDA binary has no new API and was not rebuilt. Full dependency freshness unknown; parameter metadata unavailable; units not inferred; positive tabular-table coverage not claimed. This report is not scientist acceptance of Sod theoretical results.
No Phase2E, markers/graphical binding, Cellular2D, AMR reconstruction, simulation, SSH or metadata inference. No automatic push or merge. Stop after local checkpoint.

## Local checkpoint
Commit message: feat(studio): integrate real ARCH initial-condition preview.
Tag: studio-phase2d-v0.8.0. Resolve its commit with git rev-parse studio-phase2d-v0.8.0^{commit}; final hash is reported separately to avoid embedding a self-referential commit hash.
Production service: WSL Node24.21.0/npm11.19.0, localhost4185; Host4180. User-visible demo retained for review; no autonomous next-phase work.
