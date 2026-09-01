#include "CudaBackend.h"
#include "CudaBackendBurn.h"
#include "CudaBurnNetworkTypes.h"
#include "CudaBackendDiffusion.h"
#include "CudaBackendExchange.h"
#include "CudaBackendHydro.h"
#include "DeviceBlockStore.h"

#include "amr/Block.h"
#include "amr/BoundaryPlan.h"
#include "amr/ExchangePlan.h"
#include "cuda/hydro/BoundaryPlan.h"
#include "cuda/microphysics/helm_eos_loader.h"
#include "grid/GridMetrics.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"
#include "physics/species/Species.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace arch::cuda {
namespace {

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

template <class T>
class DeviceAllocation {
public:
    DeviceAllocation() = default;
    ~DeviceAllocation() { release(); }
    DeviceAllocation(const DeviceAllocation&) = delete;
    DeviceAllocation& operator=(const DeviceAllocation&) = delete;
    DeviceAllocation(DeviceAllocation&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)),
          count_(std::exchange(other.count_, 0))
    {
    }
    DeviceAllocation& operator=(DeviceAllocation&&) = delete;

    void allocate(std::size_t count)
    {
        if (pointer_ != nullptr || count == 0
            || count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::invalid_argument("invalid CUDA allocation extent");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&pointer_),
                              count * sizeof(T)),
                   "cudaMalloc");
        count_ = count;
    }

    T* get() const noexcept { return pointer_; }
    std::size_t size() const noexcept { return count_; }

private:
    void release() noexcept
    {
        // Enclosing CUDA owners must establish the correct device and a
        // completion witness before destruction.  A release error is not a
        // recoverable leak signal: continuing would report false cleanup.
        if (pointer_ != nullptr)
            require_cuda_success_or_terminate(cudaFree(pointer_));
        pointer_ = nullptr;
        count_ = 0;
    }

    T* pointer_ = nullptr;
    std::size_t count_ = 0;
};

class StreamOwner {
public:
    StreamOwner() = default;
    ~StreamOwner()
    {
        if (stream_ != nullptr) {
            set_device_and_quiesce_or_terminate(device_ordinal_, stream_);
            require_cuda_success_or_terminate(cudaStreamDestroy(stream_));
        }
    }
    StreamOwner(const StreamOwner&) = delete;
    StreamOwner& operator=(const StreamOwner&) = delete;

    void create(int device_ordinal)
    {
        if (stream_ != nullptr)
            throw std::logic_error("CUDA stream already exists");
        check_cuda(cudaSetDevice(device_ordinal), "cudaSetDevice");
        cudaStream_t created = nullptr;
        const cudaError_t status = cudaStreamCreate(&created);
        if (status != cudaSuccess) {
            if (created != nullptr)
                require_cuda_success_or_terminate(
                    cudaStreamDestroy(created));
            throw_cuda(status, "cudaStreamCreate");
        }
        stream_ = created;
        device_ordinal_ = device_ordinal;
    }

    cudaStream_t get() const noexcept { return stream_; }

private:
    cudaStream_t stream_ = nullptr;
    int device_ordinal_ = -1;
};

class EventOwner {
public:
    EventOwner() = default;
    ~EventOwner() { release(); }
    EventOwner(const EventOwner&) = delete;
    EventOwner& operator=(const EventOwner&) = delete;
    EventOwner(EventOwner&& other) noexcept
        : event_(std::exchange(other.event_, nullptr)),
          device_ordinal_(std::exchange(other.device_ordinal_, -1))
    {
    }
    EventOwner& operator=(EventOwner&& other) noexcept
    {
        if (this != &other) {
            release();
            event_ = std::exchange(other.event_, nullptr);
            device_ordinal_ = std::exchange(other.device_ordinal_, -1);
        }
        return *this;
    }

    void create(int device_ordinal)
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

    void record(cudaStream_t stream)
    {
        if (event_ == nullptr)
            throw std::logic_error("CUDA retirement event is unavailable");
        check_cuda(cudaSetDevice(device_ordinal_),
                   "select CUDA retirement event device");
        check_cuda(cudaEventRecord(event_, stream),
                   "record CUDA retirement event");
    }

    bool ready() const
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

private:
    void release() noexcept
    {
        if (event_ != nullptr) {
            require_cuda_success_or_terminate(cudaSetDevice(device_ordinal_));
            require_cuda_success_or_terminate(cudaEventDestroy(event_));
        }
        event_ = nullptr;
        device_ordinal_ = -1;
    }

    cudaEvent_t event_ = nullptr;
    int device_ordinal_ = -1;
};

struct DeviceStateStorage {
    DeviceAllocation<double> rho;
    DeviceAllocation<double> mom_u;
    DeviceAllocation<double> mom_v;
    DeviceAllocation<double> mom_w;
    DeviceAllocation<double> eng;
    DeviceAllocation<double> enuc_rate;
    DeviceAllocation<double> species;
    int total_size = 0;
    int species_count = 0;

    void allocate(int total, int count)
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

    DeviceStateView view() const noexcept
    {
        return {rho.get(), mom_u.get(), mom_v.get(), mom_w.get(), eng.get(),
                enuc_rate.get(), species.get(), total_size, species_count};
    }
};

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

std::size_t slot_index(state::StateSlot slot)
{
    const auto value = static_cast<std::uint8_t>(slot);
    if (value > static_cast<std::uint8_t>(state::StateSlot::Scratch))
        throw std::invalid_argument("invalid state slot");
    return value;
}

bool complete_token(state::CompletionToken token) noexcept
{
    return state::is_complete(token);
}

bool same_hydro_descriptor(
    const scheduler::StageDescriptor& left,
    const scheduler::StageDescriptor& right) noexcept
{
    return left.stage == right.stage
        && left.old_slot == right.old_slot
        && left.input_slot == right.input_slot
        && left.output_slot == right.output_slot
        && left.old_weight == right.old_weight
        && left.update_weight == right.update_weight
        && left.flux_register_weight == right.flux_register_weight
        && left.input_requires_ghost == right.input_requires_ghost
        && left.refresh_ghost_after == right.refresh_ghost_after;
}

