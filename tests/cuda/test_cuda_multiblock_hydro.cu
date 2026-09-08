/**
 * @file test_cuda_multiblock_hydro.cu
 * @brief Check multiblock hydro exchange in the production CUDA backend.
 *
 * The cases include neighboring blocks and two-dimensional corners, where
 * ghost exchange ordering must agree with the host calculation.
 */
#include "amr/Block.h"
#include "amr/BoundaryPlan.h"
#include "amr/ExchangePlan.h"
#include "cuda/runtime/CudaBackend.h"
#include "cuda/runtime/amr/CudaBackendExchange.h"
#include "physics/eos/IdealGas.h"
#include "physics/species/Species.h"

#include <array>
#include <bit>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
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

arch::boundary::BoundaryPlan make_boundary_plan_2d()
{
    using namespace arch::boundary;
    BoundaryPlanInput input{};
    input.dimension = 2;
    input.active_extent = {amr::BLOCK_NX, amr::BLOCK_NY, 1};
    input.ghost_depth = amr::MAX_NG;
    input.faces.fill(BoundaryType::Inactive);
    for (const BoundaryAxis axis : {BoundaryAxis::X1, BoundaryAxis::X2}) {
        input.faces[face_index(axis, BoundarySide::Lower)] =
            BoundaryType::Outflow;
        input.faces[face_index(axis, BoundarySide::Upper)] =
            BoundaryType::Outflow;
    }
    return arch::boundary::make_boundary_plan(input);
}

amr::Block make_block(int id)
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
        state->InitSpecies(0);
    }
    for (int i = 0; i < block.grid.GetTotalX(); ++i) {
        const int cell = block.grid.GetIndex(i);
        const double value = 1000.0 * id + i;
        block.fluid_state.set(
            cell, {value, value + 0.25, value + 0.5, value + 0.75,
                   value + 1.0});
        block.fluid_state.enuc_rate[cell] = (i & 1) ? 0.0 : -0.0;
    }
    return block;
}

amr::Block make_block_2d(int logical_x, int logical_y)
{
    amr::Block block{};
    block.id = 2 * logical_y + logical_x;
    block.level = 0;
    block.logical_x1 = logical_x;
    block.logical_x2 = logical_y;
    block.active = true;
    block.grid = Grid(
        amr::MAX_NG, static_cast<double>(logical_x),
        static_cast<double>(logical_x + 1), static_cast<double>(logical_y),
        static_cast<double>(logical_y + 1), 0.0, 1.0, 1, 1, 0);
    block.grid.dim = 2;
    block.grid.geometry = "cartesian";
    block.grid.InitializeTopology();
    const int total = block.grid.GetTotalSize();
    for (FluidState* state : {
             &block.fluid_state, &block.state_next, &block.state_scratch}) {
        state->Preallocate(total);
        state->InitSpecies(0);
    }
    for (int j = 0; j < block.grid.GetTotalY(); ++j) {
        for (int i = 0; i < block.grid.GetTotalX(); ++i) {
            const int cell = block.grid.GetIndex(i, j);
            const double value = 100000.0 * block.id + 100.0 * j + i;
            block.fluid_state.set(
                cell, {value, value + 0.25, value + 0.5, value + 0.75,
                       value + 1.0});
            block.fluid_state.enuc_rate[cell] = ((i + j) & 1) ? 0.0 : -0.0;
        }
    }
    return block;
}

arch::backend::HostStateTransferView transfer_view(FluidState& state)
{
    return {
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        nullptr, state.rho.size(), 0, 0};
}

arch::cuda::CudaLaunchConfig make_launch_config()
{
    SimConfig config{};
    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = false;
    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Hllc,
        arch::dispatch::ReconstructionId::Ppm,
        arch::dispatch::LimiterId::MinMod,
        arch::dispatch::TimeIntegratorId::Euler,
        arch::dispatch::EosId::Ideal,
        arch::dispatch::NetworkId::None,
        arch::dispatch::OdeSolverId::None,
        arch::dispatch::LinearSolverId::None,
        arch::dispatch::DiffusionIntegratorId::None};
    return arch::cuda::make_cuda_launch_config(plan, config);
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

amr::SameLevelExchangePlan make_exchange_plan_2d(
    std::array<amr::BlockHandle, 4> handles)
{
    std::array<amr::SameLevelTopologyEntry, 4> topology{};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const int index = 2 * y + x;
            topology[index].logical = {2, 0,
                static_cast<std::uint32_t>(x),
                static_cast<std::uint32_t>(y), 0};
            topology[index].handle = handles[index];
            if (x > 0) topology[index].neighbors[0] = topology[index - 1].logical;
            if (x < 1) topology[index].neighbors[1] =
                amr::LogicalBlockKey{2, 0, static_cast<std::uint32_t>(x + 1),
                                     static_cast<std::uint32_t>(y), 0};
            if (y > 0) topology[index].neighbors[2] = topology[index - 2].logical;
            if (y < 1) topology[index].neighbors[3] =
                amr::LogicalBlockKey{2, 0, static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(y + 1), 0};
        }
    }
    return amr::make_same_level_exchange_plan(
        topology, 2, {amr::BLOCK_NX, amr::BLOCK_NY, 1}, amr::MAX_NG,
        handles[0].epoch);
}

