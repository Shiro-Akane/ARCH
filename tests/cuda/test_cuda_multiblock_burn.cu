#include "amr/Block.h"
#include "amr/BoundaryPlan.h"
#include "amr/ExchangePlan.h"
#include "cuda/runtime/CudaBackend.h"
#include "driver/DriverBurn.h"
#include "driver/DriverUtils.h"
#include "numerics/burnsolver/Networks.h"
#include "physics/eos/HelmEos.h"
#include "physics/species/Species.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

arch::boundary::BoundaryPlan make_boundary_plan()
{
    using namespace arch::boundary;
    BoundaryPlanInput input{};
    input.dimension = 1;
    input.active_extent = {amr::BLOCK_NX, 1, 1};
    input.ghost_depth = amr::MAX_NG;
    input.faces.fill(BoundaryType::Inactive);
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        BoundaryType::Outflow;
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
        BoundaryType::Outflow;
    return arch::boundary::make_boundary_plan(input);
}

BurnConfig make_burn_config()
{
    BurnConfig config{};
    config.use_burn = true;
    config.use_nse = false;
    config.nuclearTempMin = 1.0e8;
    config.nuclearDensMin = 1.0;
    config.smallt = 1.0e5;
    config.smallx = 1.0e-30;
    config.enucDtFactor = 0.5;
    config.odeconfig.rtol = 1.0e-4;
    config.odeconfig.atol = 1.0e-8;
    config.odeconfig.max_newton_iter = 50;
    config.odeconfig.max_substeps = 100;
    config.odeconfig.initial_dt_frac = 1.0;
    config.odeconfig.dt_safe_factor = 0.9;
    config.odeconfig.dt_fac_min = 0.1;
    config.odeconfig.dt_fac_max = 2.0;
    return config;
}

arch::cuda::CudaLaunchConfig make_launch_config(
    arch::dispatch::OdeSolverId ode)
{
    SimConfig config{};
    config.physics.eos_type = "helmholtz";
    config.physics.burn = make_burn_config();
    config.physics.diffusion.use_diffusion = false;
    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Hllc,
        arch::dispatch::ReconstructionId::Ppm,
        arch::dispatch::LimiterId::MinMod,
        arch::dispatch::TimeIntegratorId::Euler,
        arch::dispatch::EosId::Helmholtz,
        arch::dispatch::NetworkId::Aprox13,
        ode,
        arch::dispatch::LinearSolverId::DenseLu,
        arch::dispatch::DiffusionIntegratorId::None};
    return arch::cuda::make_cuda_launch_config(plan, config);
}

amr::Block make_block(int id, const HelmEos& eos)
{
    amr::Block block{};
    block.id = id;
    block.level = 0;
    block.logical_x1 = id;
    block.active = true;
    block.grid = Grid(
        amr::MAX_NG, static_cast<double>(id), static_cast<double>(id + 1),
        0.0, 1.0, 0.0, 1.0, 1, 0, 0);
    block.grid.dim = 1;
    block.grid.geometry = "cartesian";
    block.grid.InitializeTopology();
    const int total = block.grid.GetTotalSize();
    for (FluidState* state : {
             &block.fluid_state, &block.state_next, &block.state_scratch}) {
        state->Preallocate(total);
        state->InitSpecies(NetAprox13::NUM_SPECIES);
    }
    std::array<double, NetAprox13::NUM_SPECIES> composition{};
    double normalization = 0.0;
    for (int species = 0; species < NetAprox13::NUM_SPECIES; ++species) {
        composition[species] = static_cast<double>(species + 1 + id);
        normalization += composition[species];
    }
    for (double& value : composition) value /= normalization;
    constexpr double rho = 1.0e6;
    constexpr double temperature = 2.0e9;
    const double eint = eos.get_eint_from_T(
        rho, temperature, composition.data());
    const int burn_cell = block.grid.GetIndex(block.grid.Is() + id);
    for (int cell = 0; cell < total; ++cell) {
        const double cell_rho = cell == burn_cell ? rho : 1.0e-2;
        block.fluid_state.set(
            cell, {cell_rho, 0.0, 0.0, 0.0, cell_rho * eint});
        block.fluid_state.enuc_rate[cell] = -6.25;
        for (int species = 0; species < NetAprox13::NUM_SPECIES; ++species)
            block.fluid_state.X(species, cell) = composition[species];
    }
    return block;
}

arch::backend::HostStateTransferView transfer_view(FluidState& state)
{
    return {
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        state.mass_fractions.data(), state.rho.size(),
        NetAprox13::NUM_SPECIES, state.rho.size()};
}

amr::SameLevelExchangePlan make_exchange_plan(
    std::array<amr::BlockHandle, 2> handles)
{
    const amr::LogicalBlockKey left{1, 0, 0, 0, 0};
    const amr::LogicalBlockKey right{1, 0, 1, 0, 0};
    std::array<amr::SameLevelTopologyEntry, 2> topology{};
    topology[0].logical = left;
    topology[0].handle = handles[0];
    topology[0].neighbors[1] = right;
    topology[1].logical = right;
    topology[1].handle = handles[1];
    topology[1].neighbors[0] = left;
    return amr::make_same_level_exchange_plan(
        topology, 1, {amr::BLOCK_NX, 1, 1}, amr::MAX_NG,
        handles[0].epoch);
}

