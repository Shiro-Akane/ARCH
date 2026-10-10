/**
 * @file test_cuda_store_lifecycle.cpp
 * @brief Check actual CUDA allocation, transfer and retirement behavior.
 *
 * The test selects a device through the backend interface and verifies
 * complete state upload, storage reuse and resource-lifetime constraints.
 */
#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cuda_runtime_api.h>

#include "amr/AMRControl.h"
#include "amr/exchange/BoundaryPlan.h"
#include "amr/exchange/ExchangePlan.h"
#include "amr/exchange/HostBoundaryPlan.h"
#include "amr/storage/Block.h"
#include "cuda/runtime/CudaBackend.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/runtime/RuntimeStateTransaction.h"
#include "driver/schedule/DriverControl.h"
#include "physics/eos/IdealGas.h"
#include "physics/boundary/NativeRzBoundary.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/species/Species.h"

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
        state.mass_fractions.empty()?nullptr:state.mass_fractions.data(),
        state.rho.size(),static_cast<std::size_t>(state.GetNumSpecies()),
        state.GetNumSpecies()>0?state.rho.size():0};
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

// Host planes deliberately have end sentinels; species planes also have a
// stride larger than the device plane. Region checks use individual cell
// membership, independent of the implementation's rectangles or cuboids.
struct RegionBuffer {
    static constexpr double sentinel = -9876.5;
    std::array<std::vector<double>, 6> fields;
    std::vector<double> composition;
    std::size_t cells, species_stride;

    explicit RegionBuffer(std::size_t count)
        : composition(2 * (count + 7), sentinel),
          cells(count), species_stride(count + 7)
    {
        for (auto& field : fields) field.assign(count + 7, sentinel);
    }

    void fill(double seed, const Grid& grid)
    {
        // PAD_NX includes allocation-only row tails. Their sentinels must stay
        // untouched: only actual grid cells participate in either region.
        for (int k = 0; k < grid.GetTotalZ(); ++k)
            for (int j = 0; j < grid.GetTotalY(); ++j)
                for (int i = 0; i < grid.GetTotalX(); ++i) {
            const std::size_t cell = grid.GetIndex(i, j, k);
            for (std::size_t field = 0; field < fields.size(); ++field)
                fields[field][cell] = seed + 8.0 * field + 0.000125 * cell;
            fields.back()[cell] = ((cell + static_cast<int>(seed)) & 1)
                ? -0.0 : 0.0;
            composition[cell] = 0.25 + 0.01 * seed + 0.000001 * cell;
            composition[species_stride + cell] = 1.0 - composition[cell];
        }
    }

    arch::backend::HostStateTransferView view()
    {
        return {fields[0].data(), fields[1].data(), fields[2].data(),
                fields[3].data(), fields[4].data(), fields[5].data(),
                composition.data(), cells, 2, species_stride};
    }
};

// Check all transferred values, every untouched cell, signed zeros, and host
// padding. An erroneous field base/origin or species stride cannot hide behind
// a numeric tolerance or a matching implementation-side decomposition.
void require_region_buffer(
    const RegionBuffer& actual, const RegionBuffer& inside,
    const RegionBuffer& outside, const std::vector<unsigned char>& selected)
{
    const auto bits_equal = [](double a, double b) {
        return std::bit_cast<std::uint64_t>(a)
            == std::bit_cast<std::uint64_t>(b);
    };
    for (std::size_t cell = 0; cell < actual.cells; ++cell) {
        const auto& source = selected[cell] ? inside : outside;
        for (std::size_t field = 0; field < actual.fields.size(); ++field)
            require(bits_equal(actual.fields[field][cell], source.fields[field][cell]),
                    "region transfer crossed ownership or changed field bits at cell "
                        + std::to_string(cell) + ", field " + std::to_string(field));
        for (std::size_t species = 0; species < 2; ++species)
            require(bits_equal(actual.composition[species * actual.species_stride + cell],
                               source.composition[species * source.species_stride + cell]),
                    "region transfer changed species-plane ownership or bits");
    }
    for (std::size_t cell = actual.cells; cell < actual.species_stride; ++cell) {
        for (const auto& field : actual.fields)
            require(bits_equal(field[cell], RegionBuffer::sentinel),
                    "region transfer wrote beyond a host field plane");
        for (std::size_t species = 0; species < 2; ++species)
            require(bits_equal(actual.composition[species * actual.species_stride + cell],
                               RegionBuffer::sentinel),
                    "region transfer wrote host species stride padding");
    }
}

void run_region_transfers(int device_count)
{
    using arch::state::StateRegion;
    for (int dimension = 1; dimension <= 3; ++dimension) {
        amr::Block block{};
        block.id = 0;
        block.active = true;
        block.grid = Grid(amr::MAX_NG, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0,
                          1, dimension >= 2 ? 1 : 0, dimension == 3 ? 1 : 0);
        block.grid.dim = dimension;
        block.grid.InitializeTopology();
        const std::size_t cells = block.grid.GetTotalSize();
        for (FluidState* state :
             {&block.fluid_state, &block.state_next, &block.state_scratch}) {
            state->Preallocate(cells);
            state->InitSpecies(2);
        }
        SpeciesManager species;
        species.add_species("H1", 1.0, 1.0, 1.4, 1.0);
        species.add_species("He4", 4.0, 2.0, 1.4, 1.0);
        IdealGas eos(1.4, species);
        const auto boundary = make_boundary_plan(dimension);
        const amr::BlockHandle handle{{3001}, {1}};
        const arch::backend::StorageGeneration storage{3001};
        auto backend = arch::cuda::make_cuda_backend(
            block, handle, storage, kBackendDevice,
            make_launch_config(), species, boundary, eos);
        const auto access = current(handle, storage);
        RegionBuffer baseline(cells), replacement(cells), untouched(cells);
        baseline.fill(1.0, block.grid);
        replacement.fill(2.0, block.grid);
        for (StateRegion region : {StateRegion::Interior, StateRegion::Ghost}) {
            backend->enqueue_upload_slot(access, StateRegion::Interior, baseline.view());
            backend->enqueue_upload_slot(access, StateRegion::Ghost, baseline.view());
            backend->quiesce();
            std::vector<unsigned char> selected(cells, 0);
            std::size_t selected_cells = 0;
            for (int k = 0; k < block.grid.GetTotalZ(); ++k)
                for (int j = 0; j < block.grid.GetTotalY(); ++j)
                    for (int i = 0; i < block.grid.GetTotalX(); ++i) {
                        const bool interior = i >= block.grid.Is() && i < block.grid.Ie()
                            && j >= block.grid.Js() && j < block.grid.Je()
                            && k >= block.grid.Ks() && k < block.grid.Ke();
                        const bool owned = (region == StateRegion::Interior) == interior;
                        selected[block.grid.GetIndex(i, j, k)] = owned;
                        selected_cells += owned;
                    }
            const std::uint64_t expected_bytes = selected_cells * 8 * sizeof(double);
            const auto before_upload = backend->counters();
            select_device_probe(device_count);
            backend->enqueue_upload_slot(access, region, replacement.view());
            backend->quiesce();
            require_backend_device_selected("region upload lost the backend device");
            require(backend->counters().bytes_h2d - before_upload.bytes_h2d == expected_bytes,
                    "region upload byte accounting includes another region or omits species");
            RegionBuffer whole(cells);
            backend->enqueue_materialize_host_current(access, StateRegion::Interior, whole.view());
            backend->enqueue_materialize_host_current(access, StateRegion::Ghost, whole.view());
            backend->quiesce();
            require_region_buffer(whole, replacement, baseline, selected);
            RegionBuffer partial(cells);
            const auto before_download = backend->counters();
            select_device_probe(device_count);
            backend->enqueue_materialize_host_current(access, region, partial.view());
            backend->quiesce();
            require_backend_device_selected("region download lost the backend device");
            require(backend->counters().bytes_d2h - before_download.bytes_d2h == expected_bytes,
                    "region download byte accounting includes another region or omits species");
            require_region_buffer(partial, replacement, untouched, selected);
        }
    }
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
    require(backend->cuda_amr_execution_available(),
            "CUDA backend did not advertise its dynamic AMR contract");

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

/** Real resident Native EOS inspection uses no Host field download or write.
 * Independent constant-density V/W/I means cover two blocks, a late stale
 * access rejected before launch, and one actual nonfinite ghost failure.
 */
void run_native_completed_eos()
{
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    Grid root(amr::MAX_NG,1.,3.,-.5,.5,0.,1.,2,1,1);
    root.geometry="cylindrical";root.dim=2;
    std::array<amr::Block,2> blocks;
    const double poison=std::numeric_limits<double>::quiet_NaN();
    for(int b=0;b<2;++b) {
        auto& block=blocks[b];block.Reset();block.level=0;block.active=true;
        block.logical_x1=b;block.logical_x2=0;
        block.InitGeometry(root,1./amr::BLOCK_NX,1./amr::BLOCK_NY,1.,native);
        block.RequireNativeGeometryIdentity();const auto& grid=block.grid;
        for(auto* field:{&block.fluid_state,&block.state_next,&block.state_scratch}) {
            field->Preallocate(grid.GetTotalSize());field->InitSpecies(0);
            for(auto* plane:{&field->rho,&field->mom_u,&field->mom_v,&field->mom_w,
                &field->eng,&field->enuc_rate})std::fill(plane->begin(),plane->end(),poison);
        }
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int index=grid.GetIndex(i,j,0);
            const long double a=grid.GetFacePosL(i),z=grid.GetFacePosR(i);
            const long double V=(z*z-a*a)/2.L,W=(z*z*z-a*a*a)/3.L;
            const long double I=(z*z*z*z-a*a*a*a)/4.L;
            block.fluid_state.set(index,{1.,0.,0.,double(I/W),double(.125L+I/(2.L*V))});
            block.fluid_state.enuc_rate[index]=-0.;
        }
    }
    SpeciesManager species;IdealGas eos(1.4,species);
    const auto plan=make_boundary_plan(2);auto launch=make_launch_config();
    launch.density_floor=1e-12;launch.minimum_internal_energy=1e-12;
    launch.maximum_internal_energy=1e20;
    const arch::state::Bounds bounds{launch.density_floor,launch.minimum_internal_energy,
        launch.maximum_internal_energy};
    const std::array handles{amr::BlockHandle{{2101},{21}},amr::BlockHandle{{2102},{21}}};
    const std::array storage{arch::backend::StorageGeneration{2101},arch::backend::StorageGeneration{2102}};
    const std::array bindings{arch::cuda::CudaBlockBinding{&blocks[0],handles[0],storage[0],&plan},
        arch::cuda::CudaBlockBinding{&blocks[1],handles[1],storage[1],&plan}};
    auto backend=arch::cuda::make_cuda_backend(bindings,kBackendDevice,launch,species,eos);
    const std::array accesses{current(handles[0],storage[0]),current(handles[1],storage[1])};
    for(int b=0;b<2;++b)for(auto region:{arch::state::StateRegion::Interior,arch::state::StateRegion::Ghost})
        backend->enqueue_upload_slot(accesses[b],region,transfer_view(blocks[b].fluid_state));
    backend->quiesce();const auto before=backend->counters();
    require(!backend->validate_completed_native_eos_batch(accesses,bounds),
        "resident Native EOS rejected independent physical V/W means");
    const auto after=backend->counters();
    require(after.bytes_h2d==before.bytes_h2d && after.kernel_count-before.kernel_count==4
        && after.bytes_d2h-before.bytes_d2h==2*(sizeof(int)+sizeof(RzThermodynamics::AcceptanceDiagnostic)),
        "resident Native EOS downloaded state or changed batch launch ownership");
    auto stale=accesses;stale[1].storage.value+=99;
    require_rejected<std::invalid_argument>([&]{backend->validate_completed_native_eos_batch(stale,bounds);},
        "late stale Native patch was accepted");
    require(backend->counters().kernel_count==after.kernel_count,
        "Native late preflight rejection partly launched the batch");
    auto duplicate=accesses;duplicate[1]=duplicate[0];
    require_rejected<std::invalid_argument>([&]{backend->validate_completed_native_eos_batch(duplicate,bounds);},
        "duplicate resident Native patch was accepted");
    auto mixed=accesses;mixed[1].slot=arch::state::StateSlot::Next;
    require_rejected<std::invalid_argument>([&]{backend->validate_completed_native_eos_batch(mixed,bounds);},
        "mixed Native inspection slots were accepted");
    require(backend->counters().kernel_count==after.kernel_count,
        "Native duplicate/slot preflight rejection launched work");
    const int bad=blocks[1].grid.GetIndex(0,blocks[1].grid.Js(),0);
    const double saved=blocks[1].fluid_state.eng[bad];blocks[1].fluid_state.eng[bad]=poison;
    backend->enqueue_upload_slot(accesses[1],arch::state::StateRegion::Ghost,transfer_view(blocks[1].fluid_state));
    backend->quiesce();const auto failure=backend->validate_completed_native_eos_batch(accesses,bounds);
    require(failure && failure->access.block==handles[1] && failure->diagnostic.index==bad
        && failure->diagnostic.phase==RzThermodynamics::AcceptancePhase::provisional
        && failure->diagnostic.status==arch::state::Status::nonfinite,
        "resident Native EOS lost actual failed block/ghost/phase");
    blocks[1].fluid_state.eng[bad]=saved;
    backend->enqueue_upload_slot(accesses[1],arch::state::StateRegion::Ghost,transfer_view(blocks[1].fluid_state));
    backend->quiesce();require(!backend->validate_completed_native_eos_batch(accesses,bounds),
        "new resident Native inspection retained a prior launch failure");
    // Error-only classification borrows this same selected EOS and resident
    // owner. Cauchy-Schwarz with measure r dr gives W^2 < V*I, so the
    // ordinary J/W kinetic estimate exceeds the true V/I rotational energy.
    // Move half that independent positive gap below the true kinetic energy:
    // total energy remains positive, but the actual Native thermal part is negative.
    auto& target_block=blocks[0];const auto& target_grid=target_block.grid;
    const int ti=target_grid.Is()+3,tj=target_grid.Js()+3;
    const int target=target_grid.GetIndex(ti,tj,0);
    const double target_energy=target_block.fluid_state.eng[target];
    const long double a=target_grid.GetFacePosL(ti),z=target_grid.GetFacePosR(ti);
    const long double V=(z*z-a*a)/2.L,I=(z*z*z*z-a*a*a*a)/4.L;
    const long double ordinary=.5L*target_block.fluid_state.mom_w[target]
        *target_block.fluid_state.mom_w[target],mapped=I/(2.L*V);
    require(ordinary>mapped,"classifier fixture lost its independent angular kinetic gap");
    target_block.fluid_state.eng[target]=double(mapped-(ordinary-mapped)/2.L);
    backend->enqueue_upload_slot(accesses[0],arch::state::StateRegion::Interior,
        transfer_view(target_block.fluid_state));backend->quiesce();
    const auto thermal=backend->validate_completed_native_eos_batch(accesses,bounds);
    require(thermal&&thermal->access.block==handles[0]&&thermal->diagnostic.index==target
        &&RzThermodynamics::is_retryable_thermal_failure(thermal->diagnostic),
        "classifier did not retain a real resident active thermal refusal");
    const auto inspected=backend->classify_completed_native_active_thermal(accesses,bounds,*thermal);
    require(inspected.requested_failure&&!inspected.nonthermal_failure,
        "classifier did not find its original actual active thermal target");
    const auto preflight=[&](const auto& actual,const auto& original) {
        const auto before_refusal=backend->counters();
        require_rejected<std::invalid_argument>([&]{
            backend->classify_completed_native_active_thermal(actual,bounds,original);
        },"classifier accepted a stale or wrong original target");
        const auto after_refusal=backend->counters();
        require(after_refusal.kernel_count==before_refusal.kernel_count
            &&after_refusal.bytes_h2d==before_refusal.bytes_h2d
            &&after_refusal.bytes_d2h==before_refusal.bytes_d2h
            &&after_refusal.stream_sync_count==before_refusal.stream_sync_count,
            "classifier preflight refusal submitted resident work");
    };
    preflight(stale,*thermal);
    auto wrong=*thermal;++wrong.access.storage.value;preflight(accesses,wrong);
    wrong=*thermal;++wrong.diagnostic.index;preflight(accesses,wrong);
    wrong=*thermal;wrong.diagnostic.i=target_grid.Is()-1;
    wrong.diagnostic.index=target_grid.GetIndex(wrong.diagnostic.i,tj,0);preflight(accesses,wrong);
    const auto& fatal_grid=blocks[1].grid;
    const auto fatal_density=blocks[1].fluid_state.rho;
    for(int j=fatal_grid.Js();j<fatal_grid.Je();++j)for(int i=fatal_grid.Is();i<fatal_grid.Ie();++i)
        blocks[1].fluid_state.rho[fatal_grid.GetIndex(i,j,0)]=0.;
    backend->enqueue_upload_slot(accesses[1],arch::state::StateRegion::Interior,
        transfer_view(blocks[1].fluid_state));backend->quiesce();
    const auto fatal=backend->classify_completed_native_active_thermal(accesses,bounds,*thermal);
    require(fatal.nonthermal_failure&&fatal.nonthermal_failure->access.block==handles[1]
        &&fatal.nonthermal_failure->diagnostic.i>=fatal_grid.Is()&&fatal.nonthermal_failure->diagnostic.i<fatal_grid.Ie()
        &&fatal.nonthermal_failure->diagnostic.j>=fatal_grid.Js()&&fatal.nonthermal_failure->diagnostic.j<fatal_grid.Je()
        &&fatal.nonthermal_failure->diagnostic.status==arch::state::Status::nonpositive_density,
        "earlier thermal target concealed a later patch's actual fatal density");
    blocks[1].fluid_state.rho=fatal_density;
    target_block.fluid_state.eng[target]=target_energy;
    for(int b=0;b<2;++b)backend->enqueue_upload_slot(accesses[b],
        arch::state::StateRegion::Interior,transfer_view(blocks[b].fluid_state));
    blocks[1].fluid_state.eng[bad]=poison;
    backend->enqueue_upload_slot(accesses[1],arch::state::StateRegion::Ghost,
        transfer_view(blocks[1].fluid_state));backend->quiesce();
    const auto ghost_only=backend->classify_completed_native_active_thermal(accesses,bounds,*thermal);
    require(!ghost_only.requested_failure&&!ghost_only.nonthermal_failure,
        "a ghost-only failure replaced the restored active thermal target");
    preflight(accesses,*failure); // The original real ghost refusal is not an active thermal authority.
    blocks[1].fluid_state.eng[bad]=saved;
    backend->enqueue_upload_slot(accesses[1],arch::state::StateRegion::Ghost,
        transfer_view(blocks[1].fluid_state));backend->quiesce();
    require(!backend->validate_completed_native_eos_batch(accesses,bounds),
        "classifier edge checks did not restore the genuine accepted state");
    for(int b=0;b<2;++b) {
        auto observed=blocks[b].fluid_state;
        for(auto region:{arch::state::StateRegion::Interior,arch::state::StateRegion::Ghost})
            backend->enqueue_materialize_host_current(accesses[b],region,transfer_view(observed));
        backend->quiesce();require(same_bits(observed,blocks[b].fluid_state),
            "resident Native EOS wrote physical fields or Host padding");
    }
}


