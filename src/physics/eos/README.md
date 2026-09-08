# Equations of state

This directory owns thermodynamic closures and their host-side data loading.

- [eos.h](eos.h) declares the policy/view surface;
  [eos_state.h](eos_state.h) describes a thermodynamic query.
- [IdealGas.h](IdealGas.h), [HelmEos.h](HelmEos.h),
  [Tabular3DEOS.h](Tabular3DEOS.h) and [Tabular4DEOS.h](Tabular4DEOS.h)
  own the concrete shared views.
- [eos_Utils.h](eos_Utils.h), [TabularInterpolation.h](TabularInterpolation.h),
  [TabularFreeEnergy.h](TabularFreeEnergy.h) and
  [TabularInversion.h](TabularInversion.h) own shared thermodynamics,
  interpolation and bounded temperature inversion.
- [TabularLoaderUtils.h](TabularLoaderUtils.h), the table implementation files
  and [eosdispatch.h](eosdispatch.h) handle loading, validation and selection.
- [TabularSource.h](TabularSource.h) keeps source inspection, component and
  mass declarations, and fingerprint metadata independent of heavy EOS math.
- [TabularBaryonSource.h](TabularBaryonSource.h) decodes the supported original
  baryon ASCII format; [TabularCompletion.h](TabularCompletion.h) adds only
  declared missing electron/positron and photon terms during host loading.
  The same completion layer accepts normalized 3D/4D free-energy tables.

EOSDispatcher selects the format from content and builds one immutable owner.
An electron supplement defaults to the existing Timmes table; complete tables
and photon-only completion do not read it. Original baryon tables retain their
source mass and energy conventions, with source F/P/S constraining the common
potential. Invalid source/component stencils and ambiguous thermal roots are
rejected. This is not automatic recognition or scientific validation of every
EOS: see the guide for source-rounding limitations and equilibrium/burn rules.

Both CPU and CUDA views share the exact same implementation for interpolation, derivatives, and temperature recovery. While CUDA owners handle data upload and device failure transport, they strictly do not reimplement the EOS math. Furthermore, the burn layer maintains its own separate first-law coupling to these EOS queries.

You should begin by reviewing the existing [tabular EOS guide](TabularEOS.md), the full configuration [Reference](../../../docs/Reference.md), and the detailed [EOS validation](../../../validation/eos/README.md) protocols. Note that the Helmholtz implementation and its corresponding table rigorously retain their original [Timmes provenance](../../../THIRD_PARTY_NOTICES.md).
