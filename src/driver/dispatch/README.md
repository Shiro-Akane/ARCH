# Policy registration and dispatch

[PolicyDescriptor.h](PolicyDescriptor.h) names policies and aliases;
[BackendCapabilities.h](capability/BackendCapabilities.h), [RuntimeProbe.h](capability/RuntimeProbe.h)
and [ResolvedExecutionPlan.h](capability/ResolvedExecutionPlan.h) resolve a supported plan
before backend construction.

Burn, diffusion and gravity factories consume the IDs in that resolved plan.
Keep parameter interpretation here rather than adding config/string overloads
that repeat policy selection inside those factories.

[DispatchImpl.h](bindings/DispatchImpl.h) contains shared typed assembly. The Euler, RK2
and RK3 translation units bind that assembly by time-integration policy, keeping
large instantiations in explicit functional owners.

To introduce a new policy, use formal registration and capability resolution rather than resorting to an ad-hoc case-name branch. Any explicitly incompatible combinations of backends and providers must be strictly rejected during resolution. For complete details on user-facing behavior, please review the [backend guide](../../../docs/CudaBackendStatus.md).

Capability resolution checks registered bindings, dependencies and resource
requirements. Physical EOS compatibility also uses `SolverDispatch.cpp` and
`EOSDispatcher::validate_coupling`; material coefficients are evaluated later.
A positive `query_support` result is therefore not proof that every requested
channel is active or that a coupled configuration has passed scientific checks.
The [combination rules](../../../docs/Reference.md#combining-methods-and-physics)
record the current boundaries and open audit items.
