/**
 * @file CudaBackendInternal.h
 * @brief Private resource layout shared by CUDA host-control units.
 *
 * The implementation owns the stream, block stores, immutable EOS/network data
 * and reusable workspaces. Functional control units define its methods; typed
 * kernel launch interfaces remain separate from this ownership declaration.
 */

#pragma once

#include "cuda/runtime/CudaBackend.h"
#include "cuda/runtime/diffusion/CudaBackendDiffusion.h"
#include "cuda/runtime/amr/CudaBackendExchange.h"
#include "cuda/runtime/amr/CudaBackendAmrFlux.h"
#include "amr/AmrFluxExecutionPlan.h"
#include "cuda/runtime/CudaBackendTypes.h"
#include "cuda/runtime/burn/CudaBackendBurnSparse.h"
#include "cuda/runtime/DeviceBlockStore.h"

#include "cuda/hydro/BoundaryPlan.h"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/microphysics/helm_eos_loader.h"
#include "cuda/microphysics/device_network_owner.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

#include <cuda_runtime.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
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

std::size_t slot_index(state::StateSlot slot);

inline bool complete_token(state::CompletionToken token) noexcept
{
    return state::is_complete(token);
}

std::size_t burn_workspace_bytes(
    const dispatch::ResolvedExecutionPlan& plan, std::size_t active_cells,
    int species_count);

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

// Account explicit backend metadata at the actual enqueue site. Field-region
// transfers and native sparse-provider traffic retain their existing owners.
void enqueue_cuda_metadata_upload(void* destination, const void* source,
    std::size_t bytes, cudaStream_t stream, backend::BackendCounters& counters,
    const char* operation);

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
        const CudaLaunchConfig& launch, cudaStream_t stream,
        backend::BackendCounters& counters);

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
    cudaStream_t stream, backend::BackendCounters& counters);

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
    // All transport launches on this backend are ordered on `stream`. One
    // bounded scratch allocation is therefore shared across every block/stage.
    DeviceAllocation<double> species_workspace_storage;
    SpeciesWorkspaceView species_workspace{};
    // Indicators retain capacity across regrids, not field values. The ordered
    // evaluator completes before any buffer is grown or reused.
    struct RefinementScratch {
        std::unique_ptr<DeviceAllocation<std::byte>> selection;
        std::unique_ptr<DeviceAllocation<double>> errors, summary, thermodynamics, composition;
    } refinement_scratch;
    std::variant<std::monostate, IdealGasView, HelmEosView,
                 Tabular3DEOSView, Tabular4DEOSView> eos;
    // One bounded sparse ODE/factor pool serves all blocks on the ordered stream;
    // AMR topology changes must not allocate a separate solver pool per block.
    std::unique_ptr<CudaSparseBurnOwner> sparse_burn_owner;
    std::unique_ptr<DeviceNetworkOwner> dense_network_owner;
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
    void initialize_species_workspace();
    void checked_quiesce(const char* operation);
    void quiesce_or_terminate() noexcept;
    CudaBlockRuntime& require_block(backend::BackendStateAccess access);
    const CudaBlockRuntime& require_block(
        backend::BackendStateAccess access) const;
    using BlockResolver = std::function<
        CudaBlockRuntime&(backend::BackendStateAccess)>;
    // One lowering/execution path serves active slots and the unpublished
    // regrid namespace; only the validated storage resolver differs.
    void execute_same_level_exchange(
        std::span<const backend::BackendStateAccess>,
        const amr::SameLevelExchangePlan&, state::StateSlot,
        const BlockResolver&);
    void execute_coarse_fine_exchange(
        std::span<const backend::BackendStateAccess>,
        const amr::CoarseFineTransferPlan&, state::StateSlot,
        const BlockResolver&);
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

// Declare after asynchronous Host-copy sources/destinations and device owners.
// It proves completion before those objects unwind on a recoverable failure.
struct CudaQuiescenceGuard {
    CudaBackend::Impl& owner;
    bool completed = false;
    ~CudaQuiescenceGuard() { if (!completed) owner.quiesce_or_terminate(); }
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