/** Exercise the actual store owner, surface buffers and immutable prefix.
 * The Host wrapper supplies the already-qualified common numerical law; this
 * check concerns real slots, cell-major Xi, transport and unpublished ownership.
 */
void run_native_reflecting_layers(int device_count)
{
    namespace math = arch::boundary::native_rz_math;
    using arch::boundary::BoundaryAxis;
    using arch::boundary::BoundarySide;
    using arch::state::StateRegion;
    constexpr auto native = GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto state_view = [](FluidState& state) {
        return arch::backend::HostStateTransferView{
            state.rho.data(), state.mom_u.data(), state.mom_v.data(),
            state.mom_w.data(), state.eng.data(), state.enuc_rate.data(),
            state.mass_fractions.empty() ? nullptr : state.mass_fractions.data(),
            state.rho.size(), static_cast<std::size_t>(state.GetNumSpecies()),
            state.GetNumSpecies() > 0 ? state.rho.size() : 0};
    };
    const auto relative = [](double actual, double expected) {
        const double scale = std::max(std::abs(actual), std::abs(expected));
        require(std::isfinite(actual) && std::isfinite(expected)
                    && std::abs(actual - expected)
                        <= 64. * std::numeric_limits<double>::epsilon() * scale,
                "resident native reflector differs from the Host point law");
    };
    for (int species_count : {0, 2}) {
        Grid root(amr::MAX_NG, 1., 2., -.5, .5, 0., 1., 1, 1, 1);
        root.geometry = "cylindrical";
        root.dim = 2;
        amr::Block block{};
        block.Reset();
        block.active = true;
        block.InitGeometry(root, 1. / amr::BLOCK_NX, 1. / amr::BLOCK_NY, 1., native);
        block.RequireNativeGeometryIdentity();
        const auto& grid = block.grid;
        const double poison = std::numeric_limits<double>::quiet_NaN();
        for (auto* state : {&block.fluid_state, &block.state_next, &block.state_scratch}) {
            state->Preallocate(grid.GetTotalSize());
            state->InitSpecies(species_count);
            for (auto* plane : {&state->rho, &state->mom_u, &state->mom_v,
                               &state->mom_w, &state->eng, &state->enuc_rate,
                               &state->mass_fractions})
                std::fill(plane->begin(), plane->end(), poison);
        }
        for (int j = 0; j < grid.GetTotalY(); ++j)
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i, j, 0);
                const long double a = grid.GetFacePosL(i), z = grid.GetFacePosR(i);
                const long double V = (z*z-a*a)/2.L, W = (z*z*z-a*a*a)/3.L;
                const long double I = (z*z*z*z-a*a*a*a)/4.L;
                constexpr long double omega = .125L, radial = .0625L, axial = -.03125L;
                block.fluid_state.set(cell, {1., double(radial), double(axial),
                    double(omega*I/W), double(.125L + (radial*radial+axial*axial)/2.L
                        + omega*omega*I/(2.L*V))});
                block.fluid_state.enuc_rate[cell] = 1000. + cell;
                if (species_count) {
                    block.fluid_state.X(0, cell) = .2 + .001*i + .0001*j;
                    block.fluid_state.X(1, cell) = 1. - block.fluid_state.X(0, cell);
                }
                // A later corner must use the completed x1 prefix for every
                // required support ghost, rather than these resident fields.
                if (i < grid.Is()) {
                    block.fluid_state.eng[cell] = poison;
                    block.fluid_state.enuc_rate[cell] = -2000. - cell;
                }
            }
        SpeciesManager species;
        if (species_count) {
            species.add_species("H1", 1., 1., 1.4, 1.);
            species.add_species("He4", 4., 2., 1.4, 1.);
        }
        IdealGas eos(1.4, species);
        const auto boundary = make_boundary_plan(2);
        auto launch = make_launch_config();
        launch.density_floor = 1e-12;
        launch.minimum_internal_energy = 1e-12;
        launch.maximum_internal_energy = 1e20;
        const arch::state::Bounds bounds{launch.density_floor,
            launch.minimum_internal_energy, launch.maximum_internal_energy};
        SimConfig reference_config{};
        reference_config.numerics.sml_rho = bounds.density;
        reference_config.numerics.min_eint = bounds.internal_min;
        reference_config.numerics.max_eint = bounds.internal_max;
        const amr::BlockHandle handle{{2201 + static_cast<std::uint64_t>(species_count)}, {22}};
        const arch::backend::StorageGeneration storage{2201 + static_cast<std::uint64_t>(species_count)};
        auto backend = arch::cuda::make_cuda_backend(block, handle, storage,
            kBackendDevice, launch, species, boundary, eos);
        const auto access = current(handle, storage);
        for (auto region : {StateRegion::Interior, StateRegion::Ghost})
            backend->enqueue_upload_slot(access, region, state_view(block.fluid_state));
        backend->quiesce();
        const auto original = block.fluid_state;
        const auto require_unchanged = [&] {
            auto observed = original;
            for (auto region : {StateRegion::Interior, StateRegion::Ghost})
                backend->enqueue_materialize_host_current(access, region, state_view(observed));
            backend->quiesce();
            require(same_bits(observed, block.fluid_state),
                    "resident native reflector wrote fields, Xi or padding");
        };
        const auto require_candidates = [&](const std::vector<math::Request>& requests,
            const arch::backend::BoundaryCells& actual, const FluidState& donor) {
            require(actual.species_count == static_cast<std::size_t>(species_count)
                && actual.conserved.size() == requests.size()
                && actual.enuc.size() == requests.size()
                && actual.composition.size() == requests.size()*species_count,
                "resident native reflector returned an incomplete surface layout");
            for (std::size_t row = 0; row < requests.size(); ++row) {
                const auto& request = requests[row];
                arch::boundary::NativeRzBoundaryRequest host{};
                host.source = request.source;
                host.destination = request.destination;
                host.coordinates.dimension = 2;
                host.coordinates.axis = request.axis;
                host.coordinates.side = request.side;
                host.coordinates.ghost_depth = request.ghost_depth;
                host.coordinates.purpose = request.purpose;
                const auto expected = arch::boundary::EvaluateNativeRzReflectingCell(
                    grid, host, reference_config, species, eos,
                    [&](int cell) { return donor.get(cell); },
                    [&](int s, int cell) { return donor.X(s, cell); });
                const auto& a = actual.conserved[row];
                const auto& b = expected.conserved;
                for (const auto pair : {std::pair{a.rho,b.rho}, std::pair{a.mom_u,b.mom_u},
                    std::pair{a.mom_v,b.mom_v}, std::pair{a.mom_w,b.mom_w}, std::pair{a.eng,b.eng}})
                    relative(pair.first, pair.second);
                for (int s = 0; s < species_count; ++s)
                    relative(actual.composition[row*species_count+s], expected.mass_fractions[s]);
                const int source = grid.GetIndex(request.source[0], request.source[1], 0);
                require(std::bit_cast<std::uint64_t>(actual.enuc[row])
                        == std::bit_cast<std::uint64_t>(donor.enuc_rate[source]),
                        "resident native reflector lost source/prefix ENUC bits");
            }
        };
        std::vector<math::Request> radial;
        for (int j : {grid.Js()+1, grid.Js()})
            for (int depth = grid.ng; depth >= 1; --depth)
                radial.push_back({{grid.Is()+depth-1,j}, {grid.Is()-depth,j},
                    BoundaryAxis::X1, BoundarySide::Lower, depth});
        select_device_probe(device_count);
        const auto before = backend->counters();
        const auto first = backend->prepare_native_reflecting_layer(access, radial, bounds);
        require_backend_device_selected("native reflecting preparation lost backend device");
        const auto after = backend->counters();
        require(after.kernel_count-before.kernel_count == 1
            && after.bytes_h2d-before.bytes_h2d == radial.size()*sizeof(math::Request)
            && after.bytes_d2h-before.bytes_d2h
                == sizeof(int)+radial.size()*(sizeof(FluidVector)+(species_count+1)*sizeof(double)),
            "native reflecting layer downloaded full state or miscounted surface outputs");
        require_candidates(radial, first, original);
        std::vector<std::size_t> order(radial.size());
        for (std::size_t row = 0; row < order.size(); ++row) order[row] = row;
        std::sort(order.begin(), order.end(), [&](auto a, auto b) {
            return grid.GetIndex(radial[a].destination[0], radial[a].destination[1], 0)
                < grid.GetIndex(radial[b].destination[0], radial[b].destination[1], 0);
        });
        std::vector<int> indices;
        arch::backend::BoundaryCells prefix;
        prefix.species_count = species_count;
        auto completed = original;
        for (auto row : order) {
            const int cell = grid.GetIndex(radial[row].destination[0], radial[row].destination[1], 0);
            indices.push_back(cell);
            prefix.conserved.push_back(first.conserved[row]);
            prefix.enuc.push_back(first.enuc[row]);
            completed.set(cell, first.conserved[row]);
            completed.enuc_rate[cell] = first.enuc[row];
            for (int s = 0; s < species_count; ++s) {
                const double x = first.composition[row*species_count+s];
                prefix.composition.push_back(x);
                completed.X(s, cell) = x;
            }
        }
        const std::vector<math::Request> corners{
            {{0,grid.Js()}, {0,grid.Js()-1}, BoundaryAxis::X2, BoundarySide::Lower, 1},
            {{1,grid.Js()+1}, {1,grid.Js()-2}, BoundaryAxis::X2, BoundarySide::Lower, 2}};
        const auto before_corner = backend->counters();
        const auto second = backend->prepare_native_reflecting_layer(access, corners, bounds, indices, &prefix);
        const auto after_corner = backend->counters();
        require(after_corner.kernel_count-before_corner.kernel_count == 1
            && after_corner.bytes_h2d-before_corner.bytes_h2d == corners.size()*sizeof(math::Request)
                + indices.size()*(sizeof(int)+sizeof(FluidVector)+(species_count+1)*sizeof(double))
            && after_corner.bytes_d2h-before_corner.bytes_d2h
                == sizeof(int)+corners.size()*(sizeof(FluidVector)+(species_count+1)*sizeof(double)),
            "native corner prefix changed surface-only download accounting");
        require_candidates(corners, second, completed);
        require_unchanged();
        const auto reject = [&](auto&& function, std::string_view message) {
            const auto start = backend->counters();
            require_rejected(std::forward<decltype(function)>(function), message);
            const auto end = backend->counters();
            require(end.kernel_count == start.kernel_count && end.bytes_h2d == start.bytes_h2d
                && end.bytes_d2h == start.bytes_d2h,
                "native reflector preflight rejection performed device work");
        };
        auto stale = access;
        stale.storage.value += 1;
        reject([&] { backend->prepare_native_reflecting_layer(stale, radial, bounds); },
               "native reflector accepted stale storage");
        auto invalid_slot = access;
        invalid_slot.slot = static_cast<arch::state::StateSlot>(255);
        reject([&] { backend->prepare_native_reflecting_layer(invalid_slot, radial, bounds); },
               "native reflector accepted an invalid slot");
        auto changed_bounds = bounds;
        changed_bounds.density *= 2.;
        reject([&] { backend->prepare_native_reflecting_layer(access, radial, changed_bounds); },
               "native reflector accepted unfrozen bounds");
        auto duplicate = radial;
        duplicate.push_back(radial.front());
        reject([&] { backend->prepare_native_reflecting_layer(access, duplicate, bounds); },
               "native reflector accepted duplicate destinations");
        auto invalid = radial;
        invalid.back().destination[0] = grid.Is();
        reject([&] { backend->prepare_native_reflecting_layer(access, invalid, bounds); },
               "native reflector accepted an interior destination");
        auto malformed = prefix;
        malformed.enuc.pop_back();
        reject([&] { backend->prepare_native_reflecting_layer(access, corners, bounds, indices, &malformed); },
               "native reflector accepted malformed prefix planes");
        auto bad_indices = indices;
        bad_indices[1] = bad_indices[0];
        reject([&] { backend->prepare_native_reflecting_layer(access, corners, bounds, bad_indices, &prefix); },
               "native reflector accepted duplicate prefix indices");
        bad_indices = indices;
        bad_indices.back() = grid.GetIndex(grid.Is(), grid.Js()+1, 0);
        reject([&] { backend->prepare_native_reflecting_layer(access, corners, bounds, bad_indices, &prefix); },
               "native reflector accepted an interior prefix");
        require(grid.stride_y > grid.GetTotalX(), "native store test requires real row padding");
        bad_indices = indices;
        bad_indices.back() = grid.GetIndex(grid.GetTotalX(), grid.Js()+1, 0);
        reject([&] { backend->prepare_native_reflecting_layer(access, corners, bounds, bad_indices, &prefix); },
               "native reflector accepted padded prefix indices");
        const int bad_source = grid.GetIndex(radial.front().source[0], radial.front().source[1], 0);
        const double saved = block.fluid_state.eng[bad_source];
        block.fluid_state.eng[bad_source] = -1.;
        backend->enqueue_upload_slot(access, StateRegion::Interior, state_view(block.fluid_state));
        backend->quiesce();
        const auto before_failure = backend->counters();
        require_rejected<std::runtime_error>([&] {
            backend->prepare_native_reflecting_layer(access, radial, bounds);
        }, "native reflector accepted a bad thermodynamic source");
        const auto after_failure = backend->counters();
        require(after_failure.kernel_count-before_failure.kernel_count == 1
            && after_failure.bytes_h2d-before_failure.bytes_h2d == radial.size()*sizeof(math::Request)
            && after_failure.bytes_d2h-before_failure.bytes_d2h == sizeof(int),
            "native reflector failure exposed partial candidates or downloaded full state");
        require_unchanged();
        block.fluid_state.eng[bad_source] = saved;
        backend->enqueue_upload_slot(access, StateRegion::Interior, state_view(block.fluid_state));
        backend->quiesce();
        const auto recovered = backend->prepare_native_reflecting_layer(access, radial, bounds);
        require_candidates(radial, recovered, original);
        require_unchanged();
    }
}


