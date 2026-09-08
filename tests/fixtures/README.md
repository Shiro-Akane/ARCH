# Reference data and manufactured systems

Fixtures provide expected values or small controlled systems for the
[tests](../README.md). Each header records its source or derivation; reference
data and historical snapshots serve different purposes.

- [BurnMainlineReference.h](BurnMainlineReference.h): preserved endpoints from the
  frozen CPU implementation, used to explain changes rather than redefine accuracy.
- [BurnTimeReference.h](BurnTimeReference.h): independently integrated burn
  endpoints, with the production rate and EOS models held fixed.
- [HelmReference.h](HelmReference.h), [NseReference.h](NseReference.h) and
  [RoeFluxReference.h](RoeFluxReference.h): independently derived EOS, equilibrium
  and flux reference values; reproduction scripts are identified in the headers.
- [SparseTransferNetwork.h](SparseTransferNetwork.h): a manufactured conservative
  chain for sparse execution tests, not a nuclear reaction network.

Reusable numerical test traversals belong to [math/](../math/README.md).
Keep provenance with changed reference data and preserve the historical evidence
that motivated a correction.
