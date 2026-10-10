/**
 * @file test_cuda_multiblock_hydro.cu
 * @brief Check multiblock hydro exchange in the production CUDA backend.
 *
 * The cases include neighboring blocks and two-dimensional corners, where
 * ghost exchange ordering must agree with the host calculation.
 */
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "amr/AMRControl.h"
#include "amr/exchange/BoundaryPlan.h"
#include "amr/exchange/ExchangePlan.h"
#include "amr/flux/AmrFluxExecutionPlan.h"
#include "amr/refinement/RefinementIndicatorMath.h"
#include "amr/storage/Block.h"
#include "cuda/runtime/CudaBackend.h"
#include "cuda/runtime/amr/CudaBackendExchange.h"
#include "cuda/runtime/hydro/CudaBackendHydro.h"
#include "driver/runtime/RuntimeStateTransaction.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverMacroStep.h"
#include "driver/stages/GravityStage.h"
#include "numerics/flux/FluxHLL.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "numerics/state/RzNativeClosure.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/ExternalGravity.h"
#include "physics/gravity/GravitySource.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/species/Species.h"

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
        state.mass_fractions.empty()?nullptr:state.mass_fractions.data(),state.rho.size(),
        static_cast<std::size_t>(state.GetNumSpecies()),state.GetNumSpecies()>0?state.rho.size():0};
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

void verify_indicator_batch(arch::cuda::CudaBackend& scalar, arch::cuda::CudaBackend& batch,
    const std::array<arch::backend::BackendStateAccess,2>& accesses)
{
    AmrConfig config{};
    config.refine_on_rho=true;
    // Grow/shrink the binding list and change the arena layout in both
    // directions. Repeat this whole check after Hydro rotates Current slots.
    for (bool thermo : {false,true,false}) {
        config.refine_on_p=config.refine_on_temp=config.refine_on_entropy=thermo;
        std::vector<double> expected;
        for (const auto& access : accesses) {
            const auto one=scalar.evaluate_refinement_indicators(
                std::span(&access,1),config,1e-12,{});
            require(one.size()==1 && std::isfinite(one[0]),"invalid scalar indicator control");
            expected.push_back(one[0]);
        }
        const auto first=batch.evaluate_refinement_indicators(std::span(accesses).first(1),config,1e-12,{});
        require(first==std::vector<double>{expected[0]},"indicator small binding reuse changed value");
        const std::array reversed{accesses[1],accesses[0]};
        const auto before=batch.counters();
        const auto actual=batch.evaluate_refinement_indicators(reversed,config,1e-12,{});
        const auto after=batch.counters();
        require(actual.size()==2 && after.kernel_count-before.kernel_count==(thermo?4:3)
            && after.stream_sync_count-before.stream_sync_count==1
            && after.bytes_d2h-before.bytes_d2h==2*sizeof(double),
            "production indicator batch was not fused with one completion boundary");
        for (std::size_t i=0;i<2;++i)
            require(std::bit_cast<std::uint64_t>(actual[i])==std::bit_cast<std::uint64_t>(expected[1-i]),
                "production indicator batch changed block result/order");
        for (int fault=0;fault<4;++fault) {
            auto invalid=accesses;
            if (fault==0) invalid[1]=invalid[0];
            if (fault==1) ++invalid[1].storage.value;
            if (fault==2) invalid[1].slot=arch::state::StateSlot::Next;
            if (fault==3) ++invalid[1].block.epoch.value;
            bool rejected=false;
            try { (void)batch.evaluate_refinement_indicators(invalid,config,1e-12,{}); }
            catch (const std::invalid_argument&) { rejected=true; }
            const auto done=batch.counters();
            require(rejected && done.kernel_count==after.kernel_count
                && done.bytes_h2d==after.bytes_h2d && done.bytes_d2h==after.bytes_d2h
                && done.stream_sync_count==after.stream_sync_count,
                "invalid late indicator access submitted work before rejection");
        }
    }
}

// Routing/storage witness; the separate accepted-cell test owns independent
// Decimal/caloric science references. Do not treat scalar-vs-batch as a new oracle.
void verify_jeans_consumer(arch::cuda::CudaBackend& scalar, arch::cuda::CudaBackend& batch,
    const std::array<arch::backend::BackendStateAccess,2>& accesses)
{
    std::vector<double> expected;
    for (const auto& access : accesses) {
        const auto one=scalar.evaluate_jeans_resolution({&access,1});
        require(one.size()==1 && std::isfinite(one.front()),"invalid scalar JENS control");
        expected.push_back(one.front());
    }
    require(batch.evaluate_jeans_resolution(std::span(accesses).first(1))
        ==std::vector<double>{expected.front()},"JENS scratch shrink result drifted");
    const std::array reversed{accesses[1],accesses[0]};
    const auto before=batch.counters();
    const auto result=batch.evaluate_jeans_resolution(reversed);
    const auto after=batch.counters();
    require(result==std::vector<double>({expected[1],expected[0]}),"JENS block order/value drifted");
    require(after.kernel_count-before.kernel_count==4
        && after.bytes_d2h-before.bytes_d2h==2*sizeof(double)
        && after.bytes_h2d==before.bytes_h2d
        && after.stream_sync_count-before.stream_sync_count==1,
        "JENS consumer materialized fields or used extra completion boundaries");
    for(int fault=0;fault<4;++fault) {
        auto invalid=accesses;
        if(fault==0)invalid[1]=invalid[0];
        if(fault==1)++invalid[1].storage.value;
        if(fault==2)invalid[1].slot=arch::state::StateSlot::Next;
        if(fault==3)++invalid[1].block.epoch.value;
        bool rejected=false;
        try {(void)batch.evaluate_jeans_resolution(invalid);}
        catch(const std::invalid_argument&){rejected=true;}
        const auto done=batch.counters();
        require(rejected && done.kernel_count==after.kernel_count
            && done.bytes_h2d==after.bytes_h2d && done.bytes_d2h==after.bytes_d2h
            && done.stream_sync_count==after.stream_sync_count,
            "late invalid JENS access partially submitted work");
    }
    require(batch.evaluate_jeans_resolution({}).empty(),"empty JENS batch was not a no-op");
    const auto empty=batch.counters();
    require(empty.kernel_count==after.kernel_count && empty.bytes_d2h==after.bytes_d2h
        && empty.stream_sync_count==after.stream_sync_count,"empty JENS batch submitted work");
    std::cout<<"CUDA_JEANS_BACKEND_CURRENT_PASS blocks=2\n";
}

void verify_jeans_failure_recovery(arch::cuda::CudaBackend& backend,
    const std::array<arch::backend::BackendStateAccess,2>& accesses,
    std::array<amr::Block,2>& blocks)
{
    const auto original=backend.evaluate_jeans_resolution(accesses);
    for(int fault=0;fault<3;++fault) {
        FluidState damaged=blocks[1].fluid_state;
        const auto& grid=blocks[1].grid;
        const int cell=grid.GetIndex(grid.Is(),grid.Js(),grid.Ks());
        if(fault==0)damaged.rho[cell]=0.;
        if(fault==1)damaged.rho[cell]=std::numeric_limits<double>::quiet_NaN();
        if(fault==2)damaged.eng[cell]=-1.;
        backend.enqueue_upload_slot(accesses[1],arch::state::StateRegion::Interior,
            transfer_view(damaged));
        backend.quiesce();
        bool rejected=false;
        try {(void)backend.evaluate_jeans_resolution(accesses);}
        catch(const std::runtime_error&){rejected=true;}
        require(rejected,"invalid second-block accepted cell did not reject JENS batch");
        backend.enqueue_upload_slot(accesses[1],arch::state::StateRegion::Interior,
            transfer_view(blocks[1].fluid_state));
        backend.quiesce();
        require(backend.evaluate_jeans_resolution(accesses)==original,
            "JENS recovery reused an invalid latch/value or changed valid source");
    }
    std::cout<<"CUDA_JEANS_BACKEND_FAILURE_RECOVERY_PASS cases=3\n";
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
        verify_indicator_batch(*scalar,*batch,accesses);
        verify_jeans_consumer(*scalar,*batch,accesses);
        verify_jeans_failure_recovery(*batch,accesses,blocks);
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
            && after.kernel_count - before.kernel_count == 3
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
                && done.bytes_d2h - start.bytes_d2h == 2 * (sizeof(int)
                    + (10 + 2 * blocks[0].fluid_state.GetNumSpecies()) * sizeof(double))
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
        verify_indicator_batch(*scalar,*batch,accesses);
        verify_jeans_consumer(*scalar,*batch,accesses);
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

// Observe the actual Current slots before Hydro mutates them. Shared Native
// closure/indicator leaves supply the Host reference; the real backend owns
// every arena allocation, binding, upload, reduction and completion boundary.
template<class Eos,class Observe>
void verify_native_indicator_runtime(arch::backend::ComputeBackend& backend,
    std::span<const arch::backend::BackendStateAccess> accesses,
    std::span<const FluidState> initial,std::span<const Grid* const> grids,
    const Eos& eos,const arch::state::Bounds& bounds,Observe&& observe,bool mixed)
{
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    require(accesses.size()==initial.size()&&grids.size()==initial.size()&&accesses.size()>=2,
        "Native indicator runtime fixture lost actual blocks");
    std::vector<std::array<std::vector<double>,6>> scratch(initial.size());
    std::vector<amr::indicator::StateView> host(initial.size());
    for(std::size_t n=0;n<initial.size();++n) {
        const auto& u=initial[n];const auto& g=*grids[n];const int cells=g.GetTotalSize();
        const auto geometry=GridMetrics::make_geometry_view(g,native);
        for(auto& plane:scratch[n])plane.assign(cells,std::numeric_limits<double>::quiet_NaN());
        std::vector<double> fractions(u.GetNumSpecies());
        const auto read=[&u](int cell){return u.get(cell);};
        for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
            const int cell=g.GetIndex(i,j,0);
            for(int s=0;s<u.GetNumSpecies();++s)fractions[s]=u.X(s,cell);
            const auto closure=RzThermodynamics::make_cell_supported(read,cell,geometry,i,
                std::clamp(i-1,0,g.GetTotalX()-3),bounds);
            require(closure.valid()&&arch::state::validate_eos(closure.effective_mean,
                fractions.data(),u.GetNumSpecies(),bounds,eos)==arch::state::Status::valid,
                "Native indicator runtime Host mean reference is invalid");
            const auto thermo=amr::indicator::thermodynamics(closure.effective_mean,
                fractions.data(),eos,true,true,true);
            const auto point=RzThermodynamics::base_point(closure,geometry.GetCellCenterX(i));
            require(arch::state::validate_eos(point,fractions.data(),u.GetNumSpecies(),bounds,eos)
                ==arch::state::Status::valid,"Native indicator runtime Host center reference is invalid");
            scratch[n][0][cell]=thermo.pressure;scratch[n][1][cell]=thermo.temperature;
            scratch[n][2][cell]=thermo.gamma1;scratch[n][3][cell]=point.mom_u/point.rho;
            scratch[n][4][cell]=point.mom_v/point.rho;scratch[n][5][cell]=point.mom_w/point.rho;
        }
        host[n]={u.rho.data(),{u.mom_u.data(),u.mom_v.data(),u.mom_w.data()},
            u.eng.data(),u.enuc_rate.data(),u.mass_fractions.data(),scratch[n][0].data(),
            scratch[n][1].data(),scratch[n][2].data(),cells,g.Is(),g.Ie(),g.Js(),g.Je(),
            g.Ks(),g.Ke(),bounds.density,u.GetNumSpecies(),
            {scratch[n][3].data(),scratch[n][4].data(),scratch[n][5].data()}};
    }
    const auto reference=[&](const AmrConfig& config) {
        const auto selected=amr::indicator::make_selection(config,2,{});
        std::vector<double> result(initial.size(),0.);
        for(std::size_t n=0;n<result.size();++n) {
            const auto& g=*grids[n];const auto geometry=GridMetrics::make_geometry_view(g,native);
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const double value=amr::indicator::cell_error(host[n],geometry,selected.data(),
                    static_cast<int>(selected.size()),i,j,0);
                require(std::isfinite(value),"Native indicator runtime Host maximum is invalid");
                result[n]=std::max(result[n],value);
            }
        }
        return result;
    };
    double worst=0.;bool distinct_results=false;
    const auto compare=[&](std::span<const double> actual,std::span<const double> expected) {
        require(actual.size()==expected.size(),"Native indicator runtime changed result extent");
        for(std::size_t n=0;n<actual.size();++n) {
            const double error=std::abs(actual[n]-expected[n])/std::max(1.,std::abs(expected[n]));
            worst=std::max(worst,error);
            require(std::isfinite(actual[n])&&error<=2.e-13,
                "Native indicator runtime arena changed Host value or private block order");
        }
    };
    std::array<AmrConfig,7> modes{};
    for(auto& mode:modes)mode.refine_on_rho=false;
    modes[0].refine_on_p=modes[0].refine_on_temp=modes[0].refine_on_entropy=true;
    modes[1].refine_on_velx=true;modes[2].refine_on_vely=true;
    modes[3].refine_on_vorticity=true;modes[4].refine_on_div_v=true;
    modes[5].refine_on_rho=true;modes[6]=modes[0];
    std::vector<arch::backend::BackendStateAccess> reversed(accesses.rbegin(),accesses.rend());
    for(const auto& mode:modes) {
        const auto expected=reference(mode);
        for(double value:expected)distinct_results|=std::abs(value-expected.front())>2.e-13;
        const auto one=backend.evaluate_refinement_indicators(accesses.first(1),mode,bounds.density,{});
        compare(one,std::span<const double>(expected).first(1));
        const auto before=backend.counters();bool rejected=false;
        try{(void)backend.evaluate_refinement_indicators(accesses,mode,2.*bounds.density,{});}
        catch(const std::invalid_argument&){rejected=true;}
        auto unchanged=before;++unchanged.getter_count;
        require(rejected&&backend.counters()==unchanged,
            "Native indicator runtime wrong density floor submitted work before rejection");
        const std::vector<double> reverse_expected(expected.rbegin(),expected.rend());
        compare(backend.evaluate_refinement_indicators(reversed,mode,bounds.density,{}),reverse_expected);
        compare(backend.evaluate_refinement_indicators(accesses,mode,bounds.density,{}),expected);
    }
    require(!mixed||distinct_results,"Native mixed indicator fixture cannot distinguish private block order");
    const auto after=observe();require(after.size()==initial.size(),"Native indicator runtime lost Current blocks");
    for(std::size_t n=0;n<initial.size();++n)for(const auto field:{&FluidState::rho,&FluidState::mom_u,
            &FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate,&FluidState::mass_fractions})
        require(std::equal((initial[n].*field).begin(),(initial[n].*field).end(),(after[n].*field).begin(),
            [](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}),
            "Native indicator runtime changed actual Current conserved/species/ENUC fields");
    std::cout<<"CUDA_NATIVE_INDICATOR_RUNTIME_OWNER_PASS blocks="<<accesses.size()
        <<" species="<<initial.front().GetNumSpecies()<<" max_scaled_difference="<<worst<<'\n';
}

