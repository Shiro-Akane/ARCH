/**
 * @file CudaBackendStore.cpp
 * @brief Stage, publish and retire CUDA block-storage transactions.
 *
 * Candidate resources remain private while interiors, ghosts and flux plans
 * are prepared. Publication follows a completion witness and atomically changes
 * the store namespace; abandoned candidates synchronize before releasing memory.
 * The shared DeviceBlockStoreLifecycle owns identity rules, not transfer physics.
 */

#include "cuda/runtime/control/CudaBackendInternal.h"

#include <algorithm>

namespace arch::cuda {
namespace {

class CudaTopologyStoreTransaction final
    : public backend::BackendTopologyStoreTransaction {
public:
    explicit CudaTopologyStoreTransaction(
        CudaBackend::StoreTransaction&& requested)
        : transaction(std::move(requested))
    {
    }

    CudaBackend::StoreTransaction transaction;
};

CudaTopologyStoreTransaction& require_cuda_topology_transaction(
    backend::BackendTopologyStoreTransaction& transaction)
{
    auto* selected = dynamic_cast<CudaTopologyStoreTransaction*>(&transaction);
    if (selected == nullptr)
        throw std::invalid_argument(
            "topology transaction belongs to another backend");
    return *selected;
}

} // namespace

CudaBackend::StoreTransaction::Impl::Impl(
    std::shared_ptr<CudaBackend::Impl> requested_owner,
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

bool CudaBackend::StoreTransaction::Impl::current_upload_complete()
    const noexcept
{
    if (upload_failed || uploaded_current.size() != resources.size())
        return false;
    return std::all_of(
        uploaded_current.begin(), uploaded_current.end(),
        [](const auto& entry) {
            return entry.second == kCompleteCurrent;
        });
}

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
    impl_->owner->quiesce_or_terminate();
    try {
        impl_->owner->store.abort(std::move(impl_->candidate));
    } catch (...) {
        // A live transaction owns the exact candidate; abort cannot fail
        // without an internal ownership/reentrancy invariant violation.
        std::terminate();
    }
    impl_->amr_flux.reset();
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
                impl_->launch, impl_->stream.get(), impl_->runtime_counters);
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
    transaction.impl_->amr_flux.reset();
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
        // The first dynamic-AMR implementation deliberately retires
        // synchronously.  Prove the event complete before either the device
        // namespace or Host topology becomes visible; an asynchronous drain
        // can be introduced later entirely behind this backend contract.
        impl_->checked_quiesce(
            "synchronize recorded CUDA retirement fence");
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
    impl_->active_amr_flux.swap(transaction.impl_->amr_flux);
    retirement->amr_flux.swap(transaction.impl_->amr_flux);
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
            found->amr_flux.reset();
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

bool CudaBackend::supports_dynamic_topology_store() const noexcept
{
    return cuda_amr_execution_available();
}

std::unique_ptr<backend::BackendTopologyStoreTransaction>
CudaBackend::begin_topology_store_transaction(
    const amr::AmrPlanScope& scope,
    std::span<const backend::BackendTopologyBinding> bindings)
{
    std::vector<CudaBlockBinding> cuda_bindings;
    cuda_bindings.reserve(bindings.size());
    for (const auto& binding : bindings) {
        cuda_bindings.push_back({
            binding.block, binding.handle, binding.storage,
            binding.physical_boundary});
    }
    return std::make_unique<CudaTopologyStoreTransaction>(
        begin_store_transaction(scope, cuda_bindings));
}

void CudaBackend::enqueue_upload_staged_current(
    backend::BackendTopologyStoreTransaction& transaction,
    backend::BackendStateAccess access, state::StateRegion region,
    backend::HostStateTransferView host)
{
    auto& selected = require_cuda_topology_transaction(transaction);
    enqueue_upload_staged_current(
        selected.transaction,
        {selected.transaction.scope(), access,
         DeviceMigrationRole::StagedNewDestination},
        region, host);
}

void CudaBackend::migrate_staged_current(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> sources,
    const amr::ProlongationPlan& prolongation,
    const amr::RestrictionPlan& restriction)
{
    migrate_staged_current(require_cuda_topology_transaction(transaction).transaction,
                           sources, prolongation, restriction);
}

void CudaBackend::complete_staged_current_ghosts(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const amr::SameLevelExchangePlan> same_level,
    const amr::CoarseFineTransferPlan& coarse_fine)
{
    complete_staged_current_ghosts(
        require_cuda_topology_transaction(transaction).transaction,
        same_level, coarse_fine);
}

void CudaBackend::prepare_amr_flux_plan(
    const amr::AmrFluxTopologyPlan& topology,
    const amr::RefluxPlan& reflux)
{
    if (impl_->active_amr_flux
        && impl_->active_amr_flux->epoch == topology.epoch
        && impl_->active_amr_flux->topology_fingerprint
            == topology.fingerprint
        && impl_->active_amr_flux->reflux_fingerprint
            == reflux.fingerprint)
        return;
    impl_->select_device();
    std::unique_ptr<CudaAmrFluxPlanRuntime> prepared;
    try {
        prepared = make_cuda_amr_flux_plan_runtime(
            topology, reflux, impl_->store.active_entries(),
            impl_->active_resources, impl_->species_count,
            impl_->launch.diffusion.use_diffusion
                && impl_->launch.plan.diffusion_integrator
                    == dispatch::DiffusionIntegratorId::Rkl2,
            impl_->stream.get(), impl_->runtime_counters);
        impl_->checked_quiesce("prepare CUDA AMR flux plan");
    } catch (...) {
        impl_->quiesce_or_terminate();
        throw;
    }
    impl_->active_amr_flux = std::move(prepared);
}

void CudaBackend::stage_amr_flux_plan(
    backend::BackendTopologyStoreTransaction& transaction,
    const amr::AmrFluxTopologyPlan& topology,
    const amr::RefluxPlan& reflux)
{
    auto& selected = require_cuda_topology_transaction(transaction);
    auto& concrete = selected.transaction;
    if (!concrete.impl_ || concrete.impl_->consumed
        || concrete.impl_->owner.get() != impl_.get()
        || concrete.impl_->amr_flux
        || concrete.scope().to_epoch != topology.epoch)
        throw std::invalid_argument(
            "invalid staged CUDA AMR flux plan transaction");
    impl_->select_device();
    try {
        concrete.impl_->amr_flux = make_cuda_amr_flux_plan_runtime(
            topology, reflux, concrete.impl_->candidate.entries(),
            concrete.impl_->resources, impl_->species_count,
            impl_->launch.diffusion.use_diffusion
                && impl_->launch.plan.diffusion_integrator
                    == dispatch::DiffusionIntegratorId::Rkl2,
            impl_->stream.get(), impl_->runtime_counters);
    } catch (...) {
        impl_->quiesce_or_terminate();
        throw;
    }
}

void CudaBackend::publish_topology_store_transaction(
    std::unique_ptr<backend::BackendTopologyStoreTransaction> transaction)
{
    if (!transaction)
        throw std::invalid_argument("topology store transaction is null");
    auto& selected = require_cuda_topology_transaction(*transaction);
    if (!selected.transaction.impl_
        || !selected.transaction.impl_->amr_flux)
        throw std::logic_error(
            "CUDA topology publication requires a staged AMR flux plan");
    if (impl_->next_retirement_fence == 0) {
        throw std::overflow_error("CUDA retirement fence counter exhausted");
    }
    const DeviceRetirementFence fence{impl_->next_retirement_fence};
    impl_->next_retirement_fence = impl_->next_retirement_fence
        == std::numeric_limits<std::uint64_t>::max()
        ? 0 : impl_->next_retirement_fence + 1;
    publish_store_transaction(
        std::move(selected.transaction), fence);
    // Visibility has changed, so no recoverable exception may escape.  The
    // concrete publication above synchronized the recorded event first;
    // failure here is an internal invariant or device-lifetime failure.
    try {
        if (!retirement_ready(fence)) std::terminate();
        complete_store_retirement(fence);
    } catch (...) {
        std::terminate();
    }
}

} // namespace arch::cuda
