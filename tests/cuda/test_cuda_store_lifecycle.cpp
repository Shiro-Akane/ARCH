#include "amr/Block.h"
#include "amr/BoundaryPlan.h"
#include "cuda/runtime/CudaBackend.h"
#include "physics/eos/IdealGas.h"
#include "physics/species/Species.h"

#include <cuda_runtime_api.h>

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Exception = std::exception, typename Function>
void require_rejected(Function&& function, std::string_view message)
{
    bool rejected = false;
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        rejected = true;
    }
    require(rejected, message);
}

constexpr int kBackendDevice = 0;

void select_device_probe(int device_count)
{
    const int probe_device = device_count > 1 ? 1 : kBackendDevice;
    require(cudaSetDevice(probe_device) == cudaSuccess,
            "could not select CUDA device-switch probe");
}

void require_backend_device_selected(std::string_view operation)
{
    int selected = -1;
    require(cudaGetDevice(&selected) == cudaSuccess,
            "could not query current CUDA device");
    require(selected == kBackendDevice, operation);
}

arch::boundary::BoundaryPlan make_boundary_plan(int dimension = 1)
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

amr::Block make_block(int id, double seed)
{
    amr::Block block{};
    block.id = id;
    block.level = 0;
    block.logical_x1 = id;
    block.active = true;
    block.grid = Grid(
        amr::MAX_NG, static_cast<double>(id),
        static_cast<double>(id + 1), 0.0, 1.0, 0.0, 1.0, 1, 0, 0);
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
        const double value = seed + 0.125 * i + 0.003 * i * i;
        block.fluid_state.set(
            cell, {value, value + 0.25, value + 0.5, value + 0.75,
                   value + 1.0});
        block.fluid_state.enuc_rate[cell] = (i & 1) ? 0.0 : -0.0;
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

bool same_bits(const FluidState& left, const FluidState& right)
{
    const auto same = [](const auto& a, const auto& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t index = 0; index < a.size(); ++index) {
            if (std::bit_cast<std::uint64_t>(a[index])
                != std::bit_cast<std::uint64_t>(b[index]))
                return false;
        }
        return true;
    };
    return same(left.rho, right.rho) && same(left.mom_u, right.mom_u)
        && same(left.mom_v, right.mom_v)
        && same(left.mom_w, right.mom_w) && same(left.eng, right.eng)
        && same(left.enuc_rate, right.enuc_rate)
        && same(left.mass_fractions, right.mass_fractions);
}

arch::backend::BackendStateAccess current(
    amr::BlockHandle handle, arch::backend::StorageGeneration storage)
{
    return {handle, storage, arch::state::StateSlot::Current};
}

arch::cuda::DeviceMigrationAccess staged_access(
    amr::AmrPlanScope scope, amr::BlockHandle handle,
    arch::backend::StorageGeneration storage)
{
    return {
        scope, current(handle, storage),
        arch::cuda::DeviceMigrationRole::StagedNewDestination};
}

void upload_complete_current(
    arch::cuda::CudaBackend& backend,
    arch::cuda::CudaBackend::StoreTransaction& transaction,
    arch::cuda::DeviceMigrationAccess access, FluidState& state,
    int device_count)
{
    select_device_probe(device_count);
    backend.enqueue_upload_staged_current(
        transaction, access, arch::state::StateRegion::Interior,
        transfer_view(state));
    require_backend_device_selected(
        "staged interior upload did not restore the backend device");
    select_device_probe(device_count);
    backend.enqueue_upload_staged_current(
        transaction, access, arch::state::StateRegion::Ghost,
        transfer_view(state));
    require_backend_device_selected(
        "staged ghost upload did not restore the backend device");
}