/** Flush progress only for the two existing public isolated owners. */
void native_isolated_progress(const std::string& case_label,const char* side,
    const char* phase,int stage=0)
{
    if(case_label.empty())return;
    static const auto started=std::chrono::steady_clock::now();
    const double elapsed=std::chrono::duration<double>(
        std::chrono::steady_clock::now()-started).count();
    std::cout<<"CUDA_NATIVE_PUBLIC_ISOLATED_PROGRESS "<<case_label
        <<" side="<<side<<" phase="<<phase<<" stage="<<stage
        <<" elapsed_seconds="<<elapsed<<std::endl;
}

/** One actual Native macro consumer, reusing the already-matched Ideal2
 * RK2 Runtime fixture. The original batch contract ends before this helper.
 * Disabled Burn/Diffusion keep their production routes; Hydro and final EOS
 * run through the shared macro/transaction owner on both actual field sides.
 * Actual source stages use the transaction's real preparation/journal owner.
 */
template<class Eos,class Observe,class Totals>
void verify_native_source_macro(arch::driver::DriverRuntime& host_runtime,
    arch::driver::DriverRuntime& runtime,SimulationController& host_clock,
    SimulationController& clock,const Eos& eos,const Numerics::IHydroSolver& hydro,
    const arch::dispatch::ResolvedExecutionPlan& selected,double dt,
    Observe&& observe,Totals&& totals,
    const Physical::Gravity::IGravityPolicy* host_policy=nullptr,
    const Physical::Gravity::IGravityPolicy* device_policy=nullptr,
    arch::driver::GravityStage* host_source=nullptr,arch::driver::GravityStage* device_source=nullptr,
    Physical::Gravity::SelfGravity* host_self=nullptr,Physical::Gravity::SelfGravity* device_self=nullptr,
    const std::string& isolated_case={})
{
    using namespace arch;using state::StateSlot;using state::StateRegion;
    auto* const backend=runtime.backend();const int count=runtime.species().count();
    const auto& type=runtime.configuration().physics.gravity.type;
    const bool external=type=="external",self=type=="self",source=external||self;
    require(backend&&!host_runtime.backend()&&count==2
        &&selected.time_integrator==dispatch::TimeIntegratorId::Rk2
        &&(type=="none"||source)&&host_runtime.configuration().physics.gravity.type==type
        &&!runtime.configuration().physics.burn.use_burn
        &&!runtime.configuration().physics.diffusion.use_diffusion,
        "Native source macro lost its original bounded fixture");
    require(source?(host_source&&device_source&&host_policy&&device_policy
            &&host_source->active()&&device_source->active()
            &&host_source->supports_macro_step_journal(state::ExecutionSide::Host)
            &&device_source->supports_macro_step_journal(state::ExecutionSide::Device)):
            (!host_source&&!device_source&&!host_policy&&!device_policy),
        "Native macro lost its actual policy/GravityStage journal owners");
    require(self?(host_self&&device_self):(!host_self&&!device_self),
        "Native Self macro lost its original real SelfGravity owners");
    const auto bits=[](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);};
    const auto same_values=[&](const auto& a,const auto& b) {
        return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),bits);
    };
    const auto same_receipt=[&](const state::RepairBudget& a,const state::RepairBudget& b) {
        return a.semantics==b.semantics&&same_values(a.values,b.values)
            &&a.block_uid==b.block_uid&&a.stage==b.stage&&bits(a.time,b.time)
            &&std::equal(std::begin(a.position),std::end(a.position),std::begin(b.position),bits);
    };
    const auto all_slots=[&] {
        require(!runtime.active_runtime_state_transaction(),"Native macro observed armed resident slots");
        std::array<std::vector<FluidState>,3> result;
        constexpr state::SlotRotation cycle{StateSlot::Next,StateSlot::Scratch,StateSlot::Current};
        // Three real quiescent permutations return the exact original mapping.
        for(auto& slot:result) {
            slot=observe();for(std::size_t n=0;n<runtime.handles().size();++n)
                backend->rotate_slots(runtime.backend_access(n,StateSlot::Current),cycle);
        }
        return result;
    };
    const auto same_slots=[&](const auto& a,const auto& b) {
        const auto& active=runtime.control().tree->GetActiveBlocks();
        for(std::size_t slot=0;slot<3;++slot) {
            require(a[slot].size()==b[slot].size(),"Native macro rollback changed slot extent");
            for(std::size_t n=0;n<a[slot].size();++n) {
                const auto& g=runtime.control().pool->GetBlock(active[n]).grid;
                // Materialized Interior/Ghost cells are evidence; padding is not.
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                    const int c=g.GetIndex(i,j,0);
                    for(const auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
                            &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate})
                        require(bits((a[slot][n].*field)[c],(b[slot][n].*field)[c]),
                            "Native late macro rollback changed resident slot bits");
                    for(int species=0;species<count;++species)
                        require(bits(a[slot][n].X(species,c),b[slot][n].X(species,c)),
                            "Native late macro rollback changed resident composition bits");
                }
            }
        }
    };
    const auto source_rows=[](const driver::DriverRuntime& owner) {
        std::ifstream stream(owner.configuration().io.out_dir+"/gravity_solves.tsv");
        require(stream.is_open(),"Native Self macro lost its real solve diagnostics");
        std::ostringstream rows;rows<<stream.rdbuf();
        require(!stream.bad(),"Native Self macro could not read actual diagnostics");return rows.str();
    };
    const auto stage_rows=[](const std::string& text) {
        std::istringstream rows(text);std::string line;std::size_t count=0;
        while(std::getline(rows,line)){std::istringstream row(line);double time;int stage;
            if(row>>time>>stage&&stage>0)++count;}
        return count;
    };
    long double host_initial_potential=0.L,device_initial_potential=0.L;
    if(source){host_source->flush_committed_diagnostics();device_source->flush_committed_diagnostics();}
    if(self) {
        host_runtime.boundaries().configure_stage(host_clock.t_current,boundary::BoundaryPurpose::Hydro);
        runtime.boundaries().configure_stage(clock.t_current,boundary::BoundaryPurpose::Hydro);
        native_isolated_progress(isolated_case,"host","pre-macro-current-begin");
        host_source->prepare_current(host_clock.t_current,false);
        native_isolated_progress(isolated_case,"host","pre-macro-current-end");
        native_isolated_progress(isolated_case,"device","pre-macro-current-begin");
        device_source->prepare_current(clock.t_current,false);
        native_isolated_progress(isolated_case,"device","pre-macro-current-end");
        host_initial_potential=host_self->boundary_snapshot().potential_energy;
        device_initial_potential=device_self->boundary_snapshot().potential_energy;
        const auto scale=std::max(1.L,std::max(std::abs(host_initial_potential),std::abs(device_initial_potential)));
        require(std::isfinite(host_initial_potential)&&std::isfinite(device_initial_potential)
            &&std::abs(host_initial_potential-device_initial_potential)<=2.e-12L*scale,
            "Native Self macro initial accepted Current energies disagree");
    }
    // Freeze the actual accepted-Current cap once before abandoned source work,
    // as Driver does. A rejected Self attempt clears its iterative guess.
    const double host_gravity_dt=source?host_source->timestep():std::numeric_limits<double>::infinity();
    const double device_gravity_dt=source?device_source->timestep():std::numeric_limits<double>::infinity();
    const auto host_body_before=source?host_source->external_source_budget():std::array<long double,4>{};
    const auto device_body_before=source?device_source->external_source_budget():std::array<long double,4>{};
    const auto macro_initial=observe();const auto macro_initial_totals=totals(macro_initial);
    const auto execute=[&](driver::DriverRuntime& owner,SimulationController& public_clock,bool late_fault) {
        const char* side=&owner==&runtime?"device":"host";
        native_isolated_progress(isolated_case,side,late_fault?"macro-late-fault-begin":"macro-accepted-begin");
        auto* const gravity_stage=&owner==&runtime?device_source:host_source;
        const auto* const policy=&owner==&runtime?device_policy:host_policy;
        const double gravity_dt=&owner==&runtime?device_gravity_dt:host_gravity_dt;
        auto& bc=owner.boundaries();driver::DriverStageWorkspace workspace;
        const auto candidates=driver::calculate_timestep_candidates(owner,workspace,eos,&selected);
        require(std::isfinite(candidates.hydro)&&dt<=candidates.hydro,
            "Native SourceNone macro interval exceeds actual selected Hydro CFL");
        if(source)require(dt<=gravity_dt&&dt<=candidates.diffusion_sts,
            "Native source macro interval exceeds its actual accepted stability cap");
        const double start=public_clock.t_current,old_dt=public_clock.dt_old;
        const int old_step=public_clock.step_count;double advice=1.e99;
        const double old_advice=advice;bool hydro_completed=false;int attempts=0,hydros=0,diffusions=0,captures=0,late_gates=0;
        auto context=owner.stage_context();context.hydro_preparation=gravity_stage;
        context.step_start_time=start;context.step_dt=dt;
        context.configure_boundary_context=[&](double time,boundary::BoundaryPurpose purpose) {
            bc.configure_stage(time,purpose);owner.bind_native_boundary_acceptance(context,owner.handles());
            if(late_fault) {
                const auto real_gate=context.post_boundary_acceptance;
                const bool endpoint=time==start+dt&&purpose==boundary::BoundaryPurpose::Hydro;
                context.post_boundary_acceptance=[&,real_gate,endpoint](const scheduler::StageExecutionContext& actual,
                    StateSlot slot,state::StateVersion version) {
                    real_gate(actual,slot,version); // Actual completed resident EOS first.
                    if(hydro_completed&&endpoint&&slot==StateSlot::Current) {
                        ++late_gates;throw std::runtime_error("Native SourceNone macro late completed-EOS fault");
                    }
                };
            }
        };
        context.physical_boundary_preparation=[&](StateSlot slot,double time,boundary::BoundaryPurpose purpose) {
            context.configure_boundary_context(time,purpose);owner.ensure_fluid_ghosts(slot);
        };
        context.configure_boundary_context(start,boundary::BoundaryPurpose::Hydro);
        owner.bind_boundary_accounting(context);
        const auto accepted_budget=owner.hydro_boundary_budget();
        const auto real_capture=context.hydro_flux_capture_accept;
        context.hydro_flux_capture_accept=[&,real_capture](const scheduler::StageDescriptor& descriptor) {
            require(descriptor.stage==captures+1,"Native macro repeated a boundary receipt stage");
            real_capture(descriptor);++captures;
            require(same_values(owner.hydro_boundary_budget(),accepted_budget),
                "Native macro published a tentative boundary receipt before commit");
        };
        scheduler::ScopedStageBinding binding(context,owner.handles());
        auto saved_owner=driver::RuntimeStateTransaction::snapshot_owner(owner,context);
        const auto saved_fields=late_fault?all_slots():std::array<std::vector<FluidState>,3>{};
        const auto saved_surfaces=late_fault?backend->download_boundary_flux_capture():std::vector<backend::BoundaryFluxPlanes>{};
        const auto saved_stage=backend->stage_repairs,saved_reflux=backend->reflux_repairs;
        const auto saved_body=source?gravity_stage->external_source_budget():std::array<long double,4>{};
        const auto saved_source_rows=self?source_rows(owner):std::string{};
        const auto work_before=backend->counters();bool rejected=false;double accepted_dt=0.;
        try {
            accepted_dt=driver::execute_driver_macro_attempts(owner,public_clock,advice,dt,
                [&](double interval,std::uint64_t index) {
                    ++attempts;require(index==1&&interval==dt,"Native SourceNone macro changed its frozen single attempt");
                    driver::execute_driver_macro_step(owner,context,&hydro,false,
                        [&](driver::BurnHalf,double,state::CompletionToken token) {
                            require(false,"Disabled SourceNone Burn lane was entered");return token;
                        },
                        [&](double half_dt) {
                            require(half_dt==.5*dt,"Native macro changed the diffusion half interval");
                            driver::advance_diffusion(owner,workspace,context,eos,&selected,
                                public_clock.step_count,half_dt,candidates.diffusion_forward_euler);++diffusions;
                        },
                        [&](double hydro_dt) {
                            driver::advance_hydro(owner,workspace,context,&selected,hydro_dt,
                                &SolverRK2::solve<BCHandler>,policy,&hydro,driver::RuntimeStateQualification::Production);
                            ++hydros;hydro_completed=true;
                        },
                        [&](driver::CpuStage,auto&& operation){operation();});
                });
        } catch(const std::runtime_error& error) {
            if(!late_fault||std::string(error.what())!="Native SourceNone macro late completed-EOS fault")throw;
            rejected=true;
        }
        require(attempts==1&&hydros==1&&diffusions==2&&captures==2
            &&!owner.active_runtime_state_transaction()&&!owner.native_macro_retry_attempt()
            &&bits(public_clock.t_current,start)&&public_clock.step_count==old_step,
            "Native macro lost its real split/receipt/transaction or advanced public time internally");
        if(late_fault) {
            require(rejected&&late_gates==1&&bits(public_clock.dt_old,old_dt)&&bits(advice,old_advice),
                "Native late failure did not restore accepted proposal/advice");
            const auto& observed=owner.boundary_observer_operations();const auto& prior=saved_owner.counters;
            require(observed.kernel_count>=prior.kernel_count&&observed.bytes_h2d>=prior.bytes_h2d
                &&observed.bytes_d2h>=prior.bytes_d2h&&observed.stream_sync_count>=prior.stream_sync_count
                &&observed.getter_count>=prior.getter_count,
                "Native macro rollback erased actual boundary observer work");
            saved_owner.counters=observed;
            require(driver::RuntimeStateTransaction::owner_matches(owner,context,saved_owner)
                &&same_receipt(backend->stage_repairs,saved_stage)&&same_receipt(backend->reflux_repairs,saved_reflux),
                "Native late failure changed actual Runtime owner or accepted backend receipts");
            same_slots(all_slots(),saved_fields);
            const auto surfaces=backend->download_boundary_flux_capture();
            require(surfaces.size()==saved_surfaces.size(),"Native macro rollback lost actual surface owners");
            for(std::size_t n=0;n<surfaces.size();++n) {
                require(surfaces[n].block==saved_surfaces[n].block,"Native macro rollback changed surface identity");
                for(int face=0;face<6;++face)
                    require(same_values(surfaces[n].stage[face],saved_surfaces[n].stage[face])
                        &&same_values(surfaces[n].initial[face],saved_surfaces[n].initial[face]),
                        "Native macro rollback changed actual boundary capture bits");
            }
            const auto work_after=backend->counters();
            require(work_after.kernel_count>work_before.kernel_count
                &&work_after.stream_sync_count>work_before.stream_sync_count,
                "Native late macro refusal did not preserve evidence of completed resident work");
            if(source) {
                require(gravity_stage->external_source_budget()==saved_body
                    &&!policy->prepared_native_external()&&!policy->prepared_native_self(),
                    "Native late macro refusal published source receipts or retained a failed frame");
                gravity_stage->flush_committed_diagnostics(); // Real journal must be closed, with no committed rejected prefix.
                if(self)require(source_rows(owner)==saved_source_rows,
                    "Native late Self macro refusal emitted abandoned solve rows");
            }
            native_isolated_progress(isolated_case,side,"macro-late-fault-end");
            return;
        }
        require(!rejected&&accepted_dt==dt,"Native SourceNone macro accepted another interval");
        const auto endpoint_boundary=bc.snapshot_stage_context();
        require(endpoint_boundary.time()==start+dt&&endpoint_boundary.purpose()==boundary::BoundaryPurpose::Hydro,
            "Native macro final boundary is not its real Hydro endpoint");
        for(const auto handle:owner.handles()) {
            const auto current=context.ledger.inspect({handle,StateSlot::Current});
            scheduler::detail::require_settled_destination(current);
            context.ledger.require_readable({handle,StateSlot::Current},{context.side,current.interior.version,true,true});
        }
        const auto& repairs=owner.repair_budget();
        require(repairs.semantics==state::RepairSemantics::RzVolumeAngular&&repairs.species()==count
            &&std::all_of(repairs.values.begin(),repairs.values.end(),[](double value){return value==0.;}),
            "Native SourceNone macro concealed a repair");
        if(source)gravity_stage->invalidate();
        public_clock.advance(accepted_dt); // The real Driver caller advances once after commit.
        if(source) {
            gravity_stage->flush_committed_diagnostics(); // Same invalidate -> advance -> flush tail as Driver.
            require(!policy->prepared_native_external()&&!policy->prepared_native_self(),
                "Native accepted macro retained its stage frame after caller invalidation");
            if(self)require(stage_rows(source_rows(owner))==stage_rows(saved_source_rows)+2,
                "Native Self macro journal omitted or duplicated its two actual accepted stages");
            bc.configure_stage(public_clock.t_current,boundary::BoundaryPurpose::Hydro);
        }
        require(public_clock.step_count==old_step+1&&bits(public_clock.t_current,start+dt),
            "Native SourceNone macro caller advanced public time/step more than once");
        native_isolated_progress(isolated_case,side,"macro-accepted-end");
    };
    execute(runtime,clock,true); // Full late rollback, then reuse exactly that accepted entry.
    execute(host_runtime,host_clock,false);execute(runtime,clock,false);
    long double host_final_potential=0.L,device_final_potential=0.L;
    if(self) {
        native_isolated_progress(isolated_case,"host","post-macro-current-begin");
        host_source->prepare_current(host_clock.t_current,false);
        native_isolated_progress(isolated_case,"host","post-macro-current-end");
        native_isolated_progress(isolated_case,"device","post-macro-current-begin");
        device_source->prepare_current(clock.t_current,false);
        native_isolated_progress(isolated_case,"device","post-macro-current-end");
        host_final_potential=host_self->boundary_snapshot().potential_energy;
        device_final_potential=device_self->boundary_snapshot().potential_energy;
        const auto scale=std::max(1.L,std::max(std::abs(host_final_potential),std::abs(device_final_potential)));
        require(std::isfinite(host_final_potential)&&std::isfinite(device_final_potential)
            &&std::abs(host_final_potential-device_final_potential)<=2.e-12L*scale,
            "Native Self macro final accepted Current energies disagree");
    }
    long double source_work=0.L,source_torque=0.L;
    if(external) {
        const auto h=host_source->external_source_budget(),d=device_source->external_source_budget();
        for(std::size_t component=0;component<4;++component) {
            const auto hd=h[component]-host_body_before[component],dd=d[component]-device_body_before[component];
            const auto scale=std::max(1.L,std::max(std::abs(hd),std::abs(dd)));
            require(std::isfinite(hd)&&std::isfinite(dd)&&std::abs(hd-dd)<=2.e-12L*scale,
                "Native external macro actual source increments differ from Host");
            if(runtime.configuration().grid.x1_min!=0.||component==1||component==3)
                require(hd!=0.L&&dd!=0.L,"Native external macro measured no actual source increment");
        }
        source_torque=d[2]-device_body_before[2];source_work=d[3]-device_body_before[3];
    }
    const auto actual=observe();const auto actual_totals=totals(actual);double worst=0.;
    const auto& active=runtime.control().tree->GetActiveBlocks();
    for(std::size_t n=0;n<active.size();++n) {
        const auto& g=runtime.control().pool->GetBlock(active[n]).grid;
        const auto& reference=host_runtime.control().pool->GetBlock(host_runtime.control().tree->GetActiveBlocks()[n]).fluid_state;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int c=g.GetIndex(i,j,0);
            for(const auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w,&FluidState::eng}) {
                const double h=(reference.*field)[c],d=(actual[n].*field)[c];
                const double error=std::abs(d-h)/std::max(1.,std::abs(h));worst=std::max(worst,error);
                require(std::isfinite(d)&&error<=3.e-14,"Native actual SourceNone macro differs from real Host endpoint");
            }
            for(int species=0;species<count;++species)
                require(std::abs(actual[n].X(species,c)-reference.X(species,c))<=3.e-14,
                    "Native SourceNone macro changed original species parity tolerance");
            require(bits(actual[n].enuc_rate[c],macro_initial[n].enuc_rate[c])
                &&bits(reference.enuc_rate[c],macro_initial[n].enuc_rate[c]),
                "Native SourceNone macro changed actual ENUC donor bits");
        }
    }
    long double budget_error=0.;
    for(std::size_t field=0;field<actual_totals.size();++field) {
        const auto scale=self&&field==1
            ?std::max(std::abs(macro_initial_totals[field]+device_initial_potential),std::abs(actual_totals[field]+device_final_potential))
            :std::max(std::abs(macro_initial_totals[field]),std::abs(actual_totals[field]));
        long double delta=actual_totals[field]-macro_initial_totals[field];
        if(external){if(field==1)delta-=source_work;else if(field==2)delta-=source_torque;}
        if(self&&field==1)delta+=device_final_potential-device_initial_potential;
        const auto error=std::abs(delta)/scale;
        budget_error=std::max(budget_error,error);
        require(std::isfinite(error)&&error<=2.e-12L,"Native SourceNone macro violated original closed V/W budget");
    }
    const auto& host_budget=host_runtime.hydro_boundary_budget();const auto& device_budget=runtime.hydro_boundary_budget();
    require(host_budget.size()==device_budget.size(),"Native macro changed accepted boundary receipt extent");
    bool measured=false;
    for(std::size_t field=0;field<host_budget.size();++field) {
        const auto scale=std::max(1.,std::max(std::abs(host_budget[field]),std::abs(device_budget[field])));
        require(std::isfinite(device_budget[field])&&std::abs(host_budget[field]-device_budget[field])<=2.e-12*scale,
            "Native macro accepted boundary receipt differs from real Host");
        measured|=std::abs(device_budget[field])>64.*std::numeric_limits<double>::epsilon();
    }
    require(measured&&clock.step_count==1&&host_clock.step_count==1&&bits(clock.t_current,host_clock.t_current),
        "Native SourceNone macro lost its actual receipt or single public endpoint");
    std::cout<<(external?"CUDA_NATIVE_EXTERNAL_MACRO_OWNER_PASS":self?"CUDA_NATIVE_SELF_MACRO_OWNER_PASS":"CUDA_NATIVE_SOURCE_NONE_MACRO_OWNER_PASS")<<" species="<<count
        <<" stages=2 attempts_per_call=1 late_completed_eos_rollback=1 public_steps="<<clock.step_count
        <<" endpoint_time="<<clock.t_current<<" max_scaled_difference="<<worst
        <<" closed_budget_error="<<double(budget_error)<<" source_work="<<double(source_work)<<" source_torque="<<double(source_torque)
        <<" self_initial_potential="<<double(device_initial_potential)<<" self_final_potential="<<double(device_final_potential)
        <<" self_delta_potential="<<double(device_final_potential-device_initial_potential)<<'\n';
}

