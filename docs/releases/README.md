# Release notes and qualification

## v1.4.0 candidate — native Windows

[v1.4.0 notes](V1.4.0.md) ([中文](V1.4.0.zh-CN.md)) describe the candidate on [PR #21](https://github.com/Shiro-Akane/ARCH/pull/21). It provides corresponding native Windows11 x64 CPU/CUDA CLI and Studio functionality within the existing scientific scope. **Linux/WSL2 remains recommended; Windows support is newer and may still have instability or bugs.** No v1.4.0 tag or public release is claimed. The user retains merge authority; Zenodo remains deferred.

Start with [Build](../guides/Build.md), then [Studio](../guides/Studio.md) for current operation. The [26-area native qualification](../histroy/build/WindowsNativeCudaQualification-20261027.md) records implementation evidence; the [release-preparation record](../histroy/release/V1.4.0ReleasePreparation-20261027.md) separates that evidence from candidate-head CI and post-merge publication exits.

## Published and frozen versions

| Version | Record and original scope |
| --- | --- |
| [v1.3.1](V1.3.1.md) ([中文](V1.3.1.zh-CN.md)) | Maintenance source release; original Linux/WSL2 scope and immutable tag/archive remain unchanged. |
| [v1.3.0](V1.3.0.md) ([中文](V1.3.0.zh-CN.md)) | Studio GUI, modernization and strict configuration; original [scientific acceptance](../../validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json). |
| [v1.2.1](V1.2.1.md) ([中文](V1.2.1.zh-CN.md)) | Earlier maintenance record. |
| [v1.2.0](V1.2.0.md) ([中文](V1.2.0.zh-CN.md)) | Earlier feature record. |
| [v1.1.0](V1.1.0.md) ([中文](V1.1.0.zh-CN.md)) | Earlier feature record. |
| [v1.0.0](V1.0.0.md) | Original release record. |

Later Windows acceptance does not retroactively qualify frozen v1.3.1. The [2026-10-26 CPU/Studio record](../histroy/build/WindowsNativeQualification-20261026.zh-CN.md) precedes the [2026-10-27 CPU/CUDA/Studio record](../histroy/build/WindowsNativeCudaQualification-20261027.md); original source, binary and human-interaction identities remain attached to each.

## Scientific and publication boundaries

Existing model, EOS, network, geometry and coupled-method conditions remain applicable. Retained SNIa repeated-AMR failure/thermal gap and the distinction between hotspot functional checks and ignition-mechanism claims remain in [v1.3.0](V1.3.0.md#verification-and-use). Platform support does not add MPI, HIP/AMD, Apple Metal, general radiation transport, arbitrary hardware qualification or new performance claims.

Hosted CI/security results cover their declared scopes; native GPU, packaging, desktop and human evidence remain separate. Publication needs the final PR head's checks, user merge, review of independent main changes, and successful checks on the actual main revision before an immutable v1.4.0 source tag/release.

## Distribution and citation

Machine-local AGENTS/assistant skills, obsolete root plans, build products and raw simulation outputs stay excluded. Scientific LFS pointers and third-party notices remain. The [citation guide](../zenodo/README.md) distinguishes unpublished candidate metadata, released versions and the known series DOI; no new version DOI or publication date is invented.
