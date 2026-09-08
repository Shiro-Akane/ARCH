# Physics notes and numerical investigations

For supported models and runtime choices, read the [Reference](../Reference.md).
For measured accuracy, use the [Validation index](../../validation/README.md).
This directory explains model origins and preserves the reasoning behind
numerical changes.

## Model provenance

- [Timmes networks](TimmesNetworks.md)
  ([Chinese](TimmesNetworks.zh-CN.md)): built-in network origins and adaptation.
  The root [third-party notices](../../THIRD_PARTY_NOTICES.md) retain attribution
  and the author-contact record.
- [EOS tables](../../src/physics/eos/TabularEOS.md) and
  [physical constants](../../src/physics/constant/README.md) are maintained beside
  their implementations; this index links those contracts rather than copying them.

## Historical numerical investigations

The following notes retain dated diagnostics and intermediate tasks. Read their
open/pending statements in that historical context; the linked module summaries
record the later acceptance results.

- [Curvilinear metric review](CurvilinearMetricReview.md): derivation and repair
  history for finite-volume measures, geometry sources and diffusion. Later
  coverage: [AMR and geometry](../../validation/amr/README.md).
- [AMR indicator noise review](AmrIndicatorNoiseReview.md): roundoff-sensitive
  indicator analysis and shared-policy decisions. Later coverage:
  [AMR](../../validation/amr/README.md).
- [Diffusion coefficient alignment](DiffusionCoefficientAlignment.md)
  ([Chinese](DiffusionCoefficientAlignment.zh-CN.md)): the recorded comparison
  with the original coefficient definitions. Later coverage:
  [diffusion](../../validation/diffusion/README.md).

Implementation ownership remains in the
[shared-authority map](../development/ImplementationOwnership.md); these notes
are not a second mathematical implementation or a separate release checklist.