void run_multiblock_exchange()
{
    std::array<amr::Block, 2> blocks{make_block(0), make_block(1)};
    const std::array<amr::BlockHandle, 2> handles{
        amr::BlockHandle{{101}, {7}}, amr::BlockHandle{{102}, {7}}};
    const std::array<arch::backend::StorageGeneration, 2> storage{{{201}, {202}}};
    require(handles[0].epoch == handles[1].epoch,
            "multi-block fixture mixed topology epochs");
    require(storage[0] != storage[1],
            "multi-block fixture reused a storage generation");
    const auto boundary = make_boundary_plan();
    std::array<arch::cuda::CudaBlockBinding, 2> bindings{};
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        bindings[index] = {
            &blocks[index], handles[index], storage[index], &boundary};
    }
    SpeciesManager species;
    IdealGas eos(1.4, species);
    auto backend = arch::cuda::make_cuda_backend(
        bindings, 0, make_launch_config(), species, eos);
    std::array<arch::backend::BackendStateAccess, 2> accesses{};
    for (std::size_t index = 0; index < accesses.size(); ++index) {
        accesses[index] = {
            handles[index], storage[index], arch::state::StateSlot::Current};
        backend->enqueue_upload_slot(
            accesses[index], arch::state::StateRegion::Interior,
            transfer_view(blocks[index].fluid_state));
        backend->enqueue_upload_slot(
            accesses[index], arch::state::StateRegion::Ghost,
            transfer_view(blocks[index].fluid_state));
    }
    backend->quiesce();
    require(backend->contains(accesses[0]) && backend->contains(accesses[1]),
            "multi-block backend lost a valid binding");
    auto stale = accesses[1];
    stale.storage.value += 1;
    require(!backend->contains(stale),
            "multi-block backend accepted stale storage");

    const auto plan = make_exchange_plan(handles);
    const arch::state::CompletionToken completion{
        33, arch::state::CompletionState::Complete};

    // A malformed later phase must be rejected before an earlier valid phase
    // writes any destination ghost.  This is the device-side transaction
    // boundary for compiled same-level exchange metadata.
    auto malformed = plan;
    malformed.phases[1].first += 1;
    bool malformed_rejected = false;
    try {
        (void)backend->execute_same_level_exchange(
            accesses, malformed, arch::state::StateSlot::Current,
            {9}, completion);
    } catch (const std::invalid_argument&) {
        malformed_rejected = true;
    }
    require(malformed_rejected,
            "CUDA exchange accepted malformed later phase metadata");
    std::array<FluidState, 2> rejected_download{};
    for (std::size_t index = 0; index < rejected_download.size(); ++index) {
        rejected_download[index].Preallocate(
            blocks[index].grid.GetTotalSize());
        rejected_download[index].InitSpecies(0);
        backend->enqueue_materialize_host_current(
            accesses[index], arch::state::StateRegion::Ghost,
            transfer_view(rejected_download[index]));
    }
    backend->quiesce();
    for (std::size_t index = 0; index < rejected_download.size(); ++index) {
        for (int i = 0; i < blocks[index].grid.GetTotalX(); ++i) {
            if (i >= blocks[index].grid.Is()
                && i < blocks[index].grid.Ie())
                continue;
            const int cell = blocks[index].grid.GetIndex(i);
            require(
                std::bit_cast<std::uint64_t>(
                    rejected_download[index].rho[cell])
                    == std::bit_cast<std::uint64_t>(
                        blocks[index].fluid_state.rho[cell]),
                "rejected CUDA exchange partially wrote an earlier phase");
        }
    }

    require(backend->execute_same_level_exchange(
                accesses, plan, arch::state::StateSlot::Current,
                {9}, completion) == completion,
            "CUDA exchange completion token drifted");

    std::array<FluidState, 2> downloaded{};
    for (std::size_t index = 0; index < downloaded.size(); ++index) {
        downloaded[index].Preallocate(blocks[index].grid.GetTotalSize());
        downloaded[index].InitSpecies(0);
        backend->enqueue_materialize_host_current(
            accesses[index], arch::state::StateRegion::Ghost,
            transfer_view(downloaded[index]));
    }
    backend->quiesce();

    for (int depth = 0; depth < amr::MAX_NG; ++depth) {
        const int left_source = blocks[0].grid.GetIndex(
            blocks[0].grid.Ie() - amr::MAX_NG + depth);
        const int right_ghost = blocks[1].grid.GetIndex(
            blocks[1].grid.Is() - amr::MAX_NG + depth);
        const int right_source = blocks[1].grid.GetIndex(
            blocks[1].grid.Is() + depth);
        const int left_ghost = blocks[0].grid.GetIndex(
            blocks[0].grid.Ie() + depth);
        require(
            std::bit_cast<std::uint64_t>(downloaded[1].rho[right_ghost])
                == std::bit_cast<std::uint64_t>(
                    blocks[0].fluid_state.rho[left_source]),
            "right lower ghost did not receive left active state");
        require(
            std::bit_cast<std::uint64_t>(downloaded[0].rho[left_ghost])
                == std::bit_cast<std::uint64_t>(
                    blocks[1].fluid_state.rho[right_source]),
            "left upper ghost did not receive right active state");
    }
    std::cout << "CUDA_MULTIBLOCK_EXCHANGE_PASS blocks=2 operations="
              << plan.operations.size() << '\n';
}

