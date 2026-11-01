/**
 * @file SolverDispatch.cpp
 * @brief Resolve startup policies and launch the selected time-integrator driver.
 *
 * Startup validates execution requirements and backend capability before
 * initialization or strict checkpoint restoration. Narrow integrator entries
 * select the typed driver; timestep execution remains in the common driver.
 * Workflow:
 * 1. Validate startup configuration, backend capability and restart provenance;
 *    restore the same resolved chart/state identity as a fresh initialization.
 * 2. Resolve compile-time EOS, flux and integrator policy bindings.
 * 3. Start one Driver runtime without placing numerical formulas in dispatch.
 */

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "driver/initialization/InitialMesh.h"
#include "driver/SolverDispatch.h"

#include "amr/AMRControl.h"
#include "core/config/RuntimeParams.h"
#include "core/config/RuntimeConfiguration.h"
#include "data/FluidState.h"
#include "grid/Grid.h"
#include "numerics/curvilinear/GridMetrics.h"
#include "interface/ProblemGenerator.h"
#include "physics/boundary/UserBoundary.h"
#include "io/IO.h"
#include "io/OutputPath.h"
#include "io/chk/CheckpointCompatibility.h"
#include "driver/dispatch/ExecutionPreflight.h"
#include "driver/dispatch/PolicyDescriptor.h"

// Integrator-specific translation units expose these narrow dispatch entries.
void Dispatch_Euler(amr::AMRControl&, const arch::config::RuntimeConfiguration&,
                    const RunState&,
                    const arch::dispatch::ResolvedExecutionPlan&,
                    const arch::dispatch::ExecutionRequirements&,
                    const arch::dispatch::BackendResolution&,
                    arch::dispatch::StartupOrder&);
void Dispatch_RK2(amr::AMRControl&, const arch::config::RuntimeConfiguration&,
                  const RunState&,
                  const arch::dispatch::ResolvedExecutionPlan&,
                  const arch::dispatch::ExecutionRequirements&,
                  const arch::dispatch::BackendResolution&,
                  arch::dispatch::StartupOrder&);
