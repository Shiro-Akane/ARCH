# ARCH continuous integration

[Local tests](../../tests/README.md) · [中文](../../tests/README.zh-CN.md#github-持续集成)

[ci.yml](ci.yml) runs on pull requests targeting `main` and pushes to `compute/optim` and merged `main`, using disposable GitHub-hosted Ubuntu machines and read-only repository permissions. A newer revision cancels an older run for the same event and branch/PR. Tests and scientific criteria remain in their existing owners. The branch trigger permits hosted acceptance through ordinary SSH pushes without adding account credentials or changing repository settings.

## What runs

| Job | Responsibility |
| --- | --- |
| Tooling | Workflow syntax, shared-implementation/header audit, current documentation links, and the complete `python3 -B tests/tooling` suite; empty/skipped suites fail |
| CPU Release | Applicable changes build ARCH and all configured CPU tests with KLU, run the complete non-Tooling CTest inventory and the original low-density physical matrix |
| Studio and Host | Applicable changes prepare the explicit Host fixture CMake/Ninja/C++/mold tools, install locked Node dependencies, run the complete `npm --silent test` once, lint and build production assets; the build includes TypeScript checking |
| CI required | Requires all three jobs to succeed; failure, cancellation or a skipped job cannot pass |

A failed CTest or npm command still invokes the existing report checker to publish bounded, escaped failure excerpts in check annotations. The original pipeline status is retained: a complete report cannot hide a nonzero command. CTest completes the configured inventory even after a failed case, keeping the existing per-case and job timeouts, so one obsolete assertion does not hide later failures behind another full rebuild. Raw JUnit/TAP remain authoritative; annotations only aid diagnosis when runner-log downloads need authentication.

The Python suite has one CTest entry, `tooling_contracts`, for local use. CPU discovery and execution both exclude `-LE '^tooling$'` because Tooling already owns those assertions. The sparse-transcript parser controls are in that same suite. Numerical/API CTests and independent scientific oracles retain their responsibilities; a smaller number of wrapper entries is not a smaller assertion set.

Documentation checking includes the living [history index](../../docs/histroy/README.md) and its [fixed-evidence index](../../docs/development/archive/README.md). Archived bodies and immutable scientific receipts retain their original basis; they are not rewritten or counted as current instructions.

`npm test` owns the Node file selection, TAP reporter and one file worker. Host tests are included; `test:host` is a diagnostic subset and is not repeated in CI. `npm run build` already runs `tsc --noEmit`, so a second typecheck is unnecessary.

## Applicability and completion

The CPU and Studio jobs always start. Their early scope decisions use the existing [result checker](../../tools/check_ci_results.py):

```bash
python3 tools/check_ci_results.py --scope native --base FULL_BASE_SHA --head HEAD
python3 tools/check_ci_results.py --scope studio --base FULL_BASE_SHA --head HEAD
```

Recognized documentation changes and flat JSON outcome receipts under `validation/backend/results/release-readiness/` can omit native work; Tooling still runs. These receipts are not application inputs. Flat `.ts` files in `studio/tests/` belong only to the complete Node test lane and can also omit native work. Native code/data, nested helpers, other file types and any mixed native change remain conservative; production Studio code is unchanged by this exception. Studio/API documentation can also omit Node work. Maintained README filenames are documentation even beside build tools. Other build/tool/config/workflow paths remain conservative. Scientific source, native/API tests, parameter inputs and unknown paths still require native work. Standalone numerical/Device/Validation changes can omit Studio; Core interfaces, data/grid/AMR/driver/IO, model and build changes require it.

The comparison base is the PR base SHA or the push event's actual `before` SHA. Missing ancestors in a shallow checkout select complete work; they never certify a documentation-only change.

Malformed/missing refs, failed diffs, non-UTF-8 paths, empty change lists and escaping/unknown paths select the complete lane. Rename detection is disabled so a deleted source path cannot disappear behind a new documentation name. Omitted work records `NOT_APPLICABLE`; it creates no passing JUnit/TAP report. The required aggregate still requires each job's actual success.

For an applicable CPU lane, discovery and execution use the same selection. The checker requires nonempty unique inventory, the existing configuration/Preview/math/AMR/IO/KLU anchors and exactly one passing JUnit result per selected test. Missing, extra, failed, disabled or skipped entries fail. The Node checker likewise rejects empty, incomplete, failed, cancelled, skipped or TODO reports. These rules do not redefine numerical tolerances.

## Resources and evidence

CPU uses GCC 12, the existing Release preset, strict floating point, KLU, two build jobs, serial CTest and two OpenMP threads per test. The low-density runner retains its own four-thread setting. The memory guard keeps 1536 MiB available, limits extra swap to 512 MiB and watches sustained pressure. Timeouts/guard stops fail; they are not performance samples.

The CPU lane hydrates only its required Helmholtz table; the Studio lane hydrates only its 14 KiB Sod reader fixture. Both use the same [fixed-asset action](../actions/lfs-asset/action.yml) and [identity check](../actions/lfs-asset/identity.py). Its cache key derives from the tracked LFS pointer; actual bytes and SHA-256 are checked. Other tables and historical HDF5 remain pointers. The original Helm cache key is unchanged. A standard [checkout defaults to no LFS download](https://github.com/actions/checkout/blob/3d3c42e5aac5ba805825da76410c181273ba90b1/action.yml#L86-L89), so a locally hydrated fixture cannot stand in for this hosted preparation. Compiler caching is limited to 1 GiB and separated by resolved native target flags, compiler/build configuration and revision. Cached objects do not replace configuration or tests. Node uses the lockfile without restoring `node_modules`; Electron download is disabled in this engineering lane.

Tooling/CPU diagnostic artifacts retain identity, logs, inventory and JUnit for 14 days. Executables, objects, EOS tables and raw HDF/checkpoints are excluded. Studio logs remain on the disposable runner. Reviewed scientific evidence stays in [Validation](../../validation/README.md), separate from these temporary reports.

## Qualification and maintainer setup

This workflow does not execute CUDA, cuDSS, generated-network trajectories, sanitizers, long scientific campaigns, packaging or native desktop acceptance. The [release qualification index](../../docs/releases/README.md) records their actual qualification. Local actionlint/tests do not establish a hosted passing result for an unpublished working tree.

Use a normal PR targeting `main` for integration; engineering-branch pushes can execute the same checks before that PR exists. Actions are pinned to full commits; actionlint's release archive has a fixed checksum. After a real successful run, maintainers may require **CI required** in the main ruleset. Human approval and merge/publication remain separate decisions. Do not run public PR code on a persistent personal GPU runner; this workflow uses neither `pull_request_target` nor repository secrets.

## v1.3.0 source publication

[release-v1.3.0.yml](release-v1.3.0.yml) is a one-shot, explicitly authorized
release from `compute/optim`, triggered only when that workflow file changes.
It waits for the exact head's PR and push CI, all three default-setup CodeQL
analyses and the original PR comparison gate; missing, skipped, cancelled or
failed results cannot unlock it. The original candidate and note digests must
match. Existing releases/tags are never replaced.

The isolated publication job uses GitHub's temporary repository token to update
PR #12 and call the normal merge API with the accepted head SHA. It has no
admin/bypass operation and does not alter branch rules. Ordinary public PR CI
remains read-only. After merging, the entire tracked file tree must exactly
match the scanned head. The workflow explicitly dispatches the existing full
CI on the actual main commit, since token-generated pushes do not trigger
other workflows. Missing dispatch-base information conservatively selects
complete CPU and Studio work.

Only successful main CI allows the annotated `v1.3.0` tag and formal source
release. Security qualification explicitly reuses the accepted identical file
tree; it does not invent a new main scan. Notes use tag-pinned English/Chinese
guide links and the existing GUI screenshot, with actual CI/security identities
appended. No prebuilt scientific binaries or raw simulation outputs are assets.
A failure stops the current stage; no tag is moved and no rule is weakened.
See [GitHub token behavior](https://docs.github.com/en/actions/tutorials/authenticate-with-github_token)
and [release command](https://cli.github.com/manual/gh_release_create).
