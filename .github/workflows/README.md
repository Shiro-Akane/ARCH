# ARCH continuous integration

[Local test commands](../../tests/README.md) · [中文测试说明](../../tests/README.zh-CN.md#github-持续集成)

[ci.yml](ci.yml) schedules existing tests; numerical implementations and
acceptance tolerances remain in their existing source and test owners. Its
jobs run on disposable GitHub-hosted `ubuntu-24.04` machines, not a maintainer's
workstation. No personal access token, SSH key, CUDA installation or paid GPU
runner is required for this workflow.

## What runs

| Check | Coverage |
| --- | --- |
| `Tooling` | Workflow syntax, shared-authority/header audit and every Python test under `tests/tooling`; empty or skipped suites fail |
| `CPU Release` | The `cpu-release` preset with tests and KLU enabled; builds ARCH and all configured CPU tests, then runs the complete CTest inventory |
| `CI required` | Succeeds only when both jobs succeed; failed, cancelled or skipped dependencies do not count as passing |

Pull requests targeting `main`, `CUDA_complete_v1` or `physics/selfgravity`, and
pushes to those branches, trigger the workflow. There are no path exclusions, so documentation
changes also receive a check result. A newer run cancels an older run for the
same event and branch or pull request. `workflow_dispatch` supports manual
runs after the workflow is present on the default branch.

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
After execution, [check_ci_results.py](../../tools/check_ci_results.py) requires
one passing JUnit entry per configured test, without omissions or skips.
The inventory, rather than a hard-coded total, determines how many tests run.

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
checkpoint comparison and CUDA AMR/dispatch/batch/reduction anchors, and a passing
result for every selected test. Skips remain failures. This profile is not the
complete CUDA inventory or a substitute for application-level numerical,
restart and sanitizer validation; the hosted CPU job keeps its full inventory.

## Resources and reports

The CPU build starts with two compiler jobs and serial CTest execution, with
two OpenMP threads per CTest process. The physical low-density runner fixes four
threads per simulation to match its recorded acceptance runs. The existing memory guard retains 1536 MiB of
available memory, permits up to 512 MiB of additional swap and watches sustained
memory/I/O pressure. These are CI execution limits, not new runtime defaults or
performance measurements. Compiler concurrency can be tuned after observing
real runner measurements without changing production optimization.

Tooling and CPU jobs have 20- and 180-minute limits; each CTest execution has a
600-second limit. Timeouts and guard stops fail the job. The compiler cache is
limited to 1 GiB and separated by native compiler target flags and build
configuration. PR jobs may restore existing caches; only successful pushes to
`main` or `CUDA_complete_v1` save caches. Build directories and dependency source trees are not
restored as cached test results: configuration, compilation checks and tests
run each time.

Actions artifacts retain the tested commit, configure/build/test logs, CTest
inventory and JUnit results for 14 days. Only the named diagnostic paths are
uploaded, not executables, object files, EOS tables or HDF5 checkpoints. These
short-lived CI artifacts do not overwrite the reviewed records in
`validation/` and are not formal release archives.

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

Use **Actions → ARCH CI → Run workflow**, or, after GitHub CLI authentication:

```bash
gh workflow run ci.yml --repo Shiro-Akane/ARCH --ref main
gh run watch --repo Shiro-Akane/ARCH
```

Git-over-SSH can push workflow changes and thereby trigger configured events.
Manual CLI/API dispatch uses GitHub API authentication instead; SSH push
authentication alone does not grant it. If a branch rule blocks a push, use a
PR rather than weakening the rule. Re-run CI on a new PR revision before merging.

Do not run unreviewed public PR code on a persistent self-hosted GPU machine.
The current workflow uses neither `pull_request_target` nor `workflow_run`,
and checkout credentials are not persisted into the job's Git configuration.
See [GitHub's secure-use guidance](https://docs.github.com/en/actions/reference/security/secure-use).
