# Equations of state

This directory owns thermodynamic closures and their host-side data loading.

- [eos.h](eos.h) declares the policy/view surface;
  [eos_state.h](eos_state.h) describes a thermodynamic query.
- [IdealGas.h](IdealGas.h), [HelmEos.h](HelmEos.h),
  [Tabular3DEOS.h](Tabular3DEOS.h) and [Tabular4DEOS.h](Tabular4DEOS.h)
  own the concrete shared views.
- [eos_Utils.h](eos_Utils.h) and [TabularFreeEnergy.h](TabularFreeEnergy.h)
  own common thermodynamic and interpolation helpers.
- [TabularLoaderUtils.h](TabularLoaderUtils.h), the table implementation files
  and [eosdispatch.h](eosdispatch.h) handle loading, validation and selection.

CPU/CUDA views share interpolation, derivatives and temperature recovery.
CUDA owners upload data and transport device failures; they do not reimplement
the EOS. The burn layer owns its separate first-law coupling to these queries.

Start with the existing [tabular EOS guide](TabularEOS.md), the
[Reference](../../../docs/Reference.md) and [EOS validation](../../../validation/eos/README.md).
The Helmholtz implementation and table retain their
[Timmes provenance](../../../THIRD_PARTY_NOTICES.md).