void run_store_lifecycle(int device_count)
{
    amr::Block old_block = make_block(0, 1.0);
    amr::Block incomplete_block = make_block(1, 11.0);
    amr::Block abandoned_block = make_block(2, 21.0);
    amr::Block published_block = make_block(3, 31.0);
    const auto boundary = make_boundary_plan();
    SpeciesManager species;
    IdealGas eos(1.4, species);
    arch::backend::StorageGenerationIssuer storage_issuer{1001};

    const amr::BlockHandle old_handle{{1001}, {1}};
    const auto old_storage = storage_issuer.issue();
    auto backend = arch::cuda::make_cuda_backend(
        old_block, old_handle, old_storage, kBackendDevice,
        make_launch_config(), species, boundary, eos);
    require_backend_device_selected(
        "CUDA backend construction did not select its device");
    require(!backend->cuda_amr_execution_available(),
            "storage lifecycle advertised dynamic CUDA AMR");

    const auto old_access = current(old_handle, old_storage);
    backend->enqueue_upload_slot(
        old_access, arch::state::StateRegion::Interior,
        transfer_view(old_block.fluid_state));
    backend->enqueue_upload_slot(
        old_access, arch::state::StateRegion::Ghost,
        transfer_view(old_block.fluid_state));
    select_device_probe(device_count);
    backend->quiesce();
    require_backend_device_selected(
        "CUDA quiesce did not restore the backend device");
    const auto initial = backend->store_snapshot();
    require(initial.active_blocks == 1 && initial.staged_blocks == 0
                && initial.retirement_batches == 0
                && initial.immutable_owner_constructions == 1,
            "initial CUDA store snapshot drifted");

    const amr::AmrPlanScope incomplete_scope{1, {1}, {2}};
    const amr::BlockHandle incomplete_handle{{1002}, {2}};
    const auto incomplete_storage = storage_issuer.issue();
    const std::array incomplete_bindings{arch::cuda::CudaBlockBinding{
        &incomplete_block, incomplete_handle, incomplete_storage,
        &boundary}};
    select_device_probe(device_count);
    auto incomplete = backend->begin_store_transaction(
        incomplete_scope, incomplete_bindings);
    require_backend_device_selected(
        "begin_store_transaction did not restore the backend device");
    const auto incomplete_access = staged_access(
        incomplete_scope, incomplete_handle, incomplete_storage);
    select_device_probe(device_count);
    backend->enqueue_upload_staged_current(
        incomplete, incomplete_access, arch::state::StateRegion::Interior,
        transfer_view(incomplete_block.fluid_state));
    require_backend_device_selected(
        "failed-publication upload did not restore the backend device");
    require_rejected<std::logic_error>(
        [&] {
            backend->publish_store_transaction(
                std::move(incomplete), {1});
        },
        "incomplete staged Current was published");
    select_device_probe(device_count);
    backend->abort_store_transaction(std::move(incomplete));
    require_backend_device_selected(
        "explicit store abort did not restore the backend device");
    require(backend->contains(old_access)
                && !backend->contains(current(
                    incomplete_handle, incomplete_storage)),
            "incomplete publication changed active visibility");

    const amr::AmrPlanScope abandoned_scope{2, {1}, {2}};
    const amr::BlockHandle abandoned_handle{{1003}, {2}};
    const auto abandoned_storage = storage_issuer.issue();
    {
        const std::array abandoned_bindings{arch::cuda::CudaBlockBinding{
            &abandoned_block, abandoned_handle, abandoned_storage,
            &boundary}};
        select_device_probe(device_count);
        auto abandoned = backend->begin_store_transaction(
            abandoned_scope, abandoned_bindings);
        require_backend_device_selected(
            "RAII transaction begin did not restore the backend device");
        upload_complete_current(
            *backend, abandoned,
            staged_access(
                abandoned_scope, abandoned_handle, abandoned_storage),
            abandoned_block.fluid_state, device_count);
        select_device_probe(device_count);
    }
    require_backend_device_selected(
        "RAII store abort did not restore the backend device");
    require(backend->contains(old_access)
                && backend->store_snapshot().staged_blocks == 0,
            "RAII abort changed active CUDA visibility");

    const amr::AmrPlanScope publish_scope{3, {1}, {2}};
    const amr::BlockHandle published_handle{{1004}, {2}};
    const auto published_storage = storage_issuer.issue();
    const std::array publish_bindings{arch::cuda::CudaBlockBinding{
        &published_block, published_handle, published_storage, &boundary}};
    select_device_probe(device_count);
    auto published = backend->begin_store_transaction(
        publish_scope, publish_bindings);
    require_backend_device_selected(
        "publication transaction begin did not restore the backend device");
    require(published.scope() == publish_scope
                && published.entries().size() == 1,
            "staged CUDA namespace lost the shared AMR scope");
    const auto published_access = staged_access(
        publish_scope, published_handle, published_storage);
    require(backend->contains_migration(
                published,
                {publish_scope, old_access,
                 arch::cuda::DeviceMigrationRole::ActiveOldSource})
                && backend->contains_migration(published, published_access),
            "staged CUDA namespace did not bind both epochs");
    auto wrong_scope = publish_scope;
    wrong_scope.transaction_id = 4;
    require(!backend->contains_migration(
                published,
                staged_access(
                    wrong_scope, published_handle, published_storage)),
            "wrong AMR scope reached staged CUDA storage");

    upload_complete_current(
        *backend, published, published_access,
        published_block.fluid_state, device_count);
    const arch::cuda::DeviceRetirementFence fence{7};
    select_device_probe(device_count);
    backend->publish_store_transaction(std::move(published), fence);
    require_backend_device_selected(
        "store publication did not restore the backend device");
    require(backend->contains(
                current(published_handle, published_storage))
                && !backend->contains(old_access)
                && backend->store_snapshot().retirement_batches == 1,
            "CUDA store publication did not switch active visibility");
    require_rejected(
        [&] {
            backend->enqueue_materialize_host_current(
                old_access, arch::state::StateRegion::Interior,
                transfer_view(old_block.fluid_state));
        },
        "retired CUDA storage remained scientifically addressable");

    FluidState downloaded;
    downloaded.Preallocate(published_block.grid.GetTotalSize());
    downloaded.InitSpecies(0);
    const auto active_access = current(published_handle, published_storage);
    backend->enqueue_materialize_host_current(
        active_access, arch::state::StateRegion::Interior,
        transfer_view(downloaded));
    backend->enqueue_materialize_host_current(
        active_access, arch::state::StateRegion::Ghost,
        transfer_view(downloaded));
    select_device_probe(device_count);
    backend->quiesce();
    require_backend_device_selected(
        "post-publication quiesce did not restore the backend device");
    require(same_bits(downloaded, published_block.fluid_state),
            "published staged Current did not survive upload/download");
    select_device_probe(device_count);
    require(backend->retirement_ready(fence),
            "quiescent CUDA retirement event remained pending");
    require_backend_device_selected(
        "retirement query did not restore the backend device");
    require_rejected(
        [&] { backend->complete_store_retirement({8}); },
        "unknown CUDA retirement fence was accepted");
    select_device_probe(device_count);
    backend->complete_store_retirement(fence);
    require_backend_device_selected(
        "retirement completion did not restore the backend device");
    const auto completed = backend->store_snapshot();
    require(completed.active_blocks == 1 && completed.staged_blocks == 0
                && completed.retirement_batches == 0
                && completed.immutable_owner_constructions == 1
                && completed.bytes_h2d > initial.bytes_h2d,
            "completed CUDA store lifecycle retained transient ownership");

    // Force a failure after metric uploads have been enqueued.  Constructor
    // cleanup must quiesce before partial device owners are destroyed, abandon
    // the logical candidate, and leave the backend reusable.
    amr::Block failed_block = make_block(4, 41.0);
    const auto mismatched_boundary = make_boundary_plan(2);
    const amr::AmrPlanScope failed_scope{4, {2}, {3}};
    const amr::BlockHandle failed_handle{{1005}, {3}};
    const auto failed_storage = storage_issuer.issue();
    const std::array failed_bindings{arch::cuda::CudaBlockBinding{
        &failed_block, failed_handle, failed_storage,
        &mismatched_boundary}};
    select_device_probe(device_count);
    require_rejected<std::invalid_argument>(
        [&] {
            (void)backend->begin_store_transaction(
                failed_scope, failed_bindings);
        },
        "mismatched staged boundary unexpectedly constructed");
    require_backend_device_selected(
        "failed staged construction did not restore the backend device");
    require(backend->store_snapshot().staged_blocks == 0
                && backend->contains(active_access),
            "failed staged construction stranded CUDA ownership");

    amr::Block recovered_block = make_block(5, 51.0);
    const amr::AmrPlanScope recovered_scope{5, {2}, {3}};
    const amr::BlockHandle recovered_handle{{1006}, {3}};
    const auto recovered_storage = storage_issuer.issue();
    const std::array recovered_bindings{arch::cuda::CudaBlockBinding{
        &recovered_block, recovered_handle, recovered_storage, &boundary}};
    select_device_probe(device_count);
    auto recovered = backend->begin_store_transaction(
        recovered_scope, recovered_bindings);
    require_backend_device_selected(
        "post-failure transaction did not restore the backend device");
    select_device_probe(device_count);
    backend->abort_store_transaction(std::move(recovered));
    require_backend_device_selected(
        "post-failure abort did not restore the backend device");

    // The transaction keeps the implementation alive long enough to perform
    // its noexcept RAII abort even if the public backend owner disappears.
    {
        auto lifetime_backend = arch::cuda::make_cuda_backend(
            old_block, old_handle,
            arch::backend::StorageGeneration{2001}, kBackendDevice,
            make_launch_config(), species, boundary, eos);
        const std::array lifetime_bindings{arch::cuda::CudaBlockBinding{
            &incomplete_block, incomplete_handle,
            arch::backend::StorageGeneration{2002}, &boundary}};
        auto lifetime_transaction =
            lifetime_backend->begin_store_transaction(
                amr::AmrPlanScope{21, {1}, {2}}, lifetime_bindings);
        lifetime_backend.reset();
        require(lifetime_transaction.entries().size() == 1,
                "live transaction lost its backend lifetime owner");
        select_device_probe(device_count);
    }
    require_backend_device_selected(
        "lifetime-owning RAII abort did not restore the backend device");
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
        run_store_lifecycle(device_count);
        std::cout << "CUDA store lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
