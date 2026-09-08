# aprox21 built-in network

[NetAprox21.h](NetAprox21.h) defines the network policy, species ordering,
nuclear data and rate assembly. [TimmesRateLibrary.h](TimmesRateLibrary.h)
contains rate evaluations; [TimmesRhs.inc](TimmesRhs.inc) contains the reaction
RHS expressions. [TimmesJacobian.inc](TimmesJacobian.inc) supplies the network's composition-Jacobian expressions.
[NetAprox21.cpp](NetAprox21.cpp) supplies the ordinary compilation witness.

These files are the network's single mathematical authority for CPU and CUDA.
Common adapters live in [timmes_common](../timmes_common/README.md);
[burnsolver](../../../numerics/burnsolver/README.md) owns ODE integration,
and backends own execution and storage.

This network adapts Frank Timmes's `public_aprox21.f90`. Preserve source
attributions and the [third-party notices](../../../../THIRD_PARTY_NOTICES.md);
ARCH's adapters do not relicense the upstream material.

See the [network guide](../../../../docs/physics/TimmesNetworks.md),
[Reference](../../../../docs/Reference.md) and
[network validation](../../../../validation/network/README.md).
