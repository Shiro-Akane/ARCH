# Reference data and manufactured systems

Fixtures provide expected values or small controlled systems for the
[tests](../README.md). Each header records its source or derivation; reference
data and historical snapshots serve different purposes.

- [BurnMainlineReference.h](BurnMainlineReference.h): method-specific numerical
  snapshots, distinct from independent accuracy references.
- [BurnTimeReference.h](BurnTimeReference.h): independently integrated burn
  endpoints, with the production rate and EOS models held fixed.
- [HelmReference.h](HelmReference.h), [NseReference.h](NseReference.h) and
  [RoeFluxReference.h](RoeFluxReference.h): independently derived EOS, equilibrium
  and flux reference values; reproduction scripts are identified in the headers.
- [GeneratedNseReference.h](GeneratedNseReference.h): high-precision analytic
  rank-one/rank-two equilibrium values with their defining equations.
- [NativeTabularFixture.h](NativeTabularFixture.h): an analytic gas encoded in
  the native EOSDriver schema, including source-axis and failure controls.
- [SparseTransferNetwork.h](SparseTransferNetwork.h): a manufactured conservative
  chain for sparse execution tests, not a nuclear reaction network.
- [amr_composition_test_cases.h](amr_composition_test_cases.h) and
  [regrid_migration_fixture.h](regrid_migration_fixture.h): reusable composition
  and topology-transfer inputs for host/device tests.
- [checkpoint_conservation_metrics.h](checkpoint_conservation_metrics.h): shared
  test-side integration and comparison support, not a production checkpoint reader.
- [validation_provenance/](validation_provenance/README.md): fixed qualifier
  protocol inputs and artifact-mismatch negative controls, not release evidence.

All reusable numerical test traversals belong strictly in [math/](../math/README.md). When changing reference data, you must carefully keep the associated provenance and rigorously preserve the historical evidence that originally motivated the correction.