void run_route(arch::dispatch::OdeSolverId ode, const char* name)
{
    SpeciesManager species;
    NetAprox13::RegisterSpecies(species);
    const std::string table = std::string(ARCH_SOURCE_DIR)
        + "/EOS_toolkit/tables/helmholtz/helm_table.dat";
    HelmEos eos(table, &species);
    std::array<amr::Block, 2> blocks{make_block(0, eos), make_block(1, eos)};
    const std::array<amr::BlockHandle, 2> handles{
        amr::BlockHandle{{501}, {13}}, amr::BlockHandle{{502}, {13}}};
    const std::array<arch::backend::StorageGeneration, 2> storage{{{601}, {602}}};
    const auto boundary_plan = make_boundary_plan();
    std::array<arch::cuda::CudaBlockBinding, 2> bindings{};
    for (std::size_t index = 0; index < bindings.size(); ++index)
        bindings[index] = {
            &blocks[index], handles[index], storage[index], &boundary_plan};
    auto backend = arch::cuda::make_cuda_backend(
        bindings, 0, make_launch_config(ode), species, eos);
    std::array<arch::backend::BackendStateAccess, 2> current{};
    arch::state::StateResidencyLedger ledger({13});
    for (std::size_t index = 0; index < current.size(); ++index) {
        current[index] = {
            handles[index], storage[index], arch::state::StateSlot::Current};
        ledger.register_block(
            handles[index], {1}, {1, arch::state::CompletionState::Complete});
        ledger.publish_ghost(
            {handles[index], current[index].slot},
            arch::state::ExecutionSide::Host, {1},
            {2, arch::state::CompletionState::Complete});
    }
    arch::scheduler::MonotonicSchedulerClock clock(2, 1);
    for (std::size_t index = 0; index < current.size(); ++index)
        (void)arch::backend::transfer_state_regions(
            *backend, ledger, clock, current[index],
            transfer_view(blocks[index].fluid_state),
            arch::state::PendingTransferPhase::PendingH2D);
    arch::scheduler::StageExecutionContext context{
        arch::state::ExecutionSide::Device, ledger, clock};
    constexpr double burn_dt = 1.0e-16;
    std::array<arch::backend::BurnExecutionResult, 2> results{};
    (void)arch::scheduler::execute_burn_first_lane(
        context, handles, [&](arch::state::CompletionToken token) {
            for (std::size_t index = 0; index < current.size(); ++index) {
                results[index] = backend->execute_burn(
                    current[index], burn_dt, token);
                require(results[index].completion.value == token.value
                            && arch::state::is_complete(results[index].completion),
                        "multi-block burn completion token drifted");
            }
            return token;
        });

    std::vector<arch::reduction::ReductionCandidate> candidates;
    for (std::size_t index = 0; index < results.size(); ++index) {
        require(results[index].status == 0 && results[index].failed_cells == 0,
                "multi-block burn reported a failed cell");
        candidates.push_back({
            results[index].dt_recommended,
            DriverReduction::make_block_reduction_key(
                0, static_cast<std::uint64_t>(index),
                static_cast<int>(index), 0, 0,
                DriverReduction::BlockReductionComponent::BurnFirstHalf),
            true});
    }
    const double global = DriverReduction::reduce_block_minimum(
        DriverBurn::INACTIVE_LIMITER_CANDIDATE, candidates);
    std::reverse(candidates.begin(), candidates.end());
    const double permuted = DriverReduction::reduce_block_minimum(
        DriverBurn::INACTIVE_LIMITER_CANDIDATE, candidates);
    require(std::bit_cast<std::uint64_t>(global)
                == std::bit_cast<std::uint64_t>(permuted),
            "multi-block burn reduction depends on traversal order");

    const auto exchange_plan = make_exchange_plan(handles);
    const auto version = ledger.inspect({handles[0], current[0].slot})
        .interior.version;
    (void)arch::scheduler::complete_boundary(
        context, handles, current[0].slot, version,
        [&](arch::state::StateSlot slot, arch::state::StateVersion selected,
            arch::state::CompletionToken token) {
            std::array<arch::backend::BackendStateAccess, 2> accesses{};
            for (std::size_t index = 0; index < current.size(); ++index) {
                accesses[index] = current[index];
                accesses[index].slot = slot;
                (void)backend->execute_physical_boundary(
                    accesses[index], selected, token);
            }
            return backend->execute_same_level_exchange(
                accesses, exchange_plan, slot, selected, token);
        });

    std::array<FluidState, 2> downloaded{};
    for (std::size_t index = 0; index < downloaded.size(); ++index) {
        downloaded[index].Preallocate(blocks[index].grid.GetTotalSize());
        downloaded[index].InitSpecies(NetAprox13::NUM_SPECIES);
        (void)arch::backend::transfer_state_regions(
            *backend, ledger, clock, current[index], transfer_view(downloaded[index]),
            arch::state::PendingTransferPhase::PendingD2H);
        const int burn_cell = blocks[index].grid.GetIndex(
            blocks[index].grid.Is() + static_cast<int>(index));
        double sum = 0.0;
        for (int species_index = 0;
             species_index < NetAprox13::NUM_SPECIES; ++species_index)
            sum += downloaded[index].X(species_index, burn_cell);
        require(std::isfinite(downloaded[index].eng[burn_cell])
                    && std::abs(sum - 1.0) < 1.0e-8,
                "multi-block burn composition commit is invalid");
        const int sentinel = blocks[index].grid.GetIndex(
            blocks[index].grid.Is() + 3);
        require(downloaded[index].enuc_rate[sentinel] == 0.0,
                "multi-block burn did not clear whole-field ENUC");
    }
    std::cout << "CUDA_MULTIBLOCK_BURN_PASS route=" << name
              << " blocks=2 limiter=" << global << '\n';
}

} // namespace

int main()
{
    try {
        run_route(arch::dispatch::OdeSolverId::BeNr, "aprox13.be_nr");
        run_route(arch::dispatch::OdeSolverId::Bd, "aprox13.bd");
        run_route(arch::dispatch::OdeSolverId::Ros4, "aprox13.ros4");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
