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

The same helpers serve host and device compilation. ODE algorithms, provider
handles and table allocation do not belong here. ARCH-authored adapters and
Timmes-derived equations have distinct attribution: preserve individual file
headers and the [third-party notices](../../../../THIRD_PARTY_NOTICES.md).

See the [network guide](../../../../docs/physics/TimmesNetworks.md),
[Reference](../../../../docs/Reference.md) and
[network validation](../../../../validation/network/README.md).
