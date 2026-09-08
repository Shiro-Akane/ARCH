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

These identical helpers are used for both host and device compilation. It is crucial to remember that ODE algorithms, provider handles, and table allocation logic absolutely do not belong in this directory. Furthermore, ARCH-authored adapters and the Timmes-derived equations carry distinct attribution; always preserve the individual file headers as well as the [third-party notices](../../../../THIRD_PARTY_NOTICES.md).

See the [network guide](../../../../docs/physics/TimmesNetworks.md),
[Reference](../../../../docs/Reference.md) and
[network validation](../../../../validation/network/README.md).
