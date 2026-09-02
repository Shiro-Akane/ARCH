/**
 * @file CudaBackendInternal.h
 * @brief Internal ownership model shared by CUDA host-control units.
 *
 * Numerical kernels are deliberately absent.  Method bodies live in the
 * functionally owning .cpp unit so including this layout does not recompile
 * the former backend monolith in every control translation unit.
 */

#pragma once

#include "CudaBackend.h"
#include "CudaBackendDiffusion.h"
#include "CudaBackendExchange.h"
#include "CudaBackendAmrFlux.h"
#include "CudaBackendTypes.h"
#include "DeviceBlockStore.h"

#include "cuda/hydro/BoundaryPlan.h"
#include "cuda/microphysics/helm_eos_loader.h"

#include <cuda_runtime.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace arch::cuda {

[[noreturn]] void throw_cuda(cudaError_t error, const char* operation);
void check_cuda(cudaError_t error, const char* operation);
void require_cuda_success_or_terminate(cudaError_t error) noexcept;
void set_device_and_quiesce_or_terminate(
    int device_ordinal, cudaStream_t stream) noexcept;

std::size_t slot_index(state::StateSlot slot);

inline bool complete_token(state::CompletionToken token) noexcept
{
    return state::is_complete(token);
}

std::size_t burn_workspace_bytes(
    const dispatch::ResolvedExecutionPlan& plan, std::size_t active_cells);

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
        // The enclosing owner establishes the device and a completion witness.
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
    ~StreamOwner();
    StreamOwner(const StreamOwner&) = delete;
    StreamOwner& operator=(const StreamOwner&) = delete;

    void create(int device_ordinal);
    cudaStream_t get() const noexcept { return stream_; }

private:
    cudaStream_t stream_ = nullptr;
    int device_ordinal_ = -1;
};

class EventOwner {
public:
    EventOwner() = default;
    ~EventOwner();
    EventOwner(const EventOwner&) = delete;
    EventOwner& operator=(const EventOwner&) = delete;
    EventOwner(EventOwner&& other) noexcept;
    EventOwner& operator=(EventOwner&& other) noexcept;

    void create(int device_ordinal);
    void record(cudaStream_t stream);
    bool ready() const;

private:
    void release() noexcept;

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

    void allocate(int total, int count);
    DeviceStateView view() const noexcept;
};

struct DeviceAmrFluxSurfaceStorage {
    DeviceAllocation<double> values;
    int cell_count = 0;
    int species_count = 0;

    void allocate(int cells, int species);
    DeviceAmrFluxSurfaceView view() const noexcept;
};

struct CudaBlockRuntime {
    amr::BlockHandle handle;
    backend::StorageGeneration generation;
    std::array<DeviceStateStorage, 3> state_storage;
    std::array<DeviceStateView, 3> slots{};
    DeviceStateStorage face_flux;
    DeviceStateStorage hydro_delta;
    DeviceStateStorage diffusion_delta;
    DeviceStateStorage diffusion_initial_delta;
    std::array<DeviceAmrFluxSurfaceStorage, 6> amr_flux_register;
    std::array<DeviceAmrFluxSurfaceStorage, 6> amr_initial_flux;
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
        const CudaLaunchConfig& launch, cudaStream_t stream);

    DeviceStateView require_access(
        backend::BackendStateAccess access) const;

    std::uint64_t copy_host_device_region(
        DeviceStateView device, const backend::HostStateTransferView& host,
        state::StateRegion region, cudaMemcpyKind direction,
        cudaStream_t stream);
};

struct CudaAmrFluxRouteStorage {
    amr::BlockHandle source{};
    int direction = -1;
    amr::AmrCompiledFluxRegistrationRoute compiled{};
    DeviceAllocation<amr::AmrFluxRegistrationTarget> targets;
    DeviceAllocation<amr::AmrFluxRegistrationTerm> terms;
};