scheduler::HydroMethod hydro_method(
    dispatch::TimeIntegratorId integrator)
{
    using dispatch::TimeIntegratorId;
    switch (integrator) {
    case TimeIntegratorId::Euler: return scheduler::HydroMethod::Euler;
    case TimeIntegratorId::Rk2: return scheduler::HydroMethod::RK2;
    case TimeIntegratorId::Rk3: return scheduler::HydroMethod::RK3;
    }
    throw std::invalid_argument("invalid resolved Hydro integrator");
}

bool same_rkl_descriptor(
    const scheduler::RklStageDescriptor& left,
    const scheduler::RklStageDescriptor& right) noexcept
{
    return left.stage == right.stage
        && left.state_n_slot == right.state_n_slot
        && left.previous_slot == right.previous_slot
        && left.older_slot == right.older_slot
        && left.output_slot == right.output_slot
        && left.reflux_before_publish == right.reflux_before_publish
        && left.refresh_ghost_after == right.refresh_ghost_after;
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

template <class Function>
decltype(auto) visit_eos(
    std::variant<std::monostate, IdealGasView, HelmEosView,
                 Tabular3DEOSView, Tabular4DEOSView>& eos,
    Function&& function)
{
    return std::visit(
        [&]<class Eos>(Eos& value) -> decltype(auto) {
            if constexpr (std::is_same_v<Eos, std::monostate>)
                throw std::logic_error("CUDA EOS owner is unavailable");
            else
                return function(value);
        },
        eos);
}

constexpr dispatch::EosId host_eos_id(const IdealGas&) noexcept
{
    return dispatch::EosId::Ideal;
}

constexpr dispatch::EosId host_eos_id(const HelmEos&) noexcept
{
    return dispatch::EosId::Helmholtz;
}

constexpr dispatch::EosId host_eos_id(
    const Tabular3DEOSHostView&) noexcept
{
    return dispatch::EosId::Tabular3D;
}

constexpr dispatch::EosId host_eos_id(
    const Tabular4DEOSHostView&) noexcept
{
    return dispatch::EosId::Tabular4D;
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

} // namespace

struct CudaBlockRuntime {
    amr::BlockHandle handle;
    backend::StorageGeneration generation;
    std::array<DeviceStateStorage, 3> state_storage;
    std::array<DeviceStateView, 3> slots{};
    DeviceStateStorage face_flux;
    DeviceStateStorage hydro_delta;
    DeviceStateStorage diffusion_delta;
    DeviceStateStorage diffusion_initial_delta;
    DeviceAllocation<double> cfl_candidates;
    DeviceAllocation<double> cfl_result;
    DeviceAllocation<int> cfl_status;
    DeviceAllocation<double> diffusion_dt_candidates;
    DeviceAllocation<double> diffusion_dt_result;
    DeviceAllocation<int> diffusion_status;
    DeviceAllocation<std::byte> burn_workspace_storage;
    DeviceAllocation<reduction::ReductionCandidate> burn_candidates;
    DeviceAllocation<int> burn_statuses;
    DeviceAllocation<DeviceBurnSummary> burn_summary;
    DeviceAllocation<double> cell_volume;
    std::array<DeviceAllocation<double>, 3> face_area_lower;
    std::array<DeviceAllocation<double>, 3> face_area_upper;
    DeviceGridView grid{};
    DeviceCompiledBoundaryPlan boundary;
    DeviceAllocation<DeviceBoundaryTransfer> boundary_transfers;
    CudaBlockRuntime(
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
            // Member destruction begins as soon as this constructor rethrows.
            // Quiesce first so no partially built allocation can be released
            // while its upload is still in flight.
            set_device_and_quiesce_or_terminate(device_ordinal, stream);
            throw;
        }
    }

    DeviceStateView require_access(backend::BackendStateAccess access) const
    {
        if (!same_block_handle(access.block, handle)
            || !same_storage_generation(access.storage, generation))
            throw std::invalid_argument("stale CUDA backend access");
        return slots[slot_index(access.slot)];
    }

    template <class Function>
    void for_each_region_segment(state::StateRegion region, Function&& function)
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
                    if (grid.is > 0) function(grid.index(0, j, k), grid.is);
                    if (grid.ie < grid.total_x) {
                        function(grid.index(grid.ie, j, k),
                                 grid.total_x - grid.ie);
                    }
                }
            }
        }
    }

    std::uint64_t copy_host_device_region(
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
        for_each_region_segment(region, [&](int offset, int count) {
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
                               destination, source, bytes, direction,
                               stream),
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
                               destination, source, bytes, direction,
                               stream),
                           "enqueue species transfer");
                copied += bytes;
            }
        });
        return copied;
    }
};

struct CudaBackend::Impl {
    struct RetiredCudaResources {
        DeviceRetirementFence fence{};
        EventOwner event;
        std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>> resources;
    };

    int device_ordinal;
    CudaLaunchConfig launch;
    StreamOwner stream;
    std::unique_ptr<DeviceSpeciesOwner> species_owner;
    std::unique_ptr<HelmEosDeviceOwner> helm_owner;
    std::unique_ptr<Tabular3DEOSDeviceOwner> tabular3_owner;
    std::unique_ptr<Tabular4DEOSDeviceOwner> tabular4_owner;
    SpeciesPODView species_view{};
    std::variant<std::monostate, IdealGasView, HelmEosView,
                 Tabular3DEOSView, Tabular4DEOSView> eos;
    backend::BackendCounters runtime_counters{};
    std::vector<backend::BackendTraceRecord> runtime_trace;
    int species_count = 0;
    std::uint64_t immutable_owner_constructions = 0;
    std::size_t staged_resource_count = 0;
    std::vector<DeviceStoreEntry> initial_entries;
    DeviceBlockStoreLifecycle store;
    std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>>
        active_resources;
    std::list<RetiredCudaResources> retired_resources;

    static std::vector<DeviceStoreEntry> make_initial_entries(
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

    Impl(std::span<const CudaBlockBinding> bindings, int device,
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
                // Allocate the host map node before starting asynchronous
                // device construction.  Installing the completed unique_ptr
                // into this null slot is noexcept.
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
            // A failed Impl constructor skips ~Impl and immediately destroys
            // active_resources.  Make that destruction safe first.
            set_device_and_quiesce_or_terminate(
                device_ordinal, stream.get());
            throw;
        }
    }

