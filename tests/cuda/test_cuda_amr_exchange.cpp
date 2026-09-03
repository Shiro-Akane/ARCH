#include "amr/AMRControl.h"
#include "amr/BoundaryPlan.h"
#include "amr/GhostExchange.h"
#include "cuda/runtime/CudaBackend.h"
#include "physics/eos/IdealGas.h"
#include "physics/species/Species.h"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

arch::boundary::BoundaryPlan make_boundary_plan(int dimension)
{
    using namespace arch::boundary;
    BoundaryPlanInput input{};
    input.dimension = dimension;
    input.active_extent = {
        amr::BLOCK_NX,
        dimension >= 2 ? amr::BLOCK_NY : 1,
        dimension == 3 ? amr::BLOCK_NZ : 1};
    input.ghost_depth = amr::MAX_NG;
    input.faces.fill(BoundaryType::Inactive);
    for (int axis = 0; axis < dimension; ++axis) {
        input.faces[face_index(
            static_cast<BoundaryAxis>(axis), BoundarySide::Lower)] =
            BoundaryType::Outflow;
        input.faces[face_index(
            static_cast<BoundaryAxis>(axis), BoundarySide::Upper)] =
            BoundaryType::Outflow;
    }
    return arch::boundary::make_boundary_plan(input);
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

arch::backend::HostStateTransferView transfer_view(FluidState& state)
{
    return {
        state.rho.data(), state.mom_u.data(), state.mom_v.data(),
        state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
        state.mass_fractions.empty() ? nullptr : state.mass_fractions.data(),
        state.rho.size(),
        static_cast<std::size_t>(state.GetNumSpecies()), state.rho.size()};
}

FluidState& state_for(amr::Block& block, arch::state::StateSlot slot)
{
    using arch::state::StateSlot;
    if (slot == StateSlot::Current) return block.fluid_state;
    if (slot == StateSlot::Next) return block.state_next;
    return block.state_scratch;
}

void fill_block(amr::Block& block)
{
    const int total = block.grid.GetTotalSize();
    const double block_seed = 1000.0 * block.level
        + 100.0 * block.logical_x1 + 10.0 * block.logical_x2
        + block.logical_x3;
    for (int index = 0; index < total; ++index) {
        const double value = block_seed + 0.001 * index;
        block.fluid_state.rho[index] = 1.0 + 0.01 * value;
        block.fluid_state.mom_u[index] = 2.0 + 0.02 * value;
        block.fluid_state.mom_v[index] = 3.0 + 0.03 * value;
        block.fluid_state.mom_w[index] = 4.0 + 0.04 * value;
        block.fluid_state.eng[index] = 10.0 + 0.05 * value;
        block.fluid_state.enuc_rate[index] = -2.0 + 0.006 * value;
        block.fluid_state.X(0, index) = 0.2 + 1.0e-5 * value;
        block.fluid_state.X(1, index) =
            1.0 - block.fluid_state.X(0, index);
    }
    block.state_next = block.fluid_state;
    block.state_scratch = block.fluid_state;
    for (int index = 0; index < total; ++index) {
        block.state_next.rho[index] += 1.0;
        block.state_next.mom_u[index] += 2.0;
        block.state_next.mom_v[index] += 3.0;
        block.state_next.mom_w[index] += 4.0;
        block.state_next.eng[index] += 5.0;
        block.state_next.enuc_rate[index] += 6.0;
        block.state_scratch.rho[index] += 11.0;
        block.state_scratch.mom_u[index] += 12.0;
        block.state_scratch.mom_v[index] += 13.0;
        block.state_scratch.mom_w[index] += 14.0;
        block.state_scratch.eng[index] += 15.0;
        block.state_scratch.enuc_rate[index] += 16.0;
    }
}

struct LeafGrid {
    std::vector<int> level;
    std::vector<std::uint32_t> x;
    std::vector<std::uint32_t> y;
    std::vector<std::uint32_t> z;
};

LeafGrid mixed_corner_grid(int dimension)
{
    LeafGrid result;
    const int children = 1 << dimension;
    result.level.reserve(static_cast<std::size_t>(children * 2 - 1));
    result.x.reserve(result.level.capacity());
    result.y.reserve(result.level.capacity());
    result.z.reserve(result.level.capacity());

    // Refine the lower corner root.  Its children meet coarse neighbours on
    // the positive X/Y/Z faces, so one compact hierarchy covers every active
    // direction and both coarse-to-fine and fine-to-coarse routes.
    for (int child = 0; child < children; ++child) {
        result.level.push_back(1);
        result.x.push_back(static_cast<std::uint32_t>(child & 1));
        result.y.push_back(static_cast<std::uint32_t>(
            dimension >= 2 ? (child >> 1) & 1 : 0));
        result.z.push_back(static_cast<std::uint32_t>(
            dimension == 3 ? (child >> 2) & 1 : 0));
    }
    for (int root = 1; root < children; ++root) {
        result.level.push_back(0);
        result.x.push_back(static_cast<std::uint32_t>(root & 1));
        result.y.push_back(static_cast<std::uint32_t>(
            dimension >= 2 ? (root >> 1) & 1 : 0));
        result.z.push_back(static_cast<std::uint32_t>(
            dimension == 3 ? (root >> 2) & 1 : 0));
    }
    return result;
}

bool close(double left, double right)
{
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= 2.0e-13 * scale;
}

void compare_field(const std::vector<double>& expected,
                   const std::vector<double>& actual,
                   std::string_view field, int dimension,
                   arch::state::StateSlot slot, std::size_t block)
{
    require(expected.size() == actual.size(), "AMR exchange field shape drifted");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (!close(expected[index], actual[index])) {
            throw std::runtime_error(
                "CUDA AMR exchange mismatch dim=" + std::to_string(dimension)
                + " slot=" + std::to_string(static_cast<int>(slot))
                + " block=" + std::to_string(block)
                + " field=" + std::string(field)
                + " cell=" + std::to_string(index)
                + " expected=" + std::to_string(expected[index])
                + " actual=" + std::to_string(actual[index]));
        }
    }
}

