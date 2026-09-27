# Equations of state

This directory owns thermodynamic closures and their host-side data loading.

- [eos.h](eos.h) declares the policy/view surface;
  [eos_state.h](eos_state.h) describes a thermodynamic query.
- [IdealGas.h](IdealGas.h), [HelmEos.h](HelmEos.h),
  [Tabular3DEOS.h](tabular/Tabular3DEOS.h) and [Tabular4DEOS.h](tabular/Tabular4DEOS.h)
  own the concrete shared views.
- [eos_Utils.h](eos_Utils.h), [TabularInterpolation.h](tabular/TabularInterpolation.h),
  [TabularFreeEnergy.h](tabular/TabularFreeEnergy.h) and
  [TabularInversion.h](tabular/TabularInversion.h) own shared thermodynamics,
  interpolation and bounded temperature inversion.
- [TabularLoaderUtils.h](sources/TabularLoaderUtils.h), the table implementation files
  and [eosdispatch.h](eosdispatch.h) handle loading, validation and selection.
- [TabularSource.h](sources/TabularSource.h) keeps source inspection, component and
  mass declarations, and fingerprint metadata independent of heavy EOS math.
- [TabularBaryonSource.h](sources/TabularBaryonSource.h) decodes the supported original
  baryon ASCII format; [TabularCompletion.h](sources/TabularCompletion.h) adds only
  declared missing electron/positron and photon terms during host loading.
  The same completion layer accepts normalized 3D/4D free-energy tables.

EOSDispatcher selects the format from content and builds one immutable owner.
An electron supplement defaults to the existing Timmes table; complete tables
and photon-only completion do not read it. Original baryon tables retain their
source mass and energy conventions, with source F/P/S constraining the common
potential. Invalid source/component stencils and ambiguous thermal roots are
rejected. This is not automatic recognition or scientific validation of every
EOS: see the guide for source-rounding limitations and equilibrium/burn rules.

CPU and CUDA views share interpolation, derivatives and bounded temperature
recovery. CUDA owners provide upload and failure transport; the burn layer owns
the first-law coupling. Helm queries calculate only the derivative order their
caller consumes and reuse fixed-density interpolation factors within an inverse.
A thread-local host workspace may reuse a successful inverse at exactly the
same state during hydro, burn preparation, block CFL scans or AMR thermodynamic
callbacks. Each lexical operation borrows fixed EOS/table data and retains the
complete input key, original root residual and validity checks. Failed roots
are not cached; the two burn half-steps own separate workspaces. No approximate
state bins or relaxed inverse tolerance are introduced.

The inherited Helm Coulomb positivity cutoff can make energy inversion
non-unique near cold, strongly coupled states. A small inverse residual alone
does not certify a unique temperature there. The fixed protected inverse remains
unchanged; broad caller-supplied warm starts need a material-domain and root-branch
contract first. This limitation is distinct from the normalized free-energy
table inversion, which explicitly checks root ambiguity.

Start with the [tabular EOS guide](TabularEOS.md), configuration
[Reference](../../../docs/Reference.md) and [EOS validation](../../../validation/eos/README.md).
The Helmholtz implementation and table retain their
[Timmes provenance](../../../THIRD_PARTY_NOTICES.md).

`eos_coulomb_mult` is a Helmholtz physical model control: the default 1 retains
all inherited ion Coulomb corrections, while a finite value in [0,1] scales
pressure, energy and every corresponding derivative consistently. It is copied
to the device view and included in owner-cache and restart identities. The
existing nonpositive pressure/energy cutoff remains in force. This control is
independent of the missing-electron/positron completion for tabular EOS sources.