/** Account for the one public counter query made by this assertion itself.
 * Every work field and any additional production getter remain exact: counters()
 * increments getter_count before returning its snapshot, even on a no-op path.
 */
bool unchanged_counters_after_query(const arch::backend::ComputeBackend& backend,
    arch::backend::BackendCounters expected)
{
    ++expected.getter_count;
    return backend.counters()==expected;
}

/** Exercise the actual resident savepoint through public field/surface consumers.
 * The three real cyclic rotations expose each slot as Current, then restore
 * the original map. Host padding remains sentinel; resident padding is not
 * observable through these region transfers and is not qualified here.
 */
void run_resident_macro_savepoint(int device_count)
{
    using namespace arch;using state::StateSlot;using state::StateRegion;
    constexpr state::SlotRotation cycle{StateSlot::Next,StateSlot::Scratch,StateSlot::Current};
    for(int count:{0,2}) {
        std::array<amr::Block,2> blocks{make_block(0,1.),make_block(1,2.)};
        SpeciesManager species;if(count){species.add_species("H1",1.,1.,1.4,1.);species.add_species("He4",4.,2.,1.4,1.);}
        IdealGas eos(1.4,species);auto launch=make_launch_config();
        launch.diffusion={true,true,false,false,0.,.01,0.};
        launch.plan.diffusion_integrator=dispatch::DiffusionIntegratorId::Rkl1;
        const auto boundary=make_boundary_plan();
        const std::array handles{amr::BlockHandle{{3101},{31}},amr::BlockHandle{{3102},{31}}};
        const std::array storage{backend::StorageGeneration{3101},backend::StorageGeneration{3102}};
        std::array<backend::BackendStateAccess,2> accesses{current(handles[0],storage[0]),current(handles[1],storage[1])};
        for(int b=0;b<2;++b) {
            int slot=0;for(auto* field:{&blocks[b].fluid_state,&blocks[b].state_next,&blocks[b].state_scratch}) {
                field->InitSpecies(count);
                for(int i=0;i<blocks[b].grid.GetTotalX();++i) {
                    const int cell=blocks[b].grid.GetIndex(i);const double seed=100.*(1+slot)+b+.125*i;
                    field->set(cell,{2.,.1,.2,.3,seed});field->enuc_rate[cell]=(i&1)?seed:-0.;
                    for(int s=0;s<count;++s)field->X(s,cell)=s?.75:.25;
                }
                ++slot;
            }
        }
        const std::array bindings{cuda::CudaBlockBinding{&blocks[0],handles[0],storage[0],&boundary},
            cuda::CudaBlockBinding{&blocks[1],handles[1],storage[1],&boundary}};
        auto backend=cuda::make_cuda_backend(bindings,kBackendDevice,launch,species,eos);
        const auto upload=[&](int b,StateSlot slot,FluidState& field) {
            auto access=accesses[b];access.slot=slot;
            for(auto region:{StateRegion::Interior,StateRegion::Ghost})backend->enqueue_upload_slot(access,region,transfer_view(field));
        };
        for(int b=0;b<2;++b) {
            upload(b,StateSlot::Current,blocks[b].fluid_state);upload(b,StateSlot::Next,blocks[b].state_next);
            upload(b,StateSlot::Scratch,blocks[b].state_scratch);
        }
        // Preallocate the genuine controls for each physical owner before pinning.
        boundary::DiffusionBoundaryStorage controls;controls.faces[0].resize(4+count);
        controls.faces[0][0]={boundary::ScalarBoundaryKind::OutwardFlux,.03125};
        for(int b=0;b<2;++b)for(auto slot:{StateSlot::Current,StateSlot::Next,StateSlot::Scratch}) {
            auto access=accesses[b];access.slot=slot;const std::array ghost{blocks[b].grid.GetIndex(0)};
            const auto values=backend->read_boundary_cells(access,ghost,StateRegion::Ghost);
            backend->write_boundary_cells(access,ghost,values,controls);
        }
        std::array<backend::BoundaryFluxPlanes,2> layout;
        for(int b=0;b<2;++b){layout[b].block=handles[b];for(int face:{0,1})layout[b].stage[face].resize(6+count);}
        backend->configure_boundary_flux_capture(layout,1.,0.,true);
        const auto hydro=scheduler::make_hydro_plan(scheduler::HydroMethod::Euler).stages.front();
        (void)backend->execute_hydro_stage_batch(accesses,hydro,1.e-4,{1,state::CompletionState::Complete});backend->quiesce();
        const auto all_slots=[&] {
            std::array<std::array<FluidState,3>,2> result;
            for(int slot=0;slot<3;++slot)for(int b=0;b<2;++b) {
                result[b][slot]=blocks[b].fluid_state;
                for(auto region:{StateRegion::Interior,StateRegion::Ghost})
                    backend->enqueue_materialize_host_current(accesses[b],region,transfer_view(result[b][slot]));
                backend->quiesce();backend->rotate_slots(accesses[b],cycle);
            }
            return result;
        };
        const auto same_slots=[&](const auto& expected) {
            const auto observed=all_slots();for(int b=0;b<2;++b)for(int slot=0;slot<3;++slot)
                require(same_bits(observed[b][slot],expected[b][slot]),"resident rollback changed an original logical slot or Host padding");
        };
        const auto same_surfaces=[](const auto& actual,const auto& expected) {
            require(actual.size()==expected.size(),"resident rollback changed observer owner count");
            for(std::size_t b=0;b<actual.size();++b) {
                require(actual[b].block==expected[b].block,"resident rollback changed observer identity");
                for(int face=0;face<6;++face)for(int plane=0;plane<2;++plane) {
                    const auto& a=plane?actual[b].initial[face]:actual[b].stage[face];
                    const auto& e=plane?expected[b].initial[face]:expected[b].stage[face];
                    require(a.size()==e.size(),"resident rollback changed observer active shape");
                    for(std::size_t n=0;n<a.size();++n)require(std::bit_cast<std::uint64_t>(a[n])==std::bit_cast<std::uint64_t>(e[n]),
                        "resident rollback failed to restore genuine observer plane bits");
                }
            }
        };
        const auto baseline=all_slots();const auto surfaces=backend->download_boundary_flux_capture();
        require(std::any_of(surfaces[0].stage[0].begin(),surfaces[0].stage[0].end(),[](double x){return x!=0.;}),
            "resident observer reference remained zero");
        const auto rejects_no_work=[&](auto&& operation) {
            const auto before=backend->counters();require_rejected(operation,"invalid resident savepoint/namespace accepted");
            require(unchanged_counters_after_query(*backend,before),"resident preflight rejection enqueued partial work");
        };
        // Both real blocks already bind face0 on all three physical owners.
        // Reserve only that existing shape so face1 remains an unbound growth case.
        std::array<std::array<std::size_t,6>,2> retained_control_counts{};
        for(auto& face_counts:retained_control_counts)face_counts[0]=4+static_cast<std::size_t>(count);
        auto stale=accesses;stale[1].storage.value+=99;
        rejects_no_work([&]{backend->begin_macro_state_transaction(stale);});
        rejects_no_work([&]{backend->prepare_boundary_control_capacity(stale,retained_control_counts);});
        auto inactive_control_counts=retained_control_counts;inactive_control_counts[1][2]=1;
        rejects_no_work([&]{backend->prepare_boundary_control_capacity(accesses,inactive_control_counts);});
        auto duplicate=accesses;duplicate[1]=duplicate[0];rejects_no_work([&]{backend->begin_macro_state_transaction(duplicate);});
        rejects_no_work([&]{backend->begin_macro_state_transaction(std::span<const backend::BackendStateAccess>(accesses.data(),1));});
        rejects_no_work([&]{backend->begin_macro_state_transaction({});});
        auto wrong=accesses;wrong[1].slot=StateSlot::Next;rejects_no_work([&]{backend->begin_macro_state_transaction(wrong);});
        const auto rkl=scheduler::make_rkl_plan(scheduler::RklMethod::RKL1,1);
        const auto diffuse=[&]{(void)backend->execute_diffusion_stage_batch(accesses,rkl,rkl.stages.front(),1.e-4,1.,{2,state::CompletionState::Complete});};
        std::array<std::array<FluidState,3>,2> control_reference;std::vector<backend::BoundaryFluxPlanes> surface_reference;
        {auto savepoint=backend->begin_macro_state_transaction(accesses);diffuse();control_reference=all_slots();surface_reference=backend->download_boundary_flux_capture();}
        same_slots(baseline);same_surfaces(backend->download_boundary_flux_capture(),surfaces);
        // Capacity is sufficient and genuinely bound: prewarm is metadata-only.
        // Replay the original Diffusion consumer instead of inspecting raw pointers.
        const auto before_capacity=backend->counters();
        backend->prepare_boundary_control_capacity(accesses,retained_control_counts);
        require(unchanged_counters_after_query(*backend,before_capacity),
            "bound resident control prewarm enqueued work despite sufficient capacity");
        {auto savepoint=backend->begin_macro_state_transaction(accesses);diffuse();same_slots(control_reference);
            same_surfaces(backend->download_boundary_flux_capture(),surface_reference);savepoint->validate_storage();}
        same_slots(baseline);same_surfaces(backend->download_boundary_flux_capture(),surfaces);
        const auto before_save=backend->counters();
        bool exception_seen=false;
        try {
            select_device_probe(device_count);auto savepoint=backend->begin_macro_state_transaction(accesses);
            require_backend_device_selected("resident savepoint did not restore selected device");
            const auto saved=backend->counters();
            require(saved.bytes_h2d==before_save.bytes_h2d&&saved.bytes_d2h==before_save.bytes_d2h
                &&saved.kernel_count==before_save.kernel_count&&saved.stream_sync_count>before_save.stream_sync_count,
                "resident savepoint added a field transfer or false kernel");
            rejects_no_work([&]{backend->begin_macro_state_transaction(accesses);});
            rejects_no_work([&]{backend->begin_store_transaction({1,{31},{32}},bindings);});
            auto changed_layout=layout;changed_layout[1].stage[0].clear();
            rejects_no_work([&]{backend->configure_boundary_flux_capture(changed_layout,.25,.5,false);});
            same_surfaces(backend->download_boundary_flux_capture(),surfaces);
            auto growing=controls;growing.faces[1].resize(4+count);
            const std::array ghost{blocks[0].grid.GetIndex(0)};
            const auto values=backend->read_boundary_cells(accesses[0],ghost,StateRegion::Ghost);
            auto rejected_values=values;rejected_values.conserved[0].eng+=37.;
            rejects_no_work([&]{backend->write_boundary_cells(accesses[0],ghost,rejected_values,growing);});
            same_slots(baseline);
            for(int b=0;b<2;++b)for(auto slot:{StateSlot::Current,StateSlot::Next,StateSlot::Scratch}) {
                auto changed=baseline[b][static_cast<int>(slot)];for(double& value:changed.eng)value+=17.;upload(b,slot,changed);
            }
            controls.faces[0][0].value=7.;
            for(int b=0;b<2;++b){const std::array g{blocks[b].grid.GetIndex(0)};const auto v=backend->read_boundary_cells(accesses[b],g,StateRegion::Ghost);
                backend->write_boundary_cells(accesses[b],g,v,controls);backend->rotate_slots(accesses[b],cycle);}
            backend->configure_boundary_flux_capture(layout,.25,.5,false);diffuse();
            // Remove a real binding after overwrite; restoring capacity alone cannot pass replay.
            boundary::DiffusionBoundaryStorage none;backend->write_boundary_cells(accesses[0],ghost,values,none);
            savepoint->validate_storage();throw std::runtime_error("resident rollback scope fault");
        }catch(const std::runtime_error& error){exception_seen=std::string(error.what())=="resident rollback scope fault";if(!exception_seen)throw;}
        require(exception_seen&&backend->counters().bytes_h2d>before_save.bytes_h2d
            &&backend->counters().kernel_count>before_save.kernel_count,"rollback erased actual work counters");
        same_slots(baseline);same_surfaces(backend->download_boundary_flux_capture(),surfaces);
        {auto savepoint=backend->begin_macro_state_transaction(accesses);diffuse();same_slots(control_reference);
            same_surfaces(backend->download_boundary_flux_capture(),surface_reference);savepoint->validate_storage();savepoint->commit();}
        same_slots(control_reference); // Commit kept the real changed output rather than restoring baseline.
        {auto savepoint=backend->begin_macro_state_transaction(accesses);backend->copy_state_slot(accesses[0],{handles[0],storage[0],StateSlot::Scratch});}
        same_slots(control_reference); // The same grow-only savepoint can be reused immediately.
    }
}

