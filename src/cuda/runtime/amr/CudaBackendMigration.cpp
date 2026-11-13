/**
 * @file CudaBackendMigration.cpp
 * @brief Transactional device regrid using shared transfer mathematics.
 *
 * The active store remains the migration source while the private candidate
 * receives survivor copies and parent/child transfers. Completion and EOS status
 * are checked before candidate ghosts are completed and the runtime may publish
 * the new topology; these methods do not retire the old store. Completion
 * borrows Runtime's ordered boundary/EOS owner before marking Current complete.
 */
#include "amr/exchange/CoordinateSeamPlan.h"
#include "amr/transfer/RegridExecutionPlan.h"
#include "cuda/amr/RegridMigration.h"
#include "cuda/runtime/control/CudaBackendInternal.h"

#include <functional>
#include <set>

namespace arch::cuda {

CudaBlockRuntime& resolve_staged_block(CudaBackend::StoreTransaction::Impl& staged,
    backend::BackendStateAccess access)
{
    const auto& entry = staged.owner->store.migration_record(staged.candidate,
        {staged.candidate.scope(), access, DeviceMigrationRole::StagedNewDestination});
    const auto found = staged.resources.find(entry.arena);
    if (found == staged.resources.end() || !found->second)
        throw std::logic_error("staged migration runtime is missing");
    static_cast<void>(found->second->require_access(access));
    return *found->second;
}

namespace {

using Access = backend::BackendStateAccess;

DeviceRegridBlock current_block(CudaBlockRuntime& block)
{
    return {block.slots[slot_index(state::StateSlot::Current)], block.grid};
}

std::array<double*, 7> fields(DeviceStateView state)
{
    return {state.rho, state.mom_u, state.mom_v, state.mom_w,
            state.eng, state.enuc_rate, state.mass_fractions};
}

} // namespace

void CudaBackend::read_ownership_slots(Access current,
    std::span<backend::HostStateTransferView,3> host)
{
    if(impl_->hydro_attempt || current.slot!=state::StateSlot::Current)
        throw std::logic_error("ownership read requires quiescent Current");
    auto& block=impl_->require_block(current);
    for(const auto& slot:host) {
        backend::validate_host_state_transfer_view(slot);
        if(slot.species_stride!=slot.cell_count
            || slot.cell_count!=static_cast<std::size_t>(block.grid.total_size)
            || slot.species_count!=static_cast<std::size_t>(impl_->species_count))
            throw std::invalid_argument("ownership read shape mismatch");
    }
    impl_->select_device();
    CudaQuiescenceGuard guard{*impl_};
    for(std::size_t slot=0;slot<3;++slot) {
        const auto from=fields(block.slots[slot]);
        const auto& h=host[slot];
        const std::array to{h.rho,h.mom_u,h.mom_v,h.mom_w,h.eng,h.enuc_rate,h.species};
        for(std::size_t field=0;field<7;++field) {
            const auto bytes=h.cell_count*(field==6?h.species_count:1)*sizeof(double);
            if(!bytes)continue;
            check_cuda(cudaMemcpyAsync(to[field],from[field],bytes,cudaMemcpyDeviceToHost,
                impl_->stream.get()),"read CUDA ownership slot");
            impl_->runtime_counters.bytes_d2h+=bytes;
        }
    }
    impl_->checked_quiesce("complete CUDA ownership packet");
    guard.completed=true;
}

void CudaBackend::stage_ownership_slots(StoreTransaction& transaction,
    std::span<const Access> source_accesses,
    std::span<const backend::BackendTopologyBinding> incoming)
{
    if(!transaction.impl_ || transaction.impl_->consumed
        || transaction.impl_->owner.get()!=impl_.get())
        throw std::invalid_argument("invalid CUDA ownership transaction");
    auto& staged=*transaction.impl_;
    if(staged.upload_failed || staged.publication_fence.value
        || source_accesses.size()!=impl_->store.active_entries().size())
        throw std::invalid_argument("invalid CUDA ownership source scope");
    for(const auto& [arena,initialized]:staged.uploaded_current)
        if(initialized)throw std::logic_error("CUDA ownership destination already initialized");
    std::map<amr::BlockUid,CudaBlockRuntime*> sources;
    for(const auto access:source_accesses) {
        if(access.slot!=state::StateSlot::Current)
            throw std::invalid_argument("ownership source must name Current");
        static_cast<void>(impl_->store.migration_record(staged.candidate,
            {staged.candidate.scope(),access,DeviceMigrationRole::ActiveOldSource}));
        if(!sources.emplace(access.block.uid,&impl_->require_block(access)).second)
            throw std::invalid_argument("duplicate CUDA ownership source");
    }
    std::map<amr::BlockHandle,const amr::Block*> received;
    for(const auto& binding:incoming) {
        if(!binding.block || sources.contains(binding.handle.uid)
            || !received.emplace(binding.handle,binding.block).second)
            throw std::invalid_argument("invalid CUDA ownership receipt");
        static_cast<void>(resolve_staged_block(staged,
            {binding.handle,binding.storage,state::StateSlot::Current}));
    }
    // Resolve every destination and shape before writing any private allocation.
    for(const auto& entry:staged.candidate.entries()) {
        auto& destination=resolve_staged_block(staged,
            {entry.record.handle,entry.record.storage,state::StateSlot::Current});
        const auto found=sources.find(entry.record.handle.uid);
        if(found!=sources.end()) {
            if(found->second->grid.total_size!=destination.grid.total_size
                || found->second->grid.dim!=destination.grid.dim)
                throw std::invalid_argument("CUDA ownership survivor shape changed");
        } else {
            const auto* block=received.at(entry.record.handle);
            for(const auto member:{&amr::Block::fluid_state,&amr::Block::state_next,&amr::Block::state_scratch}) {
                const auto& host=block->*member;
                const std::array lengths{host.rho.size(),host.mom_u.size(),host.mom_v.size(),
                    host.mom_w.size(),host.eng.size(),host.enuc_rate.size(),host.mass_fractions.size()};
                if(host.GetNumSpecies()!=impl_->species_count)
                    throw std::invalid_argument("CUDA ownership species mismatch");
                for(std::size_t f=0;f<7;++f)
                    if(lengths[f]!=static_cast<std::size_t>(destination.grid.total_size)*(f==6?impl_->species_count:1))
                        throw std::invalid_argument("CUDA ownership field shape mismatch");
            }
        }
    }
    impl_->select_device();
    CudaQuiescenceGuard guard{*impl_};
    try {
        constexpr std::array members{&amr::Block::fluid_state,&amr::Block::state_next,&amr::Block::state_scratch};
        for(const auto& entry:staged.candidate.entries()) {
            auto& destination=resolve_staged_block(staged,
                {entry.record.handle,entry.record.storage,state::StateSlot::Current});
            const auto found=sources.find(entry.record.handle.uid);
            for(std::size_t slot=0;slot<3;++slot) {
                const auto to=fields(destination.slots[slot]);
                std::array<const double*,7> from{};
                if(found!=sources.end()) {
                    const auto resident=fields(found->second->slots[slot]);
                    std::copy(resident.begin(),resident.end(),from.begin());
                } else {
                    const auto& h=received.at(entry.record.handle)->*members[slot];
                    from={h.rho.data(),h.mom_u.data(),h.mom_v.data(),h.mom_w.data(),
                        h.eng.data(),h.enuc_rate.data(),h.mass_fractions.data()};
                }
                for(std::size_t field=0;field<7;++field) {
                    const auto bytes=static_cast<std::size_t>(destination.grid.total_size)
                        *(field==6?impl_->species_count:1)*sizeof(double);
                    if(!bytes)continue;
                    const auto kind=found!=sources.end()?cudaMemcpyDeviceToDevice:cudaMemcpyHostToDevice;
                    check_cuda(cudaMemcpyAsync(to[field],from[field],bytes,kind,impl_->stream.get()),
                        "copy CUDA ownership slot");
                    if(kind==cudaMemcpyHostToDevice)impl_->runtime_counters.bytes_h2d+=bytes;
                }
            }
        }
        impl_->checked_quiesce("complete staged CUDA ownership");
        guard.completed=true;
        for(auto& [arena,initialized]:staged.uploaded_current)
            initialized=StoreTransaction::Impl::kCompleteCurrent;
    } catch(...) {staged.upload_failed=true;throw;}
}

void CudaBackend::migrate_staged_current(
    StoreTransaction& transaction, std::span<const Access> source_accesses,
    const amr::ProlongationPlan& prolongation,
    const amr::RestrictionPlan& restriction)
{
    if (!transaction.impl_ || transaction.impl_->consumed
        || transaction.impl_->owner.get() != impl_.get())
        throw std::invalid_argument("invalid CUDA migration transaction");
    auto& staged = *transaction.impl_;
    const auto scope = staged.candidate.scope();
    if (staged.upload_failed || prolongation.scope != scope
        || restriction.scope != scope || source_accesses.empty()
        || source_accesses.size() != impl_->store.active_entries().size())
        throw std::invalid_argument("invalid CUDA migration source scope");
    for (const auto& [arena, initialized] : staged.uploaded_current)
        if (initialized != 0)
            throw std::logic_error("CUDA staged Current already initialized");

    const auto plan = amr::compile_regrid_execution_plan(
        prolongation, restriction, impl_->species_count);
    std::map<amr::BlockHandle, CudaBlockRuntime*> sources, destinations;
    std::map<amr::BlockUid, CudaBlockRuntime*> survivors;
    for (const auto& access : source_accesses) {
        if (access.slot != state::StateSlot::Current)
            throw std::invalid_argument("regrid source must be accepted Current");
        static_cast<void>(impl_->store.migration_record(staged.candidate,
            {scope, access, DeviceMigrationRole::ActiveOldSource}));
        auto& runtime = impl_->require_block(access);
        if (!sources.emplace(access.block, &runtime).second
            || !survivors.emplace(access.block.uid, &runtime).second)
            throw std::invalid_argument("duplicate CUDA migration source");
    }
    for (const auto& entry : staged.candidate.entries()) {
        const Access access{entry.record.handle, entry.record.storage, state::StateSlot::Current};
        destinations.emplace(access.block, &resolve_staged_block(staged, access));
    }

    std::set<amr::BlockHandle> reconstructed;
    std::size_t workspace_size = 0;
    const auto reserve_workspace = [&](CudaBlockRuntime& destination, int multiplier, int family_size) {
        const std::size_t cells = static_cast<std::size_t>(destination.grid.active_cell_count()) / family_size;
        const std::size_t species = static_cast<std::size_t>(impl_->species_count);
        if (species != 0 && cells > std::numeric_limits<std::size_t>::max() / species / multiplier)
            throw std::overflow_error("CUDA migration workspace overflow");
        workspace_size = std::max(workspace_size, cells * species * multiplier);
    };
    for (const auto& group : plan.prolongations) {
        static_cast<void>(sources.at(group.source.handle));
        auto& destination = *destinations.at(group.destination.handle);
        reconstructed.insert(group.destination.handle);
        reserve_workspace(destination, amr::regrid_math::prolongation_workspace_per_species,
                          1 << prolongation.dimension);
    }
    for (const auto& group : plan.restrictions) {
        for (int child = 0; child < (1 << restriction.dimension); ++child)
            static_cast<void>(sources.at(group.children[child].handle));
        auto& destination = *destinations.at(group.destination.handle);
        reconstructed.insert(group.destination.handle);
        reserve_workspace(destination, amr::regrid_math::restriction_workspace_per_species, 1);
    }
    for (const auto& [handle, destination] : destinations) {
        const auto survivor = survivors.find(handle.uid);
        if (reconstructed.contains(handle)) {
            if (survivor != survivors.end())
                throw std::invalid_argument("CUDA regrid destination overlaps surviving identity");
        } else if (survivor == survivors.end()) {
            throw std::invalid_argument("CUDA migration destination has no source");
        } else {
            const auto old = current_block(*survivor->second);
            const auto next = current_block(*destination);
            if (old.state.total_size != next.state.total_size
                || old.state.n_species != next.state.n_species
                || old.grid.dim != next.grid.dim)
                throw std::invalid_argument("CUDA survivor layout changed");
        }
    }

    impl_->select_device();
    DeviceAllocation<double> workspace;
    DeviceAllocation<int> device_status;
    if (workspace_size > 0) workspace.allocate(workspace_size);
    device_status.allocate(1);
    int status = 0;
    std::uint64_t kernel_count = 0;
    CudaQuiescenceGuard guard{*impl_};
    try {
        check_cuda(cudaMemsetAsync(device_status.get(), 0, sizeof(int), impl_->stream.get()),
                   "initialize CUDA migration status");
        // New Current padding/unused ghost cells have deterministic values.
        // Survivors copy accepted Current D2D; no accepted source is modified.
        for (const auto& [handle, destination] : destinations) {
            const auto next = current_block(*destination).state;
            const auto next_fields = fields(next);
            const auto survivor = survivors.find(handle.uid);
            const auto old_fields = survivor != survivors.end()
                ? fields(current_block(*survivor->second).state) : std::array<double*, 7>{};
            for (int field = 0; field < 7; ++field) {
                const std::size_t width = field == 6 ? next.n_species : 1;
                const std::size_t bytes = static_cast<std::size_t>(next.total_size) * width * sizeof(double);
                if (bytes == 0) continue;
                if (survivor != survivors.end())
                    check_cuda(cudaMemcpyAsync(next_fields[field], old_fields[field], bytes,
                        cudaMemcpyDeviceToDevice, impl_->stream.get()), "copy CUDA survivor Current");
                else
                    check_cuda(cudaMemsetAsync(next_fields[field], 0, bytes, impl_->stream.get()),
                               "initialize CUDA staged Current");
            }
        }
        for (const auto& group : plan.prolongations) {
            check_cuda(launch_cuda_regrid_prolongation(
                current_block(*sources.at(group.source.handle)),
                current_block(*destinations.at(group.destination.handle)), group.child_index,
                {impl_->launch.density_floor, impl_->launch.minimum_internal_energy,
                 impl_->launch.maximum_internal_energy},
                workspace.get(), workspace.size(), device_status.get(), impl_->stream.get()),
                "launch CUDA regrid prolongation");
            ++kernel_count;
        }
        for (const auto& group : plan.restrictions) {
            DeviceRegridChildren children{};
            for (int child = 0; child < (1 << restriction.dimension); ++child)
                children.blocks[child] = current_block(*sources.at(group.children[child].handle));
            check_cuda(launch_cuda_regrid_restriction(children,
                current_block(*destinations.at(group.destination.handle)),
                {impl_->launch.density_floor, impl_->launch.minimum_internal_energy,
                 impl_->launch.maximum_internal_energy},
                workspace.get(), workspace.size(), device_status.get(), impl_->stream.get()),
                "launch CUDA regrid restriction");
            ++kernel_count;
        }
        check_cuda(cudaMemcpyAsync(&status, device_status.get(), sizeof(int),
            cudaMemcpyDeviceToHost, impl_->stream.get()), "download CUDA migration status");
        impl_->checked_quiesce("complete staged CUDA regrid migration");
        guard.completed = true;
        impl_->runtime_counters.kernel_count += kernel_count;
        impl_->runtime_counters.bytes_d2h += sizeof(int);
        if (status != 0)
            throw std::runtime_error(amr::regrid_math::status_message(
                static_cast<amr::regrid_math::Status>(status)));
        for (auto& [arena, initialized] : staged.uploaded_current)
            initialized = StoreTransaction::Impl::kInteriorUploaded;
    } catch (...) {
        staged.upload_failed = true;
        throw;
    }
}

void CudaBackend::complete_staged_current_ghosts(
    StoreTransaction& transaction,
    std::span<const amr::SameLevelExchangePlan> same_level,
    const amr::CoarseFineTransferPlan& coarse_fine,
    std::span<const int> active_ids,
    std::span<const amr::BlockHandle> active_handles,
    const amr::CoordinateSeamPlan* coordinate_seam)
{
    if (!transaction.impl_ || transaction.impl_->consumed
        || transaction.impl_->owner.get() != impl_.get())
        throw std::invalid_argument("invalid CUDA staged ghost transaction");
    auto& staged = *transaction.impl_;
    if (staged.upload_failed || coarse_fine.scope.from_epoch != staged.candidate.scope().to_epoch)
        throw std::invalid_argument("invalid CUDA staged ghost scope");
    for (const auto& [arena, initialized] : staged.uploaded_current)
        if (initialized != StoreTransaction::Impl::kInteriorUploaded)
            throw std::logic_error("staged ghosts require complete migrated interiors");
    std::vector<Access> accesses;
    std::map<amr::BlockHandle, Access> by_handle;
    for (const auto& entry : staged.candidate.entries()) {
        const Access access{entry.record.handle, entry.record.storage, state::StateSlot::Current};
        accesses.push_back(access);
        by_handle.emplace(access.block, access);
    }
    // Each proposed block must occur in exactly one same-level group, even
    // when that group has no neighbor transfers.
    std::set<amr::BlockHandle> covered;
    std::vector<std::vector<Access>> level_accesses;
    for (const auto& plan : same_level) {
        auto& level = level_accesses.emplace_back();
        for (const auto& endpoint : plan.blocks) {
            level.push_back(by_handle.at(endpoint.handle));
            if (!covered.insert(endpoint.handle).second)
                throw std::invalid_argument("duplicate staged ghost level binding");
        }
    }
    if (covered.size() != accesses.size())
        throw std::invalid_argument("incomplete staged ghost level bindings");
    const Impl::BlockResolver resolve = [&](Access access) -> CudaBlockRuntime& {
        return resolve_staged_block(staged, access);
    };
    if (accesses.empty())
        throw std::invalid_argument("staged CUDA boundary domain is empty");
    std::vector<int> ids_for_access;
    if (coordinate_seam && !coordinate_seam->transfers.empty()) {
        if (active_ids.size() != active_handles.size()
            || active_ids.size() != accesses.size())
            throw std::invalid_argument(
                "staged CUDA coordinate seam bindings are incomplete");
        std::map<amr::BlockHandle, int> id_by_handle;
        for (std::size_t index = 0; index < active_ids.size(); ++index)
            if (!id_by_handle.emplace(active_handles[index],
                    active_ids[index]).second)
                throw std::invalid_argument(
                    "duplicate staged CUDA coordinate seam handle");
        ids_for_access.reserve(accesses.size());
        for (const auto& access : accesses)
            ids_for_access.push_back(id_by_handle.at(access.block));
    }
    impl_->select_device();
    CudaQuiescenceGuard guard{*impl_};
    try {

            std::uint64_t kernels = 0;
            for (const auto& access : accesses) {
                auto& block = resolve(access);
                check_cuda(launch_cuda_backend_boundary_plan(block.require_access(access),
                    block.boundary_transfers.get(), block.boundary, impl_->stream.get()),
                    "launch staged CUDA physical boundary");
                for (const auto& phase : block.boundary.phases) if (phase.count > 0) ++kernels;
            }
            impl_->checked_quiesce("complete staged CUDA physical boundaries");
            impl_->runtime_counters.kernel_count += kernels;
            for (std::size_t index = 0; index < same_level.size(); ++index)
                impl_->execute_same_level_exchange(level_accesses[index], same_level[index],
                    state::StateSlot::Current, resolve);
            impl_->execute_coarse_fine_exchange(accesses, coarse_fine, state::StateSlot::Current, resolve);
            if(coarse_fine.dimension==2&&!coarse_fine.operations.empty())
                for(std::size_t index=0;index<same_level.size();++index)
                    impl_->execute_same_level_exchange(level_accesses[index],
                        amr::make_same_level_corner_completion_plan(same_level[index]),
                        state::StateSlot::Current,resolve);
            if (coordinate_seam && !coordinate_seam->transfers.empty()) {
                impl_->execute_coordinate_seam_exchange(
                    accesses, ids_for_access, *coordinate_seam,
                    state::StateSlot::Current, resolve);
            }
        
        impl_->checked_quiesce("complete staged CUDA Current ghosts");
        guard.completed = true;
        for (auto& [arena, initialized] : staged.uploaded_current)
            initialized = StoreTransaction::Impl::kCompleteCurrent;
    } catch (...) {
        staged.upload_failed = true;
        throw;
    }
}

} // namespace arch::cuda
