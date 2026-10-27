# Release notes and qualification

## v1.4.0 — native Windows (2026-10-27)

[v1.4.0 notes](V1.4.0.md) ([中文](V1.4.0.zh-CN.md)) describe the [GitHub source release](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.4.0), integrated through PR #21. It provides corresponding native Windows 11 x64 CPU/CUDA CLI and Studio functionality within the existing scientific scope. **Linux/WSL2 remains recommended; Windows support is newer and may still have instability or bugs.** Zenodo remains deferred.

Start with [Build](../guides/Build.md), then [Studio](../guides/Studio.md) for current operation. The [26-area native qualification](../histroy/build/WindowsNativeCudaQualification-20261027.md) records implementation evidence; the [release-preparation record](../histroy/release/V1.4.0ReleasePreparation-20261027.md) preserves the earlier unpublished candidate; the [publication record](../histroy/release/V1.4.0Publication-20261027.md) separates accepted main/metadata revisions and immutable release provenance.

## Published and frozen versions

| Version | Record and original scope |
| --- | --- |
| [v1.4.0](V1.4.0.md) ([中文](V1.4.0.zh-CN.md)) | Native Windows CPU/CUDA/Studio source release, 2026-10-27; original scientific qualifications remain applicable. |
| [v1.3.1](V1.3.1.md) ([中文](V1.3.1.zh-CN.md)) | Maintenance source release; original Linux/WSL2 scope and immutable tag/archive remain unchanged. |
| [v1.3.0](V1.3.0.md) ([中文](V1.3.0.zh-CN.md)) | Studio GUI, modernization and strict configuration; original [scientific acceptance](../../validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json). |
| [v1.2.1](V1.2.1.md) ([中文](V1.2.1.zh-CN.md)) | Earlier maintenance record. |
| [v1.2.0](V1.2.0.md) ([中文](V1.2.0.zh-CN.md)) | Earlier feature record. |
| [v1.1.0](V1.1.0.md) ([中文](V1.1.0.zh-CN.md)) | Earlier feature record. |
| [v1.0.0](V1.0.0.md) | Original release record. |

Later Windows acceptance does not retroactively qualify frozen v1.3.1. The [2026-10-26 CPU/Studio record](../histroy/build/WindowsNativeQualification-20261026.zh-CN.md) precedes the [2026-10-27 CPU/CUDA/Studio record](../histroy/build/WindowsNativeCudaQualification-20261027.md); original source, binary and human-interaction identities remain attached to each.

## Scientific and publication boundaries

Existing model, EOS, network, geometry and coupled-method conditions remain applicable. Retained SNIa repeated-AMR failure/thermal gap and the distinction between hotspot functional checks and ignition-mechanism claims remain in [v1.3.0](V1.3.0.md#verification-and-use). Platform support does not add MPI, HIP/AMD, Apple Metal, general radiation transport, arbitrary hardware qualification or new performance claims.

Hosted CI/security results cover their declared scopes; native GPU, packaging, desktop and human evidence remain separate. The v1.4.0 publication gate accepts the actual merged main and metadata revision before an immutable tag/release; original qualification and release-asset provenance retain their exact source identities.

## Distribution and citation

Machine-local AGENTS/assistant skills, obsolete root plans, build products and raw simulation outputs stay excluded. Scientific LFS pointers and third-party notices remain. The [citation guide](../zenodo/README.md) distinguishes GitHub software releases, historical candidates and the known series DOI. The actual software date is recorded; no v1.4.0 Zenodo DOI is invented.