/** Reduce actual resident endpoints with independent true-volume references.
 * Only real interiors contribute. Public Ghost uploads poison all actual halos;
 * resident allocation padding stays unobservable through this transfer owner.
 */
void run_resident_diffusion_activity()
{
    using namespace arch;using state::StateSlot;using state::StateRegion;
    for(bool native:{false,true})for(int count:{0,2}) {
        std::array<amr::Block,2> blocks{make_block(0,1.),make_block(1,2.)};
        if(native) {
            Grid root(amr::MAX_NG,1.,3.,-.5,.5,0.,1.,2,1,1);root.geometry="cylindrical";root.dim=2;
            for(int b=0;b<2;++b){blocks[b].Reset();blocks[b].active=true;blocks[b].logical_x1=b;blocks[b].logical_x2=0;
                blocks[b].InitGeometry(root,1./amr::BLOCK_NX,1./amr::BLOCK_NY,1.,GridMetrics::GeometrySemantics::AxisymmetricRz);}
        }
        SpeciesManager species;if(count){species.add_species("H1",1.,1.,1.4,1.);species.add_species("He4",4.,2.,1.4,1.);}
        IdealGas eos(1.4,species);const auto boundary=make_boundary_plan(native?2:1);
        for(auto& block:blocks)for(auto* field:{&block.fluid_state,&block.state_next,&block.state_scratch}) {
            field->Preallocate(block.grid.GetTotalSize());field->InitSpecies(count);
            for(int cell=0;cell<block.grid.GetTotalSize();++cell){field->set(cell,{2.,0.,0.,0.,1.e100});field->enuc_rate[cell]=-0.;
                for(int s=0;s<count;++s)field->X(s,cell)=s?.75:.25;}
        }
        long double expected_signed[2]{},expected_absolute[2]{};std::uint64_t cells=0;
        for(auto& block:blocks){const auto& g=block.grid;for(int k=g.Ks();k<g.Ke();++k)for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const int cell=g.GetIndex(i,j,k);const double difference=((i+j)&1)?.125:-.125;
            block.fluid_state.eng[cell]=20.;block.state_next.eng[cell]=20.-difference;block.state_scratch.eng[cell]=19.75;
            const long double volume=native?std::acos(-1.L)*(static_cast<long double>(g.GetFacePosR(i))*g.GetFacePosR(i)
                -static_cast<long double>(g.GetFacePosL(i))*g.GetFacePosL(i))
                    *(static_cast<long double>(g.GetAxialFacePosR(j))-g.GetAxialFacePosL(j))
                :static_cast<long double>(g.dx1);
            expected_signed[0]+=volume*difference;expected_absolute[0]+=volume*std::abs(difference);
            expected_signed[1]+=volume*.25L;expected_absolute[1]+=volume*.25L;++cells;
        }}
        const std::array handles{amr::BlockHandle{{3201},{32}},amr::BlockHandle{{3202},{32}}};
        const std::array storage{backend::StorageGeneration{3201},backend::StorageGeneration{3202}};
        const std::array bindings{cuda::CudaBlockBinding{&blocks[0],handles[0],storage[0],&boundary},cuda::CudaBlockBinding{&blocks[1],handles[1],storage[1],&boundary}};
        auto backend=cuda::make_cuda_backend(bindings,kBackendDevice,make_launch_config(),species,eos);
        const std::array accesses{current(handles[0],storage[0]),current(handles[1],storage[1])};
        const auto upload=[&](int b,StateSlot slot,FluidState& field){auto access=accesses[b];access.slot=slot;
            for(auto region:{StateRegion::Interior,StateRegion::Ghost})backend->enqueue_upload_slot(access,region,transfer_view(field));};
        for(int b=0;b<2;++b){upload(b,StateSlot::Current,blocks[b].fluid_state);upload(b,StateSlot::Next,blocks[b].state_next);upload(b,StateSlot::Scratch,blocks[b].state_scratch);}backend->quiesce();
        const auto no_work=[&](auto&& operation){const auto before=backend->counters();require_rejected(operation,"invalid diffusion activity owner accepted");
            require(unchanged_counters_after_query(*backend,before),"diffusion activity preflight partly enqueued work");};
        const auto empty_before=backend->counters();const auto empty=backend->reduce_diffusion_energy_activity_batch({},StateSlot::Next);
        require(empty.cells==0&&empty.signed_energy_change==0.&&empty.absolute_energy_change==0.&&unchanged_counters_after_query(*backend,empty_before),"empty activity batch performed work");
        auto stale=accesses;stale[1].storage.value+=99;no_work([&]{backend->reduce_diffusion_energy_activity_batch(stale,StateSlot::Next);});
        auto duplicate=accesses;duplicate[1]=duplicate[0];no_work([&]{backend->reduce_diffusion_energy_activity_batch(duplicate,StateSlot::Next);});
        auto mixed=accesses;mixed[1].slot=StateSlot::Next;no_work([&]{backend->reduce_diffusion_energy_activity_batch(mixed,StateSlot::Next);});
        for(auto slot:{StateSlot::Current,static_cast<StateSlot>(255)})no_work([&]{backend->reduce_diffusion_energy_activity_batch(accesses,slot);});
        int reference=0;for(auto slot:{StateSlot::Next,StateSlot::Scratch}) {
            const auto before=backend->counters();const auto result=backend->reduce_diffusion_energy_activity_batch(accesses,slot);const auto after=backend->counters();
            const long double tolerance=64.L*std::numeric_limits<double>::epsilon()*std::max(1.L,expected_absolute[reference]);
            require(result.cells==cells&&std::abs(result.signed_energy_change-expected_signed[reference])<=tolerance
                &&std::abs(result.absolute_energy_change-expected_absolute[reference])<=tolerance,"resident endpoint activity lost independent true volume/cell ownership");
            require(after.kernel_count==before.kernel_count+1&&after.bytes_h2d>before.bytes_h2d
                &&after.bytes_d2h>before.bytes_d2h,"activity did not report actual compact metadata/result work");
            std::cout<<"RESIDENT_DIFFUSION_ACTIVITY chart="<<(native?"RZ":"Cartesian")<<" species="<<count
                <<" retained_slot="<<static_cast<int>(slot)<<" h2d="<<after.bytes_h2d-before.bytes_h2d
                <<" d2h="<<after.bytes_d2h-before.bytes_d2h<<" kernels="<<after.kernel_count-before.kernel_count<<'\n';++reference;
        }
        // Reading an endpoint receipt never mutates any real U/X/ENUC or host padding.
        constexpr state::SlotRotation cycle{StateSlot::Next,StateSlot::Scratch,StateSlot::Current};
        for(int slot=0;slot<3;++slot)for(int b=0;b<2;++b){auto observed=slot==0?blocks[b].fluid_state:slot==1?blocks[b].state_next:blocks[b].state_scratch;
            for(auto region:{StateRegion::Interior,StateRegion::Ghost})backend->enqueue_materialize_host_current(accesses[b],region,transfer_view(observed));backend->quiesce();
            const auto& original=slot==0?blocks[b].fluid_state:slot==1?blocks[b].state_next:blocks[b].state_scratch;
            require(same_bits(observed,original),"resident activity modified an endpoint field or host padding");backend->rotate_slots(accesses[b],cycle);}
        const auto& g=blocks[1].grid;const int bad=g.GetIndex(g.Is(),g.Js(),g.Ks());
        blocks[1].fluid_state.eng[bad]=std::numeric_limits<double>::quiet_NaN();upload(1,StateSlot::Current,blocks[1].fluid_state);backend->quiesce();
        require_rejected<std::runtime_error>([&]{backend->reduce_diffusion_energy_activity_batch(accesses,StateSlot::Next);},"nonfinite actual endpoint accepted");
        blocks[1].fluid_state.eng[bad]=20.;upload(1,StateSlot::Current,blocks[1].fluid_state);backend->quiesce();
        require(backend->reduce_diffusion_energy_activity_batch(accesses,StateSlot::Next).cells==cells,"activity scratch did not recover after nonfinite endpoint");
        backend->copy_state_slot_batch(accesses,StateSlot::Scratch);const auto zero=backend->reduce_diffusion_energy_activity_batch(accesses,StateSlot::Scratch);
        require(zero.cells==cells&&zero.signed_energy_change==0.&&zero.absolute_energy_change==0.,"identical actual resident energy endpoints had nonzero activity");
    }
}


