# Configuration v3 candidate fixtures

These hand-authored fixtures preserve the configuration v3 transport expectations.
They are distinct from responses captured from a running ARCH binary. The current
[transport protocol](../../docs/ConfigurationProtocol.md) and
[configuration API](../../docs/CONFIGURATION_API.md) define the active interface.

| Fixture | Scope |
| --- | --- |
| classification.json | All 95 old standard keys partitioned against the approved plan: 19 required, 50 conditional, 25 allowed-default, one retired. Test expectations, not another runtime registry. |
| schema.json | Complete candidate standard catalogue after G retirement (94 keys), seven Sod declarations and shared log_dir. Other model declarations remain unclaimed. |
| sod-valid.par / .json | Full selected-Sod configuration response; 94 standard + seven model + one auxiliary record. Explicit values, documented defaults, derived log_dir/dimension. Static inspection only, not simulation readiness. |
| empty.par / .json | No invented raw/parsed values. All standard records, 19 definite missing inputs; downstream conditions remain unknown except restart=false from its declared default. Case/auxiliary coverage remains false. |
| missing-switch.par / .json | Removing use_burn never becomes false; dependent requirements become unknown, while existing explicit values remain visible. |
| syntax-errors.par / .json | Partial error envelope: explicit zero/false, bad number, duplicate positions, malformed line and empty key. No occurrence wins a duplicate. |

Metadata (units/options/presentation/coordinate systems) was taken from the
existing mainline CPU schema, then classified using ConfigurationContractPlan.
The existing catalogue remains the production owner. These fixtures do not
publish old scientific fallbacks as allowed defaults. sod-valid preserves the
standard Sod input and makes the previously effective CPU/floor/HLL controls
explicit; network_name=none explicitly selects the non-nuclear Sod setup.

verify_candidates.py checks candidate consistency, input hashes, byte positions,
declaration coverage and the missing-switch invariant. It does not run ARCH or
prove that v3 is implemented. Actual Core/Host behavior is checked by the existing integration contract tests.
These historical counts and declarations are not a runtime parameter registry.

The fixture coverage should be distinguished from runtime acceptance of case-defined values, all other
registered model declarations, inactive invalid tokens, retired/unknown keys,
unknown methods, unchecked paths, overflow, whitespace-only revisions and stale
Host identities. Those checks extend the shared corpus during implementation;
they do not permit a partial rollout to masquerade as complete v3 support.
