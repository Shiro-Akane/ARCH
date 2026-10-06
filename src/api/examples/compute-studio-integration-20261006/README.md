# Current integration response examples

These are actual CPU responses from the working merge of `compute/optim` base `a566310e` and collaborator checkpoint `b7cb8b69`, including the reviewed compatibility repairs. They document an integration checkpoint, not a released binary. [provenance.json](provenance.json) records the binary and input identities.

| File | Command / purpose |
| --- | --- |
| [configuration-schema.json](configuration-schema.json) | `ARCH --config-schema`: configuration v3, 95 active standard parameters |
| [registered-cases.json](registered-cases.json) | `ARCH --list-cases`: 16 registered models; registration does not imply all Preview/Plotfile capabilities |
| [user-gravity.par](user-gravity.par) | Explicit O8 UserGravity input for inspection |
| [user-gravity-inspection.json](user-gravity-inspection.json) | `ARCH --inspect-case UserGravity --config-stdin`; current O8 setup and input inspection |

Reproduce from the repository root after building the current CPU Core:

```bash
build-studio-cpu/bin/ARCH --config-schema
build-studio-cpu/bin/ARCH --list-cases
build-studio-cpu/bin/ARCH --inspect-case UserGravity --config-stdin \
  < src/api/examples/compute-studio-integration-20261006/user-gravity.par
```

Client behavior must follow the selected binary's response rather than hard-code these counts. The files contain metadata and a small configuration only; no raw field arrays, plotfiles or checkpoints. Historical examples keep their original contract version. See the [API](../../README.md), [configuration contract](../../CONFIGURATION_API.md) and [integration report](../../../../docs/development/ComputeStudioIntegrationReport-20261006.zh-CN.md).
