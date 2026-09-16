# Policy registration and dispatch

[PolicyDescriptor.h](PolicyDescriptor.h) names policies and aliases;
[BackendCapabilities.h](BackendCapabilities.h), [RuntimeProbe.h](RuntimeProbe.h)
and [ResolvedExecutionPlan.h](ResolvedExecutionPlan.h) resolve a supported plan
before backend construction.

Burn, diffusion and gravity factories consume the IDs in that resolved plan.
Keep parameter interpretation here rather than adding config/string overloads
that repeat policy selection inside those factories.

[DispatchImpl.h](DispatchImpl.h) contains shared typed assembly. The Euler, RK2
and RK3 translation units bind that assembly by time-integration policy, keeping
large instantiations in explicit functional owners.

To introduce a new policy, use formal registration and capability resolution rather than resorting to an ad-hoc case-name branch. Any explicitly incompatible combinations of backends and providers must be strictly rejected during resolution. For complete details on user-facing behavior, please review the [backend guide](../../../docs/CudaBackendStatus.md).