/** Actual Native resident Hydro against the independent original Host integrator.
 * The five compact cases add batch/update/reflux ownership evidence to the
 * existing complete selected-face matrix; they do not qualify a source/macro.
 */
template<class Eos,class FluxPolicy,class Integrator>
void run_native_hydro_owner_case(const SpeciesManager& species,const Eos& eos,
    arch::dispatch::EosId eos_route,arch::dispatch::FluxId flux_route,
    arch::dispatch::ReconstructionId reconstruction,arch::scheduler::HydroMethod method,
    arch::dispatch::TimeIntegratorId route,bool axis,bool mixed,bool cached=false,
    bool external=false,bool self=false,bool isolated=false)
{
    using namespace arch;using state::StateSlot;using state::StateRegion;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    require(!(external&&self),"Native Hydro external and Self owners are mutually exclusive");
    require(!isolated||self,"Native isolated boundary requires the actual Self owner");
    const std::string isolated_case=isolated?
        "axis="+std::to_string(axis)+" mixed="+std::to_string(mixed):std::string{};
    if(isolated)std::cout<<"CUDA_NATIVE_PUBLIC_ISOLATED_OWNER_CASE axis="<<axis<<" mixed="<<mixed<<std::endl;
    const int count=species.count();const double pi=std::acos(-1.);
    const double density_scale=cached?1.e6:1.,speed_scale=cached?1.e6:1.,omega_scale=cached?1.e7:1.;
    const double dt=cached?2.e-12:1.e-4;
    // One finite nonzero local-orthonormal (g_r,g_z,g_phi) drives both real
    // ExternalGravity owners and the prepared Device frame. SourceNone never
    // configures or constructs it, so its frozen gravity-free contract holds.
    // A regular axis permits only the constant axial component. Off-axis the
    // same real source exercises all three local-orthonormal components.
    const double external_g_r=axis?0.:.02,external_g_z=.03,external_g_phi=axis?0.:.04;
    SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=axis?0.:1.;config.grid.x1_max=config.grid.x1_min+2.;
    config.grid.x2_min=-1.;config.grid.x2_max=1.;config.grid.amr_max_blocks=16;
    config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
    config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="reflecting";
    config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    config.numerics.sml_rho=config.numerics.min_eint=1.e-14;
    config.numerics.max_eint=cached?1.e24:1.e10;config.numerics.cfl=.4;
    config.numerics.entropy_fix_coeff=0.;config.numerics.hll_roe_wave_speed=true;
    config.numerics.solver_name=flux_route==dispatch::FluxId::Hll?"HLL":"HLLC";
    config.numerics.reconstruction=reconstruction==dispatch::ReconstructionId::Pcm?"pcm":
        reconstruction==dispatch::ReconstructionId::Muscl?"muscl":"ppm";
    config.numerics.limiter="minmod";
    config.numerics.time_integrator=route==dispatch::TimeIntegratorId::Euler?"euler":
        route==dispatch::TimeIntegratorId::Rk2?"rk2":"rk3";
    config.physics.eos_type=cached?"helmholtz":"ideal";
    config.physics.burn.use_burn=false;config.physics.diffusion.use_diffusion=false;
    // Actual source Runtime configuration; the default SourceNone owner
    // keeps the original "none" type and its unset component vector.
    config.physics.gravity.type=external?"external":self?"self":"none";
    // Both real Self owners keep the same selected physical boundary. The
    // default preserves the original homogeneous Dirichlet(0) cases exactly.
    if(self)config.physics.gravity.boundary=isolated?"isolated":"dirichlet";
    if(external) {
        config.physics.gravity.g_x=external_g_r;
        config.physics.gravity.g_y=external_g_z;
        config.physics.gravity.g_z=external_g_phi;
    }
    config.io.tmax=1.;
    const dispatch::ResolvedExecutionPlan selected{flux_route,reconstruction,dispatch::LimiterId::MinMod,
        route,eos_route,dispatch::NetworkId::None,dispatch::OdeSolverId::None,
        dispatch::LinearSolverId::None,dispatch::DiffusionIntegratorId::None};
    const auto plan=scheduler::make_hydro_plan(method);
    amr::AMRControl host_control(16,2),control(16,2);
    const auto seed=[&](amr::AMRControl& owner) {
        if(mixed)owner.tree->LoadLeafGrid(config,count,{1,1,1,1,0},
            {0,1,0,1,1},{0,0,1,1,0},{0,0,0,0,0},native);
        else owner.tree->InitRootGrid(config,count,native);
        const auto& active=owner.tree->GetActiveBlocks();
        require(active.size()==(mixed?5u:2u),"Native Hydro actual committed domain is incomplete");
        for(int id:active) {
            auto& block=owner.pool->GetBlock(id);block.RequireNativeGeometryIdentity();const auto& g=block.grid;
            require(g.ng>=3,"Native Hydro lost its real axis/MinMod stencil");
            for(auto* u:{&block.fluid_state,&block.state_next,&block.state_scratch}) {
                u->stage_repairs.reset(count,state::RepairSemantics::RzVolumeAngular);
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                    const int c=g.GetIndex(i,j,0);const long double a=g.GetFacePosL(i),b=g.GetFacePosR(i);
                    const long double l=std::min(std::abs(a),std::abs(b)),h=std::max(std::abs(a),std::abs(b));
                    // Independent antiderivatives: V=(h^2-l^2)/2,
                    // W=(h^3-l^3)/3, I=(h^4-l^4)/4. J/W=rho*Omega*I/W.
                    const long double V=(h*h-l*l)/2.,W=(h*h*h-l*l*l)/3.,I=(h*h*h*h-l*l*l*l)/4.;
                    const double r=g.GetCellCenterX(i),z=g.GetCellCenterY(j);
                    const double rho=density_scale*(2.+.02*z);
                    const double vr=.03*speed_scale*std::sin(pi*(r-config.grid.x1_min)/2.);
                    const double vz=.02*speed_scale*std::sin(pi*(z+1.)/2.);
                    const double omega=.15*omega_scale*(1.+.05*std::cos(pi*z));
                    std::vector<double> x(count);if(count){x[0]=.4+.01*std::cos(pi*z);
                        for(int s=1;s<count;++s)x[s]=(1.-x[0])/(count-1);}
                    const double thermal=cached?rho*eos.get_eint_from_T(rho,5.e7,x.data()):12.;
                    u->set(c,{rho,rho*vr,rho*vz,double((b<=0.?-1.:1.)*rho*omega*I/W),
                        double(thermal+.5L*rho*(vr*vr+vz*vz+omega*omega*I/V))});
                    const double donor=(i+j)&1?10000.*id+100.*j+i:-0.;
                    // Only Host output-slot diagnostics differ: an unchanged
                    // destination can never masquerade as actual-input inheritance.
                    u->enuc_rate[c]=(&owner==&host_control&&u!=&block.fluid_state)
                        ?(u==&block.state_next?-1.e6:-2.e6)-10000.*id-100.*j-i:donor;
                    for(int s=0;s<count;++s)u->X(s,c)=x[s];
                }
            }
        }
    };
    seed(host_control);seed(control);
    // Each genuine Self service owns its own diagnostic stream. Existing
    // SourceNone/external owners keep their original SimConfig reference.
    SimConfig host_self_config=config;
    if(self) {
        const auto directory=std::filesystem::temp_directory_path()/
            ("arch-native-self-owner-"+std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        host_self_config.io.out_dir=(directory/"host").string();
        config.io.out_dir=(directory/"device").string();
    }
    const auto& host_configuration=self?host_self_config:config;
    RunState start;start.repairs.reset(count,state::RepairSemantics::RzVolumeAngular);
    SimulationController host_counters(host_configuration,start),counters(config,start);
    BCHandler host_boundary(host_configuration,native),boundary(config,native);
    host_boundary.bind(eos,species);boundary.bind(eos,species);
    host_boundary.configure_stage(0.,boundary::BoundaryPurpose::Hydro);boundary.configure_stage(0.,boundary::BoundaryPurpose::Hydro);
    driver::DriverRuntime host_runtime(host_control,host_boundary,host_configuration,species,host_counters);
    driver::DriverRuntime runtime(control,boundary,config,species,counters);
    host_runtime.bind_native_rz_eos(eos);runtime.bind_native_rz_eos(eos);
    host_runtime.initialize_topology();runtime.initialize_topology();
    const auto recipes=runtime.prepare_backend_bindings();std::vector<cuda::CudaBlockBinding> bindings;
    for(const auto& entry:recipes)bindings.push_back({entry.block,entry.handle,entry.storage,entry.physical_boundary});
    runtime.install_backend(cuda::make_cuda_backend(bindings,0,cuda::make_cuda_launch_config(selected,config),species,eos));
    runtime.upload_initial_state();auto* const backend=runtime.backend();
    // The Runtime bootstrap still installs angular=false. Explicitly install
    // the real original angular topology for this bounded numerical consumer.
    const auto topology=control.RequireFluxTopologyPlan(count,native,-1,true);
    const auto reflux=control.RequireRefluxTopologyPlan(count,native,true);
    require(topology.angular_transport&&topology.semantics==native,
        "Native Hydro installed an ordinary torque plan");
    require(mixed?!reflux.operations.empty():reflux.operations.empty(),"Native Hydro CF topology does not match real leaves");
    backend->prepare_amr_flux_plan(topology,reflux);
    host_runtime.ensure_fluid_ghosts();runtime.ensure_fluid_ghosts();
    // Genuine prepared Native external owners: one real policy per actual
    // Runtime and one actual GravityStage journal each, borrowing the same
    // (g_r,g_z,g_phi). No frame, receipt or qualification witness is fabricated
    // here; the real GravityStage builds its frame from the live Runtime, the
    // frozen configuration and the actual journal preflight.
    std::unique_ptr<Physical::Gravity::ExternalGravity> host_force,device_force;
    std::unique_ptr<Physical::Gravity::SelfGravity> host_self,device_self;
    long double host_initial_potential=0.L,device_initial_potential=0.L;
    double self_max_face_acceleration=0.,self_source_scaled_amplitude=0.;
    std::unique_ptr<driver::GravityStage> host_source,device_source;
    if(external) {
        host_force=std::make_unique<Physical::Gravity::ExternalGravity>(
            external_g_r,external_g_z,external_g_phi);
        device_force=std::make_unique<Physical::Gravity::ExternalGravity>(
            external_g_r,external_g_z,external_g_phi);
        host_source=std::make_unique<driver::GravityStage>(host_runtime,host_force.get());
        device_source=std::make_unique<driver::GravityStage>(runtime,device_force.get());
        require(host_source->active()&&device_source->active()
            &&host_source->supports_macro_step_journal(state::ExecutionSide::Host)
            &&device_source->supports_macro_step_journal(state::ExecutionSide::Device),
            "real prepared native external journal is inactive");
    }
    if(self) {
        host_self=std::make_unique<Physical::Gravity::SelfGravity>(host_configuration.physics.gravity);
        device_self=std::make_unique<Physical::Gravity::SelfGravity>(config.physics.gravity);
        host_source=std::make_unique<driver::GravityStage>(host_runtime,host_self.get(),
            driver::GravityStage::Qualification::Production);
        device_source=std::make_unique<driver::GravityStage>(runtime,device_self.get(),
            driver::GravityStage::Qualification::Production);
        require(host_source->active()&&device_source->active()
            &&host_source->supports_macro_step_journal(state::ExecutionSide::Host)
            &&device_source->supports_macro_step_journal(state::ExecutionSide::Device),
            "real prescribed Native Self journal is inactive");
        // Prepare the actual accepted Current on each owner before any journal
        // or Hydro counter snapshot. The original service chooses shared CGS G.
        native_isolated_progress(isolated_case,"host","initial-current-begin");
        host_source->prepare_current(0.,false);
        native_isolated_progress(isolated_case,"host","initial-current-end");
        native_isolated_progress(isolated_case,"device","initial-current-begin");
        device_source->prepare_current(0.,false);
        native_isolated_progress(isolated_case,"device","initial-current-end");
        // Read the real Host publication only. Its resolved momentum signal
        // must exceed the same 3e-14 normalization used below, so a missing
        // Device force cannot hide behind the weak CGS total-energy budget.
        const auto& host_active=host_control.tree->GetActiveBlocks();
        for(std::size_t n=0;n<host_active.size();++n) {
            const auto& block=host_control.pool->GetBlock(host_active[n]);
            const auto& g=block.grid;const auto& u=block.fluid_state;
            const auto patch=host_self->patch_view(n);
            require(patch.enabled()&&patch.faces[0]&&patch.faces[1],
                "Native Self Host accepted Current has no actual face field");
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int c=g.GetIndex(i,j,0);
                for(int a=0;a<2;++a) {
                    const int neighbor=a==0?g.GetIndex(i+1,j,0):g.GetIndex(i,j+1,0);
                    const double low=patch.faces[a][c],high=patch.faces[a][neighbor];
                    require(std::isfinite(low)&&std::isfinite(high),
                        "Native Self Host accepted Current face field is nonfinite");
                    self_max_face_acceleration=std::max(self_max_face_acceleration,
                        std::max(std::abs(low),std::abs(high)));
                    const double momentum=a==0?u.mom_u[c]:u.mom_v[c];
                    const double signal=std::abs(Physical::Gravity::gravity_momentum(
                        low,high,u.rho[c],dt))/std::max(1.,std::abs(momentum));
                    self_source_scaled_amplitude=std::max(self_source_scaled_amplitude,signal);
                }
            }
        }
        require(std::isfinite(self_source_scaled_amplitude)
            &&self_source_scaled_amplitude>64.*3.e-14,
            "Native Self actual force is unresolved by the original Host/Device field tolerance");
        host_initial_potential=host_self->boundary_snapshot().potential_energy;
        device_initial_potential=device_self->boundary_snapshot().potential_energy;
        const auto scale=std::max(1.L,std::max(std::abs(host_initial_potential),
            std::abs(device_initial_potential)));
        require(std::isfinite(host_initial_potential)&&std::isfinite(device_initial_potential)
            &&std::abs(host_initial_potential-device_initial_potential)<=2.e-12L*scale,
            "Host/Device initial accepted Native Self field energy disagrees");
    }
    const auto& active=control.tree->GetActiveBlocks();std::vector<backend::BackendStateAccess> accesses;
    for(std::size_t n=0;n<active.size();++n)accesses.push_back(runtime.backend_access(n,StateSlot::Current));
    const auto observe=[&] {
        std::vector<FluidState> result;for(int id:active)result.push_back(control.pool->GetBlock(id).fluid_state);
        for(std::size_t n=0;n<result.size();++n)for(auto region:{StateRegion::Interior,StateRegion::Ghost})
            backend->enqueue_materialize_host_current(accesses[n],region,transfer_view(result[n]));
        backend->quiesce();return result;
    };
    auto initial=observe();
    if(mixed) {
        double ghost_difference=0.;int ghost_block=-1,ghost_cell=-1,ghost_field=-1;
        for(std::size_t n=0;n<active.size();++n) {
            const auto& g=control.pool->GetBlock(active[n]).grid;
            const auto& host=host_control.pool->GetBlock(host_control.tree->GetActiveBlocks()[n]).fluid_state;
            const std::array fields{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w,&FluidState::eng};
            for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                if(i>=g.Is()&&i<g.Ie()&&j>=g.Js()&&j<g.Je())continue;
                const int c=g.GetIndex(i,j,0);
                for(std::size_t f=0;f<fields.size();++f) {
                    const double expected=(host.*fields[f])[c],value=(initial[n].*fields[f])[c];
                    const double difference=std::abs(value-expected)/std::max(1.,std::abs(expected));
                    if(difference>ghost_difference){ghost_difference=difference;ghost_block=n;ghost_cell=c;ghost_field=f;}
                }
            }
        }
        std::cout.precision(17);std::cout<<"CUDA_NATIVE_INITIAL_GHOST_OBSERVATION scaled="<<ghost_difference
            <<" block="<<ghost_block<<" cell="<<ghost_cell<<" field="<<ghost_field<<'\n';
    }
    // Reuse only the existing ideal-gas axis and mixed-leaf owners; no new
    // bootstrap, EOS/source qualification, test entry or dimension-2 VELZ route.
    if(!cached&&((count==0&&axis&&!mixed)||(count==2&&mixed))) {
        std::vector<const Grid*> indicator_grids;
        for(int id:active)indicator_grids.push_back(&control.pool->GetBlock(id).grid);
        verify_native_indicator_runtime(*backend,accesses,initial,indicator_grids,eos,
            {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint},observe,mixed);
    }
    const auto totals=[&](const std::vector<FluidState>& values) {
        std::vector<long double> sum(3+count);
        for(std::size_t n=0;n<active.size();++n) {
            const auto& g=control.pool->GetBlock(active[n]).grid;const auto& u=values[n];
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int c=g.GetIndex(i,j,0);const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
                const long double V=std::acos(-1.L)*(h*h-l*l)*g.dx2;
                const long double W=2.L*std::acos(-1.L)*(h*h*h-l*l*l)*g.dx2/3.L;
                sum[0]+=u.rho[c]*V;sum[1]+=u.eng[c]*V;sum[2]+=u.mom_w[c]*W;
                for(int s=0;s<count;++s)sum[3+s]+=u.rho[c]*u.X(s,c)*V;
            }
        }
        return sum;
    };
    const auto initial_totals=totals(initial);
    const auto zero_native_receipt=[&](const state::RepairBudget& report) {
        require(report.semantics==state::RepairSemantics::RzVolumeAngular&&report.species()==count,
            "Native Hydro reported ordinary repair measures");
        require(std::all_of(report.values.begin(),report.values.end(),[](double v){return v==0.;}),
            "Native Hydro used ordinary repair/floor rather than strict provisional acceptance");
    };
    const auto no_work=[&](backend::ComputeBackend& owner,const auto& operation) {
        const auto before=owner.counters();const auto receipt=owner.stage_repairs;bool rejected=false;
        try{operation();}catch(const std::logic_error&){rejected=true;}
        auto expected=before;++expected.getter_count;
        require(rejected&&owner.counters()==expected,"Unsupported Native Hydro packet enqueued partial work");
        require(owner.stage_repairs.semantics==receipt.semantics&&owner.stage_repairs.values==receipt.values,
            "Native Hydro cold refusal reset a completed repair receipt");
    };
    const auto complete=runtime.stage_context().ledger.inspect({runtime.handles().front(),StateSlot::Current}).ghost.completion;
    for(int fault:{0,1,2}) {
        auto invalid=accesses;if(fault==0)++invalid.back().storage.value;
        if(fault==1)invalid.back()=invalid.front();if(fault==2)invalid.back().slot=StateSlot::Scratch;
        no_work(*backend,[&]{backend->execute_hydro_stage_batch(invalid,plan.stages.front(),dt,complete);});
    }
    // The non-angular-plan packet is a SourceNone-only cold refusal: with the
    // real source launch the gravity check rejects earlier for another
    // reason, so keep the original intent on the default owner only.
    if(mixed&&!external&&!self) {
        backend->prepare_amr_flux_plan(control.RequireFluxTopologyPlan(count,native,-1,false),
            control.RequireRefluxTopologyPlan(count,native,false));
        no_work(*backend,[&]{backend->execute_hydro_stage_batch(accesses,plan.stages.front(),dt,complete);});
        backend->prepare_amr_flux_plan(topology,reflux);
    }
    if(external) {
        // A real, unprepared external policy carries no live frame: the actual
        // backend must cold-refuse before enqueuing partial work, exactly as
        // the original SourceNone self/external no-frame refusals still do.
        Physical::Gravity::ExternalGravity cold(external_g_r,external_g_z,external_g_phi);
        no_work(*backend,[&]{backend->execute_hydro_stage_batch(accesses,plan.stages.front(),
            dt,complete,static_cast<const Physical::Gravity::IGravityPolicy*>(&cold));});
    }
    if(self) {
        Physical::Gravity::SelfGravity cold(config.physics.gravity);
        no_work(*backend,[&]{backend->execute_hydro_stage_batch(accesses,plan.stages.front(),
            dt,complete,static_cast<const Physical::Gravity::IGravityPolicy*>(&cold));});
        // A genuine AcceptedCurrent publication is not a Hydro frame either.
        no_work(*backend,[&]{backend->execute_hydro_stage_batch(accesses,plan.stages.front(),
            dt,complete,static_cast<const Physical::Gravity::IGravityPolicy*>(device_self.get()));});
    }
    // Second-backend configuration negatives are SourceNone-only (they rebuild
    // the launch from SimConfig); actual sources need their prepared frame.
    if(!cached&&!mixed&&count==0&&!external&&!self)for(bool self:{false,true}) {
        auto source_launch=cuda::make_cuda_launch_config(selected,config);
        source_launch.self_gravity=self;source_launch.gravity={.1,0.,0.,!self};
        auto source=cuda::make_cuda_backend(bindings,0,source_launch,species,eos);
        source->prepare_amr_flux_plan(topology,reflux);
        for(std::size_t n=0;n<accesses.size();++n)for(auto region:{StateRegion::Interior,StateRegion::Ghost})
            source->enqueue_upload_slot(accesses[n],region,transfer_view(initial[n]));
        source->quiesce();no_work(*source,[&]{source->execute_hydro_stage_batch(accesses,plan.stages.front(),dt,complete);});
    }
    const auto after_refusal=observe();
    for(std::size_t n=0;n<initial.size();++n)for(const auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,
            &FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate,&FluidState::mass_fractions})
        require(std::equal((initial[n].*field).begin(),(initial[n].*field).end(),(after_refusal[n].*field).begin(),
            [](double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}),
            "Native Hydro cold refusal changed actual Current fields");
    Numerics::HydroSolverImpl<Eos,FluxPolicy> host_hydro(eos,native);
    const auto configure=[&](driver::DriverRuntime& owner,BCHandler& bc,scheduler::StageExecutionContext& context) {
        context.step_start_time=0.;context.step_dt=dt;
        context.configure_boundary_context=[&owner,&bc,&context](double time,boundary::BoundaryPurpose purpose) {
            bc.configure_stage(time,purpose);owner.bind_native_boundary_acceptance(context,owner.handles());
        };
        context.physical_boundary_preparation=[&context,&owner](StateSlot slot,double time,boundary::BoundaryPurpose purpose) {
            context.configure_boundary_context(time,purpose);owner.ensure_fluid_ghosts(slot);
        };
    };
    // Expected real first-stage register-clear cost, derived before any actual
    // call from the immutable prepared topology alone: one clear kernel per
    // distinct (destination endpoint, destination face = 2*axis+side) register
    // surface the compiled plan requires, counted once per surface, plus a
    // single join when at least one surface exists. The same-level domain
    // legitimately owns no coarse/fine register surface, so a zero expectation
    // is valid rather than a failure; the mixed five-leaf domain is required to
    // exercise at least one real surface. Nothing here calls the function under
    // test, so the actual first-stage clear inside
    // execute_hydro_stage_batch is not duplicated or self-calibrated.
    std::uint64_t external_clear_kernels=0,external_clear_joins=0;
    if(external||self) {
        std::vector<amr::AmrFluxEndpointBinding> surface_bindings;
        surface_bindings.reserve(topology.active_endpoints.size());
        for(std::size_t n=0;n<topology.active_endpoints.size();++n)
            surface_bindings.push_back({topology.active_endpoints[n],static_cast<int>(n),count,
                amr::make_amr_flux_grid_layout(control.pool->GetBlock(topology.active_blocks[n]).grid)});
        const auto compiled=amr::compile_amr_flux_topology_plan(topology,surface_bindings);
        for(const auto& requirement:amr::build_amr_flux_surface_requirements(compiled,false))
            if((requirement.roles&static_cast<std::uint8_t>(amr::AmrFluxSurfaceRole::Register))!=0)
                ++external_clear_kernels;
        external_clear_joins=external_clear_kernels>0?1u:0u;
        if(mixed)require(external_clear_kernels>0,
            "Native external mixed coarse/fine topology exposes no real register surface");
    }
    // Independently derive the only extra Self batch upload: original operation
    // index metadata for actual routes carrying Energy. No Phi/psi is staged.
    std::uint64_t self_mapping_bytes=0;
    if(self)for(const auto& registration:topology.routes) {
        const auto& operations=registration.plan.operations;
        if(std::any_of(operations.begin(),operations.end(),[](const auto& operation) {
                return operation.field==amr::AmrField::Energy;
            }))self_mapping_bytes+=operations.size()*sizeof(std::size_t);
    }
    std::vector<std::vector<std::array<long double,2>>> momentum_scales(active.size());
    {
        auto context=host_runtime.stage_context();configure(host_runtime,host_boundary,context);
        if(external||self) {
            // Bind the real prepared Host journal before the transaction. The
            // unchanged Host integrator then runs scheduler::prepare (real Host
            // frame), claims/consumes the actual source through that frame and
            // accepts each stage descriptor through this preserved callback.
            context.hydro_preparation=host_source.get();
            const auto prior_acceptance=context.hydro_acceptance;
            context.hydro_acceptance=[&,prior_acceptance](const scheduler::StageDescriptor& descriptor) {
                host_source->accept(descriptor);if(prior_acceptance)prior_acceptance(descriptor);
                native_isolated_progress(isolated_case,"host","batch-stage-accepted",descriptor.stage);
            };
        }
        scheduler::ScopedStageBinding binding(context,host_runtime.handles());
        driver::RuntimeStateTransaction protected_fields(host_runtime,context,&host_hydro);
        // The actual selected source replaces the SourceNone nullptr.
        // Helm's nearly cancelling pressure forces require an operation-scale
        // parity norm for the two V-measure momenta. Q=|m0|+sum|dt*A*F/V|+|dt*S|
        // is computed BEFORE Device execution, from the actual Host initial
        // state, full selected face EOS and authenticated physical walls. The
        // user-approved 3e-14 coefficient is unchanged; Q is a conditioning
        // scale, not a certified EOS error bound. J/W, other fields and
        // independent totals retain their original comparisons.
        if(cached) {
            require(plan.stages.size()==1&&!mixed&&!external,
                "Helm conditioning observation requires the original source-free Euler owner");
            context.configure_boundary_context(0.,boundary::BoundaryPurpose::Hydro);
            host_runtime.ensure_fluid_ghosts();
            const boundary::HostHydroBoundaryDomainAuthority walls_domain(host_boundary,
                host_control,scheduler::current_stage_binding(),plan.stages.front());
            for(std::size_t n=0;n<active.size();++n) {
                const int id=host_control.tree->GetActiveBlocks()[n];
                const auto& block=host_control.pool->GetBlock(id);const auto& u=block.fluid_state;
                const auto& g=block.grid;const int size=g.GetTotalSize();
                const boundary::HostHydroBoundaryAuthority wall(walls_domain,n,id,u,g);
                FluxAdmissibility::MeanThermoCache means;means.reset(size);
                means.roe_wave_speed=config.numerics.hll_roe_wave_speed;
                means.geometry_semantics=native;
                means.physical_bounds={config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint};
                means.hydro_boundary=wall.require_view(&host_control,id,u,g);
                const auto geometry=GridMetrics::make_geometry_view(g,native);
                auto& scales=momentum_scales[n];scales.resize(size);
                std::vector<FluidVector> flux(size),source(size);
                std::vector<double> species_flux(count*size);
                for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                    const int c=g.GetIndex(i,j,0);
                    scales[c]={std::abs((long double)u.mom_u[c]),std::abs((long double)u.mom_v[c])};
                }
                const auto collect=[&] {
                    for(int dir=0;dir<2;++dir) {
                        std::fill(flux.begin(),flux.end(),FluidVector{});
                        std::fill(species_flux.begin(),species_flux.end(),0.);
                        FluxPolicy::compute_fluxes(u,eos,g,flux,species_flux,dir,
                            config.numerics.entropy_fix_coeff,&means);
                        const int stride=dir==0?1:g.stride_y;
                        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                            const int c=g.GetIndex(i,j,0);
                            const long double factor=dt/GridMetrics::CellVolume(geometry,i,j,0);
                            const long double a=GridMetrics::FaceArea(geometry,dir,i,j,0,false);
                            const long double z=GridMetrics::FaceArea(geometry,dir,i,j,0,true);
                            scales[c][0]+=factor*(std::abs(a*flux[c].mom_u)+std::abs(z*flux[c+stride].mom_u));
                            scales[c][1]+=factor*(std::abs(a*flux[c].mom_v)+std::abs(z*flux[c+stride].mom_v));
                        }
                    }
                    TimeIntegration::add_geometric_sources(source,u,eos,g,dt,native);
                };
                if constexpr(requires{typename Eos::HostHydroScope;}) {
                    typename Eos::HostHydroScope eos_scope(eos);collect();
                } else collect();
                for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                    const int c=g.GetIndex(i,j,0);
                    scales[c][0]+=std::abs((long double)source[c].mom_u);
                    scales[c][1]+=std::abs((long double)source[c].mom_v);
                    require(std::isfinite(scales[c][0])&&std::isfinite(scales[c][1]),
                        "Nonfinite Host Helm operation conditioning scale");
                }
            }
            walls_domain.require_complete_domain();
        }
        Integrator::solve(host_control,dt,host_boundary,
            external?static_cast<const Physical::Gravity::IGravityPolicy*>(host_force.get()):
                self?static_cast<const Physical::Gravity::IGravityPolicy*>(host_self.get()):nullptr,
            &host_hydro,config.numerics);
        protected_fields.commit();
    }
    double reflux_activity=0.;
    {
        auto context=runtime.stage_context();configure(runtime,boundary,context);
        if(external||self) {
            // Bind the real prepared Device journal before the transaction; the
            // scheduler then prepares the actual Device frame and this preserved
            // callback accepts each genuinely consumed stage descriptor.
            context.hydro_preparation=device_source.get();
            const auto prior_acceptance=context.hydro_acceptance;
            context.hydro_acceptance=[&,prior_acceptance](const scheduler::StageDescriptor& descriptor) {
                device_source->accept(descriptor);if(prior_acceptance)prior_acceptance(descriptor);
                native_isolated_progress(isolated_case,"device","batch-stage-accepted",descriptor.stage);
            };
        }
        scheduler::ScopedStageBinding binding(context,runtime.handles());
        driver::RuntimeStateTransaction protected_fields(runtime,context,nullptr);
        const auto executor=[&](const scheduler::StageDescriptor& descriptor,state::CompletionToken token) {
            // SourceNone keeps its outer first-stage clear; actual sources
            // let the real call perform that same clear internally so no
            // duplicate register surface is cleared.
            if(!external&&!self&&descriptor.stage==1)backend->clear_amr_flux_register(token);
            const auto before=backend->counters();
            require(backend->execute_hydro_stage_batch(accesses,descriptor,dt,token,
                external?static_cast<const Physical::Gravity::IGravityPolicy*>(device_force.get()):
                    self?static_cast<const Physical::Gravity::IGravityPolicy*>(device_self.get()):nullptr)==token,
                "Native Hydro actual batch lost its scheduler completion");
            auto expected=before;const auto waves=count?active.size():1u;
            expected.kernel_count+=7*waves+(cached?waves:0)+topology.routes.size();
            ++expected.stream_sync_count;++expected.getter_count;
            expected.bytes_h2d+=active.size()*sizeof(cuda::DeviceHydroBatchBlock);
            expected.bytes_d2h+=active.size()*(sizeof(int)+(state::RepairView::fixed_size+2*count)*sizeof(double));
            // The prepared source's one whole-domain lease check validates
            // retained Device transaction storage and joins its owner stream
            // before launch. Per-patch checks do not repeat that global join.
            if(external||self)++expected.stream_sync_count;
            if(external) {
                // External-only real topology cost: the actual body-source
                // kernels (3 per wave), the 4*N-double compact budget download
                // and, on the original first-stage batch call only, the
                // topology-derived register clear and its single join.
                expected.kernel_count+=3*waves;
                expected.bytes_d2h+=accesses.size()*4*sizeof(double);
                if(descriptor.stage==1) {
                    expected.kernel_count+=external_clear_kernels;
                    expected.stream_sync_count+=external_clear_joins;
                }
            }
            if(self) {
                expected.bytes_h2d+=self_mapping_bytes;
                if(descriptor.stage==1) {
                    expected.kernel_count+=external_clear_kernels;
                    expected.stream_sync_count+=external_clear_joins;
                }
            }
            const auto observed_work=backend->counters();
            if(observed_work!=expected)std::cerr<<"CUDA_NATIVE_HYDRO_WORK_MISMATCH stage="<<descriptor.stage
                <<" external="<<external<<" patches="<<active.size()<<" species="<<count
                <<" kernels="<<observed_work.kernel_count<<'/'<<expected.kernel_count
                <<" h2d="<<observed_work.bytes_h2d<<'/'<<expected.bytes_h2d
                <<" d2h="<<observed_work.bytes_d2h<<'/'<<expected.bytes_d2h
                <<" joins="<<observed_work.stream_sync_count<<'/'<<expected.stream_sync_count
                <<" getters="<<observed_work.getter_count<<'/'<<expected.getter_count<<'\n';
            require(observed_work==expected,"Native Hydro stage changed full transfer/kernel/one-join contract");
            zero_native_receipt(backend->stage_repairs);return token;
        };
        const auto boundary_step=[&](StateSlot slot,state::StateVersion version,state::CompletionToken token) {
            return runtime.execute_device_boundary(slot,version,token);
        };
        const auto rotation=[&]{for(const auto access:accesses)backend->rotate_slots(access,plan.final_rotation);};
        const auto finish_reflux=[&](const scheduler::HydroPlan&,StateSlot slot,state::CompletionToken token) {
            const auto before_fields=mixed?observe():std::vector<FluidState>{};const auto before=backend->counters();
            require(backend->execute_amr_reflux(slot,dt,token)==token,"Native Hydro reflux lost actual completion");
            auto expected=before;++expected.getter_count;
            if(mixed){++expected.kernel_count;++expected.stream_sync_count;
                expected.bytes_h2d+=active.size()*sizeof(cuda::DeviceAmrFluxBlockView);
                expected.bytes_d2h+=sizeof(int)+active.size()*(state::RepairView::fixed_size+2*count)*sizeof(double);}
            require(backend->counters()==expected,"Native Hydro reflux changed full transfer/kernel/join contract");
            zero_native_receipt(backend->reflux_repairs);
            if(mixed){const auto after=observe();for(std::size_t n=0;n<active.size();++n){const auto& g=control.pool->GetBlock(active[n]).grid;
                for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i){const int c=g.GetIndex(i,j,0);
                    reflux_activity=std::max(reflux_activity,std::abs(after[n].mom_w[c]-before_fields[n].mom_w[c]));}}}
            return token;
        };
        scheduler::execute_hydro_plan(context,runtime.handles(),plan,executor,boundary_step,rotation,finish_reflux);
        for(const auto handle:runtime.handles()){const auto accepted=context.ledger.inspect({handle,StateSlot::Current});
            context.ledger.require_readable({handle,StateSlot::Current},{state::ExecutionSide::Device,accepted.interior.version,true,true});}
        protected_fields.commit();
    }
    // Accepted source budget is available only after each real transaction
    // commits. The genuine Host and Device journals must agree on all four
    // components under the original 2e-12 scale (max(1,|value|) for nearzero).
    long double source_work=0.L,source_torque=0.L;
    if(external) {
        const auto host_budget=host_source->external_source_budget();
        const auto device_budget=device_source->external_source_budget();
        for(std::size_t component=0;component<4;++component) {
            require(std::isfinite(host_budget[component])&&std::isfinite(device_budget[component]),
                "accepted Native external source budget is nonfinite");
            if(!axis||component==1||component==3)
                require(host_budget[component]!=0.L&&device_budget[component]!=0.L,
                    "prepared Native external source measured no resolvable active budget component");
            const auto scale=std::max(1.L,std::max(std::abs(host_budget[component]),
                std::abs(device_budget[component])));
            require(std::abs(host_budget[component]-device_budget[component])<=2.e-12L*scale,
                "Host/Device accepted Native external source budget disagrees");
        }
        source_torque=device_budget[2];source_work=device_budget[3];
    }
    long double host_final_potential=0.L,device_final_potential=0.L;
    if(self) {
        // The original batch owner leaves public time at zero. Close/flush its
        // actual journal, then solve accepted Current at that actual Runtime
        // time with the same selected physical boundary and gauge, once per side.
        host_source->flush_committed_diagnostics();device_source->flush_committed_diagnostics();
        native_isolated_progress(isolated_case,"host","post-batch-current-begin");
        host_source->prepare_current(0.,false);
        native_isolated_progress(isolated_case,"host","post-batch-current-end");
        native_isolated_progress(isolated_case,"device","post-batch-current-begin");
        device_source->prepare_current(0.,false);
        native_isolated_progress(isolated_case,"device","post-batch-current-end");
        host_final_potential=host_self->boundary_snapshot().potential_energy;
        device_final_potential=device_self->boundary_snapshot().potential_energy;
        const auto scale=std::max(1.L,std::max(std::abs(host_final_potential),
            std::abs(device_final_potential)));
        require(std::isfinite(host_final_potential)&&std::isfinite(device_final_potential)
            &&std::abs(host_final_potential-device_final_potential)<=2.e-12L*scale,
            "Host/Device final accepted Native Self field energy disagrees");
    }
    auto final=observe();double worst=0.,worst_absolute=0.,activity=0.;
    std::size_t field_mismatches=0;
    for(std::size_t n=0;n<active.size();++n) {
        const auto& g=control.pool->GetBlock(active[n]).grid;
        const auto& reference=host_control.pool->GetBlock(host_control.tree->GetActiveBlocks()[n]).fluid_state;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int c=g.GetIndex(i,j,0);
            for(const auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w,&FluidState::eng}) {
                const double expected=(reference.*field)[c],value=(final[n].*field)[c];
                const double absolute=std::abs(value-expected),difference=absolute/std::max(1.,std::abs(expected));
                worst=std::max(worst,difference);worst_absolute=std::max(worst_absolute,absolute);
                require(std::isfinite(value),"Native actual full batch produced a nonfinite field");
                const bool conditioned=cached&&(field==&FluidState::mom_u||field==&FluidState::mom_v);
                const long double scale=conditioned?std::max({1.L,std::abs((long double)expected),
                    momentum_scales[n][c][field==&FluidState::mom_u?0:1]}):std::max(1.L,std::abs((long double)expected));
                const long double parity_error=absolute/scale;
                if(parity_error>3.e-14L) {
                    ++field_mismatches;
                    if(field_mismatches<=8) {
                    std::cerr.precision(17);std::cerr<<"Native fullbatch mismatch species="<<count<<" cached="<<cached
                        <<" mixed="<<mixed<<" block="<<n<<" cell="<<c<<" i="<<i<<" j="<<j
                        <<" field="<<(field==&FluidState::rho?"rho":field==&FluidState::mom_u?"mom_u":field==&FluidState::mom_v?"mom_v":field==&FluidState::mom_w?"mom_w":"eng")
                        <<" actual="<<value<<" Host="<<expected
                        <<" absolute="<<absolute<<" result_scaled="<<difference<<" operation_scaled="<<double(parity_error)
                        <<" scale="<<double(scale)<<" budget=3e-14\n";
                    }
                }
                activity=std::max(activity,std::abs(value-(initial[n].*field)[c])/std::max(1.,std::abs((initial[n].*field)[c])));
            }
            for(int s=0;s<count;++s)require(std::abs(final[n].X(s,c)-reference.X(s,c))<=3.e-14,
                "Native actual full batch species differs from real Host integrator");
            require(std::bit_cast<std::uint64_t>(final[n].enuc_rate[c])==std::bit_cast<std::uint64_t>(initial[n].enuc_rate[c])
                &&std::bit_cast<std::uint64_t>(reference.enuc_rate[c])==std::bit_cast<std::uint64_t>(initial[n].enuc_rate[c]),
                "Native Hydro update/reflux changed ENUC donor bits");
        }
    }
    require(activity>64.*std::numeric_limits<double>::epsilon(),"Native Hydro manufactured no resolvable actual transport/source activity");
    if(mixed)require(reflux_activity>0.,"Native Hydro coarse/fine angular register/reflux remained inactive");
    const auto final_totals=totals(final);long double budget_error=0.;
    for(std::size_t field=0;field<final_totals.size();++field) {
        const auto scale=self&&field==1
            ?std::max(std::abs(initial_totals[field]+device_initial_potential),
                std::abs(final_totals[field]+device_final_potential))
            :std::max(std::abs(initial_totals[field]),std::abs(final_totals[field]));
        // Independent total mass/species closure is unchanged; the accepted
        // source work/torque are the only real additions to the energy/angular
        // totals for External. Self compares Egas+W in the same selected gauge.
        long double delta=final_totals[field]-initial_totals[field];
        if(external) {if(field==1)delta-=source_work;else if(field==2)delta-=source_torque;}
        if(self&&field==1)delta+=device_final_potential-device_initial_potential;
        const auto error=std::abs(delta)/scale;budget_error=std::max(budget_error,error);
        require(std::isfinite(error)&&error<=2.e-12L,"Native Hydro violated independent closed V/W integral budget");
    }
    // Keep the original field budget/failure, but observe the independent
    // conserved integrals before stopping at a first cancelling momentum cell.
    if(field_mismatches) {
        std::cerr.precision(17);
        std::cerr<<"CUDA_NATIVE_HYDRO_FIELD_DIAGNOSTIC mismatches="<<field_mismatches
            <<" max_scaled_difference="<<worst<<" max_absolute_difference="<<worst_absolute
            <<" closed_budget_error="<<double(budget_error)<<" original_field_budget=3e-14\n";
    }
    require(field_mismatches==0,"Native actual full batch differs from real Host integrator");
    // Same-owner EOS-failure reuse drives the SourceNone four-argument batch;
    // actual sources require their prepared frame, so keep this default-only.
    if(!cached&&!mixed&&count==0&&!external&&!self) {
        // A real last-patch required EOS failure is drained, then valid input
        // reuses this same owner. The batch itself is not claimed transactional.
        const auto reuse_token=runtime.stage_context().ledger.inspect({runtime.handles().front(),StateSlot::Current}).ghost.completion;
        auto damaged=final.back();const auto& g=control.pool->GetBlock(active.back()).grid;
        damaged.eng[g.GetIndex(g.Is()-1,g.Js(),0)]=std::numeric_limits<double>::infinity();
        backend->enqueue_upload_slot(accesses.back(),StateRegion::Ghost,transfer_view(damaged));backend->quiesce();
        const auto before=backend->counters();bool rejected=false;
        try{backend->execute_hydro_stage_batch(accesses,plan.stages.front(),dt,reuse_token);}
        catch(const std::runtime_error& error){rejected=std::string(error.what()).find("block="+std::to_string(accesses.back().block.uid.value))!=std::string::npos;}
        const auto failed=backend->counters();
        require(rejected&&failed.kernel_count>before.kernel_count&&failed.stream_sync_count>before.stream_sync_count,
            "Native Hydro required EOS failure did not drain real enqueued work");
        for(std::size_t n=0;n<final.size();++n)for(auto region:{StateRegion::Interior,StateRegion::Ghost})
            backend->enqueue_upload_slot(accesses[n],region,transfer_view(final[n]));
        backend->quiesce();
        require(backend->execute_hydro_stage_batch(accesses,plan.stages.front(),dt,reuse_token)==reuse_token,
            "Native Hydro same owner retained failed EOS latch after valid reuse");
        zero_native_receipt(backend->stage_repairs);
    }
    require(!runtime.active_runtime_state_transaction()&&!runtime.native_macro_retry_attempt()
        &&counters.step_count==0&&counters.t_current==0.,"Native batch test advanced public macro/source/retry qualification");
    std::cout<<"CUDA_NATIVE_HYDRO_OWNER_PASS species="<<count<<" axis="<<axis<<" mixed="<<mixed
        <<" cached_eos="<<cached<<" helm_momentum_operation_norm="<<cached<<" external="<<external<<" self="<<self<<" stages="<<plan.stages.size()<<" max_scaled_difference="<<worst
        <<" max_absolute_difference="<<worst_absolute<<" closed_budget_error="<<double(budget_error)<<" activity="<<activity<<" reflux_activity="<<reflux_activity
        <<" source_torque="<<double(source_torque)<<" source_work="<<double(source_work)
        <<" self_initial_potential="<<double(device_initial_potential)
        <<" self_final_potential="<<double(device_final_potential)
        <<" self_delta_potential="<<double(device_final_potential-device_initial_potential)
        <<" self_max_face_acceleration="<<self_max_face_acceleration
        <<" self_source_scaled_amplitude="<<self_source_scaled_amplitude<<'\n';
    // The original batch's public time/step==0 assertion above is unchanged.
    // Reuse the original SourceNone fixture and each actual axis/mixed source
    // fixture after batch acceptance; each separate full macro advances once.
    if constexpr(std::is_same_v<Eos,IdealGas>) {
        if(!cached&&count==2&&route==dispatch::TimeIntegratorId::Rk2
            &&((!external&&!self&&!axis&&!mixed)||external||self))
            verify_native_source_macro(host_runtime,runtime,host_counters,counters,eos,
                host_hydro,selected,dt,observe,totals,
                external?static_cast<const Physical::Gravity::IGravityPolicy*>(host_force.get()):
                    self?static_cast<const Physical::Gravity::IGravityPolicy*>(host_self.get()):nullptr,
                external?static_cast<const Physical::Gravity::IGravityPolicy*>(device_force.get()):
                    self?static_cast<const Physical::Gravity::IGravityPolicy*>(device_self.get()):nullptr,
                host_source.get(),device_source.get(),host_self.get(),device_self.get(),isolated_case);
    }
}

