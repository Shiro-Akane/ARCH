# Continuous integration

[Local tests](../../tests/README.md) · [中文测试指南](../../tests/README.zh-CN.md#github-持续集成)

[ci.yml](ci.yml) runs on pull requests targeting `main` and pushes to `main` or `compute/optim`. Jobs use disposable GitHub-hosted Ubuntu machines and read-only repository permissions. A newer revision cancels the older run for the same event and branch or pull request.

## Jobs

| Job | Coverage |
| --- | --- |
| Tooling | Workflow syntax, shared-implementation/header audit, maintained documentation links, and `python3 -B tests/tooling` |
| CPU Release | ARCH and configured CPU tests with KLU, the complete non-Tooling CTest inventory, and the low-density physical matrix |
| Studio and Host | Host fixture tools, locked Node dependencies, the complete `npm --silent test` suite, lint, and production assets with TypeScript checking |
| CI required | Requires all three jobs to succeed; failed, cancelled or skipped jobs fail the aggregate |

`tooling_contracts` is the Python suite's local CTest entry. The CPU lane excludes its `tooling` label because the Tooling job runs it separately. `npm test` owns the Node file selection, TAP reporter and single file worker; its Host tests are included. `npm run build` includes `tsc --noEmit`.

The [result checker](../../tools/check_ci_results.py) requires a nonempty, unique CTest inventory and one passing JUnit result per selected test. Missing, extra, disabled, failed or skipped entries fail. The Node checker also rejects empty, incomplete, cancelled, skipped and TODO reports. Failed commands retain their failure status while the checker adds bounded excerpts to check annotations. Raw JUnit/TAP and runner logs provide the full diagnostics.

## Change selection

The CPU and Studio jobs always start. Each selects its work from the pull request's base SHA or the push event's `before` SHA:

```bash
python3 tools/check_ci_results.py --scope native --base FULL_BASE_SHA --head HEAD
python3 tools/check_ci_results.py --scope studio --base FULL_BASE_SHA --head HEAD
```

Documentation-only changes can omit native and Node work. Flat JSON outcome records under `validation/backend/results/release-readiness/` and flat TypeScript test files under `studio/tests/` can omit native work. Standalone numerical, Device and Validation changes can omit Studio work. Core interfaces, data/grid/AMR/driver/IO, models and build changes require the affected full suites.

Unknown paths, malformed references, failed diffs, missing ancestors, non-UTF-8 paths and empty change lists select complete work. Rename detection is disabled so deleted source paths remain visible to selection. Omitted work records `NOT_APPLICABLE`; the job must still complete successfully for the aggregate check.

## Resources and artifacts

The CPU lane uses GCC 12, the Release preset, strict floating point, KLU, two build jobs, serial CTest and two OpenMP threads per test. The low-density runner uses four threads. The memory guard keeps 1536 MiB available, limits extra swap to 512 MiB and stops sustained pressure. Guard stops and timeouts fail the job.

Only the CPU Helmholtz table and the Studio 14 KiB Sod fixture are hydrated from LFS. Both use the [fixed-asset action](../actions/lfs-asset/action.yml), which derives its cache key from the tracked pointer and verifies actual size and SHA-256. Other table and HDF5 files remain pointers. Compiler caches are capped at 1 GiB and separated by target flags, compiler/build configuration and revision. Node installs from the lockfile without restoring `node_modules`; Electron binary download is disabled.

Tooling and CPU diagnostic artifacts retain identity, logs, inventory and JUnit for 14 days. They exclude executables, objects, EOS tables and raw HDF5/checkpoints. Studio logs remain in the workflow log. Scientific results and their reproduction instructions live in [Validation](../../validation/README.md).

## Additional validation

Hosted CI does not execute CUDA, cuDSS, generated-network trajectories, sanitizers, long scientific campaigns, native Windows builds or desktop packaging/window tests. Use the applicable [validation guide](../../validation/README.md), [build guide](../../docs/guides/Build.md) and [platform coverage](../../docs/releases/PlatformSupport.md) for those checks.

Actions are pinned to full commits; the actionlint archive has a fixed checksum. This workflow uses neither `pull_request_target` nor repository secrets and does not publish releases. Public pull-request code should run on disposable workers rather than persistent personal GPU machines.

## MPI Hydro gate

The MPI job builds separate ON/OFF applications, checks the intended rank count through the shared execution preflight, exercises real 1/2/4-rank Host exchange/reductions, and compares 1D/2D/3D production output against the MPI-OFF application bit for bit. The existing fixtures also cover uneven partitions, tight pool capacity, rank input disagreement and unsupported startup modes. Auto-to-CPU selection is checked in both the query and an evolved trajectory. The MPI gate participates in CI required.

The same job also runs a two-rank Cartesian 2D AMR trajectory, validates native checkpoint continuation, and rejects heterogeneous executable identities and incomplete or overlapping restart topologies even after their checksums are recomputed. MPI application admission is owned by ExecutionPreflight: fixed Cartesian 1D/2D/3D and dynamic Cartesian 2D levels 0..2, nonempty ranks, built-in boundaries, no source operators, and same-rank collection restart on a shared output filesystem.\n\nHosted runners may oversubscribe ranks for correctness. Their timings are not scaling evidence. Only diagnostics and small comparison summaries are uploaded, not HDF5, binaries or build trees. CUDA MPI is validated separately on the designated GPU server; this CPU job does not establish GPU, multi-GPU or multi-node acceptance. See the [application integration record](../../docs/development/MPI-GPU-AMR-20261112.zh-CN.md).
