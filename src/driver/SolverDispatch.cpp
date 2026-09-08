/**
 * @file SolverDispatch.cpp
 * @brief Resolve startup policies and launch the selected time-integrator driver.
 *
 * Startup validates execution requirements and backend capability before
 * initialization or strict checkpoint restoration. Narrow integrator entries
 * select the typed driver; timestep execution remains in the common driver.
 */

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "SolverDispatch.h"

#include "../amr/AMRControl.h"
#include "../core/RuntimeParams.h"
#include "../data/FluidState.h"
#include "../grid/Grid.h"
#include "../interface/ProblemGenerator.h"
#include "../io/IO.h"
#include "../io/chk/CheckpointCompatibility.h"
#include "../physics/eos/eosdispatch.h"
#include "dispatch/BackendCapabilities.h"
#include "dispatch/PolicyDescriptor.h"

#ifndef ARCH_CUDA_BUILD_ENABLED
#define ARCH_CUDA_BUILD_ENABLED 0
#endif

// Integrator-specific translation units expose these narrow dispatch entries.
void Dispatch_Euler(amr::AMRControl&, const SimConfig&, const SpeciesManager&,
                    const RunState&,
                    const arch::dispatch::ResolvedExecutionPlan&,
                    const arch::dispatch::ExecutionRequirements&,
                    const arch::dispatch::BackendResolution&,
                    arch::dispatch::StartupOrder&);
void Dispatch_RK2(amr::AMRControl&, const SimConfig&, const SpeciesManager&,
                  const RunState&,
                  const arch::dispatch::ResolvedExecutionPlan&,
                  const arch::dispatch::ExecutionRequirements&,
                  const arch::dispatch::BackendResolution&,
                  arch::dispatch::StartupOrder&);
void Dispatch_RK3(amr::AMRControl&, const SimConfig&, const SpeciesManager&,
                  const RunState&,
                  const arch::dispatch::ResolvedExecutionPlan&,
                  const arch::dispatch::ExecutionRequirements&,
                  const arch::dispatch::BackendResolution&,
                  arch::dispatch::StartupOrder&);

