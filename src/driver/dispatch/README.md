# Policy registration and dispatch

[PolicyDescriptor.h](PolicyDescriptor.h) names policies and aliases;
[BackendCapabilities.h](BackendCapabilities.h), [RuntimeProbe.h](RuntimeProbe.h)
and [ResolvedExecutionPlan.h](ResolvedExecutionPlan.h) resolve a supported plan
before backend construction.

[DispatchImpl.h](DispatchImpl.h) contains shared typed assembly. The Euler, RK2
and RK3 translation units bind that assembly by time-integration policy, keeping
large instantiations in explicit functional owners.

Add a policy through registration and capability resolution, not a case-name
branch. Explicitly incompatible backend/provider choices must be rejected.
See the [backend guide](../../../docs/CudaBackendStatus.md) for user-facing behavior.
