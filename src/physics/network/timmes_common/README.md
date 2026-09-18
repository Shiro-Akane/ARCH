# Shared Timmes-network support

This directory supplies common support for the maintained built-in networks.

- [TimmesNetworkSupport.h](TimmesNetworkSupport.h) adapts network policies to
  shared species, RHS, Jacobian and energy interfaces.
- [AproxRateAssembly.h](AproxRateAssembly.h), [RatePair.h](RatePair.h) and
  [TfactorsData.h](TfactorsData.h) organize rates and temperature factors.
- [ScreeningTimmes.h](ScreeningTimmes.h), [Ecapnuc.h](Ecapnuc.h) and
  [NuclearConstants.h](NuclearConstants.h) retain the relevant microphysics
  and nuclear-data conventions.
- [Dual.h](Dual.h) supplies automatic-differentiation support.

Host and device compilation use the same helpers. ODE algorithms, solver-library
handles and table allocation belong to their respective numerical/backend owners.
ARCH adapters and Timmes-derived equations retain their individual attribution;
preserve the file headers and [third-party notices](../../../../THIRD_PARTY_NOTICES.md).

See the [network guide](../../../../docs/physics/TimmesNetworks.md),
[Reference](../../../../docs/Reference.md) and
[network validation](../../../../validation/network/README.md).
