# Read-only AMR recorder

This optional recorder exports patch features for offline analysis. It observes
the existing refinement criterion and the resulting 2:1 balance decisions; it
does not predict, select or override refinement. Normal simulations leave it off.

```ini
predictive_amr_record = true
predictive_amr_record_prefix = output/patch_features
predictive_amr_horizon = 4
predictive_amr_history = 4
```

Boolean values accept upper or lower case, consistently with other ARCH switches.
The prefix is optional; without it, output uses the simulation directory and
base name. Horizon is positive and history is nonnegative; both are metadata values for downstream
analysis, not controls on the solver or an implemented inference model.

## Observation boundary

The shared driver calls the recorder after the numerical criterion and balance
closure, before applying the regrid transaction. CUDA supplies the already
computed device indicators. When recording is enabled, accepted cell fields and
ghosts are made visible on the host for statistics. Recording therefore adds
data transfers and file output; disabling it does not request those transfers.
The final accepted state is recorded through the same visibility boundary.

The recorder owns no physics, interpolation, refinement or state-migration
implementation. Rollback restores diagnostic fields with the transaction, and
pool reuse clears them. An output failure is an error, not a successful empty
dataset.

## Output files

| Suffix | Contents |
| --- | --- |
| `_manifest.json` | Schema and run configuration |
| `_nodes.csv` | Patch geometry, canonical indicator and physical summaries |
| `_edges.csv` | Directed face neighbors, orientation and level differences |
| `_events.csv` | Topology/action counts and observation cost |
| `_conservation.csv` | Volume-integrated mass, momentum, energy and species |

Morton code, level and logical coordinates identify spatial patches. A memory
pool's `block_id` can be reused and is not a persistent spatial identity.
Conservation diagnostics use the shared `GridMetrics::CellVolume` geometry.
The manifest's `schema_version` describes the dataset layout, not a public
software release number.

`criterion_action`, `balanced_action` and `balance_override` are observed labels.
They must not be used as predictive input features. Offline histories need to
follow parent/child spatial lineage, and training/test splits need separate
trajectories. Model development is separate from this recording interface.

## Verification

[Host contracts](../../../tests/host/test_predictive_amr_recorder.cpp) cover
configuration, read-only behavior, dimensions, lifecycle and output failure.
The [application regression](../../../validation/backend/validate_predictive_amr.py)
checks CPU/CUDA recorder off/on and split-run equivalence with 1D Sod and 2D/3D
Sedov. It compares checkpoint data exactly within each backend; it makes no
performance claim and does not replace scientific AMR validation.