/** Original cold angular counterexample with inactive Burn/species removed.
 * Constant nu, no thermal/species diffusion and gamma=1.4 preserve the first
 * D recurrence. The Host reads its genuine failing RKL mean; Device borrows
 * the real compact EOS/classifier and is observed only outside transactions.
 */
namespace native_device_retry_checks {
using namespace arch;
constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr double cold_internal=0x1p-25;
using Hydro=Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>>;
bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool values(const std::vector<double>& a,const std::vector<double>& b) {
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),bits);
}
bool receipt(const state::RepairBudget& a,const state::RepairBudget& b,bool identity=true) {
    return a.semantics==b.semantics&&values(a.values,b.values)&&(!identity||a.block_uid==b.block_uid)
        &&a.stage==b.stage&&bits(a.time,b.time)
        &&std::equal(std::begin(a.position),std::end(a.position),std::begin(b.position),bits);
}
std::array<long double,3> totals(const FluidState& u,const Grid& g) {
    std::array<long double,3> sum{};
    for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
        const int n=g.GetIndex(i,j,0);const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
        const long double V=std::acos(-1.L)*(h*h-l*l)*g.dx2,W=2.L*std::acos(-1.L)*(h*h*h-l*l*l)*g.dx2/3.L;
        sum[0]+=u.rho[n]*V;sum[1]+=u.eng[n]*V;sum[2]+=u.mom_w[n]*W;
    }
    return sum;
}
struct Fixture {
    SimConfig config;SpeciesManager species;amr::AMRControl control{8,2};RunState start{};
    std::unique_ptr<IdealGas> eos;std::unique_ptr<SimulationController> clock;
    std::unique_ptr<BCHandler> bc;std::unique_ptr<driver::DriverRuntime> runtime;
    std::unique_ptr<Hydro> hydro;std::optional<scheduler::StageExecutionContext> context;
    driver::DriverStageWorkspace workspace;
    dispatch::ResolvedExecutionPlan selected{dispatch::FluxId::Hllc,dispatch::ReconstructionId::Pcm,
        dispatch::LimiterId::MinMod,dispatch::TimeIntegratorId::Euler,dispatch::EosId::Ideal,
        dispatch::NetworkId::None,dispatch::OdeSolverId::None,dispatch::LinearSolverId::None,
        dispatch::DiffusionIntegratorId::Rkl1};
    double fe=0.,advice=.75;std::vector<double> intervals;int rejected=0,diffusions=0,hydros=0;
    bool observed=false;FluidVector failed_value{};
    explicit Fixture(bool device) {
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=config.grid.x2_min=0.;config.grid.x1_max=config.grid.x2_max=16.;
        config.grid.x1l_boundary_type="outflow";config.grid.x1r_boundary_type="reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
        config.grid.amr_max_blocks=8;config.amr.lrefinemin=config.amr.lrefinemax=0;
        config.numerics.time_integrator="euler";config.numerics.solver_name="HLLC";
        config.numerics.reconstruction="pcm";config.physics.eos_type="ideal";
        config.physics.gravity.type="none";config.physics.burn.use_burn=false;
        auto& d=config.physics.diffusion;d.use_diffusion=d.use_viscous_diffusion=true;
        d.nu_visc=2.;d.use_thermal_diffusion=d.use_species_diffusion=false;d.integrator="RKL1";
        config.io.tmax=8.;config.io.plt_dt=config.io.chk_dt=0.;
        require(d.diff_cfl==.8&&cold_internal>config.numerics.min_eint,
            "Device retry changed the original thermal floor or diffusion CFL");
        eos=std::make_unique<IdealGas>(1.4,species);control.tree->InitRootGrid(config,0,native);
        control.flux_register.EnsureSpecies(0);auto& b=block();const auto& g=b.grid;
        require(control.tree->GetActiveBlocks().size()==1&&g.dx1==1.&&g.dx2==1.
            &&g.dyadic_identity.bound&&g.dyadic_identity.periodic_axial,
            "Device retry lost the original actual unit root");
        for(auto* u:{&b.fluid_state,&b.state_next,&b.state_scratch}) {
            u->stage_repairs.reset(0,state::RepairSemantics::RzVolumeAngular);
            for(int n=0;n<g.GetTotalSize();++n){u->set(n,{1.,0.,0.,0.,1.5});u->enuc_rate[n]=0.;}
            for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
                const long double v=(h*h-l*l)/2.,w=(h*h*h-l*l*l)/3.,I=(h*h*h*h-l*l*l*l)/4.;
                const bool first=std::max(std::abs(l),std::abs(h))<=1.L;
                const long double omega=first?0.L:1.L,e=first?cold_internal:1.5L;
                u->set(g.GetIndex(i,j,0),{1.,0.,0.,double(omega*I/w),double(e+omega*omega*I/(2.*v))});
            }
        }
        start.time=.375;start.step=1;start.has_timestep_state=true;start.dt_old=.375;
        start.repairs.reset(0,state::RepairSemantics::RzVolumeAngular);
        clock=std::make_unique<SimulationController>(config,start);bc=std::make_unique<BCHandler>(config,native);
        bc->bind(*eos,species);require(!bc->has_user(),"Device retry introduced a user boundary");
        bc->configure_stage(start.time,boundary::BoundaryPurpose::Hydro);
        runtime=std::make_unique<driver::DriverRuntime>(control,*bc,config,species,*clock);
        runtime->bind_native_rz_eos(*eos);runtime->initialize_topology();
        if(device) {
            std::vector<cuda::CudaBlockBinding> bindings;
            for(const auto& e:runtime->prepare_backend_bindings())bindings.push_back({e.block,e.handle,e.storage,e.physical_boundary});
            runtime->install_backend(cuda::make_cuda_backend(bindings,0,cuda::make_cuda_launch_config(selected,config),species,*eos));
            runtime->upload_initial_state();runtime->backend()->prepare_amr_flux_plan(
                control.RequireFluxTopologyPlan(0,native,-1,true),control.RequireRefluxTopologyPlan(0,native,true));
        }
        hydro=std::make_unique<Hydro>(*eos,native);context.emplace(runtime->stage_context());bind_frame(.2);
        const auto candidates=driver::calculate_timestep_candidates(*runtime,workspace,*eos,&selected);
        fe=candidates.diffusion_forward_euler;
        require(std::abs(fe-1./44.)<=3.e-14&&candidates.hydro>=.2,
            "Device retry changed the independent FE row or bypassed actual Hydro CFL");
    }
    amr::Block& block(){return control.pool->GetBlock(control.tree->GetActiveBlocks().front());}
    void bind_frame(double dt) {
        context->step_start_time=clock->t_current;context->step_dt=dt;
        context->boundary_start_time=clock->t_current;context->boundary_step_dt=.5*dt;
        context->configure_boundary_context=[this](double time,boundary::BoundaryPurpose purpose) {
            bc->configure_stage(time,purpose);runtime->bind_native_boundary_acceptance(*context,runtime->handles());
            if(!runtime->backend()) {
                const auto real=context->post_boundary_acceptance;
                context->post_boundary_acceptance=[this,real](const scheduler::StageExecutionContext& actual,
                    state::StateSlot slot,state::StateVersion version) {
                    const auto* frame=scheduler::current_rkl_completed_boundary();
                    if(context->step_dt==.2&&frame&&frame->completed&&frame->context==&actual) {
                        const auto member=TimeIntegration::hydro_boundary_state_member(slot);const auto& g=block().grid;
                        failed_value=(block().*member).get(g.GetIndex(g.Is(),g.Js(),0));observed=true;
                    }
                    real(actual,slot,version);
                };
            }
        };
        context->physical_boundary_preparation=[this](state::StateSlot slot,double time,boundary::BoundaryPurpose purpose) {
            context->configure_boundary_context(time,purpose);runtime->ensure_fluid_ghosts(slot);
        };
        context->configure_boundary_context(clock->t_current,boundary::BoundaryPurpose::Hydro);
        runtime->bind_boundary_accounting(*context);
        const auto one=scheduler::make_rkl_plan(scheduler::RklMethod::RKL1,1);
        context->rkl_flux_capture_begin(one.stages.front(),one);runtime->ensure_fluid_ghosts();
    }
    std::array<FluidState,3> slots() {
        require(!runtime->active_runtime_state_transaction(),"Retry witness downloaded armed resident fields");
        auto& b=block();std::array<FluidState,3> result{b.fluid_state,b.state_next,b.state_scratch};
        if(auto* backend=runtime->backend()) {
            constexpr state::SlotRotation cycle{state::StateSlot::Next,state::StateSlot::Scratch,state::StateSlot::Current};
            for(auto& u:result) {
                for(auto region:{state::StateRegion::Interior,state::StateRegion::Ghost})
                    backend->enqueue_materialize_host_current(runtime->backend_access(0,state::StateSlot::Current),region,transfer_view(u));
                backend->quiesce();backend->rotate_slots(runtime->backend_access(0,state::StateSlot::Current),cycle);
            }
        }
        return result; // Three quiescent permutations preserve the actual mapping/ledger.
    }
    void same_slots(const std::array<FluidState,3>& a,const std::array<FluidState,3>& b) {
        const auto& g=block().grid;
        for(int s=0;s<3;++s)for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
            const int n=g.GetIndex(i,j,0);
            for(auto field:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate})
                require(bits((a[s].*field)[n],(b[s].*field)[n]),"Native retry changed actual slot fields/mapping");
        }
    }
    double execute(double cap) {
        const double accepted=driver::execute_driver_macro_attempts(*runtime,*clock,advice,cap,
            [this](double dt,std::uint64_t index) {
                intervals.push_back(dt);bind_frame(dt);
                auto owner=driver::RuntimeStateTransaction::snapshot_owner(*runtime,*context);
                const auto before=slots();auto* backend=runtime->backend();
                const auto surfaces=backend?backend->download_boundary_flux_capture():std::vector<arch::backend::BoundaryFluxPlanes>{};
                const auto stage=backend?backend->stage_repairs:state::RepairBudget{};
                const auto reflux=backend?backend->reflux_repairs:state::RepairBudget{};
                const auto work=backend?backend->counters():arch::backend::BackendCounters{};
                try {
                    scheduler::ScopedStageBinding binding(*context,runtime->handles());
                    driver::NativeMacroRetryAttempt retry(*runtime,*context,index,scheduler::RklMethod::RKL1,fe);
                    driver::execute_driver_macro_step(*runtime,*context,hydro.get(),false,
                        [](driver::BurnHalf,double,state::CompletionToken token){require(false,"Disabled retry Burn was entered");return token;},
                        [&](double half){++diffusions;driver::advance_diffusion(*runtime,workspace,*context,*eos,&selected,clock->step_count,half,fe);},
                        [&](double full){++hydros;driver::advance_hydro(*runtime,workspace,*context,&selected,full,
                            &SolverEuler::solve<BCHandler>,nullptr,hydro.get(),driver::RuntimeStateQualification::Production);},
                        [](driver::CpuStage,auto&& call){call();});
                } catch(const driver::NativeThermalStepRejection& error) {
                    ++rejected;const auto& d=error.evidence();const auto& g=block().grid;
                    require(d.pool_index==block().id&&d.handle==runtime->handles().front()
                        &&d.diagnostic.i>=g.Is()&&d.diagnostic.i<g.Ie()&&d.diagnostic.j>=g.Js()&&d.diagnostic.j<g.Je()
                        &&RzThermodynamics::is_retryable_thermal_failure(d.diagnostic)
                        &&!runtime->active_runtime_state_transaction()&&!runtime->native_macro_retry_attempt(),
                        "Device retry used a foreign, ghost or nonthermal refusal");
                    const auto& observed_work=runtime->boundary_observer_operations();
                    require(observed_work.kernel_count>=owner.counters.kernel_count&&observed_work.bytes_h2d>=owner.counters.bytes_h2d
                        &&observed_work.bytes_d2h>=owner.counters.bytes_d2h&&observed_work.stream_sync_count>=owner.counters.stream_sync_count
                        &&observed_work.getter_count>=owner.counters.getter_count,"Device retry erased failed boundary work");
                    owner.counters=observed_work; // Keep failed work, preserve every other frozen owner field.
                    require(driver::RuntimeStateTransaction::owner_matches(*runtime,*context,owner),"Device retry did not restore the full actual owner");
                    const auto& activity=runtime->diffusion_activity_totals();
                    require(clock->t_current==start.time&&clock->step_count==start.step&&activity.accepted_macros==0
                        &&activity.accepted_halves==0&&activity.cells==0&&activity.signed_energy_change==0.
                        &&activity.absolute_energy_change==0.,"Failed retry published public time/activity");
                    if(backend) {
                        const auto after=backend->counters();require(after.kernel_count>work.kernel_count&&after.stream_sync_count>work.stream_sync_count
                            &&after.bytes_h2d>=work.bytes_h2d&&after.bytes_d2h>=work.bytes_d2h&&after.getter_count>=work.getter_count,
                            "Device retry erased actual rejected RKL work");
                        require(receipt(stage,backend->stage_repairs)&&receipt(reflux,backend->reflux_repairs),
                            "Device retry changed accepted repair receipts");
                        const auto restored=backend->download_boundary_flux_capture();require(restored.size()==surfaces.size(),"Device retry lost capture owners");
                        for(std::size_t b=0;b<surfaces.size();++b){require(restored[b].block==surfaces[b].block,"Device retry changed capture identity");
                            for(int f=0;f<6;++f)require(values(restored[b].stage[f],surfaces[b].stage[f])&&values(restored[b].initial[f],surfaces[b].initial[f]),"Device retry changed capture planes");}
                    }
                    same_slots(slots(),before);throw;
                }
            });
        require(bits(accepted,.1)&&clock->t_current==start.time&&clock->step_count==start.step
            &&bits(clock->dt_old,.1)&&bits(advice,1e99),"Retry advice/proposal did not commit exactly the accepted interval");
        for(const auto handle:runtime->handles()) {
            const auto u=context->ledger.inspect({handle,state::StateSlot::Current});scheduler::detail::require_settled_destination(u);
            context->ledger.require_readable({handle,state::StateSlot::Current},{context->side,u.interior.version,true,true});
        }
        require(runtime->repair_budget().semantics==state::RepairSemantics::RzVolumeAngular
            &&std::all_of(runtime->repair_budget().values.begin(),runtime->repair_budget().values.end(),[](double x){return x==0.;}),
            "Device retry concealed a repair");
        if(const auto* backend=runtime->backend())for(const auto* report:{&backend->stage_repairs,&backend->reflux_repairs})
            require(report->semantics==state::RepairSemantics::RzVolumeAngular
                &&std::all_of(report->values.begin(),report->values.end(),[](double x){return x==0.;}),
                "Device retry concealed a backend stage/reflux repair");
        const auto endpoint=bc->snapshot_stage_context();require(endpoint.time()==start.time+.1&&endpoint.purpose()==boundary::BoundaryPurpose::Hydro,
            "Device retry accepted another boundary endpoint");
        clock->advance(accepted);require(clock->step_count==start.step+1&&bits(clock->t_current,start.time+.1),"Retry caller advanced public time twice");
        return accepted;
    }
};
void run() {
    struct ActivityEnvironment {
        std::optional<std::string> old;
        ActivityEnvironment(){if(const auto* v=std::getenv("ARCH_TRACE_DIFFUSION_ACTIVITY"))old=v;
            require(::setenv("ARCH_TRACE_DIFFUSION_ACTIVITY","1",1)==0,"Cannot enable actual retry diffusion activity");}
        ~ActivityEnvironment(){if(old)::setenv("ARCH_TRACE_DIFFUSION_ACTIVITY",old->c_str(),1);else ::unsetenv("ARCH_TRACE_DIFFUSION_ACTIVITY");}
    } activity;
    Fixture host(false),retry(true),direct(true);const auto initial=retry.slots();retry.same_slots(initial,direct.slots());
    const auto initial_totals=totals(initial[0],retry.block().grid);
    host.execute(.2);retry.execute(.2);direct.execute(.1);
    require(host.observed&&host.rejected==1&&retry.rejected==1&&direct.rejected==0
        &&retry.intervals==std::vector<double>({.2,.1})&&direct.intervals==std::vector<double>({.1})
        &&retry.diffusions==3&&retry.hydros==1&&direct.diffusions==2&&direct.hydros==1,
        "Native Device did not really reject .2 then accept the unchanged .1 split");
    require(std::abs(host.failed_value.mom_w-2967./6272.)<=3.e-14
        &&std::abs(host.failed_value.eng-(cold_internal+14596739./177020928.))<=3.e-14
        &&std::abs(host.failed_value.eng-4.*std::pow(host.failed_value.mom_w/3.,2)-(cold_internal-3009439./177020928.))<=3.e-14,
        "Zero-species retry changed the original independent cold angular recurrence");
    const auto actual=retry.slots(),reference=direct.slots();retry.same_slots(actual,reference);
    const auto& g=retry.block().grid;const auto& h=host.block().fluid_state;
    for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i){const int n=g.GetIndex(i,j,0);
        for(auto f:{&FluidState::rho,&FluidState::mom_u,&FluidState::mom_v,&FluidState::mom_w,&FluidState::eng})
            require(std::isfinite((actual[0].*f)[n])&&std::abs((actual[0].*f)[n]-(h.*f)[n])<=3.e-14*std::max(1.,std::abs((h.*f)[n])),
                "Device retry differs from the actual same-method Host endpoint");
        require(bits(actual[0].enuc_rate[n],0.)&&bits(h.enuc_rate[n],0.),"Inactive retry changed ENUC bits");}
    const auto same_region=[](const state::RegionCoherence& a,const state::RegionCoherence& b){return a.residency==b.residency
        &&a.version==b.version&&a.completion==b.completion&&a.pending_transfer==b.pending_transfer;};
    for(auto slot:{state::StateSlot::Current,state::StateSlot::Next,state::StateSlot::Scratch}) {
        const auto a=retry.context->ledger.inspect({retry.runtime->handles().front(),slot});
        const auto b=direct.context->ledger.inspect({direct.runtime->handles().front(),slot});
        scheduler::detail::require_settled_destination(a);scheduler::detail::require_settled_destination(b);
        require(same_region(a.interior,b.interior)&&same_region(a.ghost,b.ghost)&&a.ghost_source_version==b.ghost_source_version,
            "Failed retry changed final slot coherence/history relative to direct .1");
    }
    require(values(retry.runtime->hydro_boundary_budget(),direct.runtime->hydro_boundary_budget())
        &&values(retry.runtime->diffusion_boundary_budget(),direct.runtime->diffusion_boundary_budget())
        &&receipt(retry.runtime->repair_budget(),direct.runtime->repair_budget(),false)
        &&receipt(retry.runtime->backend()->stage_repairs,direct.runtime->backend()->stage_repairs,false)
        &&receipt(retry.runtime->backend()->reflux_repairs,direct.runtime->backend()->reflux_repairs,false),
        "Device retry published different actual boundary/repair receipts");
    const auto final_totals=totals(actual[0],g);const int component[]{0,4,3};
    const auto& H=retry.runtime->hydro_boundary_budget();const auto& D=retry.runtime->diffusion_boundary_budget();
    require(H.size()==6&&D.size()==6,"Device retry lost actual Native boundary receipt shape");
    for(int f=0;f<3;++f) {
        const long double outward=H[component[f]]+D[component[f]];
        const auto scale=std::max(std::abs(initial_totals[f]),std::abs(final_totals[f]))+std::abs(outward);
        const auto residual=final_totals[f]-initial_totals[f]+outward;
        require(std::isfinite(residual)&&std::abs(residual)<=2.e-12L*scale,
            "Device retry violated the unchanged V/W mass/energy/angular budget");
    }
    const auto& a=retry.runtime->diffusion_activity_totals();const auto& b=direct.runtime->diffusion_activity_totals();
    require(a.accepted_macros==1&&a.accepted_halves==2&&a.cells==2u*amr::BLOCK_NX*amr::BLOCK_NY
        &&a.accepted_macros==b.accepted_macros&&a.accepted_halves==b.accepted_halves&&a.cells==b.cells
        &&a.signed_energy_change==b.signed_energy_change&&a.absolute_energy_change==b.absolute_energy_change
        &&a.absolute_energy_change>0.,"Failed Device attempt leaked into actual accepted diffusion activity");
    require(retry.runtime->backend()->counters().kernel_count>direct.runtime->backend()->counters().kernel_count,
        "Device retry dropped the actual rejected attempt's work count");
    std::cout<<"CUDA_NATIVE_DEVICE_THERMAL_RETRY_OWNER_PASS attempts=2 rejected_dt=.2 accepted_dt=.1 species=0 burn=0 public_steps=1\n";
}
} // namespace native_device_retry_checks

