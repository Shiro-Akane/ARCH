# Predictive AMR data and model contract

## Authority boundary

The canonical sequence remains:

```text
EvaluateRefinement -> RippleCheck -> observer -> Regrid apply
```

The observer records the exact numerical criterion and 2:1 closure result. It
cannot modify either. Later inference will follow:

```text
refine = deterministic_numerical_criterion OR predictive_early_hint
```

A model may cause an early refinement or preallocation. It may never cancel a
refinement required by the numerical criterion. Model code stays in the
adaptive-runtime layer; it must not fork the physics, AMR indicator, 2:1
closure, Morton layout, or regrid implementation.

## Phase 0 snapshot schema

One leaf patch is one node. Directed face-neighbor relations are edges. Node
identity uses Morton code plus level and logical coordinates; `block_id` is
diagnostic only because the memory pool may reuse it.

The recorder writes five files using one prefix:

- `_manifest.json`: schema and run configuration;
- `_nodes.csv`: geometry, canonical indicator, physical summaries, gradients,
  divergence, Mach and compression statistics;
- `_edges.csv`: typed directed face edges, orientation, level difference, and
  normalized relative position;
- `_events.csv`: topology/action counts and observer overhead.
- `_conservation.csv`: volume-weighted mass, momentum, energy, and species
  totals at every pre-regrid snapshot plus the final solver state. Cell volumes
  call the canonical `GridMetrics::CellVolume` implementation.

`criterion_action`, `balanced_action`, and `balance_override` are labels and
audit columns. Training code must never include them as input features.

## What the data implies

ARCH produces a discrete-time dynamic graph. Patches are born, split, merge,
and disappear at regrid events. History must therefore follow spatial lineage,
not reusable `block_id` values. A child's initial history is inherited from its
parent; a restored parent's history is pooled from its children. The offline
label builder uses level/logical-coordinate region overlap for this purpose.

Refine events are also extremely rare in the first trace: moving Sod contains
3 realized refinements among 2,645 node-event rows. Ordinary accuracy, random
row splits, and independent row oversampling are invalid. Labels are defined
as discrete-time hazards over future regrid events and are right-censored when
a trajectory ends before the requested horizon.

## Model ladder

Each step is an independent experiment and must beat all earlier steps under
the same end-to-end budget before it can be promoted.

### G0: non-graph controls

- Deterministic AMR only: safety and performance reference.
- Logistic regression and a small MLP on the same patch features: quantify the
  value of graph structure instead of assuming it.
- Optional gradient-boosted trees offline as a feature-quality diagnostic; not
  an in-solver dependency.

### G1: recommended first GNN

- A 2-3-layer residual, edge-conditioned MPNN. Edge types encode face
  orientation and level difference.
- A short GRU or temporal convolution over lineage-aware event histories.
- A discrete-time survival/hazard head for `where + when`, evaluated at
  horizons 1, 2, 4, and 8.
- Separate heads for numerical-criterion refine and balance-closure-only refine.
  An optional auxiliary head forecasts the future refinement indicator to
  regularize the representation.

This model is intentionally small enough to batch all leaf patches at a regrid
event on one GPU and to expose inference latency early.

### G2: data-specific improvements

Add only through ablation:

1. temporal trend features: indicator/gradient deltas, short slopes, time since
   refinement, patch age, and recent refine frequency;
2. lineage initialization: parent-to-child state transfer and pooled
   child-to-parent state, without changing the Morton tree;
3. Morton-parent multiscale tokens or sparse coarse edges when local messages
   miss a long-range shock/refinement wave;
4. hardware-cost heads using measured patch kernel time, bytes moved, and
   memory pressure, after stable profiler data exists;
5. a two-stage high-recall candidate filter only if full-graph inference exceeds
   the latency or memory budget.

Synthetic graph edges are not allowed to alter the physical neighbor graph.
Topology-aware imbalance methods may inform the loss or sampling strategy, but
any augmented topology must remain training-only and pass a no-augmentation
ablation.

### G3: conditional research models

Temporal Graph Transformers or graph state-space/Mamba models are justified
only if G1/G2 demonstrably miss long histories and their online cost fits the
measured runtime budget. They are not Phase 1 defaults.

## Loss, sampling, calibration, and uncertainty

- Batch by complete events and trajectories. Use cost-sensitive BCE,
  asymmetric focal loss, or logit adjustment; select by minority recall at a
  fixed extra-patch budget.
- Mine hard negatives near moving fronts, but evaluate at natural prevalence.
- Split by complete trajectory and problem family. Never place rows from the
  same run in multiple splits.
- Start with held-out-trajectory temperature scaling and report Brier score,
  expected calibration error, and reliability curves.
- Evaluate temporal conformal prediction only after documenting chronological
  non-exchangeability and empirical coverage. It supplements the deterministic
  safeguard; it does not replace it.

## Evaluation contract

Offline model metrics:

- PR-AUC, recall, false-negative rate, and precision;
- event-level recall and recall at a fixed extra-patch/cell budget;
- time-to-refine error and recall by prediction horizon;
- calibration and coverage, broken down by level and problem family.

End-to-end solver metrics:

- solution L1/L2/L-infinity error against canonical AMR;
- conservation drift and any positivity/failure count;
- active patches/cells, regrid count, and premature-refinement lifetime;
- wall time, inference time per regrid, peak GPU memory, and data-transfer cost.

Promotion is staged: offline replay, shadow mode, predictive early-refine with
the deterministic OR safeguard, then broader cases. Smooth, shock,
moving-feature, and application trajectories must all be represented.

## Research basis

The architecture is informed by recent primary work, without importing a new
solver implementation:

- DynAMO (JCP 2024), anticipatory AMR evaluated through simulation outcomes:
  https://www.sciencedirect.com/science/article/pii/S0021999124001736
- RL for AMR (AISTATS 2023), local policies and end-to-end mesh decisions:
  https://proceedings.mlr.press/v206/yang23e.html
- ASMR++ (2024/2026), explicit handling of evolving AMR elements:
  https://arxiv.org/abs/2406.08440
- Multiscale AMR GNNs (2024), hierarchy-aware message passing to limit
  over-smoothing: https://arxiv.org/abs/2402.08863
- Class-imbalanced graph learning (ICML 2024), topology-induced minority bias:
  https://proceedings.mlr.press/v235/liu24ay.html
- Dynamic-graph conformal prediction (2024) and non-exchangeable temporal graph
  conformal prediction (2025): https://arxiv.org/abs/2405.19230 and
  https://arxiv.org/abs/2507.02151

Heavy architectures remain optional experiments. They do not expand the Phase
0 Patch API and do not enter the solver before a smaller baseline demonstrates
an end-to-end limitation.