void compare_state(const FluidState& expected, const FluidState& actual,
                   int dimension, arch::state::StateSlot slot,
                   std::size_t block)
{
    compare_field(expected.rho, actual.rho, "rho", dimension, slot, block);
    compare_field(expected.mom_u, actual.mom_u, "mom_u", dimension, slot, block);
    compare_field(expected.mom_v, actual.mom_v, "mom_v", dimension, slot, block);
    compare_field(expected.mom_w, actual.mom_w, "mom_w", dimension, slot, block);
    compare_field(expected.eng, actual.eng, "eng", dimension, slot, block);
    compare_field(expected.enuc_rate, actual.enuc_rate, "enuc", dimension, slot, block);
    compare_field(expected.mass_fractions, actual.mass_fractions, "X",
                  dimension, slot, block);
}

void run_dimension(int dimension)
{
    SimConfig config{};
    config.grid.dim = dimension;
    config.grid.nblockx1 = 2;
    config.grid.nblockx2 = dimension >= 2 ? 2 : 0;
    config.grid.nblockx3 = dimension == 3 ? 2 : 0;
    config.grid.amr_max_blocks = 64;
    config.amr.lrefinemin = 0;
    config.amr.lrefinemax = 1;

    amr::AMRControl control(64, dimension);
    const LeafGrid leaves = mixed_corner_grid(dimension);
    control.tree->LoadLeafGrid(
        config, 2, leaves.level, leaves.x, leaves.y, leaves.z);
    const auto& active = control.tree->GetActiveBlocks();
    require(active.size() == leaves.level.size(),
            "mixed-corner hierarchy leaf count drifted");

    std::vector<amr::BlockHandle> handles;
    std::vector<arch::backend::StorageGeneration> storage;
    handles.reserve(active.size());
    storage.reserve(active.size());
    arch::backend::StorageGenerationIssuer storage_issuer{1000};
    for (std::size_t index = 0; index < active.size(); ++index) {
        handles.push_back({{1000 + index}, {31}});
        storage.push_back(storage_issuer.issue());
        fill_block(control.pool->GetBlock(active[index]));
    }
    control.BindActiveHandles(handles);
    const auto plan = control.ghost_exchange.BuildCoarseFinePlan(
        control.pool, control.tree, dimension, handles);
    require(!plan.operations.empty(),
            "mixed-corner hierarchy produced no coarse-fine exchange");

    const auto boundary = make_boundary_plan(dimension);
    SpeciesManager species;
    species.add_species("light", 1.0, 1.0, 1.4, 1.0);
    species.add_species("heavy", 4.0, 2.0, 1.4, 1.0);
    IdealGas eos(1.4, species);
    std::vector<arch::cuda::CudaBlockBinding> bindings;
    bindings.reserve(active.size());
    for (std::size_t index = 0; index < active.size(); ++index) {
        bindings.push_back({
            &control.pool->GetBlock(active[index]), handles[index],
            storage[index], &boundary});
    }
    auto backend = arch::cuda::make_cuda_backend(
        bindings, 0, make_launch_config(), species, eos);

    const std::array slots{
        arch::state::StateSlot::Current,
        arch::state::StateSlot::Next,
        arch::state::StateSlot::Scratch};
    for (const auto slot : slots) {
        std::vector<arch::backend::BackendStateAccess> accesses;
        accesses.reserve(active.size());
        for (std::size_t index = 0; index < active.size(); ++index) {
            auto& state = state_for(control.pool->GetBlock(active[index]), slot);
            const arch::backend::BackendStateAccess access{
                handles[index], storage[index], slot};
            accesses.push_back(access);
            backend->enqueue_upload_slot(
                access, arch::state::StateRegion::Interior,
                transfer_view(state));
            backend->enqueue_upload_slot(
                access, arch::state::StateRegion::Ghost,
                transfer_view(state));
        }
        const arch::state::CompletionToken completed{
            1, arch::state::CompletionState::Complete};
        require(backend->execute_coarse_fine_exchange(
                    accesses, plan, slot, {1}, completed) == completed,
                "CUDA AMR exchange completion token drifted");

        const arch::state::SlotRotation expose_slot{
            slot,
            slot == arch::state::StateSlot::Next
                ? arch::state::StateSlot::Current
                : arch::state::StateSlot::Next,
            slot == arch::state::StateSlot::Scratch
                ? arch::state::StateSlot::Current
                : arch::state::StateSlot::Scratch};
        if (slot != arch::state::StateSlot::Current) {
            for (std::size_t index = 0; index < active.size(); ++index) {
                const arch::backend::BackendStateAccess current{
                    handles[index], storage[index],
                    arch::state::StateSlot::Current};
                backend->rotate_slots(current, expose_slot);
            }
        }

        std::vector<FluidState> actual(active.size());
        for (std::size_t index = 0; index < active.size(); ++index) {
            actual[index] = state_for(
                control.pool->GetBlock(active[index]), slot);
            const arch::backend::BackendStateAccess current{
                handles[index], storage[index],
                arch::state::StateSlot::Current};
            backend->enqueue_materialize_host_current(
                current, arch::state::StateRegion::Interior,
                transfer_view(actual[index]));
            backend->enqueue_materialize_host_current(
                current, arch::state::StateRegion::Ghost,
                transfer_view(actual[index]));
        }
        backend->quiesce();
        if (slot != arch::state::StateSlot::Current) {
            for (std::size_t index = 0; index < active.size(); ++index) {
                const arch::backend::BackendStateAccess current{
                    handles[index], storage[index],
                    arch::state::StateSlot::Current};
                backend->rotate_slots(current, expose_slot);
            }
        }

        FluidState amr::Block::* member = &amr::Block::fluid_state;
        if (slot == arch::state::StateSlot::Next)
            member = &amr::Block::state_next;
        else if (slot == arch::state::StateSlot::Scratch)
            member = &amr::Block::state_scratch;
        control.ghost_exchange.ExecuteCoarseFinePlan(
            plan, control.pool, control.tree, dimension, member, handles);
        for (std::size_t index = 0; index < active.size(); ++index) {
            compare_state(
                state_for(control.pool->GetBlock(active[index]), slot),
                actual[index], dimension, slot, index);
        }
    }

    std::cout << "CUDA_AMR_EXCHANGE_PASS dimension=" << dimension
              << " blocks=" << active.size()
              << " operations=" << plan.operations.size() << '\n';
}

} // namespace

int main()
{
    int device_count = 0;
    const cudaError_t probe = cudaGetDeviceCount(&device_count);
    if (probe != cudaSuccess || device_count == 0) {
        std::cout << "SKIP: CUDA runtime device unavailable\n";
        static_cast<void>(cudaGetLastError());
        return 77;
    }
    try {
        run_dimension(1);
        run_dimension(2);
        run_dimension(3);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
