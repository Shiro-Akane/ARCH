/**
 * @file ExecutionPreflight.cpp
 * @brief Resolve the same prepared execution request for Run checks and Dispatch.
 */
#include "driver/dispatch/ExecutionPreflight.h"

#include "physics/eos/eosdispatch.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef ARCH_CUDA_BUILD_ENABLED
#define ARCH_CUDA_BUILD_ENABLED 0
#endif

namespace arch::dispatch {
namespace {
// This is the single MPI policy boundary for both queries and Run.
// It owns no communicator, mesh or allocator; storage stays with AmrTree/MemoryPool.
void validate_mpi_launch(const SimConfig& config, int ranks)
{
    if (ranks < 1)
        throw std::invalid_argument("MPI rank count must be positive");
    if (ranks == 1) return;
#if !ARCH_MPI_ENABLED
    throw std::invalid_argument("MPI build disabled; configure ARCH_ENABLE_MPI=ON");
#endif
    if (config.grid.dim < 1 || config.grid.dim > 3 || config.grid.geometry != "cartesian")
        throw std::invalid_argument("MPI requires Cartesian 1-D/2-D/3-D geometry");
    if(config.amr.lrefinemin!=0 || config.amr.lrefinemax<0 || config.amr.lrefinemax>3
        || (config.amr.lrefinemax>0 && config.grid.dim>2))
        throw std::invalid_argument("MPI AMR requires Cartesian 1-D/2-D with level 0..3 and lrefinemin=0");
    if(config.amr.refine_on_jeans)
        throw std::invalid_argument("MPI Jeans refinement is unsupported");
    if (config.grid.nblockx1 < ranks)
        throw std::invalid_argument("MPI requires one or more X slabs per rank");
    if (config.physics.burn.use_burn || config.physics.diffusion.use_diffusion ||
        config.physics.gravity.type != "none")
        throw std::invalid_argument("MPI sources (burn, diffusion, gravity) are unsupported");
    if(config.io.restart && std::filesystem::path(config.io.restart_file).extension()!=".collection")
        throw std::invalid_argument("MPI restart requires a published .collection directory");
    if(config.io.base_name.empty() || config.io.base_name=="." || config.io.base_name==".."
        || std::filesystem::path(config.io.base_name).filename().string()!=config.io.base_name)
        throw std::invalid_argument("MPI output base_name must be one filename component");
    const auto supported_face = [](const std::string& face) {
        return face == "periodic" || face == "outflow" || face == "reflecting";
    };
    const auto supported_pair = [&](const std::string& lower, const std::string& upper) {
        return supported_face(lower) && supported_face(upper)
            && ((lower == "periodic") == (upper == "periodic"));
    };
    if (!supported_pair(config.grid.x1l_boundary_type, config.grid.x1r_boundary_type) ||
        (config.grid.dim >= 2 &&
         !supported_pair(config.grid.x2l_boundary_type, config.grid.x2r_boundary_type)) ||
        (config.grid.dim == 3 &&
         !supported_pair(config.grid.x3l_boundary_type, config.grid.x3r_boundary_type)))
        throw std::invalid_argument("MPI requires built-in boundaries and paired periodic faces");
    const int max_blocks = config.grid.amr_max_blocks > 0 ? config.grid.amr_max_blocks : 10000;
    const auto transverse = static_cast<std::int64_t>(std::max(1, config.grid.nblockx2))
        * std::max(1, config.grid.nblockx3);
    // Bound multiplication before the existing allocator divides the global pool.
    if (transverse > max_blocks || config.grid.nblockx1 > max_blocks / transverse)
        throw std::invalid_argument("MPI global max_blocks is smaller than the root mesh");
}
} // namespace
ExecutionPreflight ResolveExecutionPreflight(
    SimConfig& effective, const SpeciesManager& specs, StartupOrder& startup_order,
    int mpi_ranks)
{
    validate_mpi_launch(effective, mpi_ranks);
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
    ExecutionRequirements requirements = resolved_requirements.value;
    requirements.mpi_ranks = mpi_ranks;
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