void Dispatch_RK3(amr::AMRControl&, const arch::config::RuntimeConfiguration&,
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
    const std::filesystem::path directory = io::output_directory(config.Get<std::string>(
        "log_dir", config.io.out_dir));
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
    if (config.grid.geometry == "cylindrical") {
        static constexpr const char* cylindrical[] = {"r", "z", "phi"};
        return cylindrical[axis];
    }
    if (config.grid.dim == 2) return "phi";
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

void DispatchSolver(ProblemGenerator &problem,
                    const arch::config::PreparedConfiguration &prepared)
{
    if (!prepared.belongs_to(problem))
        throw std::invalid_argument("Prepared configuration belongs to another model instance.");
    const auto& requested_config = prepared.config();
    if (requested_config.LoadedInput()->purpose != arch::config::ConfigurationPurpose::Evolution)
        throw std::invalid_argument("Initial-state preparation cannot start time evolution.");
    const auto& specs = prepared.species();
    // Own the effective configuration for the complete driver lifetime. Auto
    // selection must reach CPU and device views, initialisation and restart IO.
    SimConfig effective = requested_config;
    auto resolved_boundaries = arch::boundary::ResolveCaseBoundaries(
        problem.RegisteredName(), problem.SourceFile(), effective);
    // Setup can feed callback-visible values. Include its compiled identity as
    // well as sibling callback digests; output paths remain portable metadata.
    if (resolved_boundaries.physical || resolved_boundaries.gravity)
        resolved_boundaries.identity += ";case-sha=" + problem.SourceSha256();
    arch::boundary::ScopedUserBoundarySelection boundary_selection(
        std::move(resolved_boundaries), effective, specs);
    std::cout << "[Dispatch] Initializing System..." << std::endl;

    using namespace arch::dispatch;
    StartupOrder startup_order;
    const auto preflight = ResolveExecutionPreflight(effective, specs, startup_order);
    const arch::config::RuntimeConfiguration runtime(effective, prepared);
    const auto& config = runtime.config();
    const auto& plan = preflight.plan;
    const auto& requirements = preflight.requirements;
    const auto& backend = preflight.backend;
    write_backend_sidecar(config, plan, backend);
    // The shared Grid owner validates the retained chart; initialization lends only EOS identity.
    const ProblemInitializationContext initialization{plan.eos};

    const int required_ng = requirements.required_ghost_depth;
    if (required_ng > amr::MAX_NG) {
        throw std::runtime_error("Required ghost cells exceed AMR static MAX_NG!");
    }
    std::cout << "[Dispatch] Required Ghost Cell count: " << required_ng << " (Static MAX_NG: " << amr::MAX_NG << ")" << std::endl;

    // --- AMR Initialization ---
    int max_blocks = config.grid.amr_max_blocks > 0 ? config.grid.amr_max_blocks : 10000;
    int rank = 0, rank_count = 1;
#if ARCH_MPI_ENABLED
    int initialized = 0;
    MPI_Initialized(&initialized);
    if (initialized) {
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &rank_count);
    }
#endif
    if (rank_count > 1) {
        const auto transverse_roots = static_cast<std::int64_t>(std::max(1, config.grid.nblockx2))
            * std::max(1, config.grid.nblockx3);
        // Check before multiplying by X, including inputs near the integer limit.
        if (transverse_roots > max_blocks || config.grid.nblockx1 > max_blocks / transverse_roots)
            throw std::invalid_argument("MPI global max_blocks is smaller than the root mesh");
        const int global_roots = static_cast<int>(config.grid.nblockx1 * transverse_roots);
        // Keep the original pool and free list. Only its per-rank capacity
        // changes; no shadow blocks or second field allocator is introduced.
        const int roots =
            static_cast<int>(static_cast<std::int64_t>(config.grid.nblockx1) * (rank + 1) / rank_count -
                             static_cast<std::int64_t>(config.grid.nblockx1) * rank / rank_count)
            * static_cast<int>(transverse_roots);
        // Reserve owned roots first, then share spare slots. Clamping an
        // independently divided capacity could exceed the global max_blocks.
        const int spare_blocks = max_blocks - global_roots;
        const int local_spare =
            static_cast<int>(static_cast<std::int64_t>(spare_blocks) * (rank + 1) / rank_count -
                             static_cast<std::int64_t>(spare_blocks) * rank / rank_count);
        max_blocks = roots + local_spare;
    }
    amr::AMRControl amr_ctrl(max_blocks, config.grid.dim);
    amr_ctrl.tree->SetRootPartition(rank, rank_count);

    amr_ctrl.tree->ConfigureRefinementSpecies(config.amr, specs);
    RunState run_state;

    if (config.io.restart)
    {
        std::cout << "[Dispatch] Restarting from checkpoint: " << config.io.restart_file << std::endl;
        const auto expected_provenance = io::inspect_checkpoint_provenance(
            config, specs, plan.eos, requirements.burn,
            canonical_policy_name<NetworkPolicies>(plan.network),
            requirements.use_nse);
        // The same resolved identity owns fresh initialization and strict
        // restart; a legacy filename never acquires a new chart by itself.
        const auto expected_geometry = io::checkpoint_geometry_identity(config.grid.dim, config.grid.geometry);
        read_chk(config.io.restart_file, amr_ctrl, run_state, config, specs,
                 expected_provenance, expected_geometry);
        std::cout << ">>> Grid Config | Dim: " << config.grid.dim
                  << " | Geometry: " << config.grid.geometry << std::endl;
        print_amr_resolution_summary(config);
    }
    else
    {
        std::cout << "[Dispatch] Initializing Root Grid (Level 0)..." << std::endl;
        arch::driver::InitializeRootState(amr_ctrl, problem, config, specs, initialization);
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

    if (rank_count > 1) {
        std::ofstream partition(io::output_directory(config.io.out_dir) + "/partition.txt");
        if (!partition)
            throw std::runtime_error("cannot write MPI partition metadata");
        partition << "schema=1\nrank=" << rank << "\nsize=" << rank_count
                  << "\nroot_begin=" << amr_ctrl.tree->RootBegin()
                  << "\nroot_end=" << amr_ctrl.tree->RootEnd()
                  << "\npartition_axis=0\ntransverse_root_blocks="
                  << std::max(1, config.grid.nblockx2) * std::max(1, config.grid.nblockx3)
                  << "\nglobal_root_blocks=" << static_cast<std::int64_t>(config.grid.nblockx1)
                      * std::max(1, config.grid.nblockx2) * std::max(1, config.grid.nblockx3)
                  << "\nlocal_pool_capacity=" << max_blocks
                  << "\noutput_scope=rank-local\nrestart_supported=false\n";
        partition.close();
        if (!partition)
            throw std::runtime_error("cannot finish MPI partition metadata");
        std::cout << "[MPI] rank=" << rank << '/' << rank_count << " owned roots=["
                  << amr_ctrl.tree->RootBegin() << ',' << amr_ctrl.tree->RootEnd()
                  << ") pool=" << max_blocks << "; rank-local plots, no checkpoints" << std::endl;
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
        std::cout << " | G_const = " << arch::constants::gravity::cgs::gravitational_constant;
    }
    std::cout << std::endl;

    if (config.physics.diffusion.use_diffusion)
    {
        std::cout << "           -> Diffusion Solver Initialized: " << config.physics.diffusion.integrator
                  << " (CFL_diff = " << config.physics.diffusion.diff_cfl << ")" << std::endl;
    }

    if (plan.time_integrator == arch::dispatch::TimeIntegratorId::Rk2)
    {
        Dispatch_RK2(amr_ctrl, runtime, run_state, plan, requirements,
                     backend, startup_order);
    }
    else if (plan.time_integrator == arch::dispatch::TimeIntegratorId::Rk3)
    {
        Dispatch_RK3(amr_ctrl, runtime, run_state, plan, requirements,
                     backend, startup_order);
    }
    else if (plan.time_integrator == arch::dispatch::TimeIntegratorId::Euler)
    {
        Dispatch_Euler(amr_ctrl, runtime, run_state, plan, requirements,
                       backend, startup_order);
    }
    else
    {
        throw std::logic_error("resolved time integrator has no dispatcher");
    }
}