void run_2d_corner_exchange()
{
    std::array<amr::Block, 4> blocks{
        make_block_2d(0, 0), make_block_2d(1, 0),
        make_block_2d(0, 1), make_block_2d(1, 1)};
    const std::array<amr::BlockHandle, 4> handles{
        amr::BlockHandle{{111}, {8}}, amr::BlockHandle{{112}, {8}},
        amr::BlockHandle{{113}, {8}}, amr::BlockHandle{{114}, {8}}};
    const std::array<arch::backend::StorageGeneration, 4> storage{
        arch::backend::StorageGeneration{211},
        arch::backend::StorageGeneration{212},
        arch::backend::StorageGeneration{213},
        arch::backend::StorageGeneration{214}};
    const auto boundary = make_boundary_plan_2d();
    std::array<arch::cuda::CudaBlockBinding, 4> bindings{};
    for (std::size_t index = 0; index < bindings.size(); ++index)
        bindings[index] = {
            &blocks[index], handles[index], storage[index], &boundary};
    SpeciesManager species;
    IdealGas eos(1.4, species);
    auto backend = arch::cuda::make_cuda_backend(
        bindings, 0, make_launch_config(), species, eos);
    std::array<arch::backend::BackendStateAccess, 4> ordered{};
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        ordered[index] = {
            handles[index], storage[index], arch::state::StateSlot::Current};
        backend->enqueue_upload_slot(
            ordered[index], arch::state::StateRegion::Interior,
            transfer_view(blocks[index].fluid_state));
        backend->enqueue_upload_slot(
            ordered[index], arch::state::StateRegion::Ghost,
            transfer_view(blocks[index].fluid_state));
    }
    backend->quiesce();
    const std::array accesses{
        ordered[3], ordered[1], ordered[0], ordered[2]};
    const auto plan = make_exchange_plan_2d(handles);
    const arch::state::CompletionToken completion{
        34, arch::state::CompletionState::Complete};
    const auto before_exchange = backend->counters();
    require(backend->execute_same_level_exchange(
                accesses, plan, arch::state::StateSlot::Current,
                {10}, completion) == completion,
            "2D CUDA exchange completion token drifted");
    const auto after_exchange = backend->counters();
    const auto metadata_bytes = accesses.size() * sizeof(arch::cuda::DeviceExchangeBlock)
        + plan.operations.size() * sizeof(arch::cuda::DeviceExchangeOperation);
    require(after_exchange.bytes_h2d - before_exchange.bytes_h2d == metadata_bytes,
            "same-level metadata uploads are missing from transfer counters");
    require(after_exchange.bytes_d2h == before_exchange.bytes_d2h,
            "same-level exchange unexpectedly downloads state");

    FluidState downloaded{};
    downloaded.Preallocate(blocks[3].grid.GetTotalSize());
    downloaded.InitSpecies(0);
    backend->enqueue_materialize_host_current(
        ordered[3], arch::state::StateRegion::Ghost,
        transfer_view(downloaded));
    backend->quiesce();
    for (int dy = 0; dy < amr::MAX_NG; ++dy) {
        for (int dx = 0; dx < amr::MAX_NG; ++dx) {
            const int destination = blocks[3].grid.GetIndex(
                blocks[3].grid.Is() - amr::MAX_NG + dx,
                blocks[3].grid.Js() - amr::MAX_NG + dy);
            const int source = blocks[0].grid.GetIndex(
                blocks[0].grid.Ie() - amr::MAX_NG + dx,
                blocks[0].grid.Je() - amr::MAX_NG + dy);
            require(std::bit_cast<std::uint64_t>(downloaded.eng[destination])
                        == std::bit_cast<std::uint64_t>(
                            blocks[0].fluid_state.eng[source]),
                    "2D CUDA Y phase did not consume the X ghost corner");
        }
    }
    std::cout << "CUDA_MULTIBLOCK_CORNER_PASS blocks=4 operations="
              << plan.operations.size() << '\n';
}

} // namespace

int main()
{
    try {
        run_multiblock_exchange();
        run_2d_corner_exchange();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