namespace
{

template <class List>
std::string_view policy_name(typename List::id_type id)
{
    const std::string_view name =
        arch::dispatch::canonical_policy_name<List>(id);
    if (!name.empty()) return name;
    throw std::logic_error("resolved policy has no descriptor");
}

const char* backend_name(arch::dispatch::ComputeBackend backend)
{
    using arch::dispatch::ComputeBackend;
    switch (backend) {
    case ComputeBackend::Cpu: return "cpu";
    case ComputeBackend::Cuda: return "cuda";
    case ComputeBackend::Auto: return "auto";
    }
    throw std::logic_error("invalid backend id");
}

void write_backend_sidecar(
    const SimConfig& config,
    const arch::dispatch::ResolvedExecutionPlan& plan,
    const arch::dispatch::BackendResolution& resolution)
{
    using namespace arch::dispatch;
    const std::filesystem::path directory = config.Get<std::string>(
        "log_dir", config.io.out_dir);
    std::filesystem::create_directories(directory);
    const std::filesystem::path path =
        directory / (config.io.base_name + "_backend_plan.txt");
    std::ofstream output(path, std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot write backend resolution sidecar");
    output << "schema=1\n"
           << "requested=" << backend_name(resolution.requested_backend) << '\n'
           << "resolved=" << backend_name(resolution.resolved_backend) << '\n'
           << "capability_code="
           << static_cast<unsigned int>(resolution.code) << '\n'
           << "fallback_reason=" << resolution.fallback_reason << '\n'
           << "device_ordinal=" << resolution.device.ordinal << '\n'
           << "device_name=" << resolution.device.device_name.data() << '\n'
           << "compute_capability=" << resolution.device.compute_major << '.'
           << resolution.device.compute_minor << '\n'
           << "runtime_version=" << resolution.device.runtime_version << '\n'
           << "driver_version=" << resolution.device.driver_version << '\n'
           << "flux=" << policy_name<FluxPolicies>(plan.flux) << '\n'
           << "reconstruction="
           << policy_name<ReconstructionPolicies>(plan.reconstruction) << '\n'
           << "limiter=" << policy_name<LimiterPolicies>(plan.limiter) << '\n'
           << "time=" << policy_name<TimeIntegratorPolicies>(
                  plan.time_integrator) << '\n'
           << "eos=" << policy_name<EosPolicies>(plan.eos) << '\n'
           << "network=" << policy_name<NetworkPolicies>(plan.network) << '\n'
           << "ode=" << policy_name<OdeSolverPolicies>(plan.ode_solver) << '\n'
           << "linear=" << policy_name<LinearSolverPolicies>(
                  plan.linear_solver) << '\n'
           << "diffusion=" << policy_name<DiffusionIntegratorPolicies>(
                  plan.diffusion_integrator) << '\n';
    if (!output)
        throw std::runtime_error("failed writing backend resolution sidecar");
}

/**
 * @brief Returns the physical label for one logical coordinate axis.
 *
 * Startup reporting exposes logical coordinate widths. Curvilinear arc lengths
 * remain the responsibility of GridMetrics in finite-volume operators.
 */
const char* axis_label(const SimConfig& config, int axis)
{
    if (config.grid.geometry == "cartesian") {
        static constexpr const char* cartesian[] = {"x", "y", "z"};
        return cartesian[axis];
    }
    if (axis == 0) return "r";
    if (config.grid.dim == 2) return "phi";
    if (config.grid.geometry == "cylindrical") {
        static constexpr const char* cylindrical[] = {"r", "z", "phi"};
        return cylindrical[axis];
    }
    static constexpr const char* spherical[] = {"r", "theta", "phi"};
    return spherical[axis];
}

/**
 * @brief Prints the Level 0..lrefinemax logical-resolution table before regridding.
 *
 * This presentation helper belongs to the dispatch translation unit because it
 * describes dispatch-time topology, not the time-evolution contract in Driver.h.
 */
void print_amr_resolution_summary(const SimConfig& config)
{
    const double dx1 = (config.grid.x1_max - config.grid.x1_min) /
                       (config.grid.nblockx1 * amr::BLOCK_NX);
    const double dx2 = config.grid.dim >= 2
        ? (config.grid.x2_max - config.grid.x2_min) / (config.grid.nblockx2 * amr::BLOCK_NY)
        : 0.0;
    const double dx3 = config.grid.dim == 3
        ? (config.grid.x3_max - config.grid.x3_min) / (config.grid.nblockx3 * amr::BLOCK_NZ)
        : 0.0;

    std::cout << ">>> AMR Levels  | Max Blocks: " << config.grid.amr_max_blocks
              << " | Finest Level: " << config.amr.lrefinemax << std::endl;
    for (int level = 0; level <= config.amr.lrefinemax; ++level) {
        const double refinement = std::ldexp(1.0, level);
        std::cout << "    Level " << level << "   | dx1(" << axis_label(config, 0)
                  << "): " << dx1 / refinement;
        if (config.grid.dim >= 2) {
            std::cout << ", dx2(" << axis_label(config, 1) << "): "
                      << dx2 / refinement;
        }
        if (config.grid.dim == 3) {
            std::cout << ", dx3(" << axis_label(config, 2) << "): "
                      << dx3 / refinement;
        }
        std::cout << std::endl;
    }
}

} // namespace

// The Public Dispatch Function

void DispatchSolver(const std::string &solver_name,
                    ProblemGenerator &problem,
                    const SimConfig &config,
                    const SpeciesManager &specs)
{
    std::cout << "[Dispatch] Initializing System..." << std::endl;

    using namespace arch::dispatch;
    StartupOrder startup_order;
    const auto requested_backend = parse_compute_backend(
        config.execution.compute_backend);
    if (!requested_backend.ok)
        throw std::runtime_error(std::string(requested_backend.error));

    const auto parsed_plan = resolve_execution_plan(
        config, [&] {
            return inspect_eos_table_rank(
                EOSDispatcher::table_path(config, "Tabular"));
        }, specs.count());
    if (!parsed_plan.ok)
        throw std::runtime_error(std::string(parsed_plan.error));
    const auto cpu_candidate = materialize_execution_plan(
        parsed_plan.value, ComputeBackend::Cpu, specs.count());
    const auto cuda_candidate = materialize_execution_plan(
        parsed_plan.value, ComputeBackend::Cuda, specs.count());
    if (!cpu_candidate.ok || !cuda_candidate.ok)
        throw std::logic_error("failed to materialize backend execution plans");
    startup_order.record(StartupEvent::Parsed);
    const auto resolved_requirements =
        resolve_execution_requirements(config, specs.count());
    if (!resolved_requirements.ok)
        throw std::runtime_error(std::string(resolved_requirements.error));
    const ExecutionRequirements& requirements = resolved_requirements.value;
    startup_order.record(StartupEvent::RequirementsBuilt);

    const RuntimeProbeResult probe = probe_runtime_native({
        requested_backend.value,
        static_cast<bool>(ARCH_CUDA_BUILD_ENABLED),
        config.execution.cuda_device});
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
    write_backend_sidecar(config, plan, backend);
    const ProblemInitializationContext initialization{plan.eos};

    const int required_ng = requirements.required_ghost_depth;
    if (required_ng > amr::MAX_NG) {
        throw std::runtime_error("Required ghost cells exceed AMR static MAX_NG!");
    }
    std::cout << "[Dispatch] Required Ghost Cell count: " << required_ng << " (Static MAX_NG: " << amr::MAX_NG << ")" << std::endl;

    // --- AMR Initialization ---
    int max_blocks = config.grid.amr_max_blocks > 0 ? config.grid.amr_max_blocks : 10000;
    amr::AMRControl amr_ctrl(max_blocks, config.grid.dim);

    amr_ctrl.tree->ConfigureRefinementSpecies(config.amr, specs);
    RunState run_state;

    if (config.io.restart)
    {
        std::cout << "[Dispatch] Restarting from checkpoint: " << config.io.restart_file << std::endl;
        const auto expected_provenance = io::inspect_checkpoint_provenance(
            config, specs, plan.eos, requirements.burn,
            canonical_policy_name<NetworkPolicies>(plan.network),
            requirements.use_nse);
        read_chk(config.io.restart_file, amr_ctrl, run_state, config, specs,
                 expected_provenance);
        std::cout << ">>> Grid Config | Dim: " << config.grid.dim
                  << " | Geometry: " << config.grid.geometry << std::endl;
        print_amr_resolution_summary(config);
    }
    else
    {
        std::cout << "[Dispatch] Initializing Root Grid (Level 0)..." << std::endl;
        amr_ctrl.tree->InitRootGrid(config, specs.count());
        std::cout << "[Dispatch] Initializing Data via Problem Generator..." << std::endl;
        problem.InitializeData(amr_ctrl, config, specs, initialization);
        std::cout << ">>> Grid Config | Dim: " << config.grid.dim
                  << " | Geometry: " << config.grid.geometry << std::endl;
        print_amr_resolution_summary(config);

        if (config.amr.lrefinemax > 0) {
            // Every initial refinement pass is completed in Driver after the
            // selected EOS evaluator, physical boundaries, and neighbor ghost
            // exchange are available. Regrid's conservative prolongation is
            // the sole owner of new fine-cell states; re-running a problem
            // initializer here would replace those cell averages and change
            // conserved integrals.
            amr_ctrl.tree->DeferInitialRefinement(config.amr.lrefinemax);
        }
    }

    std::cout << "[Dispatch] Resolving Gravity Policy..." << std::endl;
    std::string grav_type = config.physics.gravity.type;
    std::cout << "           -> Type: " << grav_type;

    if (grav_type == "external" || grav_type == "External" || grav_type == "EXTERNAL")
    {
        std::cout << " | g = (" << config.physics.gravity.g_x << ", "
                  << config.physics.gravity.g_y << ", "
                  << config.physics.gravity.g_z << ")";
    }
    else if (grav_type == "self" || grav_type == "Self" || grav_type == "SELF")
    {
        std::cout << " | G_const = " << config.physics.gravity.G_const;
    }
    std::cout << std::endl;

    if (config.physics.diffusion.use_diffusion)
    {
        std::cout << "           -> Diffusion Solver Initialized: " << config.physics.diffusion.integrator
                  << " (CFL_diff = " << config.physics.diffusion.diff_cfl << ")" << std::endl;
    }

    if (plan.time_integrator == arch::dispatch::TimeIntegratorId::Rk2)
    {
        Dispatch_RK2(amr_ctrl, config, specs, run_state, plan, requirements,
                     backend, startup_order);
    }
    else if (plan.time_integrator == arch::dispatch::TimeIntegratorId::Rk3)
    {
        Dispatch_RK3(amr_ctrl, config, specs, run_state, plan, requirements,
                     backend, startup_order);
    }
    else if (plan.time_integrator == arch::dispatch::TimeIntegratorId::Euler)
    {
        Dispatch_Euler(amr_ctrl, config, specs, run_state, plan, requirements,
                       backend, startup_order);
    }
    else
    {
        throw std::logic_error("resolved time integrator has no dispatcher");
    }
}
