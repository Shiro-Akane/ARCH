# Physics model notes

For supported models and runtime choices, read the [Reference](../Reference.md).
For measured accuracy, use the [Validation index](../../validation/README.md).
This directory explains model origins and links the maintained model contracts.

## Model provenance

- [Timmes networks](TimmesNetworks.md)
  ([Chinese](TimmesNetworks.zh-CN.md)): built-in network origins and adaptation.
  The root [third-party notices](../../THIRD_PARTY_NOTICES.md) retain attribution
  and the author-contact record.
- [EOS tables](../../src/physics/eos/TabularEOS.md) and
  [physical constants](../../src/physics/constant/README.md) are maintained beside
  their implementations; this index links those contracts rather than copying them.

## Accuracy and implementation

- [AMR and geometry validation](../../validation/amr/README.md) explains
  conservative transfers, coordinate measures and refinement checks.
- [Diffusion validation](../../validation/diffusion/README.md) describes transport
  reference problems, convergence and measured errors.
- [Gravity validation](../../validation/gravity/README.md) covers external sources
  and the supported self-gravity boundaries, coupling and device checks.

See the [implementation ownership map](../development/ImplementationOwnership.md)
when extending a model or its numerical implementation.
