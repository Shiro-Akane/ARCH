/**
 * @file ExecutionPreflight.cpp
 * @brief Resolve the same prepared execution request for Run checks and Dispatch.
 */
#include "driver/dispatch/ExecutionPreflight.h"

#include "physics/eos/eosdispatch.h"

#include <iostream>
#include <stdexcept>
#include <string>

#ifndef ARCH_CUDA_BUILD_ENABLED
#define ARCH_CUDA_BUILD_ENABLED 0
#endif

namespace arch::dispatch {
ExecutionPreflight ResolveExecutionPreflight(
    SimConfig& effective, const SpeciesManager& specs, StartupOrder& startup_order)
{
    const auto requested_backend = parse_compute_backend(
        effective.execution.compute_backend);
    if (!requested_backend.ok)
        throw std::runtime_error(std::string(requested_backend.error));

    const auto parsed_plan = resolve_execution_plan(
        effective, [&] {
            return inspect_eos_table_rank(
                EOSDispatcher::table_path(effective, "Tabular"));
        }, specs.count());
    if (!parsed_plan.ok)
        throw std::runtime_error(std::string(parsed_plan.error));
    if (parsed_plan.value.eos == EosId::Tabular3D
        || parsed_plan.value.eos == EosId::Tabular4D) {
        const auto source = inspect_tabular_source(
            EOSDispatcher::table_path(effective, "Tabular"));
        EOSDispatcher::validate_coupling(
            effective, source, parsed_plan.value.flux == FluxId::Sw);
    }
    if (parsed_plan.value.eos == EosId::Helmholtz && parsed_plan.value.flux == FluxId::Sw)
        throw std::invalid_argument("Steger-Warming requires a composition-only gamma; select HLL, HLLC, Roe or VL for Helmholtz EOS.");
    resolve_nse_request(effective.physics.burn, parsed_plan.value.network);
    if (effective.physics.burn.nse_auto && effective.physics.burn.use_burn) {
        std::cout << "[Dispatch] use_nse=auto resolved to "
                  << (effective.physics.burn.use_nse ? "enabled" : "disabled")
                  << " (" << network_nse_reason(parsed_plan.value.network) << ")"
                  << "; thresholds T>" << effective.physics.burn.nseTempThreshold
                  << " K, rho>" << effective.physics.burn.nseDensThreshold
                  << " g/cm^3." << std::endl;
    }
    if (effective.physics.burn.use_burn && effective.physics.burn.use_nse
        && !network_supports_nse(parsed_plan.value.network)) {
        throw std::invalid_argument(
            "use_nse=true requires an NSE-capable network: "
            + std::string(network_nse_reason(parsed_plan.value.network))
            + ". Select use_nse=auto or false to retain ordinary ODE burning.");
    }
    const auto cpu_candidate = materialize_execution_plan(
        parsed_plan.value, ComputeBackend::Cpu, specs.count());
    const auto cuda_candidate = materialize_execution_plan(
        parsed_plan.value, ComputeBackend::Cuda, specs.count());
    if (!cpu_candidate.ok || !cuda_candidate.ok)
        throw std::logic_error("failed to materialize backend execution plans");
    startup_order.record(StartupEvent::Parsed);
    const auto resolved_requirements =
        resolve_execution_requirements(effective, specs.count());
    if (!resolved_requirements.ok)
        throw std::runtime_error(std::string(resolved_requirements.error));
    const ExecutionRequirements& requirements = resolved_requirements.value;
    startup_order.record(StartupEvent::RequirementsBuilt);

    const RuntimeProbeResult probe = probe_runtime_native({
        requested_backend.value,
        static_cast<bool>(ARCH_CUDA_BUILD_ENABLED),
        effective.execution.cuda_device,
        cuda_candidate.value.linear_solver == LinearSolverId::CuDss
            && policy_cuda_supported<LinearSolverPolicies>(LinearSolverId::CuDss)});
    startup_order.record(StartupEvent::Probed);
    const CapabilityResult support = query_support(
        cpu_candidate.value, cuda_candidate.value, requirements, probe);
    startup_order.record(StartupEvent::SupportQueried);
    const BackendResolution backend = resolve_backend(
        requested_backend.value, support, probe,
        StartupPhase::BeforeConstruction);
    startup_order.record(StartupEvent::Resolved);
    const ResolvedExecutionPlan plan =
        backend.resolved_backend == ComputeBackend::Cpu
            ? cpu_candidate.value : cuda_candidate.value;
    return {plan, requirements, backend};
}
} // namespace arch::dispatch
