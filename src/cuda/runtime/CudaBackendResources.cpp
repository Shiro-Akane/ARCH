/**
 * @file CudaBackendResources.cpp
 * @brief CUDA stream, allocation, block-runtime, and owner lifetime control.
 */

#include "CudaBackendInternal.h"
#include "CudaBurnNetworkTypes.h"

#include "amr/Block.h"
#include "amr/BoundaryPlan.h"
#include "cuda/microphysics/common.h"
#include "grid/GridMetrics.h"
#include "physics/species/Species.h"

#include <atomic>
#include <string>

namespace arch::cuda {
namespace {

void validate_host_state_shape(
    const FluidState& state, int total, int species_count)
{
    const std::size_t size = static_cast<std::size_t>(total);
    if (state.block_total_size_ != total
        || state.GetNumSpecies() != species_count
        || state.rho.size() != size || state.mom_u.size() != size
        || state.mom_v.size() != size || state.mom_w.size() != size
        || state.eng.size() != size || state.enuc_rate.size() != size
        || state.mass_fractions.size()
            != size * static_cast<std::size_t>(species_count)) {
        throw std::invalid_argument("Host FluidState layout is inconsistent");
    }
}

constexpr bool same_block_handle(
    amr::BlockHandle left, amr::BlockHandle right) noexcept
{
    return left.uid.value == right.uid.value
        && left.epoch.value == right.epoch.value;
}

constexpr bool same_storage_generation(
    backend::StorageGeneration left,
    backend::StorageGeneration right) noexcept
{
    return left.value == right.value;
}

DeviceLayoutGeneration issue_device_layout_generation()
{
    static std::atomic<std::uint64_t> next{1};
    const std::uint64_t value = next.fetch_add(1, std::memory_order_relaxed);
    if (value == 0 || value == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("CUDA layout generation exhausted");
    return {value};
}

template <class Function>
void for_each_region_segment(
    const DeviceGridView& grid, state::StateRegion region,
    Function&& function)
{
    if (region != state::StateRegion::Interior
        && region != state::StateRegion::Ghost)
        throw std::invalid_argument("invalid state region");
    for (int k = 0; k < grid.total_z; ++k) {
        for (int j = 0; j < grid.total_y; ++j) {
            const bool active_row =
                k >= grid.ks && k < grid.ke
                && j >= grid.js && j < grid.je;
            if (region == state::StateRegion::Interior) {
                if (active_row)
                    function(grid.index(grid.is, j, k), grid.ie - grid.is);
            } else if (!active_row) {
                function(grid.index(0, j, k), grid.total_x);
            } else {
                if (grid.is > 0)
                    function(grid.index(0, j, k), grid.is);
                if (grid.ie < grid.total_x) {
                    function(
                        grid.index(grid.ie, j, k), grid.total_x - grid.ie);
                }
            }
        }
    }
}

struct BurnWorkspaceSizeVisitor {
    std::size_t bytes_per_cell = 0;

    template <class Registration>
    void operator()()
    {
        using Binding = typename dispatch::PolicyRegistration<
            Registration>::CudaBinding;
        if constexpr (!std::is_same_v<Binding, dispatch::AbsentBinding>
                      && !std::is_same_v<
                          Binding, dispatch::CudaNoNetworkBinding>) {
            using Network = burn_detail::NetworkTypeFor<Binding>;
            bytes_per_cell =
                sizeof(BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>);
        }
    }
};

} // namespace

[[noreturn]] void throw_cuda(cudaError_t error, const char* operation)
{
    throw std::runtime_error(
        std::string(operation) + ": " + cudaGetErrorString(error));
}

void check_cuda(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess) throw_cuda(error, operation);
}

void require_cuda_success_or_terminate(cudaError_t error) noexcept
{
    if (error != cudaSuccess) std::terminate();
}

void set_device_and_quiesce_or_terminate(
    int device_ordinal, cudaStream_t stream) noexcept
{
    require_cuda_success_or_terminate(cudaSetDevice(device_ordinal));
    if (stream != nullptr)
        require_cuda_success_or_terminate(cudaStreamSynchronize(stream));
}

std::size_t slot_index(state::StateSlot slot)
{
    const auto value = static_cast<std::uint8_t>(slot);
    if (value > static_cast<std::uint8_t>(state::StateSlot::Scratch))
        throw std::invalid_argument("invalid state slot");
    return value;
}

std::size_t burn_workspace_bytes(
    const dispatch::ResolvedExecutionPlan& plan, std::size_t active_cells)
{
    if (plan.network == dispatch::NetworkId::None
        && plan.ode_solver == dispatch::OdeSolverId::None
        && plan.linear_solver == dispatch::LinearSolverId::None)
        return 0;
    if (plan.linear_solver != dispatch::LinearSolverId::DenseLu)
        throw std::invalid_argument(
            "CUDA burn requires its registered DenseLU route");
    BurnWorkspaceSizeVisitor visitor;
    const bool found = dispatch::visit_policy<dispatch::NetworkPolicies>(
        plan.network, visitor);
    if (!found || visitor.bytes_per_cell == 0)
        throw std::invalid_argument(
            "CUDA burn network has no device workspace binding");
    const std::size_t workspace_count =
        plan.ode_solver == dispatch::OdeSolverId::BeNr ? 1 : active_cells;
    if (workspace_count > std::numeric_limits<std::size_t>::max()
                              / visitor.bytes_per_cell)
        throw std::overflow_error("CUDA burn workspace extent overflow");
    return workspace_count * visitor.bytes_per_cell;
}

StreamOwner::~StreamOwner()
{
    if (stream_ != nullptr) {
        set_device_and_quiesce_or_terminate(device_ordinal_, stream_);
        require_cuda_success_or_terminate(cudaStreamDestroy(stream_));
    }
}

void StreamOwner::create(int device_ordinal)
{
    if (stream_ != nullptr)
        throw std::logic_error("CUDA stream already exists");
    check_cuda(cudaSetDevice(device_ordinal), "cudaSetDevice");
    cudaStream_t created = nullptr;
    const cudaError_t status = cudaStreamCreate(&created);
    if (status != cudaSuccess) {
        if (created != nullptr)
            require_cuda_success_or_terminate(cudaStreamDestroy(created));
        throw_cuda(status, "cudaStreamCreate");
    }
    stream_ = created;
    device_ordinal_ = device_ordinal;
}

EventOwner::~EventOwner()
{
    release();
}

EventOwner::EventOwner(EventOwner&& other) noexcept
    : event_(std::exchange(other.event_, nullptr)),
      device_ordinal_(std::exchange(other.device_ordinal_, -1))
{
}

EventOwner& EventOwner::operator=(EventOwner&& other) noexcept
{
    if (this != &other) {
        release();
        event_ = std::exchange(other.event_, nullptr);
        device_ordinal_ = std::exchange(other.device_ordinal_, -1);
    }
    return *this;
}

void EventOwner::create(int device_ordinal)
{
    if (event_ != nullptr)
        throw std::logic_error("CUDA retirement event already exists");
    check_cuda(cudaSetDevice(device_ordinal),
               "select CUDA retirement event device");
    device_ordinal_ = device_ordinal;
    const cudaError_t status = cudaEventCreateWithFlags(
        &event_, cudaEventDisableTiming);
    if (status != cudaSuccess) {
        if (event_ != nullptr) {
            require_cuda_success_or_terminate(cudaEventDestroy(event_));
            event_ = nullptr;
        }
        device_ordinal_ = -1;
        throw_cuda(status, "create CUDA retirement event");
    }
}

void EventOwner::record(cudaStream_t stream)
{
    if (event_ == nullptr)
        throw std::logic_error("CUDA retirement event is unavailable");
    check_cuda(cudaSetDevice(device_ordinal_),
               "select CUDA retirement event device");
    check_cuda(cudaEventRecord(event_, stream),
               "record CUDA retirement event");
}

bool EventOwner::ready() const
{
    if (event_ == nullptr)
        throw std::logic_error("CUDA retirement event is unavailable");
    check_cuda(cudaSetDevice(device_ordinal_),
               "select CUDA retirement event device");
    const cudaError_t status = cudaEventQuery(event_);
    if (status == cudaSuccess) return true;
    if (status == cudaErrorNotReady) return false;
    throw_cuda(status, "query CUDA retirement event");
}

void EventOwner::release() noexcept
{
    if (event_ != nullptr) {
        require_cuda_success_or_terminate(cudaSetDevice(device_ordinal_));
        require_cuda_success_or_terminate(cudaEventDestroy(event_));
    }
    event_ = nullptr;
    device_ordinal_ = -1;
}

void DeviceStateStorage::allocate(int total, int count)
{
    if (total <= 0 || count < 0 || count > kMaxDeviceSpecies)
        throw std::invalid_argument("invalid device state shape");
    total_size = total;
    species_count = count;
    rho.allocate(total);
    mom_u.allocate(total);
    mom_v.allocate(total);
    mom_w.allocate(total);
    eng.allocate(total);
    enuc_rate.allocate(total);
    if (count > 0) {
        species.allocate(static_cast<std::size_t>(total)
                         * static_cast<std::size_t>(count));
    }
}

DeviceStateView DeviceStateStorage::view() const noexcept
{
    return {rho.get(), mom_u.get(), mom_v.get(), mom_w.get(), eng.get(),
            enuc_rate.get(), species.get(), total_size, species_count};
}

void DeviceAmrFluxSurfaceStorage::allocate(int cells, int species)
{
    const std::size_t scalars = amr_flux_surface_scalar_count(cells, species);
    if (scalars == 0)
        throw std::invalid_argument("invalid compact AMR flux surface");
    values.allocate(scalars);
    cell_count = cells;
    species_count = species;
}

DeviceAmrFluxSurfaceView DeviceAmrFluxSurfaceStorage::view() const noexcept
{
    return make_amr_flux_surface_view(
        values.get(), cell_count, species_count);
}

CudaBlockRuntime::CudaBlockRuntime(
    const amr::Block& block, amr::BlockHandle requested_handle,
    backend::StorageGeneration requested_generation,
    int expected_species_count, int device_ordinal,
    const boundary::BoundaryPlan& logical_boundary,
    const CudaLaunchConfig& launch, cudaStream_t stream)
    : handle(requested_handle), generation(requested_generation)
{
    if (device_ordinal < 0)
        throw std::invalid_argument("negative CUDA block device ordinal");
    check_cuda(cudaSetDevice(device_ordinal),
               "select CUDA block construction device");
    try {
        if (!amr::is_valid(handle) || !backend::is_valid(generation))
            throw std::invalid_argument("invalid backend identity");
        if (block.grid.geometry != "cartesian")
            throw std::invalid_argument(
                "CUDA E3 requires Cartesian geometry");
        const int total = block.grid.GetTotalSize();
        const int count = block.fluid_state.GetNumSpecies();
        if (count != expected_species_count || count > kMaxDeviceSpecies)
            throw std::invalid_argument("CUDA species registry mismatch");
        validate_host_state_shape(block.fluid_state, total, count);
        validate_host_state_shape(block.state_next, total, count);
        validate_host_state_shape(block.state_scratch, total, count);

        for (auto& storage : state_storage) storage.allocate(total, count);
        for (std::size_t slot = 0; slot < slots.size(); ++slot)
            slots[slot] = state_storage[slot].view();
        face_flux.allocate(total, count);
        hydro_delta.allocate(total, count);
        diffusion_delta.allocate(total, count);
        diffusion_initial_delta.allocate(total, count);

        const bool cache_initial_operator =
            launch.diffusion.use_diffusion
            && launch.plan.diffusion_integrator
                == dispatch::DiffusionIntegratorId::Rkl2;
        for (int face = 0; face < 2 * block.grid.dim; ++face) {
            const auto& neighbours = block.face_neighbors[face];
            if (neighbours.level_diff == 1) {
                amr_flux_register[face].allocate(
                    amr::flux_plan_detail::face_cell_count(
                        block.grid.dim,
                        static_cast<amr::AmrAxis>(face / 2)),
                    count);
            }
            if (cache_initial_operator && neighbours.count > 0
                && neighbours.level_diff != 0) {
                amr_initial_flux[face].allocate(
                    amr::flux_plan_detail::face_cell_count(
                        block.grid.dim,
                        static_cast<amr::AmrAxis>(face / 2)),
                    count);
            }
        }

        grid = make_device_grid_view(block.grid);
        if (!valid_hydro_grid(grid))
            throw std::invalid_argument("invalid CUDA grid layout");
        const int active = grid.active_cell_count();
        if (active <= 0)
            throw std::invalid_argument("CUDA block has no active cells");
        cfl_candidates.allocate(active);
        cfl_result.allocate(1);
        cfl_status.allocate(1);
        diffusion_dt_candidates.allocate(active);
        diffusion_dt_result.allocate(1);
        diffusion_status.allocate(1);
        if (launch.burn.use_burn) {
            burn_workspace_storage.allocate(burn_workspace_bytes(
                launch.plan, static_cast<std::size_t>(active)));
            burn_candidates.allocate(active);
            burn_statuses.allocate(active);
            burn_summary.allocate(1);
        }

        cell_volume.allocate(total);
        for (int axis = 0; axis < 3; ++axis) {
            face_area_lower[axis].allocate(total);
            face_area_upper[axis].allocate(total);
        }
        std::vector<double> volumes(total, 0.0);
        std::array<std::vector<double>, 3> lower{
            std::vector<double>(total, 0.0),
            std::vector<double>(total, 0.0),
            std::vector<double>(total, 0.0)};
        std::array<std::vector<double>, 3> upper{
            std::vector<double>(total, 0.0),
            std::vector<double>(total, 0.0),
            std::vector<double>(total, 0.0)};
        for (int k = block.grid.Ks(); k < block.grid.Ke(); ++k) {
            for (int j = block.grid.Js(); j < block.grid.Je(); ++j) {
                for (int i = block.grid.Is(); i < block.grid.Ie(); ++i) {
                    const int cell = block.grid.GetIndex(i, j, k);
                    volumes[cell] = GridMetrics::CellVolume(
                        block.grid, i, j, k);
                    for (int axis = 0; axis < block.grid.dim; ++axis) {
                        lower[axis][cell] = GridMetrics::FaceArea(
                            block.grid, axis, i, j, k, false);
                        upper[axis][cell] = GridMetrics::FaceArea(
                            block.grid, axis, i, j, k, true);
                    }
                }
            }
        }
        const std::size_t metric_bytes =
            static_cast<std::size_t>(total) * sizeof(double);
        check_cuda(cudaMemcpyAsync(
                       cell_volume.get(), volumes.data(), metric_bytes,
                       cudaMemcpyHostToDevice, stream),
                   "upload cell volume");
        grid.cell_volume = cell_volume.get();
        for (int axis = 0; axis < 3; ++axis) {
            check_cuda(cudaMemcpyAsync(
                           face_area_lower[axis].get(), lower[axis].data(),
                           metric_bytes, cudaMemcpyHostToDevice, stream),
                       "upload lower face area");
            check_cuda(cudaMemcpyAsync(
                           face_area_upper[axis].get(), upper[axis].data(),
                           metric_bytes, cudaMemcpyHostToDevice, stream),
                       "upload upper face area");
            grid.face_area_lower[axis] = face_area_lower[axis].get();
            grid.face_area_upper[axis] = face_area_upper[axis].get();
        }

        boundary = compile_boundary_plan(logical_boundary, grid);
        boundary_transfers.allocate(boundary.transfers.size());
        check_cuda(cudaMemcpyAsync(
                       boundary_transfers.get(), boundary.transfers.data(),
                       boundary.transfers.size()
                           * sizeof(DeviceBoundaryTransfer),
                       cudaMemcpyHostToDevice, stream),
                   "upload boundary transfers");
    } catch (...) {
        // Member destruction starts immediately after a constructor rethrow.
        set_device_and_quiesce_or_terminate(device_ordinal, stream);
        throw;
    }
}

DeviceStateView CudaBlockRuntime::require_access(
    backend::BackendStateAccess access) const
{
    if (!same_block_handle(access.block, handle)
        || !same_storage_generation(access.storage, generation))
        throw std::invalid_argument("stale CUDA backend access");
    return slots[slot_index(access.slot)];
}

std::uint64_t CudaBlockRuntime::copy_host_device_region(
    DeviceStateView device, const backend::HostStateTransferView& host,
    state::StateRegion region, cudaMemcpyKind direction,
    cudaStream_t stream)
{
    backend::validate_host_state_transfer_view(host);
    if (host.cell_count != static_cast<std::size_t>(grid.total_size)
        || host.species_count != static_cast<std::size_t>(device.n_species))
        throw std::invalid_argument("Host/device transfer shape mismatch");
    std::uint64_t copied = 0;
    const std::array<double*, 6> device_fields{
        device.rho, device.mom_u, device.mom_v, device.mom_w,
        device.eng, device.enuc_rate};
    const std::array<double*, 6> host_fields{
        host.rho, host.mom_u, host.mom_v, host.mom_w,
        host.eng, host.enuc_rate};
    for_each_region_segment(grid, region, [&](int offset, int count) {
        const std::size_t bytes =
            static_cast<std::size_t>(count) * sizeof(double);
        for (std::size_t field = 0; field < device_fields.size(); ++field) {
            void* destination = direction == cudaMemcpyHostToDevice
                ? static_cast<void*>(device_fields[field] + offset)
                : static_cast<void*>(host_fields[field] + offset);
            const void* source = direction == cudaMemcpyHostToDevice
                ? static_cast<const void*>(host_fields[field] + offset)
                : static_cast<const void*>(device_fields[field] + offset);
            check_cuda(cudaMemcpyAsync(
                           destination, source, bytes, direction, stream),
                       "enqueue state transfer");
            copied += bytes;
        }
        for (int species = 0; species < device.n_species; ++species) {
            double* device_pointer = device.mass_fractions
                + static_cast<std::size_t>(species) * device.total_size
                + offset;
            double* host_pointer = host.species
                + static_cast<std::size_t>(species) * host.species_stride
                + offset;
            void* destination = direction == cudaMemcpyHostToDevice
                ? static_cast<void*>(device_pointer)
                : static_cast<void*>(host_pointer);
            const void* source = direction == cudaMemcpyHostToDevice
                ? static_cast<const void*>(host_pointer)
                : static_cast<const void*>(device_pointer);
            check_cuda(cudaMemcpyAsync(
                           destination, source, bytes, direction, stream),
                       "enqueue species transfer");
            copied += bytes;
        }
    });
    return copied;
}

const CudaAmrFluxRouteStorage* CudaAmrFluxPlanRuntime::find_route(
    amr::BlockHandle source, int direction) const noexcept
{
    const auto found = route_index.find({source, direction});
    return found == route_index.end() ? nullptr
                                      : routes[found->second].get();
}

std::array<CudaAmrFluxDirectionRouteView, 3> make_cuda_amr_route_views(
    const CudaAmrFluxPlanRuntime* plan, amr::BlockHandle source)
{
    std::array<CudaAmrFluxDirectionRouteView, 3> result{};
    if (plan == nullptr) return result;
    for (int direction = 0; direction < plan->dimension; ++direction) {
        const auto* route = plan->find_route(source, direction);
        if (route == nullptr) continue;
        unsigned int source_faces = 0;
        for (const auto& target : route->compiled.targets) {
            if (target.source_face < 0 || target.source_face >= 6)
                throw std::invalid_argument(
                    "CUDA AMR source face is invalid");
            source_faces |= 1U << target.source_face;
        }
        result[direction] = {
            plan->device_blocks.get(),
            static_cast<int>(plan->host_blocks.size()),
            route->compiled.source_block, direction,
            route->targets.get(),
            static_cast<int>(route->compiled.targets.size()),
            route->terms.get(),
            static_cast<int>(route->compiled.terms.size()),
            source_faces};
    }
    return result;
}

std::unique_ptr<CudaAmrFluxPlanRuntime> make_cuda_amr_flux_plan_runtime(
    const amr::AmrFluxTopologyPlan& topology,
    const amr::RefluxPlan& reflux,
    std::span<const DeviceStoreEntry> entries,
    const std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>>&
        resources,
    int expected_species_count, bool cache_initial_operator,
    cudaStream_t stream)
{
    amr::validate_amr_flux_topology_plan(topology);
    amr::validate_amr_plan(reflux);
    if (stream == nullptr || entries.empty()
        || entries.size() != resources.size()
        || topology.active_endpoints.size() != entries.size()
        || topology.species_count != expected_species_count
        || reflux.dimension != topology.dimension
        || reflux.scope.from_epoch != topology.epoch
        || reflux.scope.to_epoch != topology.epoch)
        throw std::invalid_argument("invalid CUDA AMR flux plan inputs");

    std::map<amr::BlockHandle, CudaBlockRuntime*> runtimes_by_handle;
    for (const auto& entry : entries) {
        const auto found = resources.find(entry.arena);
        if (found == resources.end() || found->second == nullptr
            || found->second->handle != entry.record.handle
            || !runtimes_by_handle.emplace(
                entry.record.handle, found->second.get()).second)
            throw std::invalid_argument(
                "CUDA AMR flux resource namespace drifted");
    }

    auto result = std::make_unique<CudaAmrFluxPlanRuntime>();
    result->epoch = topology.epoch;
    result->topology_fingerprint = topology.fingerprint;
    result->reflux_fingerprint = reflux.fingerprint;
    result->dimension = topology.dimension;
    result->species_count = topology.species_count;
    result->runtime_blocks.reserve(topology.active_endpoints.size());
    result->host_blocks.reserve(topology.active_endpoints.size());
    std::vector<amr::AmrFluxEndpointBinding> bindings;
    bindings.reserve(topology.active_endpoints.size());
    for (std::size_t index = 0;
         index < topology.active_endpoints.size(); ++index) {
        const auto& endpoint = topology.active_endpoints[index];
        const auto found = runtimes_by_handle.find(endpoint.handle);
        if (found == runtimes_by_handle.end())
            throw std::invalid_argument(
                "CUDA AMR flux endpoint is not staged");
        CudaBlockRuntime& runtime = *found->second;
        if (runtime.grid.dim != topology.dimension
            || runtime.face_flux.species_count != topology.species_count)
            throw std::invalid_argument("CUDA AMR flux layout drifted");
        DeviceAmrFluxBlockView block{};
        block.state = runtime.slots[slot_index(state::StateSlot::Current)];
        block.stage_flux = runtime.face_flux.view();
        block.grid = runtime.grid;
        for (int face = 0; face < 6; ++face) {
            block.registers[face] = runtime.amr_flux_register[face].view();
            block.initial_flux[face] = runtime.amr_initial_flux[face].view();
        }
        result->runtime_blocks.push_back(&runtime);
        result->host_blocks.push_back(block);
        bindings.push_back({
            endpoint, static_cast<int>(index), topology.species_count,
            amr::AmrFluxGridLayout{
                runtime.grid.dim, runtime.grid.total_size,
                runtime.grid.stride_y, runtime.grid.stride_z,
                runtime.grid.is, runtime.grid.ie,
                runtime.grid.js, runtime.grid.je,
                runtime.grid.ks, runtime.grid.ke}});
    }

    const auto compiled = amr::compile_amr_flux_topology_plan(
        topology, bindings);
    result->surface_requirements =
        amr::build_amr_flux_surface_requirements(
            compiled, cache_initial_operator);
    for (const auto& requirement : result->surface_requirements) {
        if (requirement.block < 0
            || requirement.block
                >= static_cast<int>(result->host_blocks.size()))
            throw std::invalid_argument(
                "CUDA AMR surface block is outside staged namespace");
        const auto& block = result->host_blocks[
            static_cast<std::size_t>(requirement.block)];
        const bool needs_register =
            (requirement.roles
             & static_cast<std::uint8_t>(
                 amr::AmrFluxSurfaceRole::Register)) != 0;
        const bool needs_initial =
            (requirement.roles
             & static_cast<std::uint8_t>(
                 amr::AmrFluxSurfaceRole::InitialOperatorCache)) != 0;
        if ((needs_register
                && (!valid_amr_flux_surface(
                        block.registers[requirement.face])
                    || block.registers[requirement.face].cell_count
                        != requirement.cell_count))
            || (needs_initial
                && (!valid_amr_flux_surface(
                        block.initial_flux[requirement.face])
                    || block.initial_flux[requirement.face].cell_count
                        != requirement.cell_count)))
            throw std::invalid_argument(
                "CUDA AMR compact surface allocation is missing");
    }

    result->device_blocks.allocate(result->host_blocks.size());
    check_cuda(cudaMemcpyAsync(
                   result->device_blocks.get(), result->host_blocks.data(),
                   result->host_blocks.size()
                       * sizeof(DeviceAmrFluxBlockView),
                   cudaMemcpyHostToDevice, stream),
               "upload CUDA AMR flux block views");

    result->routes.reserve(compiled.routes.size());
    for (std::size_t index = 0; index < compiled.routes.size(); ++index) {
        const auto& lowered = compiled.routes[index];
        if (lowered.targets.empty() || lowered.terms.empty())
            throw std::invalid_argument("CUDA AMR flux route is empty");
        auto route = std::make_unique<CudaAmrFluxRouteStorage>();
        route->source = topology.routes[index].source.handle;
        route->direction = topology.routes[index].key.axis
            == amr::AmrAxis::X ? 0
            : (topology.routes[index].key.axis == amr::AmrAxis::Y ? 1 : 2);
        route->compiled = lowered;
        route->targets.allocate(lowered.targets.size());
        route->terms.allocate(lowered.terms.size());
        check_cuda(cudaMemcpyAsync(
                       route->targets.get(), lowered.targets.data(),
                       lowered.targets.size()
                           * sizeof(amr::AmrFluxRegistrationTarget),
                       cudaMemcpyHostToDevice, stream),
                   "upload CUDA AMR flux targets");
        check_cuda(cudaMemcpyAsync(
                       route->terms.get(), lowered.terms.data(),
                       lowered.terms.size()
                           * sizeof(amr::AmrFluxRegistrationTerm),
                       cudaMemcpyHostToDevice, stream),
                   "upload CUDA AMR flux terms");
        if (!result->route_index.emplace(
                std::pair{route->source, route->direction},
                result->routes.size()).second)
            throw std::invalid_argument("duplicate CUDA AMR flux route");
        result->routes.push_back(std::move(route));
    }

    result->compiled_reflux = amr::compile_amr_reflux_plan(
        reflux, bindings, topology.species_count);
    if (!result->compiled_reflux.targets.empty()) {
        if (result->compiled_reflux.contributions.empty())
            throw std::invalid_argument("CUDA AMR reflux has no contributions");
        result->reflux_targets.allocate(
            result->compiled_reflux.targets.size());
        result->reflux_contributions.allocate(
            result->compiled_reflux.contributions.size());
        check_cuda(cudaMemcpyAsync(
                       result->reflux_targets.get(),
                       result->compiled_reflux.targets.data(),
                       result->compiled_reflux.targets.size()
                           * sizeof(amr::AmrRefluxTarget),
                       cudaMemcpyHostToDevice, stream),
                   "upload CUDA AMR reflux targets");
        check_cuda(cudaMemcpyAsync(
                       result->reflux_contributions.get(),
                       result->compiled_reflux.contributions.data(),
                       result->compiled_reflux.contributions.size()
                           * sizeof(amr::AmrRefluxContribution),
                       cudaMemcpyHostToDevice, stream),
                   "upload CUDA AMR reflux contributions");
    }
    return result;
}

std::vector<DeviceStoreEntry> CudaBackend::Impl::make_initial_entries(
    std::span<const CudaBlockBinding> bindings)
{
    std::vector<DeviceStoreEntry> result;
    result.reserve(bindings.size());
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const auto& binding = bindings[index];
        if (binding.block == nullptr || binding.physical_boundary == nullptr)
            throw std::invalid_argument("null CUDA block binding");
        result.push_back({
            {binding.handle, binding.storage,
             issue_device_layout_generation()},
            DeviceArenaSlot{index + 1}});
    }
    return result;
}