/** Exercise the actual final Native axis/corner service after a second exchange.
 * Two axial neighbor blocks share r=0. A fresh interior donor and real second
 * exchange distinguish final signed completion from stale first-stage images.
 * The existing Host axis-only executor owns parity and corner references; this
 * is resident boundary transport, not a completed Runtime/EOS acceptance.
 */
void run_native_final_axis_backend(int device_count)
{
    using namespace arch;using state::StateSlot;using state::StateRegion;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr state::CompletionToken token{17,state::CompletionState::Complete};
    const auto fill=[](amr::Block& block,int count,int owner) {
        const auto& g=block.grid;
        for(auto* field:{&block.fluid_state,&block.state_next,&block.state_scratch}) {
            field->Preallocate(g.GetTotalSize());field->InitSpecies(count);
            // Host allocation tails remain known sentinels. Region transfers
            // do not expose or initialize resident allocation-only padding.
            for(auto* plane:{&field->rho,&field->mom_u,&field->mom_v,&field->mom_w,&field->eng,&field->enuc_rate})
                std::fill(plane->begin(),plane->end(),-98765.);
            for(int k=0;k<g.GetTotalZ();++k)for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                const int cell=g.GetIndex(i,j,k);const double code=10000.*owner+100.*j+i;
                field->set(cell,{100.+code,3.+code,5.+2.*code,7.+3.*code,100000.+code});
                field->enuc_rate[cell]=(i+j)&1?9.+4.*code:-0.;
                for(int s=0;s<count;++s)field->X(s,cell)=s?.75:.25;
                if(i<g.Is()&&g.x1_min==0.) {
                    field->set(cell,{-1234.,-2345.,-3456.,-4567.,-5678.});field->enuc_rate[cell]=-6789.;
                    for(int s=0;s<count;++s)field->X(s,cell)=-7.-s;
                }
            }
            const int cell=g.GetIndex(g.Is(),g.dim>=2?g.Js()+1:g.Js(),g.Ks());field->mom_u[cell]=0.;field->mom_w[cell]=-0.;
        }
    };
    for(int count:{0,2}) {
        Grid root(amr::MAX_NG,0.,1.,-1.,1.,0.,1.,1,2,1);root.geometry="cylindrical";root.dim=2;
        std::array<amr::Block,2> blocks;
        for(int b=0;b<2;++b) {
            blocks[b].Reset();blocks[b].active=true;blocks[b].logical_x1=0;blocks[b].logical_x2=b;
            blocks[b].InitGeometry(root,1./amr::BLOCK_NX,1./amr::BLOCK_NY,1.,native);
            blocks[b].RequireNativeGeometryIdentity();fill(blocks[b],count,b);
        }
        SpeciesManager species;if(count){species.add_species("H1",1.,1.,1.4,1.);species.add_species("He4",4.,2.,1.4,1.);}
        IdealGas eos(1.4,species);
        auto input=make_boundary_plan(2).input();input.faces[0]=boundary::BoundaryType::RzAxis;
        const auto axis=boundary::make_boundary_plan(input);
        const std::array handles{amr::BlockHandle{{3301},{33}},amr::BlockHandle{{3302},{33}}};
        const std::array storage{backend::StorageGeneration{3301},backend::StorageGeneration{3302}};
        const std::array bindings{cuda::CudaBlockBinding{&blocks[0],handles[0],storage[0],&axis},
            cuda::CudaBlockBinding{&blocks[1],handles[1],storage[1],&axis}};
        auto backend=cuda::make_cuda_backend(bindings,kBackendDevice,make_launch_config(),species,eos);
        const std::array accesses{current(handles[0],storage[0]),current(handles[1],storage[1])};
        const auto materialize=[&] {
            std::array<FluidState,2> result{blocks[0].fluid_state,blocks[1].fluid_state};
            for(int b=0;b<2;++b)for(auto region:{StateRegion::Interior,StateRegion::Ghost})
                backend->enqueue_materialize_host_current(accesses[b],region,transfer_view(result[b]));
            backend->quiesce();return result;
        };
        for(int b=0;b<2;++b)for(auto region:{StateRegion::Interior,StateRegion::Ghost})
            backend->enqueue_upload_slot(accesses[b],region,transfer_view(blocks[b].fluid_state));backend->quiesce();
        require(backend->execute_native_axis_boundary_batch(accesses,{1},{1,state::CompletionState::Complete})
            ==state::CompletionToken{1,state::CompletionState::Complete},"initial axis completion lost its actual token");
        std::array<amr::SameLevelTopologyEntry,2> topology;
        for(int b=0;b<2;++b){topology[b].logical={2,0,0,static_cast<std::uint32_t>(b),0};topology[b].handle=handles[b];}
        topology[0].neighbors[3]=topology[1].logical;topology[1].neighbors[2]=topology[0].logical;
        const auto exchange=amr::make_same_level_exchange_plan(topology,2,{amr::BLOCK_NX,amr::BLOCK_NY,1},amr::MAX_NG,handles[0].epoch);
        (void)backend->execute_same_level_exchange(accesses,exchange,StateSlot::Current,{1},{2,state::CompletionState::Complete});
        for(int b=0;b<2;++b) {
            const auto& g=blocks[b].grid;auto& field=blocks[b].fluid_state;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int cell=g.GetIndex(i,j,0);field.eng[cell]+=1000.;field.enuc_rate[cell]+=2000.;
                if(field.mom_u[cell]!=0.)field.mom_u[cell]+=30.;
                if(field.mom_w[cell]!=0.)field.mom_w[cell]+=70.;
            }
            backend->enqueue_upload_slot(accesses[b],StateRegion::Interior,transfer_view(field));
        }
        backend->quiesce();
        (void)backend->execute_same_level_exchange(accesses,exchange,StateSlot::Current,{2},{3,state::CompletionState::Complete});
        // Fresh poison is confined to actual negative-r axis/corner ghosts;
        // positive-r exchanged donors and outer user-style values stay resident.
        for(int b=0;b<2;++b) {
            const auto& g=blocks[b].grid;std::vector<int> indices;
            backend::BoundaryCells values;values.species_count=static_cast<std::size_t>(count);
            for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.Is();++i) {
                indices.push_back(g.GetIndex(i,j,0));values.conserved.push_back({-1234.,-2345.,-3456.,-4567.,-5678.});
                values.enuc.push_back(-6789.);for(int s=0;s<count;++s)values.composition.push_back(-7.-s);
            }
            backend->write_boundary_cells(accesses[b],indices,values,{});
        }
        const auto before_fields=materialize();auto reference=before_fields;
        for(int b=0;b<2;++b)boundary::host::execute_rz_axis(axis,
            boundary::host::compile(axis,boundary::host::make_layout(blocks[b].grid)),reference[b]);
        const auto reject_before_work=[&](auto&& operation) {
            const auto before=backend->counters();require_rejected(operation,"invalid final Native axis batch accepted");
            require(unchanged_counters_after_query(*backend,before),"final Native axis batch preflight launched or transferred partial work");
        };
        auto stale=accesses;stale[1].storage.value+=99;reject_before_work([&]{backend->execute_native_axis_boundary_batch(stale,{3},token);});
        auto duplicate=accesses;duplicate[1]=duplicate[0];reject_before_work([&]{backend->execute_native_axis_boundary_batch(duplicate,{3},token);});
        auto mixed=accesses;mixed[1].slot=StateSlot::Next;reject_before_work([&]{backend->execute_native_axis_boundary_batch(mixed,{3},token);});
        auto invalid=accesses;invalid[1].slot=static_cast<StateSlot>(255);reject_before_work([&]{backend->execute_native_axis_boundary_batch(invalid,{3},token);});
        reject_before_work([&]{backend->execute_native_axis_boundary_batch(accesses,{},token);});
        reject_before_work([&]{backend->execute_native_axis_boundary_batch(accesses,{3},{17,state::CompletionState::Pending});});
        reject_before_work([&]{backend->execute_native_axis_boundary_batch(accesses,{3},{0,state::CompletionState::Complete});});
        const auto rejected_fields=materialize();for(int b=0;b<2;++b)require(same_bits(rejected_fields[b],before_fields[b]),
            "failed late Native axis preflight changed resident logical fields");
        select_device_probe(device_count);const auto before=backend->counters();
        require(backend->execute_native_axis_boundary_batch(accesses,{3},token)==token,"final Native axis lost joined completion token");
        require_backend_device_selected("final Native axis completion did not select its device");
        const auto after=backend->counters();
        require(after.kernel_count-before.kernel_count==4&&after.bytes_h2d==before.bytes_h2d&&after.bytes_d2h==before.bytes_d2h
            &&after.stream_sync_count>before.stream_sync_count,"final Native axis reran physical seeds or uploaded/downloaded cached metadata");
        const auto observed=materialize();for(int b=0;b<2;++b)require(same_bits(observed[b],reference[b]),
            "final resident axis/corner differs from original Host signed executor or rewrites outer/interior user values");
        // Repeating the cached signed completion is idempotent and adds exactly
        // the two original nonempty X/Y phases per actual axis owner.
        const auto repeat_before=backend->counters();require(backend->execute_native_axis_boundary_batch(accesses,{3},token)==token,"cached final Native axis lost completion");
        const auto repeat_after=backend->counters();require(repeat_after.kernel_count-repeat_before.kernel_count==4
            &&repeat_after.bytes_h2d==repeat_before.bytes_h2d&&repeat_after.bytes_d2h==repeat_before.bytes_d2h,"cached final Native axis reuploaded metadata");
        const auto repeated=materialize();for(int b=0;b<2;++b)require(same_bits(repeated[b],reference[b]),"signed axis completion is not idempotent");
        // Ordinary and positive-r Native owners have distinct real factories;
        // neither a caller enum nor an empty axis list creates Native authority.
        for(bool ordinary:{false,true}) {
            amr::Block other=make_block(0,1.);
            if(!ordinary){Grid off_root(amr::MAX_NG,1.,2.,-.5,.5,0.,1.,1,1,1);off_root.geometry="cylindrical";off_root.dim=2;
                other.Reset();other.active=true;other.InitGeometry(off_root,1./amr::BLOCK_NX,1./amr::BLOCK_NY,1.,native);other.RequireNativeGeometryIdentity();}
            fill(other,count,5);const auto other_boundary=make_boundary_plan(ordinary?1:2);
            const amr::BlockHandle handle{{3401},{34}};const backend::StorageGeneration generation{3401};
            auto owner=cuda::make_cuda_backend(other,handle,generation,kBackendDevice,make_launch_config(),species,other_boundary,eos);
            const std::array access{current(handle,generation)};
            for(auto region:{StateRegion::Interior,StateRegion::Ghost})owner->enqueue_upload_slot(access[0],region,transfer_view(other.fluid_state));owner->quiesce();
            const auto original=other.fluid_state;const auto initial=owner->counters();
            if(ordinary){require_rejected([&]{owner->execute_native_axis_boundary_batch(access,{1},token);},"ordinary owner received a Native final-axis grant");
                require(unchanged_counters_after_query(*owner,initial),"ordinary final-axis refusal enqueued work");}
            else {require(owner->execute_native_axis_boundary_batch(access,{1},token)==token,"off-axis no-op lost token");const auto final=owner->counters();
                require(final.kernel_count==initial.kernel_count&&final.bytes_h2d==initial.bytes_h2d&&final.bytes_d2h==initial.bytes_d2h,
                    "off-axis Native final completion wrote physical seed ghosts or transferred metadata");}
            for(auto region:{StateRegion::Interior,StateRegion::Ghost})owner->enqueue_materialize_host_current(access[0],region,transfer_view(other.fluid_state));owner->quiesce();
            require(same_bits(other.fluid_state,original),"ordinary refusal/off-axis Native no-op changed actual logical fields or Host padding");
        }
    }
}

