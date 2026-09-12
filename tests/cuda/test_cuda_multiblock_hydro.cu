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

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
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

arch::cuda::CudaLaunchConfig make_launch_config(
    arch::dispatch::TimeIntegratorId method = arch::dispatch::TimeIntegratorId::Euler)
{
    SimConfig config{};
    config.physics.burn.use_burn = false;
    config.physics.diffusion.use_diffusion = false;
    const arch::dispatch::ResolvedExecutionPlan plan{
        arch::dispatch::FluxId::Hllc,
        arch::dispatch::ReconstructionId::Ppm,
        arch::dispatch::LimiterId::MinMod,
        method,
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

void run_hydro_batch_contract()
{
    using namespace arch;
    using state::StateSlot;
    const std::array methods{
        std::pair{scheduler::HydroMethod::Euler, dispatch::TimeIntegratorId::Euler},
        std::pair{scheduler::HydroMethod::RK2, dispatch::TimeIntegratorId::Rk2},
        std::pair{scheduler::HydroMethod::RK3, dispatch::TimeIntegratorId::Rk3}};
    for (const auto [method, route] : methods) {
        std::array<amr::Block, 2> blocks{make_block(0), make_block(1)};
        for (auto& block : blocks) {
            for (int i = 0; i < block.grid.GetTotalX(); ++i) {
                const double rho = 1.0 + 0.1 * block.id + 0.001 * i;
                block.fluid_state.set(block.grid.GetIndex(i),
                    {rho, 0.1 * rho, 0.0, 0.0, 3.0 + 0.005 * rho});
            }
        }
        const auto boundary = make_boundary_plan();
        const std::array<backend::BackendStateAccess, 2> accesses{{
            {{{701}, {19}}, {801}, StateSlot::Current},
            {{{702}, {19}}, {802}, StateSlot::Current}}};
        std::array<cuda::CudaBlockBinding, 2> bindings;
        for (std::size_t i = 0; i < blocks.size(); ++i)
            bindings[i] = {&blocks[i], accesses[i].block, accesses[i].storage, &boundary};
        SpeciesManager species;
        IdealGas eos(1.4, species);
        auto scalar = cuda::make_cuda_backend(bindings, 0, make_launch_config(route), species, eos);
        auto batch = cuda::make_cuda_backend(bindings, 0, make_launch_config(route), species, eos);
        const auto upload = [&](cuda::CudaBackend& target) {
            for (std::size_t i = 0; i < blocks.size(); ++i)
                for (const auto slot : {StateSlot::Current, StateSlot::Next, StateSlot::Scratch})
                    for (const auto region : {state::StateRegion::Interior, state::StateRegion::Ghost}) {
                        auto access = accesses[i];
                        access.slot = slot;
                        target.enqueue_upload_slot(access, region, transfer_view(blocks[i].fluid_state));
                    }
            target.quiesce();
        };
        upload(*scalar);
        upload(*batch);
        const std::array<double, 2> reference_dt{
            scalar->compute_hydro_dt(accesses[0], 0.8),
            scalar->compute_hydro_dt(accesses[1], 0.8)};
        // Grow 1 -> 2, reorder, shrink 2 -> 1 and repeat, without stale tails.
        require(batch->compute_hydro_dt(accesses[0], 0.8) == reference_dt[0], "single CFL changed");
        const std::array reversed{accesses[1], accesses[0]};
        const auto before = batch->counters();
        const auto values = batch->compute_hydro_dt_batch(reversed, 0.8);
        const auto after = batch->counters();
        require(values == std::vector<double>({reference_dt[1], reference_dt[0]}),
                "CFL batch result order/value drifted");
        require(after.stream_sync_count - before.stream_sync_count == 1
            && after.kernel_count - before.kernel_count == 4
            && after.bytes_d2h - before.bytes_d2h == 2 * (sizeof(double) + sizeof(int)),
            "CFL batch did not use one completion boundary");
        require(batch->compute_hydro_dt(accesses[1], 0.8) == reference_dt[1], "CFL capacity reuse drifted");
        const auto plan = scheduler::make_hydro_plan(method);
        const state::CompletionToken token{99, state::CompletionState::Complete};
        const auto empty_before = batch->counters();
        require(batch->compute_hydro_dt_batch({}, 0.8).empty()
            && batch->execute_hydro_stage_batch({}, plan.stages.front(), 0.01, token) == token,
            "empty local Hydro batch is not a no-op");
        const auto empty_after = batch->counters();
        require(empty_before.kernel_count == empty_after.kernel_count
            && empty_before.bytes_d2h == empty_after.bytes_d2h
            && empty_before.stream_sync_count == empty_after.stream_sync_count,
            "empty batch submitted CUDA work");
        for (int fault = 0; fault < 3; ++fault) {
            auto invalid = accesses;
            if (fault == 0) invalid[1] = invalid[0];
            if (fault == 1) invalid[1].storage.value += 100;
            if (fault == 2) invalid[1].slot = StateSlot::Scratch;
            bool dt_rejected = false, stage_rejected = false;
            try { (void)batch->compute_hydro_dt_batch(invalid, 0.8); }
            catch (const std::invalid_argument&) { dt_rejected = true; }
            try { (void)batch->execute_hydro_stage_batch(invalid, plan.stages.front(), 0.01, token); }
            catch (const std::invalid_argument&) { stage_rejected = true; }
            const auto rejected = batch->counters();
            require(dt_rejected && stage_rejected
                && rejected.kernel_count == empty_after.kernel_count
                && rejected.stream_sync_count == empty_after.stream_sync_count,
                "invalid later Hydro access submitted work before rejection");
        }
        const double dt = 0.1 * std::min(reference_dt[0], reference_dt[1]);
        for (const auto slot : {StateSlot::Current, StateSlot::Next, StateSlot::Scratch}) {
            auto selected = accesses;
            for (auto& access : selected) {
                access.slot = slot;
                (void)scalar->execute_physical_boundary(access, {1}, token);
            }
            const auto start = batch->counters();
            require(batch->execute_physical_boundary_batch(selected, {1}, token) == token,
                    "boundary batch token drifted");
            const auto done = batch->counters();
            require(done.kernel_count - start.kernel_count == 1
                && done.stream_sync_count - start.stream_sync_count == 1,
                "1D physical boundary was not batched across blocks");
        }
        for (const auto& descriptor : plan.stages) {
            for (const auto access : accesses)
                (void)scalar->execute_hydro_stage(access, descriptor, dt, token);
            const auto start = batch->counters();
            require(batch->execute_hydro_stage_batch(accesses, descriptor, dt, token) == token,
                    "Hydro batch completion token drifted");
            const auto done = batch->counters();
            require(done.stream_sync_count - start.stream_sync_count == 1
                && done.bytes_d2h - start.bytes_d2h == 2 * sizeof(int)
                && done.kernel_count - start.kernel_count == 4,
                "Hydro stage synchronized per block");
            if (descriptor.refresh_ghost_after) {
                for (auto access : accesses) {
                    access.slot = descriptor.output_slot;
                    (void)scalar->execute_physical_boundary(access, {1}, token);
                    (void)batch->execute_physical_boundary(access, {1}, token);
                }
            }
        }
        for (const auto access : accesses) {
            scalar->rotate_slots(access, plan.final_rotation);
            batch->rotate_slots(access, plan.final_rotation);
        }
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            FluidState a, b;
            a.Preallocate(blocks[i].grid.GetTotalSize()); a.InitSpecies(0);
            b.Preallocate(blocks[i].grid.GetTotalSize()); b.InitSpecies(0);
            scalar->enqueue_materialize_host_current(accesses[i], state::StateRegion::Interior, transfer_view(a));
            batch->enqueue_materialize_host_current(accesses[i], state::StateRegion::Interior, transfer_view(b));
            scalar->quiesce(); batch->quiesce();
            for (int x = blocks[i].grid.Is(); x < blocks[i].grid.Ie(); ++x) {
                const int cell = blocks[i].grid.GetIndex(x);
                for (const auto field : {std::pair{&a.rho, &b.rho}, {&a.mom_u, &b.mom_u},
                         {&a.mom_v, &b.mom_v}, {&a.mom_w, &b.mom_w}, {&a.eng, &b.eng},
                         {&a.enuc_rate, &b.enuc_rate}})
                    require(std::bit_cast<std::uint64_t>((*field.first)[cell])
                        == std::bit_cast<std::uint64_t>((*field.second)[cell]),
                        "Hydro scalar/batch interior bits differ");
            }
        }
        // Exercise first AND last block failures, then prove a valid reuse can
        // clear the old latch. A ghost-only fault isolates the stage EOS path.
        for (std::size_t bad = 0; bad < blocks.size(); ++bad) {
            for (const bool ghost_only : {false, true}) {
                const int x = blocks[bad].grid.Is() - (ghost_only ? 1 : 0);
                const int cell = blocks[bad].grid.GetIndex(x);
                auto& energy = blocks[bad].fluid_state.eng[cell];
                const double saved = energy;
                // IdealGas's existing max(0, pressure) maps NaN energy to a
                // finite pressure, so NaN does not establish an EOS failure.
                // Positive infinity survives that floor. Check the shared
                // leaf first so the error-transport witness cannot go vacuous.
                energy = std::numeric_limits<double>::infinity();
                require(!std::isfinite(eos.get_view().get_pressure(
                            blocks[bad].fluid_state.get(cell), nullptr)),
                        "Hydro EOS fault fixture produced a finite pressure");
                upload(*batch);
                bool rejected = false;
                std::string failure_detail = "no exception";
                try {
                    if (ghost_only)
                        (void)batch->execute_hydro_stage_batch(accesses, plan.stages.front(), dt, token);
                    else
                        (void)batch->compute_hydro_dt_batch(accesses, 0.8);
                } catch (const std::runtime_error& error) {
                    failure_detail = error.what();
                    rejected = std::string(error.what()).find("block="
                        + std::to_string(accesses[bad].block.uid.value)) != std::string::npos;
                }
                if (!rejected)
                    throw std::runtime_error("Hydro batch EOS failure witness: method="
                        + std::to_string(static_cast<int>(method)) + " block="
                        + std::to_string(accesses[bad].block.uid.value) + " path="
                        + (ghost_only ? "stage-ghost" : "CFL-interior") + " observed="
                        + failure_detail);
                energy = saved;
                upload(*batch);
                require(batch->compute_hydro_dt_batch(accesses, 0.8)
                        == std::vector<double>({reference_dt[0], reference_dt[1]}),
                        "CFL batch reused a stale failure latch");
                require(batch->execute_hydro_stage_batch(accesses, plan.stages.front(), dt, token) == token,
                        "Hydro batch reused a stale EOS failure latch");
            }
        }
    }
    std::cout << "CUDA_HYDRO_BATCH_CONTRACT_PASS methods=3 blocks=2\n";
}

} // namespace

int main()
{
    try {
        run_multiblock_exchange();
        run_2d_corner_exchange();
        run_hydro_batch_contract();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