/** Bounded actual consumers; original selected-face/cache math matrices remain
 * the owners of complete flux/limiter policy support and independent face science.
 */
void run_native_hydro_batch_contract()
{
    using namespace arch;using dispatch::EosId;using dispatch::FluxId;
    using dispatch::ReconstructionId;using dispatch::TimeIntegratorId;using scheduler::HydroMethod;
    for(int count:{0,2,31}) {
        SpeciesManager species;for(int s=0;s<count;++s)species.add_species("gas"+std::to_string(s),1.,1.,1.4,3.);
        IdealGas eos(1.4,species);
        if(count==0)run_native_hydro_owner_case<IdealGas,FluxHLLC<PCMReconstruction>,SolverEuler>(
            species,eos,EosId::Ideal,FluxId::Hllc,ReconstructionId::Pcm,HydroMethod::Euler,TimeIntegratorId::Euler,true,false);
        if(count==2){run_native_hydro_owner_case<IdealGas,FluxHLL<MusclReconstruction<MinMod>>,SolverRK2>(
            species,eos,EosId::Ideal,FluxId::Hll,ReconstructionId::Muscl,HydroMethod::RK2,TimeIntegratorId::Rk2,false,false);
            run_native_hydro_owner_case<IdealGas,FluxHLLC<PPMReconstruction>,SolverRK2>(
                species,eos,EosId::Ideal,FluxId::Hllc,ReconstructionId::Ppm,HydroMethod::RK2,TimeIntegratorId::Rk2,false,true);
            // Two real prepared Native external variants of this same 2-species
            // IdealGas RK2 owner: the same-level cylindrical-axis domain and the
            // mixed five-leaf off-axis domain. Both reuse the existing inputs,
            // references and tolerances; only the real source owner is added.
            run_native_hydro_owner_case<IdealGas,FluxHLL<MusclReconstruction<MinMod>>,SolverRK2>(
                species,eos,EosId::Ideal,FluxId::Hll,ReconstructionId::Muscl,HydroMethod::RK2,TimeIntegratorId::Rk2,true,false,false,true);
            run_native_hydro_owner_case<IdealGas,FluxHLLC<PPMReconstruction>,SolverRK2>(
                species,eos,EosId::Ideal,FluxId::Hllc,ReconstructionId::Ppm,HydroMethod::RK2,TimeIntegratorId::Rk2,false,true,false,true);
            // Two genuine prescribed Self variants of the original Ideal2 RK2
            // owner, with unchanged state/species/ENUC and integral thresholds.
            run_native_hydro_owner_case<IdealGas,FluxHLL<MusclReconstruction<MinMod>>,SolverRK2>(
                species,eos,EosId::Ideal,FluxId::Hll,ReconstructionId::Muscl,HydroMethod::RK2,TimeIntegratorId::Rk2,true,false,false,false,true);
            run_native_hydro_owner_case<IdealGas,FluxHLLC<PPMReconstruction>,SolverRK2>(
                species,eos,EosId::Ideal,FluxId::Hllc,ReconstructionId::Ppm,HydroMethod::RK2,TimeIntegratorId::Rk2,false,true,false,false,true);
            // Public isolated variants reuse these exact axis/two-root and
            // off-axis/five-leaf Self owners, fields, timesteps and budgets.
            // Production Current, original RK2 stages and the full macro
            // transaction invoke the existing actual ring execution owner.
            run_native_hydro_owner_case<IdealGas,FluxHLL<MusclReconstruction<MinMod>>,SolverRK2>(
                species,eos,EosId::Ideal,FluxId::Hll,ReconstructionId::Muscl,HydroMethod::RK2,TimeIntegratorId::Rk2,true,false,false,false,true,true);
            run_native_hydro_owner_case<IdealGas,FluxHLLC<PPMReconstruction>,SolverRK2>(
                species,eos,EosId::Ideal,FluxId::Hllc,ReconstructionId::Ppm,HydroMethod::RK2,TimeIntegratorId::Rk2,false,true,false,false,true,true);}
        if(count==31)run_native_hydro_owner_case<IdealGas,FluxHLLC<PPMReconstruction>,SolverRK3>(
            species,eos,EosId::Ideal,FluxId::Hllc,ReconstructionId::Ppm,HydroMethod::RK3,TimeIntegratorId::Rk3,true,false);
    }
    SpeciesManager species;species.add_species("C12",12.,6.,5./3.,1.5e8);species.add_species("O16",16.,8.,5./3.,1.5e8);
    HelmEos eos(std::string(ARCH_SOURCE_DIR)+"/EOS_toolkit/tables/helmholtz/helm_table.dat",&species);
    run_native_hydro_owner_case<HelmEos,FluxHLLC<PPMReconstruction>,SolverEuler>(
        species,eos,EosId::Helmholtz,FluxId::Hllc,ReconstructionId::Ppm,HydroMethod::Euler,TimeIntegratorId::Euler,false,false,true);
    native_device_retry_checks::run();
}

} // namespace

int main()
{
    try {
        run_multiblock_exchange();
        run_2d_corner_exchange();
        run_hydro_batch_contract();
        run_native_hydro_batch_contract();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
