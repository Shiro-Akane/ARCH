# aprox19 built-in network

[NetAprox19.h](NetAprox19.h) defines the network policy, species ordering,
nuclear data and rate assembly. [TimmesRateLibrary.h](TimmesRateLibrary.h)
contains rate evaluations; [TimmesRhs.inc](TimmesRhs.inc) contains the reaction
RHS expressions. [TimmesJacobian.inc](TimmesJacobian.inc) supplies the network's composition-Jacobian expressions.
[NetAprox19.cpp](NetAprox19.cpp) supplies the ordinary compilation witness.

These files constitute the network's single, unified mathematical authority for both CPU and CUDA targets. All common adapters are maintained in [timmes_common](../timmes_common/README.md); meanwhile, [burnsolver](../../../numerics/burnsolver/README.md) exclusively owns the ODE integration, and the respective backends handle execution and storage.

This network adapts Frank Timmes's `public_aprox19.f90`. Preserve source
attributions and the [third-party notices](../../../../THIRD_PARTY_NOTICES.md);
ARCH's adapters do not relicense the upstream material.

See the [network guide](../../../../docs/physics/TimmesNetworks.md),
[Reference](../../../../docs/Reference.md) and
[network validation](../../../../validation/network/README.md).
