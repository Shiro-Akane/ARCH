# ARCH continuous integration

[Local test commands](../../tests/README.md) · [中文测试说明](../../tests/README.zh-CN.md#github-持续集成)

[ci.yml](ci.yml) schedules existing tests; numerical implementations and
acceptance tolerances remain in their existing source and test owners. Its
jobs run on disposable GitHub-hosted `ubuntu-24.04` machines, not a maintainer's
workstation. No personal access token, SSH key, CUDA installation or paid GPU
runner is required for this workflow. FLASH and other simulation programs are
not build, test or workflow dependencies; maintainers run external comparisons
separately when useful.

## What runs

| Check | Coverage |
| --- | --- |
| `Tooling` | Workflow syntax, shared-authority/header audit and every selected Python test under `tests/tooling`; empty or skipped suites fail |
| `CPU Release` | The `cpu-release` preset with tests and KLU enabled; builds ARCH and all configured CPU tests, then runs every numerical/API CTest; Tooling owns the runner contracts |
| `Studio and Host` | Clean locked install with Node 24.21.0; full Node suite once (including Host), serial file workers, lint and production build with type checking; empty/skipped/TODO reports fail |
| `CI required` | Succeeds only when Tooling, CPU Release and Studio/Host all succeed; failed, cancelled or skipped dependencies do not count as passing |

Pull requests targeting `main` trigger the workflow, including documentation
changes. A new revision cancels an older run for the same pull request.

The CPU checkout downloads only the tracked Helmholtz EOS table through Git LFS.
Its temporary Git configuration restricts `lfs.fetchinclude` during the existing
authenticated checkout; credentials are still removed afterwards. Current tests
create their own tabular fixtures, so unrelated large tables and archived HDF5
results stay as LFS pointers. This changes downloads, not test selection. CMake
discovers or fetches its existing HighFive and SuiteSparse dependencies. GCC 12
is selected for both C and C++; Release optimization, LTO and the shared
floating-point contract are unchanged. CMake's test inventory is checked for
CPU coverage anchors, including KLU, burning, EOS, AMR and checkpoint tests.
Shared-stage and gravity preparation contracts are now explicit anchors too.
Configuration v3 parser/resolution/direct-entry/API contracts and the existing
Preview initialization/model/metadata/sampling/session/resource contracts are
also required anchors, together with JENS diagnostics and indicator mathematics.
Publication completion, checkpoint conservation, conservative stage acceptance,
RKL repair weights, physical boundaries and AMR flux surfaces are required
anchors as well. Reconstruction/flux limiter mathematics, native reduction/CFL
state handling, same-level exchange and Preview expansion contracts are covered
by their existing anchors.
Removing their CMake registrations therefore fails coverage even when every
remaining JUnit entry passes. This does not run another suite or authorize a
scientific gate: every selected test runs once, and missing/skipped entries
remain failures.
After execution, [check_ci_results.py](../../tools/check_ci_results.py) requires
one passing JUnit entry per selected test, without omissions or skips.
The inventory, rather than a hard-coded total, determines how many tests run.

Eight runner contracts are also registered with CTest for standalone local use.
They carry the `tooling` label; hosted CPU inventory and execution both use
`-LE '^tooling$'`, while the Tooling job discovers their complete Python suite.
An unfiltered local CTest run includes them. The two hosted jobs cover the
existing contracts once, and CTest failures cannot disappear by applying
different discovery and execution selections.

The exact native-reference tooling owner keeps its standard-library, rational
and synthetic engineering checks in default discovery. Its one actual
Machin/arb arithmetic witness is a maintainer opt-in:
`ARCH_VALIDATE_OPTIONAL_REFERENCES=1`. Default selection records that witness
as `NOT_REQUESTED` / `NOT_RUN`; it produces no skip or numerical pass. Explicit
selection requires an already installed flint backend and fails when it is
unavailable. ARCH build and hosted CI require no flint installation. The
module reports its actual selected inventory in the test log; selected-suite
completeness and all numerical assertions remain unchanged.

For a local default check from the repository root:

```bash
env -u PYTHONPATH -u ARCH_VALIDATE_OPTIONAL_REFERENCES python3 -B -m unittest discover \
  -s tests/tooling -p test_rz_matched_native_reference.py -v
```

A maintainer with a preexisting backend can select the same owner explicitly:

```bash
ARCH_VALIDATE_OPTIONAL_REFERENCES=1 python3 -B -m unittest discover \
  -s tests/tooling -p test_rz_matched_native_reference.py -v
```

The selection JSON in the test log distinguishes a requested pending witness
from unrequested evidence. Its actual unittest result supplies execution
success or failure; neither result grants Core/Runtime science qualification.

The CPU job also runs the 72-case positive-density physical matrix in
`validation/low_density`, using its unchanged scientific budgets and a fresh
output directory. NumPy/h5py are validation-only dependencies. The job retains
metrics, inputs and logs, not HDF5 checkpoints. `low_density_math` is a required
CTest coverage anchor, so removing its registration cannot silently pass CI.

The CPU job does not execute CUDA, cuDSS, generated nuclear trajectories,
Compute Sanitizer or the complete scientific Validation campaign. In
particular, a passing Python test of a CUDA runner is a tooling result, not
evidence of device execution. Follow [Validation](../../validation/README.md)
for those checks. A future GPU workflow must validate its device, providers
and test inventory explicitly before reporting complete GPU coverage.

Local Driver changes can use the explicitly scoped `driver-cuda` result profile:
capture `ctest --show-only=json-v1` with the same selection used for execution,
then run `tools/check_ci_results.py --profile driver-cuda --inventory <inventory.json>
--junit <ctest.xml>`. It requires the shared scheduler, gravity preparation,
checkpoint comparison and CUDA AMR/dispatch/batch/reduction anchors, together
with hydro-leaf parity, authenticated geometry/metric caches, refinement
indicators, limiter/conservative-acceptance mathematics and EOS backend parity.
It requires a passing result for every selected test. Skips remain failures. This profile is not the
complete CUDA inventory or a substitute for application-level numerical,
restart and sanitizer validation; the hosted CPU job keeps its full inventory.

## Studio and Host coverage

The Studio job first records an affected-module decision from the PR base and
tested merge revision. Studio/API paths and build, tool or configuration changes
run the full lane. Clearly independent numerical implementation, Core-only
tests, scientific validation and documentation can omit Node setup and execution.
Unknown paths, missing refs, failed diffs and empty change lists select the full
lane. Rename detection is disabled so moving a Studio file preserves its deleted
path in the decision.

The job always runs and `CI required` still requires its success. An unrelated
diff records `NOT_APPLICABLE`, not a completed Node test run; an affected diff
keeps the complete suite and its original failure/skip checks. The classifier
and its regression cases belong to the existing tooling entry.

The existing workflow now includes one Linux Studio/Host job. Its Node runtime
matches the locally verified 24.21.0 environment. The setup-node action is pinned
to 820762786026740c76f36085b0efc47a31fe5020; see its
[official inputs](https://github.com/actions/setup-node) for runtime selection.
npm ci uses the committed lockfile, without a restored node_modules directory or
package-manager cache. Electron binary download is disabled because this lane
does not launch a native window or produce a desktop package.

The command uses the same tests/*.test.ts selection as npm test, with TAP
reporting for completion checks. Host tests are already included and are not
run again with test:host. The existing check_ci_results.py rejects empty,
failed, cancelled, skipped, TODO or inconsistent Node reports. npm run build
already executes tsc --noEmit before Vite, so CI does not duplicate typecheck.
Per-step local timing is diagnostic; it is not a scientific benchmark.

The job performs no Core Build/Preview/simulation and no desktop UAT. It consumes
the repository API fixtures as well as Studio fixtures, so a standalone studio/
archive is insufficient. A hosted passing result remains unproven until this
revision is reviewed/pushed and the actual workflow runs.

There is no new Studio artifact upload. The runner-local build-ci/studio/
contains check reports, versions and timing only; dist, node_modules and
scientific output are not added to artifacts. The pre-existing Tooling and CPU
diagnostic upload rules below are unchanged.

## Resources and reports

The CPU build starts with two compiler jobs and serial CTest execution, with
two OpenMP threads per CTest process. The physical low-density runner fixes four
threads per simulation to match its recorded acceptance runs. The existing memory guard retains 1536 MiB of
available memory, permits up to 512 MiB of additional swap and watches sustained
memory/I/O pressure. These are CI execution limits, not new runtime defaults or
performance measurements. Compiler concurrency can be tuned after observing
real runner measurements without changing production optimization.

Tooling and Studio/Host each have a 20-minute limit; CPU has a 180-minute limit; each CTest execution has a
600-second limit. Timeouts and guard stops fail the job. The compiler cache is
limited to 1 GiB and separated by native compiler target flags and build
configuration. PR jobs restore matching compiler caches and save them after all CPU gates pass
in the current PR cache namespace. Build directories and dependency source trees are not
restored as cached test results: configuration, compilation checks and tests
run each time.

Actions artifacts retain the tested commit, configure/build/test logs, CTest
inventory and JUnit results for 14 days. Only the named diagnostic paths are
uploaded, not executables, object files, EOS tables or HDF5 checkpoints. These
short-lived CI artifacts do not overwrite the reviewed records in
`validation/` and are not formal release archives.

Future physics work follows the [compute plan](../../docs/development/ComputeOptimizationPlan.zh-CN.md):
record actual build/test duration, map each scientific requirement to its test,
and consolidate repeated coverage before delivery. Completion checks account
for every test in each selected inventory; long-run and performance campaigns
retain their separate scientific records and resource limits.

Current Native RZ source/field and energy comparisons, short dynamic coupling,
warm fresh-process restart, actual Native Device qualification and desktop
interactions use their existing owners or explicit maintainer modes. Hosted
unit/API/tooling completion does not complete these scientific and application
exits. Sustained CPU/GPU runs and alternating timings at the same physical
endpoint remain maintenance campaigns, with frozen inputs, all measurements
retained and compilation excluded from timing. Their inputs and processed
provenance can be archived compactly after every consumer finishes; raw
scientific outputs stay local. No particular external GPU or simulation
program is a required workflow dependency.

## Maintainer setup

1. Merge the workflow through a normal PR. If GitHub requests permission to run
   an external contributor's workflow, review its changes before approving it.
2. In **Settings → Actions → General**, allow the GitHub-authored actions used
   here. The workflow requests only `contents: read` and needs no repository
   secrets or permission to approve PRs. All action references are pinned to
   full commit IDs; actionlint's downloaded release has a pinned checksum.
3. After the first successful run, add **CI required** from **GitHub Actions**
   under the `main` ruleset's **Require status checks to pass** setting. Choose
   required human approvals separately; a CI result is not an approving review.
   Do not mark the not-yet-configured
   GPU lane as a required status check.

Open or update a pull request targeting `main`, then inspect its **Checks** tab.
A branch push updates that PR's tested revision. After authenticated GitHub CLI
setup, `gh run watch --repo Shiro-Akane/ARCH` follows its latest run. Branch rules
and required human reviews continue to govern merging.

Do not run unreviewed public PR code on a persistent self-hosted GPU machine.
The current workflow uses neither `pull_request_target` nor `workflow_run`,
and checkout credentials are not persisted into the job's Git configuration.
See [GitHub's secure-use guidance](https://docs.github.com/en/actions/reference/security/secure-use).