    ~Impl() noexcept
    {
        // Member resources are destroyed after this body.  A failed device
        // selection or synchronization cannot safely fall through to free.
        quiesce_or_terminate();
    }

    void select_device() const
    {
        check_cuda(cudaSetDevice(device_ordinal),
                   "select CUDA backend device");
    }

    void checked_quiesce(const char* operation)
    {
        select_device();
        check_cuda(cudaStreamSynchronize(stream.get()), operation);
        ++runtime_counters.stream_sync_count;
    }

    void quiesce_or_terminate() noexcept
    {
        set_device_and_quiesce_or_terminate(device_ordinal, stream.get());
        ++runtime_counters.stream_sync_count;
    }

    CudaBlockRuntime& require_block(backend::BackendStateAccess access)
    {
        const auto& entry = store.record(access);
        const auto found = active_resources.find(entry.arena);
        if (found == active_resources.end())
            throw std::logic_error("active CUDA arena resource is missing");
        return *found->second;
    }

    const CudaBlockRuntime& require_block(
        backend::BackendStateAccess access) const
    {
        const auto& entry = store.record(access);
        const auto found = active_resources.find(entry.arena);
        if (found == active_resources.end())
            throw std::logic_error("active CUDA arena resource is missing");
        return *found->second;
    }

    CudaBlockRuntime& first_block() noexcept
    {
        const auto arena = store.active_entries().front().arena;
        const auto found = active_resources.find(arena);
        if (found == active_resources.end()) std::terminate();
        return *found->second;
    }
    const CudaBlockRuntime& first_block() const noexcept
    {
        const auto arena = store.active_entries().front().arena;
        const auto found = active_resources.find(arena);
        if (found == active_resources.end()) std::terminate();
        return *found->second;
    }

    void initialize_eos(const IdealGas& host, const SpeciesManager& species)
    {
        if (immutable_owner_constructions != 0)
            throw std::logic_error("CUDA immutable owner already exists");
        species_owner = std::make_unique<DeviceSpeciesOwner>(
            species, stream.get());
        species_view = species_owner->view();
        eos = species_owner->ideal_gas_view(host.global_gamma);
        finish_eos_upload("upload Ideal EOS");
        ++immutable_owner_constructions;
    }

    void initialize_eos(const HelmEos& host, const SpeciesManager&)
    {
        if (immutable_owner_constructions != 0)
            throw std::logic_error("CUDA immutable owner already exists");
        helm_owner = std::make_unique<HelmEosDeviceOwner>(host, stream.get());
        const HelmEosView view = helm_owner->view();
        species_view = view.specs;
        eos = view;
        finish_eos_upload("upload Helm EOS");
        ++immutable_owner_constructions;
    }

    void initialize_eos(
        const Tabular3DEOSHostView& host, const SpeciesManager&)
    {
        if (immutable_owner_constructions != 0)
            throw std::logic_error("CUDA immutable owner already exists");
        tabular3_owner = std::make_unique<Tabular3DEOSDeviceOwner>(
            host, stream.get());
        const Tabular3DEOSView view = tabular3_owner->view();
        species_view = view.specs;
        eos = view;
        finish_eos_upload("upload Tabular3 EOS");
        ++immutable_owner_constructions;
    }

    void initialize_eos(
        const Tabular4DEOSHostView& host, const SpeciesManager&)
    {
        if (immutable_owner_constructions != 0)
            throw std::logic_error("CUDA immutable owner already exists");
        tabular4_owner = std::make_unique<Tabular4DEOSDeviceOwner>(
            host, stream.get());
        const Tabular4DEOSView view = tabular4_owner->view();
        species_view = view.specs;
        eos = view;
        finish_eos_upload("upload Tabular4 EOS");
        ++immutable_owner_constructions;
    }

    void finish_eos_upload(const char* operation)
    {
        checked_quiesce(operation);
    }
};

struct CudaBackend::StoreTransaction::Impl {
    static constexpr std::uint8_t kInteriorUploaded = 1U;
    static constexpr std::uint8_t kGhostUploaded = 2U;
    static constexpr std::uint8_t kCompleteCurrent =
        kInteriorUploaded | kGhostUploaded;

    std::shared_ptr<CudaBackend::Impl> owner;
    DeviceBlockStoreLifecycle::Candidate candidate;
    std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>> resources;
    std::map<DeviceArenaSlot, std::uint8_t> uploaded_current;
    bool upload_failed = false;
    bool consumed = false;

    Impl(std::shared_ptr<CudaBackend::Impl> requested_owner,
         DeviceBlockStoreLifecycle::Candidate requested_candidate,
         std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>>
             requested_resources,
         std::map<DeviceArenaSlot, std::uint8_t> requested_uploaded_current)
        noexcept
        : owner(std::move(requested_owner)),
          candidate(std::move(requested_candidate)),
          resources(std::move(requested_resources)),
          uploaded_current(std::move(requested_uploaded_current))
    {
    }

    bool current_upload_complete() const noexcept
    {
        if (upload_failed || uploaded_current.size() != resources.size())
            return false;
        return std::all_of(
            uploaded_current.begin(), uploaded_current.end(),
            [](const auto& entry) {
                return entry.second == kCompleteCurrent;
            });
    }
};

CudaBackend::StoreTransaction::StoreTransaction(
    std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation))
{
    if (!impl_)
        throw std::invalid_argument(
            "CUDA store transaction implementation is null");
}

CudaBackend::StoreTransaction::~StoreTransaction()
{
    if (!impl_ || impl_->consumed || !impl_->owner) return;
    // Failure to select/quiesce means staged allocations may still be in use.
    // Do not proceed into logical abort or resource destruction in that state.
    impl_->owner->quiesce_or_terminate();
    try {
        impl_->owner->store.abort(std::move(impl_->candidate));
    } catch (...) {
        // A transaction owns the exact live candidate for this store.  Abort
        // can fail only after an internal ownership/reentrancy invariant has
        // been violated; silently continuing would strand the staged ID.
        std::terminate();
    }
    impl_->resources.clear();
    impl_->uploaded_current.clear();
    impl_->owner->staged_resource_count = 0;
    impl_->consumed = true;
}