/** Compare actual ordered Native Device candidates with the existing Host owner.
 * Callback preparation remains read-only, whole x1 layers precede x2 corners,
 * and the published compact ghosts retain donor ENUC exactly. Public transfers
 * observe logical cells only; resident padding and full Runtime are unqualified.
 */
void run_native_ordered_device_boundary(int device_count)
{
    using namespace arch;using state::StateRegion;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto close=[](double actual,double expected) {
        require(std::isfinite(actual)&&std::abs(actual-expected)
            <=64.*std::numeric_limits<double>::epsilon()*std::max(1.,std::abs(expected)),
            "ordered Native Device boundary differs from the original Host 64-epsilon reference");
    };
    for(int count:{0,2})for(bool user:{false,true}) {
        SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=1.;config.grid.x1_max=2.;config.grid.x2_min=-.5;config.grid.x2_max=.5;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type=user?"user":"reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type=user?"user":"reflecting";
        config.numerics.sml_rho=config.numerics.min_eint=1.e-14;config.numerics.max_eint=1.e12;
        config.physics.diffusion.use_diffusion=config.physics.diffusion.use_thermal_diffusion=true;
        SpeciesManager species;if(count){species.add_species("H1",1.,1.,1.4,1.);species.add_species("He4",4.,2.,1.4,1.);}
        IdealGas eos(1.4,species);const double cv=count?1.:IdealGasView::default_specific_heat_cv;
        Grid root(amr::MAX_NG,1.,2.,-.5,.5,0.,1.,1,1,1);root.geometry="cylindrical";root.dim=2;
        amr::Block block;block.Reset();block.active=true;
        block.InitGeometry(root,1./amr::BLOCK_NX,1./amr::BLOCK_NY,1.,native);block.RequireNativeGeometryIdentity();
        const auto& grid=block.grid;require(grid.ng==amr::MAX_NG&&grid.x1_min>0.,"ordered BC fixture lost full off-axis halo");
        for(auto* field:{&block.fluid_state,&block.state_next,&block.state_scratch}) {
            field->Preallocate(grid.GetTotalSize());field->InitSpecies(count);
            for(int cell=0;cell<grid.GetTotalSize();++cell){field->set(cell,{4.,0.,0.,0.,40.*cv});field->enuc_rate[cell]=-999.;
                for(int s=0;s<count;++s)field->X(s,cell)=s?.75:.25;}
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j,0);const bool interior=i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je();
                const long double a=grid.GetFacePosL(i),b=grid.GetFacePosR(i),v=(b*b-a*a)/2.;
                const double rho=user&&!interior?4.:double(1.L+(b*b*b-a*a*a)/(192.L*v));
                field->set(cell,{rho,user?0.:.1*rho,user?0.:.2*rho,0.,(user?10.*cv:20.*cv)*rho});
                field->enuc_rate[cell]=(i+j)&1?100.*j+i:-0.;
            }
        }
        const auto seed=block.fluid_state;boundary::BoundaryPurpose purpose=boundary::BoundaryPurpose::Hydro;
        int fault=0,calls=0;std::array<int,2> axis_calls{};
        boundary::ResolvedUserBoundaries callbacks;callbacks.identity="ordered-native-actual-store";
        if(user)callbacks.physical=[&](const boundary::PhysicalBoundaryContext& context) {
            require(context.time==.375&&context.purpose==purpose,"actual callback lost selected stage time/purpose");
            const int axis=static_cast<int>(context.axis);++calls;++axis_calls[axis];
            if((fault==1&&calls==11)||(fault==2&&context.axis==boundary::BoundaryAxis::X2))
                throw std::runtime_error("ordered callback scope fault");
            if(context.axis==boundary::BoundaryAxis::X2) {
                close(context.interior.rho,1.+context.ghost_point.r_cy/64.);
                close(context.interior.temperature,10.);
            }
            PrimitiveData point;point.rho=1.+context.ghost_point.r_cy/64.;point.SetTemperature(10.);
            if(count)point.mass_fractions={.25,.75};
            boundary::PhysicalBoundaryData data;data.hydro=point;
            if(purpose==boundary::BoundaryPurpose::Diffusion)
                data.temperature={boundary::ScalarBoundaryKind::NormalGradient,0.};
            return data;
        };
        boundary::ScopedUserBoundarySelection selected(callbacks,config,species);
        BCHandler handler(config,native);handler.bind(eos,species);
        const amr::BlockHandle handle{{3501},{35}};const backend::StorageGeneration generation{3501};
        auto launch=make_launch_config();launch.density_floor=config.numerics.sml_rho;
        launch.minimum_internal_energy=config.numerics.min_eint;launch.maximum_internal_energy=config.numerics.max_eint;
        auto owner=cuda::make_cuda_backend(block,handle,generation,kBackendDevice,launch,species,handler.logical_plan(),eos);
        const auto access=current(handle,generation);
        const auto upload=[&]{for(auto role:{StateRegion::Interior,StateRegion::Ghost})
            owner->enqueue_upload_slot(access,role,transfer_view(block.fluid_state));owner->quiesce();};
        const auto materialize=[&]{auto result=seed;for(auto role:{StateRegion::Interior,StateRegion::Ghost})
            owner->enqueue_materialize_host_current(access,role,transfer_view(result));owner->quiesce();return result;};
        const auto reject_no_work=[&](auto&& operation){const auto before=owner->counters();
            require_rejected(operation,"Native Device BC accepted candidate/store frame drift");
            require(unchanged_counters_after_query(*owner,before),"Native Device BC frame rejection enqueued work");};
        if(user) {
            // Warm only real allocation capacity. The existing Current producer
            // first binds its genuine non-None controls; Next/Scratch receive
            // their own initialized fixture fields, never copies of Current.
            const std::array<backend::BackendStateAccess,1> domain{access};
            for(int n=1;n<3;++n) {
                auto& field=n==1?block.state_next:block.state_scratch;
                for(double& value:field.enuc_rate)value+=1000.*n;
                auto target=access;target.slot=n==1?state::StateSlot::Next:state::StateSlot::Scratch;
                for(auto region:{StateRegion::Interior,StateRegion::Ghost})
                    owner->enqueue_upload_slot(target,region,transfer_view(field));
            }
            block.fluid_state=seed;upload();purpose=boundary::BoundaryPurpose::Diffusion;
            handler.configure_stage(.375,purpose);calls=0;axis_calls={};
            const auto produce=[&](state::StateSlot slot) {
                auto target=access;target.slot=slot;
                auto value=handler.prepare_native_device(*owner,target,grid);
                handler.validate_native_device_candidate(value,*owner,target,grid);
                handler.publish_native_device(std::move(value),*owner,target,grid);
            };
            produce(state::StateSlot::Current);
            constexpr state::SlotRotation cycle{state::StateSlot::Next,state::StateSlot::Scratch,state::StateSlot::Current};
            const std::array host_templates{block.fluid_state,block.state_next,block.state_scratch};
            const auto all_slots=[&] {
                auto result=host_templates;
                for(int n=0;n<3;++n) {
                    for(auto region:{StateRegion::Interior,StateRegion::Ghost})
                        owner->enqueue_materialize_host_current(access,region,transfer_view(result[n]));
                    owner->quiesce();owner->rotate_slots(access,cycle);
                }
                return result;
            };
            const auto initial_fields=all_slots();const int donor=grid.GetIndex(grid.Is(),grid.Js(),0);
            for(int n=0;n<3;++n)require(std::bit_cast<std::uint64_t>(initial_fields[n].enuc_rate[donor])
                ==std::bit_cast<std::uint64_t>(host_templates[n].enuc_rate[donor]),"independent physical-slot upload lost its actual donor marker");
            require(initial_fields[1].enuc_rate[donor]==initial_fields[0].enuc_rate[donor]+1000.
                &&initial_fields[2].enuc_rate[donor]==initial_fields[0].enuc_rate[donor]+2000.,
                "capacity fixture did not expose three genuinely distinct physical-slot donors");
            const auto stage=handler.snapshot_stage_context();
            const int initial_calls=calls;const auto shape_before=owner->counters();
            const auto extents=handler.native_device_control_extents(grid,count);
            require(extents[0]==static_cast<std::size_t>(grid.Je()-grid.Js())*(4+count)
                &&extents[1]==extents[0]&&extents[2]==static_cast<std::size_t>(grid.Ie()-grid.Is())*(4+count)
                &&extents[3]==extents[2]&&extents[4]==0&&extents[5]==0,
                "capacity request lost actual Diffusion tangential/active/species shape");
            require(calls==initial_calls&&handler.stage_context_matches(stage)&&unchanged_counters_after_query(*owner,shape_before),
                "pure control extent helper performed callback/stage/backend work");
            const std::array<std::array<std::size_t,6>,1> shapes{extents};
            const std::array<std::array<std::size_t,6>,1> zero_shapes{};
            const auto zero_before=owner->counters();owner->prepare_boundary_control_capacity(domain,zero_shapes);
            require(unchanged_counters_after_query(*owner,zero_before),"zero control-capacity request synchronized or changed real work counters");
            auto bad=shapes;--bad[0][0];
            reject_no_work([&]{owner->prepare_boundary_control_capacity(domain,bad);});
            bad=shapes;bad[0][4]=1;reject_no_work([&]{owner->prepare_boundary_control_capacity(domain,bad);});
            auto stale=domain;++stale[0].storage.value;
            reject_no_work([&]{owner->prepare_boundary_control_capacity(stale,shapes);});
            auto next=domain;next[0].slot=state::StateSlot::Next;
            reject_no_work([&]{owner->prepare_boundary_control_capacity(next,shapes);});
            const std::array duplicate{access,access};const std::array<std::array<std::size_t,6>,2> duplicate_shapes{extents,extents};
            reject_no_work([&]{owner->prepare_boundary_control_capacity(duplicate,duplicate_shapes);});
            reject_no_work([&]{owner->prepare_boundary_control_capacity({},{});}); // Omitted committed one-block domain.
            reject_no_work([&]{owner->prepare_boundary_control_capacity(domain,{});});
            select_device_probe(device_count);const auto before=owner->counters();
            owner->prepare_boundary_control_capacity(domain,shapes);const auto prepared=owner->counters();const int prepared_calls=calls;
            require_backend_device_selected("control capacity did not select its actual backend device");
            require(prepared.kernel_count==before.kernel_count&&prepared.getter_count==before.getter_count+1
                &&prepared.bytes_h2d==before.bytes_h2d&&prepared.bytes_d2h==before.bytes_d2h
                &&prepared.stream_sync_count>before.stream_sync_count,
                "new unbound capacity performed numerical/field work or missed its stream join");
            require(prepared_calls==initial_calls&&handler.stage_context_matches(stage),"capacity prewarm invoked a callback or changed BC context");
            const auto repeated_before=owner->counters();owner->prepare_boundary_control_capacity(domain,shapes);
            require(unchanged_counters_after_query(*owner,repeated_before),"sufficient control capacity performed extra stream/work operations");
            const auto warmed_fields=all_slots();for(int n=0;n<3;++n)
                require(same_bits(warmed_fields[n],initial_fields[n]),"capacity preparation changed a real physical-slot U/X/ENUC field");
            const auto macro_before=owner->counters();bool scope_failed=false;
            try {
                auto savepoint=owner->begin_macro_state_transaction(domain);
                reject_no_work([&]{owner->prepare_boundary_control_capacity(domain,shapes);});
                for(auto slot:{state::StateSlot::Current,state::StateSlot::Next,state::StateSlot::Scratch})produce(slot);
                const auto consumed=all_slots();const int ghost=grid.GetIndex(grid.Is()-1,grid.Js(),0);
                for(int n=0;n<3;++n)require(std::bit_cast<std::uint64_t>(consumed[n].enuc_rate[ghost])
                    ==std::bit_cast<std::uint64_t>(initial_fields[n].enuc_rate[donor]),
                    "three-slot normal BC producer failed to consume the corresponding real physical donor");
                owner->rotate_slots(access,cycle);savepoint->validate_storage();
                throw std::runtime_error("capacity publication rollback scope fault");
            }catch(const std::runtime_error& error) {
                scope_failed=std::string(error.what())=="capacity publication rollback scope fault";if(!scope_failed)throw;
            }
            require(scope_failed&&owner->counters().kernel_count>macro_before.kernel_count
                &&owner->counters().bytes_h2d>macro_before.bytes_h2d,"resident rollback erased real producer work counters");
            const auto restored=all_slots();for(int n=0;n<3;++n)
                require(same_bits(restored[n],initial_fields[n]),"capacity-backed failed macro did not restore real fields and slot permutation");
            {auto savepoint=owner->begin_macro_state_transaction(domain);
                for(auto slot:{state::StateSlot::Current,state::StateSlot::Next,state::StateSlot::Scratch})produce(slot);
                savepoint->validate_storage();savepoint->commit();}
            // Reuse is through the genuine producer, without a control getter.
            // Fields alone cannot prove private bound-control values/pointers.
            const auto enough_before=owner->counters();owner->prepare_boundary_control_capacity(domain,shapes);
            require(unchanged_counters_after_query(*owner,enough_before),"committed three-slot controls lost their warmed capacity");
            std::cout<<"NATIVE_CONTROL_CAPACITY species="<<count<<" join="<<prepared.stream_sync_count-before.stream_sync_count
                <<" callbacks_during_capacity="<<prepared_calls-initial_calls<<'\n';
        }
        for(auto role:{boundary::BoundaryPurpose::Hydro,boundary::BoundaryPurpose::Diffusion}) {
            purpose=role;handler.configure_stage(.375,role);block.fluid_state=seed;upload();
            auto reference=seed;calls=0;axis_calls={};auto host=handler.prepare_native(reference,grid);
            handler.validate_native_candidate(host,reference,grid);require(same_bits(reference,seed),"Host reference preparation wrote its seed");
            handler.publish_native_noexcept(std::move(host),reference);const auto expected_calls=axis_calls;
            calls=0;axis_calls={};select_device_probe(device_count);const auto before=owner->counters();
            auto candidate=handler.prepare_native_device(*owner,access,grid);const auto prepared=owner->counters();
            require_backend_device_selected("Native Device BC preparation did not select its real backend device");
            require(prepared.kernel_count-before.kernel_count==2,"ordered BC must use exactly two reflecting layers or two real role gathers");
            if(user) {
                const auto logical_cells=static_cast<std::uint64_t>(grid.GetTotalX())*grid.GetTotalY();
                require(axis_calls==expected_calls&&axis_calls[0]>0&&axis_calls[1]>0&&calls>2,
                    "ordered callback node count/prefix differs between Host and real Device preparation");
                require(prepared.bytes_d2h-before.bytes_d2h<=logical_cells*(6+count)*sizeof(double)
                    &&prepared.bytes_h2d-before.bytes_h2d<=logical_cells*sizeof(int),
                    "user preparation gathered beyond the unique real logical packed footprint");
            }
            require(same_bits(materialize(),seed),"read-only Device BC preparation changed a resident logical field");
            handler.validate_native_device_candidate(candidate,*owner,access,grid);
            const auto context=handler.snapshot_stage_context();handler.configure_stage(.5,role);
            reject_no_work([&]{handler.validate_native_device_candidate(candidate,*owner,access,grid);});handler.restore_stage_context_noexcept(context);
            auto stale=access;++stale.storage.value;
            reject_no_work([&]{handler.validate_native_device_candidate(candidate,*owner,stale,grid);});
            reject_no_work([&]{(void)handler.prepare_native_device(*owner,stale,grid);});
            auto other_grid=grid;reject_no_work([&]{handler.validate_native_device_candidate(candidate,*owner,access,other_grid);});
            const double root_upper=config.grid.x1_max;config.grid.x1_max=std::nextafter(root_upper,3.);
            reject_no_work([&]{handler.validate_native_device_candidate(candidate,*owner,access,grid);});config.grid.x1_max=root_upper;
            const double patch_upper=block.grid.x1_max;block.grid.x1_max=std::nextafter(patch_upper,3.);
            reject_no_work([&]{handler.validate_native_device_candidate(candidate,*owner,access,grid);});block.grid.x1_max=patch_upper;
            require(same_bits(materialize(),seed),"candidate frame rejection changed resident fields");
            const auto publish_before=owner->counters();handler.publish_native_device(std::move(candidate),*owner,access,grid);
            const auto published=owner->counters();require(published.kernel_count==publish_before.kernel_count+1,"ordered publication did not use one compact ghost scatter");
            const auto actual=materialize();
            for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j,0);const auto a=actual.get(cell),e=reference.get(cell);
                const std::array av{a.rho,a.mom_u,a.mom_v,a.mom_w,a.eng},ev{e.rho,e.mom_u,e.mom_v,e.mom_w,e.eng};
                for(std::size_t f=0;f<av.size();++f)close(av[f],ev[f]);
                require(std::bit_cast<std::uint64_t>(actual.enuc_rate[cell])==std::bit_cast<std::uint64_t>(reference.enuc_rate[cell]),"ordered publication changed exact source ENUC inheritance");
                for(int s=0;s<count;++s)close(actual.X(s,cell),reference.X(s,cell));
                if(i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je()) {
                    const auto u=seed.get(cell);const std::array original{u.rho,u.mom_u,u.mom_v,u.mom_w,u.eng};
                    for(std::size_t f=0;f<av.size();++f)require(std::bit_cast<std::uint64_t>(av[f])==std::bit_cast<std::uint64_t>(original[f]),"boundary publication wrote a real interior");
                    for(int k=0;k<count;++k)require(std::bit_cast<std::uint64_t>(actual.X(k,cell))==std::bit_cast<std::uint64_t>(seed.X(k,cell)),"boundary publication wrote real interior composition");
                }
            }
            std::cout<<"NATIVE_ORDERED_BC species="<<count<<" user="<<user<<" purpose="<<static_cast<int>(role)
                <<" prepare_kernels="<<prepared.kernel_count-before.kernel_count<<" h2d="<<prepared.bytes_h2d-before.bytes_h2d
                <<" d2h="<<prepared.bytes_d2h-before.bytes_d2h<<'\n';
            if(user)for(int failing:{1,2}) {
                block.fluid_state=seed;upload();fault=failing;calls=0;axis_calls={};
                require_rejected<std::runtime_error>([&]{(void)handler.prepare_native_device(*owner,access,grid);},"later sibling/axis callback fault was accepted");
                fault=0;require(same_bits(materialize(),seed),"failed later sibling/axis preparation partly published resident ghosts");
            }
            block.fluid_state=seed;upload();calls=0;axis_calls={};auto invalidated=handler.prepare_native_device(*owner,access,grid);
            handler.bind(eos,species);reject_no_work([&]{handler.validate_native_device_candidate(invalidated,*owner,access,grid);});
            require(same_bits(materialize(),seed),"binding drift wrote resident fields");
        }
    }
    // An actual internal dyadic patch has no physical surfaces even though its
    // root has four reflecting walls. Empty prepare/validate/publish is strictly
    // metadata-only: no species getter, controls, transfer, launch or sync.
    for(int count:{0,2}) {
        SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=config.grid.nblockx2=3;config.grid.nblockx3=0;
        config.grid.x1_min=1.;config.grid.x1_max=4.;config.grid.x2_min=0.;config.grid.x2_max=3.;
        config.numerics.sml_rho=config.numerics.min_eint=1.e-14;config.numerics.max_eint=1.e12;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="reflecting";
        SpeciesManager species;if(count){species.add_species("H1",1.,1.,1.4,1.);species.add_species("He4",4.,2.,1.4,1.);}
        IdealGas eos(1.4,species);boundary::ScopedUserBoundarySelection selection({},config,species);
        BCHandler handler(config,native);handler.bind(eos,species);handler.configure_stage(.375,boundary::BoundaryPurpose::Hydro);
        Grid root(amr::MAX_NG,1.,4.,0.,3.,0.,1.,3,3,1);root.geometry="cylindrical";root.dim=2;
        amr::Block block;block.Reset();block.active=true;block.logical_x1=block.logical_x2=1;
        block.InitGeometry(root,1./amr::BLOCK_NX,1./amr::BLOCK_NY,1.,native);block.RequireNativeGeometryIdentity();
        for(auto* field:{&block.fluid_state,&block.state_next,&block.state_scratch}){field->Preallocate(block.grid.GetTotalSize());field->InitSpecies(count);}
        const amr::BlockHandle handle{{3601},{36}};const backend::StorageGeneration generation{3601};
        auto launch=make_launch_config();launch.density_floor=config.numerics.sml_rho;
        launch.minimum_internal_energy=config.numerics.min_eint;launch.maximum_internal_energy=config.numerics.max_eint;
        auto owner=cuda::make_cuda_backend(block,handle,generation,kBackendDevice,launch,species,handler.logical_plan(),eos);
        const auto access=current(handle,generation);const auto before=owner->counters();
        auto candidate=handler.prepare_native_device(*owner,access,block.grid);
        handler.validate_native_device_candidate(candidate,*owner,access,block.grid);
        handler.publish_native_device(std::move(candidate),*owner,access,block.grid);
        require(unchanged_counters_after_query(*owner,before),"empty physical patch performed transfer/sync/control/getter/kernel work");
    }
}


