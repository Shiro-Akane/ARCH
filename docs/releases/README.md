# Frozen release qualifications

The current maintenance baseline is [ARCH v1.3.1](V1.3.1.md) ([中文](V1.3.1.zh-CN.md)). It follows the accepted [v1.3.0 release](V1.3.0.md) and its [scoped scientific acceptance](../../validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json).

## Applicable qualification

This patch updates Studio build dependencies and release documentation. Scientific implementation, mathematical libraries, Studio data-reading and data-writing implementation, inputs, test definitions, numerical tolerances and recorded historical evidence retain their original identities. The [original acceptance record](../../validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json) remains authoritative for the physical models, backends and workloads actually qualified.

Supported execution remains Linux/WSL2 and the recorded CPU and single NVIDIA GPU configuration. Windows native execution, MPI, additional GPU vendors and other platforms require separate qualification. The retained SNIa repeated-AMR failure and thermal gap, and the distinction between hotspot functional checks and an ignition-mechanism claim, remain applicable. See the [v1.3.0 verification and use section](V1.3.0.md#verification-and-use) for qualifications and measured limits.

Hosted Tooling, CPU and Studio CI cover their declared scopes; they do not establish native Windows desktop behavior, additional GPU hardware, all scientific trajectories or a new performance result. Historical CPU/CUDA timings and costs remain attached to their originating implementation and acceptance records.

## Distribution and local work instructions

Machine-local `AGENTS.md`, project assistant skills and the completed root modernization/release plans are excluded from current distributions. Historical commits retain their original provenance. Users may supply their own local instructions. This public index records frozen qualification, rather than a machine-specific execution queue.

Use the [build guide](../guides/Build.md), [Studio guide](../guides/Studio.md) and [Zenodo record documentation](../zenodo/README.md) for current user-facing instructions.