CudaBackend::Impl::Impl(
    std::span<const CudaBlockBinding> bindings, int device,
    const CudaLaunchConfig& launch_config,
    const SpeciesManager& species)
    : device_ordinal(device), launch(launch_config),
      species_count(species.count()),
      initial_entries(make_initial_entries(bindings)),
      store(std::span<const DeviceStoreEntry>(initial_entries))
{
    if (device_ordinal < 0)
        throw std::invalid_argument("negative CUDA device ordinal");
    stream.create(device_ordinal);
    try {
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            const auto& binding = bindings[index];
            const auto& entry = initial_entries[index];
            auto [slot, inserted] =
                active_resources.try_emplace(entry.arena);
            if (!inserted)
                throw std::logic_error("duplicate initial CUDA arena");
            slot->second = std::make_unique<CudaBlockRuntime>(
                *binding.block, entry.record.handle, entry.record.storage,
                species_count, device_ordinal,
                *binding.physical_boundary, launch, stream.get());
        }
        checked_quiesce(
            "synchronize immutable block construction uploads");
    } catch (...) {
        // A failed constructor skips ~Impl; quiesce before member cleanup.
        set_device_and_quiesce_or_terminate(
            device_ordinal, stream.get());
        throw;
    }
}

CudaBackend::Impl::~Impl() noexcept
{
    // Member resources are destroyed after this body.
    quiesce_or_terminate();
}