/** Drive genuine committed Native Runtime boundaries through the real backend.
 * One axis/N0 builtin domain and one off-axis/N2 user domain retain independent
 * genuine Host Runtime references. Four role cases check the original numerical
 * window, exact ENUC/interiors, actual EOS-only cache and late callback rollback.
 * No macro/source/retry/production capability or allocation padding is tested.
 */
void run_native_runtime_boundary(int device_count)
{
    using namespace arch;using state::StateSlot;using state::StateRegion;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto close=[](double actual,double expected) {
        require(std::isfinite(actual)&&std::abs(actual-expected)
            <=64.*std::numeric_limits<double>::epsilon()*std::max(1.,std::abs(expected)),
            "Native Runtime boundary differs from its original Host 64-epsilon reference");
    };
    for(bool user:{false,true}) {
        const int count=user?2:0;
        SimConfig config;config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=user?2:1;config.grid.nblockx2=user?1:2;config.grid.nblockx3=0;
        config.grid.x1_min=user?1.:0.;config.grid.x1_max=user?3.:1.;
        config.grid.x2_min=user?-.5:-1.;config.grid.x2_max=user?.5:1.;
        config.grid.amr_max_blocks=8;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type=user?"user":"reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type=user?"user":"reflecting";
        config.numerics.sml_rho=config.numerics.min_eint=1.e-14;config.numerics.max_eint=1.e12;
        config.physics.burn.use_burn=false;
        config.physics.diffusion.use_diffusion=config.physics.diffusion.use_thermal_diffusion=true;
        config.physics.diffusion.use_viscous_diffusion=config.physics.diffusion.use_species_diffusion=false;
        config.physics.diffusion.alpha_therm=.01;
        SpeciesManager species;if(count){species.add_species("H1",1.,1.,1.4,1.);species.add_species("He4",4.,2.,1.4,1.);}
        IdealGas eos(1.4,species);const double cv=count?1.:IdealGasView::default_specific_heat_cv;
        double callback_time=0.;boundary::BoundaryPurpose purpose=boundary::BoundaryPurpose::Hydro;
        bool fault=false;std::atomic<int> calls{0},late_calls{0};std::array<std::atomic<int>,2> axis_calls{};
        boundary::ResolvedUserBoundaries callbacks;callbacks.identity="native-runtime-actual-committed-store";
        if(user)callbacks.physical=[&](const boundary::PhysicalBoundaryContext& context) {
            require(context.time==callback_time&&context.purpose==purpose,"Runtime callback lost physical stage time/purpose");
            ++calls;++axis_calls[static_cast<int>(context.axis)];
            if(fault&&context.axis==boundary::BoundaryAxis::X2&&context.ghost_point.r_cy>2.5) {
                ++late_calls;throw std::runtime_error("Native Runtime late callback fault");
            }
            PrimitiveData point;point.rho=1.+context.ghost_point.r_cy/64.;point.SetTemperature(10.);
            point.mass_fractions={.25,.75};boundary::PhysicalBoundaryData data;data.hydro=point;
            if(purpose==boundary::BoundaryPurpose::Diffusion)
                data.temperature={boundary::ScalarBoundaryKind::NormalGradient,0.};
            return data;
        };
        boundary::ScopedUserBoundarySelection selection(callbacks,config,species);
        amr::AMRControl host_control(8,2),control(8,2);
        /** Seed actual Native pool owners; the original mean law is shared with
         * the already-qualified ordered user fixture, not a numerical advance.
         */
        const auto seed=[&](amr::AMRControl& selected) {
            selected.tree->InitRootGrid(config,count,native);
            const auto& active=selected.tree->GetActiveBlocks();require(active.size()==2,"Runtime boundary lost its real two-root domain");
            for(std::size_t b=0;b<active.size();++b) {
                auto& block=selected.pool->GetBlock(active[b]);block.RequireNativeGeometryIdentity();const auto& g=block.grid;
                require(g.ng==amr::MAX_NG,"Runtime boundary lost its original full halo");
                for(auto* field:{&block.fluid_state,&block.state_next,&block.state_scratch}) {
                    field->stage_repairs.reset(count,state::RepairSemantics::RzVolumeAngular);
                    for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                        const int cell=g.GetIndex(i,j,0);const bool interior=i>=g.Is()&&i<g.Ie()&&j>=g.Js()&&j<g.Je();
                        const long double a=g.GetFacePosL(i),z=g.GetFacePosR(i),v=(z*z-a*a)/2.;
                        const double rho=user?(interior?double(1.L+(z*z*z-a*a*a)/(192.L*v)):4.):2.;
                        field->set(cell,{rho,0.,0.,0.,10.*cv*rho});
                        field->enuc_rate[cell]=(i+j)&1?10000.*b+100.*j+i:-0.;
                        for(int s=0;s<count;++s)field->X(s,cell)=s?.75:.25;
                    }
                }
            }
        };
        seed(host_control);seed(control);
        RunState start;start.repairs.reset(count,state::RepairSemantics::RzVolumeAngular);
        SimulationController host_counters(config,start),counters(config,start);
        BCHandler host_handler(config,native),handler(config,native);host_handler.bind(eos,species);handler.bind(eos,species);
        host_handler.configure_stage(0.,purpose);handler.configure_stage(0.,purpose);
        driver::DriverRuntime host_runtime(host_control,host_handler,config,species,host_counters);
        driver::DriverRuntime runtime(control,handler,config,species,counters);
        host_runtime.bind_native_rz_eos(eos);runtime.bind_native_rz_eos(eos);
        host_runtime.initialize_topology();runtime.initialize_topology();
        const auto topology=runtime.prepare_backend_bindings();std::vector<cuda::CudaBlockBinding> bindings;
        std::vector<FluidState> initial;
        for(const auto& entry:topology){bindings.push_back({entry.block,entry.handle,entry.storage,entry.physical_boundary});initial.push_back(entry.block->fluid_state);}
        auto launch=make_launch_config();launch.diffusion=DiffFlux::make_diffusion_config_view(config);
        launch.density_floor=config.numerics.sml_rho;
        launch.minimum_internal_energy=config.numerics.min_eint;launch.maximum_internal_energy=config.numerics.max_eint;
        runtime.install_backend(cuda::make_cuda_backend(bindings,kBackendDevice,launch,species,eos));runtime.upload_initial_state();
        auto* const backend=runtime.backend();const auto& active=control.tree->GetActiveBlocks();
        auto initial_context=runtime.stage_context();const auto initial_version=initial_context.ledger.inspect({runtime.handles().front(),StateSlot::Current}).interior.version;
        /** Observe only actual Current logical cells; allocation-only padding is
         * not a Device witness and Next/Scratch are never materialized here.
         */
        const auto materialize=[&] {
            auto result=initial;
            for(std::size_t b=0;b<result.size();++b)for(auto region:{StateRegion::Interior,StateRegion::Ghost})
                backend->enqueue_materialize_host_current(runtime.backend_access(b,StateSlot::Current),region,transfer_view(result[b]));
            backend->quiesce();return result;
        };
        const auto reference=[&] {
            host_handler.configure_stage(callback_time,purpose);calls=0;axis_calls[0]=0;axis_calls[1]=0;
            host_runtime.ensure_fluid_ghosts(StateSlot::Current);
            std::vector<FluidState> result;for(int id:host_control.tree->GetActiveBlocks())result.push_back(host_control.pool->GetBlock(id).fluid_state);
            return result;
        };
        const auto compare=[&](const std::vector<FluidState>& observed,const std::vector<FluidState>& expected) {
            require(observed.size()==expected.size()&&observed.size()==initial.size(),"Runtime boundary omitted a genuine domain");
            for(std::size_t b=0;b<observed.size();++b) {
                const auto& g=control.pool->GetBlock(active[b]).grid;
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                    const int cell=g.GetIndex(i,j,0);const auto a=observed[b].get(cell),e=expected[b].get(cell),original=initial[b].get(cell);
                    const std::array av{a.rho,a.mom_u,a.mom_v,a.mom_w,a.eng},ev{e.rho,e.mom_u,e.mom_v,e.mom_w,e.eng},iv{original.rho,original.mom_u,original.mom_v,original.mom_w,original.eng};
                    for(std::size_t f=0;f<av.size();++f)close(av[f],ev[f]);
                    require(std::bit_cast<std::uint64_t>(observed[b].enuc_rate[cell])==std::bit_cast<std::uint64_t>(expected[b].enuc_rate[cell]),"Runtime boundary changed exact ENUC donor inheritance");
                    const bool interior=i>=g.Is()&&i<g.Ie()&&j>=g.Js()&&j<g.Je();
                    for(int s=0;s<count;++s) {
                        close(observed[b].X(s,cell),expected[b].X(s,cell));
                        if(interior)require(std::bit_cast<std::uint64_t>(observed[b].X(s,cell))==std::bit_cast<std::uint64_t>(initial[b].X(s,cell)),"Runtime boundary wrote interior composition");
                    }
                    if(interior)for(std::size_t f=0;f<av.size();++f)
                        require(std::bit_cast<std::uint64_t>(av[f])==std::bit_cast<std::uint64_t>(iv[f]),"Runtime boundary wrote a true interior");
                }
            }
        };
        const auto require_publication=[&] {
            auto context=runtime.stage_context();
            for(const auto handle:runtime.handles()) {
                const auto coherence=context.ledger.inspect({handle,StateSlot::Current});
                require(coherence.interior.version==initial_version&&coherence.ghost_source_version==initial_version,
                    "Nondynamical Runtime boundary changed interior version or ghost provenance");
                context.ledger.require_readable({handle,StateSlot::Current},{state::ExecutionSide::Device,initial_version,true,true});
            }
            require(!runtime.active_runtime_state_transaction()&&!runtime.native_macro_retry_attempt()
                &&counters.step_count==0&&counters.t_current==0.,"Nondynamical Runtime boundary retained a macro/retry lease or advanced physics");
        };
        for(auto role:{boundary::BoundaryPurpose::Hydro,boundary::BoundaryPurpose::Diffusion}) {
            purpose=role;callback_time=role==boundary::BoundaryPurpose::Hydro?.375:.625;
            const auto expected=reference();const int expected_calls=calls.load();const std::array expected_axes{axis_calls[0].load(),axis_calls[1].load()};
            handler.configure_stage(callback_time,purpose);calls=0;axis_calls[0]=0;axis_calls[1]=0;
            select_device_probe(device_count);runtime.ensure_fluid_ghosts(StateSlot::Current);
            require_backend_device_selected("Native Runtime did not use its actual Device owner");
            require(calls.load()==expected_calls&&axis_calls[0].load()==expected_axes[0]&&axis_calls[1].load()==expected_axes[1],
                "Runtime callback node order/coverage differs from the genuine Host owner");
            require_publication();compare(materialize(),expected);
            auto context=runtime.stage_context();const auto owner=driver::RuntimeStateTransaction::snapshot_owner(runtime,context);
            const auto callbacks_before=calls.load();const auto before=backend->counters();
            runtime.ensure_fluid_ghosts(StateSlot::Current);const auto after=backend->counters();
            auto expected_counter=before;expected_counter.kernel_count+=2*active.size();
            expected_counter.bytes_d2h+=active.size()*(sizeof(int)+sizeof(RzThermodynamics::AcceptanceDiagnostic));
            ++expected_counter.stream_sync_count;expected_counter.getter_count+=3; // Internal before, original trace query, this observation.
            require(after==expected_counter&&calls.load()==callbacks_before,
                "Runtime cache repeated savepoint/capacity/callback/exchange work instead of only actual completed EOS");
            require(driver::RuntimeStateTransaction::owner_matches(runtime,context,owner),"Runtime cache changed exact ledger/clock/BC/budgets/owner");
            compare(materialize(),expected);
            std::cout<<"NATIVE_RUNTIME_BC species="<<count<<" user="<<user<<" purpose="<<static_cast<int>(role)
                <<" domains="<<active.size()<<" cache_eos_kernels="<<after.kernel_count-before.kernel_count<<'\n';
        }
        if(user) {
            purpose=boundary::BoundaryPurpose::Diffusion;callback_time=.875;const auto expected=reference();
            handler.configure_stage(callback_time,purpose);auto context=runtime.stage_context();
            const auto before_fields=materialize();const auto owner=driver::RuntimeStateTransaction::snapshot_owner(runtime,context);
            calls=0;late_calls=0;const auto before=backend->counters();fault=true;bool rejected=false;
            try {runtime.ensure_fluid_ghosts(StateSlot::Current);}
            catch(const std::runtime_error& error){rejected=std::string(error.what())=="Native Runtime late callback fault";if(!rejected)throw;}
            fault=false;const auto after=backend->counters();
            require(rejected&&late_calls.load()>0&&calls.load()>late_calls.load()&&after.kernel_count>before.kernel_count,
                "Late real user-domain callback did not fail after prior actual boundary work");
            require(driver::RuntimeStateTransaction::owner_matches(runtime,context,owner),"Failed real Runtime boundary changed ledger/clock/BC/budgets/lease");
            const auto restored=materialize();for(std::size_t b=0;b<restored.size();++b)
                require(same_bits(restored[b],before_fields[b]),"Late Runtime callback failure changed actual Current logical fields or observation padding");
            calls=0;runtime.ensure_fluid_ghosts(StateSlot::Current);require(calls.load()>0,"Failed Runtime boundary became a false cache hit");
            require_publication();compare(materialize(),expected);
            std::cout<<"NATIVE_RUNTIME_BC late_callback_rollback=1 successful_reuse=1 actual_work_preserved=1 PASS\n";
        }
    }
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
        run_region_transfers(device_count);
        run_store_lifecycle(device_count);
        run_native_completed_eos();
        run_native_reflecting_layers(device_count);
        run_resident_macro_savepoint(device_count);
        run_resident_diffusion_activity();
        run_native_final_axis_backend(device_count);
        run_native_ordered_device_boundary(device_count);
        run_native_runtime_boundary(device_count);
        std::cout << "CUDA store lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
