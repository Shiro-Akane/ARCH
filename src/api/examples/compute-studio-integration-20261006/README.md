# Configuration and registry response samples

These CPU response samples illustrate configuration v3, registry discovery and UserGravity inspection. [provenance.json](provenance.json) records the producing binary and input identities. Query the selected executable for its current catalogue.

| File | Command / purpose |
| --- | --- |
| [configuration-schema.json](configuration-schema.json) | `ARCH --config-schema`: configuration v3, 95 active standard parameters |
| [registered-cases.json](registered-cases.json) | `ARCH --list-cases`: 16 registered models; registration does not imply all Preview/Plotfile capabilities |
| [user-gravity.par](user-gravity.par) | Explicit UserGravity input for inspection |
| [user-gravity-inspection.json](user-gravity-inspection.json) | `ARCH --inspect-case UserGravity --config-stdin`; Setup and input inspection |

Reproduce from the repository root after building the current CPU Core:

```bash
build-studio-cpu/bin/ARCH --config-schema
build-studio-cpu/bin/ARCH --list-cases
build-studio-cpu/bin/ARCH --inspect-case UserGravity --config-stdin \
  < src/api/examples/compute-studio-integration-20261006/user-gravity.par
```

Client behavior must follow the selected binary's response rather than hard-code these counts. The files contain metadata and a small configuration only; no raw field arrays, plotfiles or checkpoints. Historical examples keep their original contract version. See the [API](../../README.md), [configuration contract](../../docs/CONFIGURATION_API.md).