void CudaBackend::Impl::select_device() const
{
    check_cuda(cudaSetDevice(device_ordinal),
               "select CUDA backend device");
}

void CudaBackend::Impl::checked_quiesce(const char* operation)
{
    select_device();
    check_cuda(cudaStreamSynchronize(stream.get()), operation);
    ++runtime_counters.stream_sync_count;
}

void CudaBackend::Impl::quiesce_or_terminate() noexcept
{
    set_device_and_quiesce_or_terminate(device_ordinal, stream.get());
    ++runtime_counters.stream_sync_count;
}

CudaBlockRuntime& CudaBackend::Impl::require_block(
    backend::BackendStateAccess access)
{
    const auto& entry = store.record(access);
    const auto found = active_resources.find(entry.arena);
    if (found == active_resources.end())
        throw std::logic_error("active CUDA arena resource is missing");
    return *found->second;
}

const CudaBlockRuntime& CudaBackend::Impl::require_block(
    backend::BackendStateAccess access) const
{
    const auto& entry = store.record(access);
    const auto found = active_resources.find(entry.arena);
    if (found == active_resources.end())
        throw std::logic_error("active CUDA arena resource is missing");
    return *found->second;
}

CudaBlockRuntime& CudaBackend::Impl::first_block() noexcept
{
    const auto arena = store.active_entries().front().arena;
    const auto found = active_resources.find(arena);
    if (found == active_resources.end()) std::terminate();
    return *found->second;
}

const CudaBlockRuntime& CudaBackend::Impl::first_block() const noexcept
{
    const auto arena = store.active_entries().front().arena;
    const auto found = active_resources.find(arena);
    if (found == active_resources.end()) std::terminate();
    return *found->second;
}

} // namespace arch::cuda
