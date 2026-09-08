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

Both CPU and CUDA views share the exact same implementation for interpolation, derivatives, and temperature recovery. While CUDA owners handle data upload and device failure transport, they strictly do not reimplement the EOS math. Furthermore, the burn layer maintains its own separate first-law coupling to these EOS queries.

You should begin by reviewing the existing [tabular EOS guide](TabularEOS.md), the full configuration [Reference](../../../docs/Reference.md), and the detailed [EOS validation](../../../validation/eos/README.md) protocols. Note that the Helmholtz implementation and its corresponding table rigorously retain their original [Timmes provenance](../../../THIRD_PARTY_NOTICES.md).