struct CudaAmrFluxPlanRuntime {
    amr::TopologyEpoch epoch{};
    std::uint64_t topology_fingerprint = 0;
    std::uint64_t reflux_fingerprint = 0;
    int dimension = 0;
    int species_count = 0;
    std::vector<CudaBlockRuntime*> runtime_blocks;
    std::vector<DeviceAmrFluxBlockView> host_blocks;
    DeviceAllocation<DeviceAmrFluxBlockView> device_blocks;
    std::vector<std::unique_ptr<CudaAmrFluxRouteStorage>> routes;
    std::map<std::pair<amr::BlockHandle, int>, std::size_t> route_index;
    amr::AmrCompiledRefluxPlan compiled_reflux{};
    DeviceAllocation<amr::AmrRefluxTarget> reflux_targets;
    DeviceAllocation<amr::AmrRefluxContribution> reflux_contributions;
    std::vector<amr::AmrFluxSurfaceRequirement> surface_requirements;

    const CudaAmrFluxRouteStorage* find_route(
        amr::BlockHandle source, int direction) const noexcept;
};

std::unique_ptr<CudaAmrFluxPlanRuntime> make_cuda_amr_flux_plan_runtime(
    const amr::AmrFluxTopologyPlan& topology,
    const amr::RefluxPlan& reflux,
    std::span<const DeviceStoreEntry> entries,
    const std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>>&
        resources,
    int expected_species_count, bool cache_initial_operator,
    cudaStream_t stream);

std::array<CudaAmrFluxDirectionRouteView, 3> make_cuda_amr_route_views(
    const CudaAmrFluxPlanRuntime* plan, amr::BlockHandle source);

struct CudaBackend::Impl {
    struct RetiredCudaResources {
        DeviceRetirementFence fence{};
        EventOwner event;
        std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>> resources;
        std::unique_ptr<CudaAmrFluxPlanRuntime> amr_flux;
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
    std::uint64_t next_retirement_fence = 1;
    std::size_t staged_resource_count = 0;
    std::vector<DeviceStoreEntry> initial_entries;
    DeviceBlockStoreLifecycle store;
    std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>>
        active_resources;
    std::unique_ptr<CudaAmrFluxPlanRuntime> active_amr_flux;
    std::list<RetiredCudaResources> retired_resources;

    static std::vector<DeviceStoreEntry> make_initial_entries(
        std::span<const CudaBlockBinding> bindings);

    Impl(std::span<const CudaBlockBinding> bindings, int device,
         const CudaLaunchConfig& launch_config,
         const SpeciesManager& species);
    ~Impl() noexcept;

    void select_device() const;
    void checked_quiesce(const char* operation);
    void quiesce_or_terminate() noexcept;
    CudaBlockRuntime& require_block(backend::BackendStateAccess access);
    const CudaBlockRuntime& require_block(
        backend::BackendStateAccess access) const;
    CudaBlockRuntime& first_block() noexcept;
    const CudaBlockRuntime& first_block() const noexcept;

    void initialize_eos(const IdealGas& host, const SpeciesManager& species);
    void initialize_eos(const HelmEos& host, const SpeciesManager&);
    void initialize_eos(
        const Tabular3DEOSHostView& host, const SpeciesManager&);
    void initialize_eos(
        const Tabular4DEOSHostView& host, const SpeciesManager&);
    void finish_eos_upload(const char* operation);
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
    std::unique_ptr<CudaAmrFluxPlanRuntime> amr_flux;
    bool upload_failed = false;
    bool consumed = false;

    Impl(std::shared_ptr<CudaBackend::Impl> requested_owner,
         DeviceBlockStoreLifecycle::Candidate requested_candidate,
         std::map<DeviceArenaSlot, std::unique_ptr<CudaBlockRuntime>>
             requested_resources,
         std::map<DeviceArenaSlot, std::uint8_t> requested_uploaded_current)
        noexcept;

    bool current_upload_complete() const noexcept;
};

} // namespace arch::cuda