CudaBackend::StoreTransaction::StoreTransaction(
    StoreTransaction&& other) noexcept = default;

std::span<const DeviceStoreEntry>
CudaBackend::StoreTransaction::entries() const
{
    if (!impl_ || impl_->consumed)
        throw std::logic_error("CUDA store transaction is no longer usable");
    return impl_->candidate.entries();
}

amr::AmrPlanScope CudaBackend::StoreTransaction::scope() const
{
    if (!impl_ || impl_->consumed)
        throw std::logic_error("CUDA store transaction is no longer usable");
    return impl_->candidate.scope();
}

CudaBackend::CudaBackend(std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation))
{
    if (!impl_) throw std::invalid_argument("CUDA backend implementation is null");
}

CudaBackend::~CudaBackend() = default;

state::ExecutionSide CudaBackend::side() const noexcept
{
    return state::ExecutionSide::Device;
}

amr::BlockHandle CudaBackend::block_handle() const noexcept
{
    return impl_->first_block().handle;
}

backend::StorageGeneration CudaBackend::storage_generation() const noexcept
{
    return impl_->first_block().generation;
}

bool CudaBackend::contains(
    backend::BackendStateAccess access) const noexcept
{
    return impl_->store.contains(access);
}

double CudaBackend::compute_hydro_dt(
    backend::BackendStateAccess current, double cfl)
{
    auto& block = impl_->require_block(current);
    const DeviceStateView state = block.require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("Hydro dt requires Current");
    const CudaHydroWorkspaceView workspace{
        block.face_flux.view(), block.hydro_delta.view(),
        block.cfl_candidates.get(), block.cfl_result.get(),
        block.cfl_status.get()};
    cudaError_t launch_error = cudaSuccess;
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch_error = launch_cuda_backend_hydro_dt(
            state, block.grid, eos, cfl, workspace, impl_->stream.get());
    });
    check_cuda(launch_error, "launch Hydro dt");
    double result = 0.0;
    int status = static_cast<int>(reduction::ReductionStatus::Empty);
    check_cuda(cudaMemcpyAsync(
                   &result, block.cfl_result.get(), sizeof(double),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download Hydro dt");
    check_cuda(cudaMemcpyAsync(
                   &status, block.cfl_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download Hydro CFL reduction status");
    quiesce();
    impl_->runtime_counters.kernel_count += 2;
    impl_->runtime_counters.bytes_d2h += sizeof(double) + sizeof(int);
    if (status != static_cast<int>(reduction::ReductionStatus::Ok))
        throw std::runtime_error("Invalid CUDA hydro CFL reduction");
    return result;
}

state::CompletionToken CudaBackend::execute_hydro_stage(
    backend::BackendStateAccess current,
    const scheduler::StageDescriptor& descriptor,
    double dt, state::CompletionToken expected)
{
    auto& block = impl_->require_block(current);
    static_cast<void>(block.require_access(current));
    const scheduler::HydroPlan plan = scheduler::make_hydro_plan(
        hydro_method(impl_->launch.plan.time_integrator));
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !same_hydro_descriptor(
            descriptor, plan.stages[descriptor.stage - 1]))
        throw std::invalid_argument("invalid Hydro stage contract");
    const DeviceStateView old_state = block.slots[slot_index(descriptor.old_slot)];
    const DeviceStateView input = block.slots[slot_index(descriptor.input_slot)];
    const DeviceStateView output = block.slots[slot_index(descriptor.output_slot)];
    CudaBackendLaunchResult launch{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch = launch_cuda_backend_hydro_stage(
            impl_->launch.plan, old_state, input, output,
            block.hydro_delta.view(), block.face_flux.view(), block.grid, eos,
            impl_->launch.entropy_fix_coefficient,
            impl_->launch.density_floor,
            impl_->launch.minimum_internal_energy,
            impl_->launch.maximum_internal_energy,
            descriptor, dt, impl_->stream.get());
        });
    if (!launch.route_found)
        throw std::logic_error("CUDA Hydro route is unavailable");
    check_cuda(launch.error, "launch Hydro stage");
    quiesce();
    impl_->runtime_counters.kernel_count +=
        static_cast<std::uint64_t>(launch.kernels_launched);
    return expected;
}

state::CompletionToken CudaBackend::execute_physical_boundary(
    backend::BackendStateAccess access, state::StateVersion version,
    state::CompletionToken expected)
{
    auto& block = impl_->require_block(access);
    const DeviceStateView selected = block.require_access(access);
    if (!state::is_valid(version) || !complete_token(expected))
        throw std::invalid_argument("invalid boundary completion contract");
    check_cuda(launch_cuda_backend_boundary_plan(
                   selected, block.boundary_transfers.get(),
                   block.boundary, impl_->stream.get()),
               "launch boundary plan");
    quiesce();
    for (const auto& phase : block.boundary.phases)
        if (phase.count > 0) ++impl_->runtime_counters.kernel_count;
    return expected;
}

state::CompletionToken CudaBackend::execute_same_level_exchange(
    std::span<const backend::BackendStateAccess> accesses,
    const amr::SameLevelExchangePlan& plan, state::StateSlot slot,
    state::StateVersion source_version,
    state::CompletionToken expected)
{
    if (!state::is_valid(source_version) || !complete_token(expected)
        || !amr::is_valid(plan.epoch) || plan.fingerprint == 0
        || amr::compute_same_level_exchange_fingerprint(plan)
            != plan.fingerprint
        || accesses.empty() || accesses.size() != plan.blocks.size())
        throw std::invalid_argument("invalid CUDA same-level exchange contract");

    std::map<amr::BlockHandle, int> indices;
    std::vector<DeviceExchangeBlock> host_blocks;
    host_blocks.reserve(accesses.size());
    int species_count = -1;
    for (const auto& access : accesses) {
        if (access.slot != slot
            || access.block.epoch.value != plan.epoch.value)
            throw std::invalid_argument("stale CUDA exchange access");
        auto& block = impl_->require_block(access);
        if (!indices.emplace(access.block, static_cast<int>(host_blocks.size())).second)
            throw std::invalid_argument("duplicate CUDA exchange access");
        const DeviceStateView selected = block.require_access(access);
        if (species_count >= 0 && selected.n_species != species_count)
            throw std::invalid_argument("CUDA exchange species counts differ");
        species_count = selected.n_species;
        host_blocks.push_back({selected, block.grid});
    }
    for (const auto& endpoint : plan.blocks) {
        if (!amr::is_valid(endpoint.handle)
            || endpoint.handle.epoch.value != plan.epoch.value
            || indices.find(endpoint.handle) == indices.end())
            throw std::invalid_argument("CUDA exchange endpoint is missing");
    }

    struct PreparedExchangePhase {
        std::vector<DeviceExchangeOperation> operations;
        std::uint64_t cells = 0;
        DeviceAllocation<DeviceExchangeOperation> device_operations;
        DeviceAllocation<double> scratch;
    };
    std::array<PreparedExchangePhase, 3> prepared_phases{};
    const std::uint64_t field_count =
        static_cast<std::uint64_t>(6 + species_count);
    if (field_count < 6
        || field_count > static_cast<std::uint64_t>(
            std::numeric_limits<int>::max()))
        throw std::overflow_error("CUDA exchange field count overflow");
    std::size_t expected_first = 0;
    for (std::size_t phase_index = 0; phase_index < plan.phases.size(); ++phase_index) {
        const auto phase = plan.phases[phase_index];
        if (phase.id != static_cast<amr::ExchangePhaseId>(phase_index)
            || phase.first != expected_first
            || phase.first > plan.operations.size()
            || phase.count > plan.operations.size() - phase.first
            || phase.count > static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
            throw std::invalid_argument("invalid CUDA exchange phase metadata");
        expected_first += phase.count;
        if (phase.count == 0) continue;

        auto& prepared = prepared_phases[phase_index];
        prepared.operations.reserve(phase.count);
        for (std::size_t offset = 0; offset < phase.count; ++offset) {
            const auto& operation = plan.operations[phase.first + offset];
            if (operation.ordinal != phase.first + offset
                || operation.phase != phase.id
                || operation.source_box.extent
                    != operation.destination_box.extent)
                throw std::invalid_argument("invalid CUDA exchange operation");
            const auto source = indices.find(operation.source.handle);
            const auto destination = indices.find(operation.destination.handle);
            if (source == indices.end() || destination == indices.end())
                throw std::invalid_argument("stale CUDA exchange operation");
            const auto validate_box = [&](const amr::LogicalExchangeBox& box,
                                          const DeviceGridView& grid) {
                for (int axis = 0; axis < 3; ++axis) {
                    const std::int64_t origin = axis == 0 ? grid.is
                        : (axis == 1 ? grid.js : grid.ks);
                    const std::int64_t total = axis == 0 ? grid.total_x
                        : (axis == 1 ? grid.total_y : grid.total_z);
                    const std::int64_t first = origin + box.first[axis];
                    const std::int64_t end = first + box.extent[axis];
                    if (box.extent[axis] == 0 || first < 0 || end > total)
                        throw std::out_of_range(
                            "CUDA exchange box is outside block layout");
                }
            };
            validate_box(operation.source_box, host_blocks[source->second].grid);
            validate_box(
                operation.destination_box,
                host_blocks[destination->second].grid);
            const std::uint64_t cells =
                amr::exchange_detail::checked_product(
                    operation.source_box.extent);
            if (prepared.cells
                > std::numeric_limits<std::uint64_t>::max() - cells)
                throw std::overflow_error("CUDA exchange phase size overflow");
            DeviceExchangeOperation compiled{};
            compiled.source_block = source->second;
            compiled.destination_block = destination->second;
            compiled.scratch_first = prepared.cells;
            for (int axis = 0; axis < 3; ++axis) {
                compiled.source_first[axis] = operation.source_box.first[axis];
                compiled.destination_first[axis] =
                    operation.destination_box.first[axis];
                compiled.extent[axis] = operation.source_box.extent[axis];
            }
            prepared.operations.push_back(compiled);
            prepared.cells += cells;
        }
        if (prepared.cells
            > std::numeric_limits<std::size_t>::max() / field_count)
            throw std::overflow_error("CUDA exchange scratch size overflow");
    }
    if (expected_first != plan.operations.size())
        throw std::invalid_argument("CUDA exchange phases do not cover plan");

    DeviceAllocation<DeviceExchangeBlock> device_blocks;
    device_blocks.allocate(host_blocks.size());
    check_cuda(cudaMemcpyAsync(
                   device_blocks.get(), host_blocks.data(),
                   host_blocks.size() * sizeof(DeviceExchangeBlock),
                   cudaMemcpyHostToDevice, impl_->stream.get()),
               "upload CUDA exchange blocks");
    for (auto& prepared : prepared_phases) {
        if (prepared.operations.empty()) continue;
        prepared.device_operations.allocate(prepared.operations.size());
        prepared.scratch.allocate(static_cast<std::size_t>(
            prepared.cells * field_count));
        check_cuda(cudaMemcpyAsync(
                       prepared.device_operations.get(),
                       prepared.operations.data(),
                       prepared.operations.size()
                           * sizeof(DeviceExchangeOperation),
                       cudaMemcpyHostToDevice, impl_->stream.get()),
                   "upload CUDA exchange operations");
    }

    for (auto& prepared : prepared_phases) {
        if (prepared.operations.empty()) continue;
        check_cuda(launch_cuda_backend_exchange_phase(
                       device_blocks.get(), prepared.device_operations.get(),
                       static_cast<int>(prepared.operations.size()),
                       static_cast<int>(field_count), prepared.cells,
                       prepared.scratch.get(), impl_->stream.get()),
                   "launch CUDA same-level exchange phase");
        impl_->runtime_counters.kernel_count += 2;
        quiesce();
    }
    return expected;
}

void CudaBackend::rotate_slots(
    backend::BackendStateAccess current, state::SlotRotation rotation)
{
    auto& block = impl_->require_block(current);
    static_cast<void>(block.require_access(current));
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("rotation requires Current access");
    const std::array<std::size_t, 3> source{
        slot_index(rotation.current_from), slot_index(rotation.next_from),
        slot_index(rotation.scratch_from)};
    if (source[0] == source[1] || source[0] == source[2]
        || source[1] == source[2])
        throw std::invalid_argument("slot rotation is not a permutation");
    const auto old = block.slots;
    for (std::size_t destination = 0; destination < 3; ++destination)
        block.slots[destination] = old[source[destination]];
}

double CudaBackend::compute_diffusion_dt(
    backend::BackendStateAccess current)
{
    auto& block = impl_->require_block(current);
    const DeviceStateView selected = block.require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("diffusion dt requires Current");
    const CudaBackendDiffusionWorkspace workspace{
        block.face_flux.view(), block.diffusion_dt_candidates.get(),
        block.diffusion_dt_result.get(), block.diffusion_status.get()};
    CudaBackendLaunchResult result{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        result = launch_cuda_backend_diffusion_dt(
            selected, eos, impl_->species_view, block.grid,
            impl_->launch.diffusion, workspace, impl_->stream.get());
    });
    check_cuda(result.error, "launch diffusion dt");
    int status = 0;
    double dt = 0.0;
    check_cuda(cudaMemcpyAsync(
                   &status, block.diffusion_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion status");
    check_cuda(cudaMemcpyAsync(
                   &dt, block.diffusion_dt_result.get(), sizeof(double),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion dt");
    quiesce();
    impl_->runtime_counters.kernel_count += result.kernels_launched;
    impl_->runtime_counters.bytes_d2h += sizeof(int) + sizeof(double);
    if (status != 0) throw std::runtime_error("diffusion dt candidate failed");
    return dt;
}

void CudaBackend::copy_state_slot(
    backend::BackendStateAccess source,
    backend::BackendStateAccess destination)
{
    if (!same_block_handle(source.block, destination.block)
        || !same_storage_generation(source.storage, destination.storage))
        throw std::invalid_argument("state copy crosses CUDA blocks");
    auto& block = impl_->require_block(source);
    const DeviceStateView from = block.require_access(source);
    const DeviceStateView to = block.require_access(destination);
    if (source.slot == destination.slot)
        throw std::invalid_argument("state copy aliases one logical slot");
    check_cuda(copy_cuda_backend_state_slot(from, to, impl_->stream.get()),
               "copy state slot");
    quiesce();
}

state::CompletionToken CudaBackend::execute_diffusion_stage(
    backend::BackendStateAccess current, const scheduler::RklPlan& plan,
    const scheduler::RklStageDescriptor& descriptor,
    double dt, double dt_fe, state::CompletionToken expected)
{
    auto& block = impl_->require_block(current);
    static_cast<void>(block.require_access(current));
    const bool frozen_rkl1 = impl_->launch.plan.diffusion_integrator
        == dispatch::DiffusionIntegratorId::Rkl1;
    const bool frozen_rkl2 = impl_->launch.plan.diffusion_integrator
        == dispatch::DiffusionIntegratorId::Rkl2;
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || !impl_->launch.diffusion.use_diffusion
        || (!frozen_rkl1 && !frozen_rkl2)
        || plan.second_order != frozen_rkl2
        || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !same_rkl_descriptor(
            descriptor, plan.stages[descriptor.stage - 1])
        || !(dt > 0.0) || !(dt_fe > 0.0))
        throw std::invalid_argument("invalid diffusion stage contract");
    const DeviceStateView state_n =
        block.slots[slot_index(descriptor.state_n_slot)];
    const DeviceStateView previous =
        block.slots[slot_index(descriptor.previous_slot)];
    const DeviceStateView older =
        block.slots[slot_index(descriptor.older_slot)];
    const DeviceStateView output =
        block.slots[slot_index(descriptor.output_slot)];
    const CudaBackendDiffusionWorkspace workspace{
        block.face_flux.view(), block.diffusion_dt_candidates.get(),
        block.diffusion_dt_result.get(), block.diffusion_status.get()};
    CudaBackendLaunchResult operation{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        operation = launch_cuda_backend_diffusion_stage(
            plan, descriptor, state_n, previous, older, output,
            block.diffusion_delta.view(),
            block.diffusion_initial_delta.view(), eos, impl_->species_view,
            block.grid, impl_->launch.diffusion, workspace, dt,
            impl_->stream.get());
    });
    check_cuda(operation.error, "launch diffusion operator");
    int status = 0;
    check_cuda(cudaMemcpyAsync(
                   &status, block.diffusion_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion stage status");
    quiesce();
    impl_->runtime_counters.kernel_count +=
        operation.kernels_launched;
    impl_->runtime_counters.bytes_d2h += sizeof(int);
    if (status != 0) throw std::runtime_error("diffusion stage failed");
    return expected;
}

backend::BurnExecutionResult CudaBackend::execute_burn(
    backend::BackendStateAccess current, double dt,
    state::CompletionToken expected)
{
    auto& block = impl_->require_block(current);
    static_cast<void>(block.require_access(current));
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || !(dt > 0.0))
        throw std::invalid_argument("invalid burn contract");
    if (!impl_->launch.burn.use_burn) {
        check_cuda(cudaMemsetAsync(
                       block.slots[slot_index(state::StateSlot::Current)].enuc_rate,
                       0,
                       static_cast<std::size_t>(block.grid.total_size)
                           * sizeof(double),
                       impl_->stream.get()),
                   "clear disabled burn diagnostic");
        quiesce();
        return {DriverBurn::INACTIVE_LIMITER_CANDIDATE, 0, 0, expected};
    }
    DeviceStateView selected =
        block.slots[slot_index(state::StateSlot::Current)];
    check_cuda(cudaMemsetAsync(
                   selected.enuc_rate, 0,
                   static_cast<std::size_t>(block.grid.total_size)
                       * sizeof(double),
                   impl_->stream.get()),
               "clear burn diagnostic");
    cudaError_t launch_error = cudaErrorInvalidValue;
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch_error = launch_cuda_burn_route(
            impl_->launch.plan, selected, block.grid,
            block.burn_workspace_storage.get(), block.burn_candidates.get(),
            block.burn_statuses.get(), block.burn_summary.get(), dt, eos,
            impl_->launch.burn, impl_->stream.get());
    });
    check_cuda(launch_error, "launch burn route");
    DeviceBurnSummary summary{};
    check_cuda(cudaMemcpyAsync(
                   &summary, block.burn_summary.get(), sizeof(summary),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download burn summary");
    quiesce();
    impl_->runtime_counters.kernel_count += 2;
    impl_->runtime_counters.bytes_d2h += sizeof(summary);
    return {summary.limiter, summary.failed_cells, summary.status, expected};
}

void CudaBackend::enqueue_materialize_host_current(
    backend::BackendStateAccess current, state::StateRegion region,
    backend::HostStateTransferView host)
{
    auto& block = impl_->require_block(current);
    const DeviceStateView selected = block.require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("materialization requires Current");
    impl_->runtime_counters.bytes_d2h += block.copy_host_device_region(
        selected, host, region, cudaMemcpyDeviceToHost, impl_->stream.get());
}

void CudaBackend::enqueue_upload_slot(
    backend::BackendStateAccess access, state::StateRegion region,
    backend::HostStateTransferView host)
{
    auto& block = impl_->require_block(access);
    const DeviceStateView selected = block.require_access(access);
    impl_->runtime_counters.bytes_h2d += block.copy_host_device_region(
        selected, host, region, cudaMemcpyHostToDevice, impl_->stream.get());
}

void CudaBackend::quiesce()
{
    impl_->checked_quiesce("synchronize CUDA backend");
}

backend::BackendCounters CudaBackend::counters() const noexcept
{
    ++impl_->runtime_counters.getter_count;
    return impl_->runtime_counters;
}

void CudaBackend::append_trace(backend::BackendTraceRecord record)
{
    if (!impl_->store.contains({record.block, record.storage, record.slot}))
        throw std::invalid_argument("trace targets stale CUDA storage");
    impl_->runtime_trace.push_back(record);
}

std::span<const backend::BackendTraceRecord>
CudaBackend::trace_snapshot() const noexcept
{
    ++impl_->runtime_counters.getter_count;
    return impl_->runtime_trace;
}

CudaBackend::StoreTransaction CudaBackend::begin_store_transaction(
    amr::AmrPlanScope scope,
    std::span<const CudaBlockBinding> bindings)
{
    if (bindings.empty())
        throw std::invalid_argument("CUDA staged block set is empty");

    std::vector<DeviceStoreProposal> proposals;
    proposals.reserve(bindings.size());
    for (const auto& binding : bindings) {
        if (binding.block == nullptr || binding.physical_boundary == nullptr)
            throw std::invalid_argument("null staged CUDA block binding");
        proposals.push_back({binding.handle, binding.storage});
    }

    // External code may have selected another CUDA device since the backend
    // was created.  Establish the allocation/stream device before staging.
    impl_->select_device();
    auto candidate = impl_->store.prepare(scope, proposals);
    std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>> resources;
    std::map<DeviceArenaSlot, std::uint8_t> uploaded_current;
    try {
        const auto entries = candidate.entries();
        if (entries.size() != bindings.size())
            throw std::logic_error("staged CUDA record count drifted");
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            const auto& binding = bindings[index];
            // Allocate all throwing host nodes before the runtime starts any
            // asynchronous upload.  unique_ptr installation is then noexcept,
            // and every live runtime remains in the outer map for the checked
            // catch-path quiescence.
            auto [resource_slot, resource_inserted] =
                resources.try_emplace(entry.arena);
            auto [upload_slot, upload_inserted] =
                uploaded_current.try_emplace(entry.arena, 0U);
            if (!resource_inserted || !upload_inserted)
                throw std::logic_error("duplicate staged CUDA arena");
            resource_slot->second = std::make_unique<CudaBlockRuntime>(
                *binding.block, entry.record.handle, entry.record.storage,
                impl_->species_count, impl_->device_ordinal,
                *binding.physical_boundary,
                impl_->launch, impl_->stream.get());
            static_cast<void>(upload_slot);
        }
        if (resources.size() != uploaded_current.size())
            throw std::logic_error("invalid staged CUDA upload map");
        for (const auto& [arena, runtime] : resources) {
            if (runtime == nullptr || !uploaded_current.contains(arena))
                throw std::logic_error("invalid staged CUDA resource map");
        }
        auto implementation = std::make_unique<StoreTransaction::Impl>(
            impl_, std::move(candidate), std::move(resources),
            std::move(uploaded_current));
        impl_->staged_resource_count = implementation->resources.size();
        return StoreTransaction(std::move(implementation));
    } catch (...) {
        // resources is destroyed after this handler.  If cleanup cannot
        // prove the stream quiescent, fail fast rather than freeing in flight.
        impl_->quiesce_or_terminate();
        impl_->staged_resource_count = 0;
        throw;
    }
}

bool CudaBackend::contains_migration(
    const StoreTransaction& transaction,
    DeviceMigrationAccess access) const noexcept
{
    if (!transaction.impl_ || transaction.impl_->consumed
        || transaction.impl_->owner.get() != impl_.get())
        return false;
    return impl_->store.contains_migration(
        transaction.impl_->candidate, access);
}

void CudaBackend::enqueue_upload_staged_current(
    StoreTransaction& transaction, DeviceMigrationAccess access,
    state::StateRegion region, backend::HostStateTransferView host)
{
    if (!transaction.impl_ || transaction.impl_->consumed
        || transaction.impl_->owner.get() != impl_.get()
        || access.role != DeviceMigrationRole::StagedNewDestination
        || access.access.slot != state::StateSlot::Current
        || (region != state::StateRegion::Interior
            && region != state::StateRegion::Ghost)) {
        throw std::invalid_argument(
            "invalid staged CUDA Current upload transaction");
    }
    if (transaction.impl_->upload_failed)
        throw std::logic_error(
            "failed staged CUDA upload transaction must be aborted");

    const auto& entry = impl_->store.migration_record(
        transaction.impl_->candidate, access);
    const auto found = transaction.impl_->resources.find(entry.arena);
    const auto uploaded = transaction.impl_->uploaded_current.find(entry.arena);
    if (found == transaction.impl_->resources.end()
        || uploaded == transaction.impl_->uploaded_current.end())
        throw std::logic_error("staged CUDA arena resource is missing");

    const std::uint8_t region_bit = region == state::StateRegion::Interior
        ? StoreTransaction::Impl::kInteriorUploaded
        : StoreTransaction::Impl::kGhostUploaded;
    if ((uploaded->second & region_bit) != 0)
        throw std::logic_error("staged CUDA Current region uploaded twice");

    try {
        impl_->select_device();
        const DeviceStateView selected =
            found->second->require_access(access.access);
        impl_->runtime_counters.bytes_h2d
            += found->second->copy_host_device_region(
                selected, host, region, cudaMemcpyHostToDevice,
                impl_->stream.get());
        uploaded->second |= region_bit;
    } catch (...) {
        transaction.impl_->upload_failed = true;
        throw;
    }
}

void CudaBackend::abort_store_transaction(StoreTransaction&& transaction)
{
    if (!transaction.impl_ || transaction.impl_->consumed
        || transaction.impl_->owner.get() != impl_.get())
        throw std::invalid_argument("invalid CUDA store abort");
    impl_->checked_quiesce("synchronize staged CUDA abort");
    impl_->store.abort(std::move(transaction.impl_->candidate));
    transaction.impl_->resources.clear();
    transaction.impl_->uploaded_current.clear();
    impl_->staged_resource_count = 0;
    transaction.impl_->consumed = true;
}

void CudaBackend::publish_store_transaction(
    StoreTransaction&& transaction, DeviceRetirementFence fence)
{
    if (!transaction.impl_ || transaction.impl_->consumed
        || transaction.impl_->owner.get() != impl_.get())
        throw std::invalid_argument("invalid CUDA store publication");
    if (!transaction.impl_->current_upload_complete())
        throw std::logic_error(
            "CUDA store publication requires complete staged Current");

    // Publication cannot acquire an event or release any namespace until all
    // staged uploads have completed on the backend's checked device.
    impl_->checked_quiesce("synchronize staged CUDA publication");
    impl_->retired_resources.emplace_back();
    auto retirement = std::prev(impl_->retired_resources.end());
    retirement->fence = fence;
    try {
        retirement->event.create(impl_->device_ordinal);
        // Record the retirement witness before changing any logical
        // visibility.  From lifecycle publication onward every operation is
        // non-throwing.
        retirement->event.record(impl_->stream.get());
        impl_->store.publish_after_success(
            std::move(transaction.impl_->candidate), fence,
            [](std::span<const DeviceStoreEntry>) noexcept {});
    } catch (...) {
        impl_->retired_resources.erase(retirement);
        throw;
    }

    static_assert(noexcept(impl_->active_resources.swap(
        transaction.impl_->resources)));
    static_assert(noexcept(retirement->resources.swap(
        transaction.impl_->resources)));
    impl_->active_resources.swap(transaction.impl_->resources);
    retirement->resources.swap(transaction.impl_->resources);
    impl_->staged_resource_count = 0;
    transaction.impl_->consumed = true;
}

bool CudaBackend::retirement_ready(DeviceRetirementFence fence) const
{
    const auto found = std::find_if(
        impl_->retired_resources.begin(), impl_->retired_resources.end(),
        [fence](const Impl::RetiredCudaResources& resources) {
            return resources.fence == fence;
        });
    if (found == impl_->retired_resources.end())
        throw std::invalid_argument("unknown CUDA retirement fence");
    check_cuda(cudaSetDevice(impl_->device_ordinal), "cudaSetDevice");
    return found->event.ready();
}

void CudaBackend::complete_store_retirement(DeviceRetirementFence fence)
{
    auto found = std::find_if(
        impl_->retired_resources.begin(), impl_->retired_resources.end(),
        [fence](const Impl::RetiredCudaResources& resources) {
            return resources.fence == fence;
        });
    if (found == impl_->retired_resources.end())
        throw std::invalid_argument("unknown CUDA retirement fence");
    check_cuda(cudaSetDevice(impl_->device_ordinal), "cudaSetDevice");
    if (!found->event.ready())
        throw std::logic_error("CUDA retirement fence is still pending");

    impl_->store.complete_retirement(
        fence, [&](std::span<const DeviceStoreEntry>) noexcept {
            found->resources.clear();
        });
    impl_->retired_resources.erase(found);
}

CudaStoreSnapshot CudaBackend::store_snapshot() const noexcept
{
    return {
        static_cast<std::uint64_t>(impl_->store.active_entries().size()),
        static_cast<std::uint64_t>(impl_->staged_resource_count),
        static_cast<std::uint64_t>(impl_->retired_resources.size()),
        impl_->runtime_counters.bytes_h2d,
        impl_->immutable_owner_constructions};
}

template <class Eos>
std::unique_ptr<CudaBackend> make_cuda_backend_impl(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const Eos& eos)
{
    if (launch.plan.eos != host_eos_id(eos))
        throw std::invalid_argument(
            "resolved EOS does not match the CUDA factory owner");
    auto implementation = std::make_unique<CudaBackend::Impl>(
        blocks, device_ordinal, launch, species);
    implementation->initialize_eos(eos, species);
    return std::make_unique<CudaBackend>(std::move(implementation));
}

template <class Eos>
std::unique_ptr<CudaBackend> make_single_cuda_backend_impl(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const Eos& eos)
{
    const std::array<CudaBlockBinding, 1> blocks{{
        {&block, handle, storage, &boundary}}};
    return make_cuda_backend_impl(
        std::span<const CudaBlockBinding>(blocks), device_ordinal, launch,
        species, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const IdealGas& eos)
{
    return make_single_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const HelmEos& eos)
{
    return make_single_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary,
    const Tabular3DEOSHostView& eos)
{
    return make_single_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary,
    const Tabular4DEOSHostView& eos)
{
    return make_single_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const IdealGas& eos)
{
    return make_cuda_backend_impl(
        blocks, device_ordinal, launch, species, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const HelmEos& eos)
{
    return make_cuda_backend_impl(
        blocks, device_ordinal, launch, species, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const Tabular3DEOSHostView& eos)
{
    return make_cuda_backend_impl(
        blocks, device_ordinal, launch, species, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const Tabular4DEOSHostView& eos)
{
    return make_cuda_backend_impl(
        blocks, device_ordinal, launch, species, eos);
}

} // namespace arch::cuda
