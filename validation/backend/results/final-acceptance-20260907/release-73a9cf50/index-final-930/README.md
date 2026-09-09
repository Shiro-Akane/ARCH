# Candidate evidence index

This is a delivery audit of existing records, not a release certificate or a new scientific validation run.

| Gate | Status | Review |
| --- | --- | --- |
| artifacts.freeze | pass | Existing final artifact list rechecked. |
| assets.recovery | pass | Required scoped evidence and identity checks passed. |
| regression.release | pass | Required scoped evidence and identity checks passed. |
| regression.cpu | pass | Required scoped evidence and identity checks passed. |
| regression.debug | pass | Required scoped evidence and identity checks passed. |
| runtime.cartesian | pass | Required scoped evidence and identity checks passed. |
| runtime.curved | pass | Required scoped evidence and identity checks passed. |
| runtime.uniform | pass | Required scoped evidence and identity checks passed. |
| runtime.generated | pass | Required scoped evidence and identity checks passed. |
| restart.smooth | pass | Required scoped evidence and identity checks passed. |
| restart.burn | pass | Required scoped evidence and identity checks passed. |
| science.burn_independent | pass | Required scoped evidence and identity checks passed. |
| science.burn_application | pass | Required scoped evidence and identity checks passed. |
| science.sedov | pass | Required scoped evidence and identity checks passed. |
| science.nse | pass | Required scoped evidence and identity checks passed. |
| science.eos_application | pass | Required scoped evidence and identity checks passed. |
| science.eos_balance | pass | Required scoped evidence and identity checks passed. |
| science.gravity_application | pass | Required scoped evidence and identity checks passed. |
| science.gravity_balance | pass | Required scoped evidence and identity checks passed. |
| science.gaussian | pass | Required scoped evidence and identity checks passed. |
| science.hydro_time | pass | Required scoped evidence and identity checks passed. |
| science.geometry | pass | Required scoped evidence and identity checks passed. |
| science.weak_cv | pass | Required scoped evidence and identity checks passed. |
| science.weak_helm | pass | Required scoped evidence and identity checks passed. |
| science.sparse | pass | Required scoped evidence and identity checks passed. |
| reliability.sustained | pass | Required scoped evidence and identity checks passed. |
| runtime.dynamic_3d | pass | Required scoped evidence and identity checks passed. |
| runtime.dynamic_curved | pass | Required scoped evidence and identity checks passed. |
| sanitizer.dynamic_3d_memcheck | pass | Required scoped evidence and identity checks passed. |
| sanitizer.dynamic_3d_racecheck | pass | Required scoped evidence and identity checks passed. |
| sanitizer.focused_memcheck | pass | Required scoped evidence and identity checks passed. |
| sanitizer.focused_racecheck | pass | Required scoped evidence and identity checks passed. |
| sanitizer.curved_memcheck | pass | Required scoped evidence and identity checks passed. |
| sanitizer.curved_racecheck | pass | Required scoped evidence and identity checks passed. |
| sanitizer.restart_memcheck | pass | Required scoped evidence and identity checks passed. |
| sanitizer.restart_racecheck | pass | Required scoped evidence and identity checks passed. |
| resources.capacity | pass | Required scoped evidence and identity checks passed. |
| resources.compilation | pass | Required scoped evidence and identity checks passed. |
| runtime.aggregate | pass | Required scoped evidence and identity checks passed. |
| manual.documentation | pass | Reviewed the 38 listed current English/Chinese public and curated result documents for the declared shared CPU/CUDA profile, dispatch/provider boundaries, checkpoint compatibility, table/network setup, build concurrency and resource scope. Scientific, application, instrumentation and capacity results remain distinct. Focused memcheck-929 and racecheck-903 each have 23 complete clean routes; only the approved sparse race observation is 1e-12, while ordinary science and memcheck retain 1e-10 with unchanged ODE/storage coverage and budgets. Overall status is centralized, actual failed and historical results stay labeled, and raw hardware chronology remains in contributor/results records. Current local links pass. Timmes attribution explicitly records author authorization for unrestricted free use. This is a content review, not merely file-existence approval. |
| manual.delivery_assets | pass | Reviewed frozen-worktree source-readiness, not a commit, packaged source archive, portable binary release or redistribution authorization. The full current source identity matches the accepted campaigns and the preserved asset inventory. All 139 untracked maintained inputs are present, unignored and still match their inventoried hashes; 452 scoped files are present and the three retired CUDA headers have no active build/include consumers. Delivery must include these untracked inputs and intended deletions rather than archive HEAD alone. The actual Helm table matches its LFS object; all 50 configured generated-package files were independently matched to durable recovery records. Source distribution provides maintained generator modules and recipes; exact generated-package redistribution additionally requires its files, upstream notices and rate-data provenance. Native CPU flags, configured CUDA images and external cuDSS dependencies identify a local build, not a universal binary package. The historical asset JSON is unchanged, including its earlier documentation hashes; the current document identities above and current asset identities below supersede only those document observations. No clean-clone rebuild, new package generation, git mutation or publication is claimed. |
| deferred.audit150 | deferred | Owner-approved large-network external-machine queue, 2026-09-06 13:19 UTC; not a local pass. |
| deferred.audit200 | deferred | Owner-approved large-network external-machine queue, 2026-09-06 13:19 UTC; not a local pass. |

Full report identities, original execution controls and retained failed/interrupted attempts are in [index.json](index.json).
The runtime qualifier and this index both retain `release_qualified: false`.
