/**
 * @file CudaBackendMacroStep.cpp
 * @brief Protect resident fields for the original Runtime Hydro attempt.
 * D2D backups use existing DeviceStateStorage and the backend's ordered stream.
 * Topology remains frozen; restore copies into the same original allocations.
 */
#include "cuda/runtime/control/CudaBackendInternal.h"
#include "cuda/runtime/control/CudaBackendMacroStep.h"

namespace arch::cuda {
namespace {
bool same_storage(DeviceStateView a,DeviceStateView b) noexcept {
    return a.rho==b.rho&&a.mom_u==b.mom_u&&a.mom_v==b.mom_v&&a.mom_w==b.mom_w
        &&a.eng==b.eng&&a.enuc_rate==b.enuc_rate&&a.mass_fractions==b.mass_fractions
        &&a.total_size==b.total_size&&a.n_species==b.n_species;
}
cudaError_t copy_fields(DeviceStateView from,DeviceStateView to,cudaStream_t stream) noexcept {
    const std::array sources{from.rho,from.mom_u,from.mom_v,from.mom_w,from.eng,from.enuc_rate,from.mass_fractions};
    const std::array destinations{to.rho,to.mom_u,to.mom_v,to.mom_w,to.eng,to.enuc_rate,to.mass_fractions};
    for(std::size_t field=0;field<sources.size();++field) {
        const std::size_t count=static_cast<std::size_t>(from.total_size)*(field==6?from.n_species:1);
        if(!count)continue;
        const auto error=cudaMemcpyAsync(destinations[field],sources[field],count*sizeof(double),
            cudaMemcpyDeviceToDevice,stream);
        if(error!=cudaSuccess)return error;
    }
    return cudaSuccess;
}
}

void CudaBackend::begin_hydro_attempt(std::span<const backend::BackendStateAccess> currents) {
    if(impl_->hydro_attempt||impl_->store.has_staged_transaction()
        ||currents.size()!=impl_->store.active_entries().size())
        throw std::logic_error("CUDA Hydro snapshot requires the complete quiescent active store");
    validate_hydro_batch_accesses(currents);
    if(!impl_->active_amr_flux||impl_->active_amr_flux->captured_stage
        ||impl_->active_amr_flux->register_upload_ready)
        throw std::logic_error("CUDA Hydro snapshot requires consumed flux stage buffers");
    impl_->select_device();
    impl_->checked_quiesce("drain before CUDA Hydro snapshot");
    auto saved=std::make_unique<CudaHydroAttempt>(impl_->species_count);
    CudaQuiescenceGuard guard{*impl_};
    saved->flux=impl_->active_amr_flux.get();
    saved->stage_repairs=stage_repairs;saved->reflux_repairs=reflux_repairs;
    saved->blocks.reserve(currents.size());
    // All allocations precede any enqueue; no partial snapshot can become active.
    for(auto access:currents) {
        auto& block=impl_->require_block(access);
        auto entry=std::make_unique<CudaHydroAttempt::Block>();
        entry->access=access;entry->owner=&block;entry->original_slots=block.slots;
        for(int slot=0;slot<3;++slot)
            entry->saved[slot].allocate(block.slots[slot].total_size,block.slots[slot].n_species);
        saved->blocks.push_back(std::move(entry));
    }
    for(const auto& block:saved->blocks)for(int slot=0;slot<3;++slot)
        check_cuda(copy_fields(block->original_slots[slot],block->saved[slot].view(),impl_->stream.get()),
            "copy CUDA Hydro entry state");
    impl_->checked_quiesce("complete CUDA Hydro snapshot");
    guard.completed=true;
    impl_->hydro_attempt=std::move(saved);
}

bool CudaBackend::hydro_attempt_layout_matches() const noexcept {
    const auto* saved=impl_->hydro_attempt.get();
    if(!saved||impl_->store.has_staged_transaction()
        ||saved->flux!=impl_->active_amr_flux.get()
        ||saved->blocks.size()!=impl_->store.active_entries().size())return false;
    for(const auto& entry:saved->blocks) {
        if(!impl_->store.contains(entry->access))return false;
        const auto* block=impl_->find_block(entry->access.block);
        if(block!=entry->owner||block->generation!=entry->access.storage)return false;
        unsigned found=0;
        for(const auto& live:block->slots) {
            int original=0;
            while(original<3&&!same_storage(live,entry->original_slots[original]))++original;
            if(original==3||(found&(1u<<original)))return false;
            found|=1u<<original;
        }
        if(found!=7)return false;
    }
    return true;
}

void CudaBackend::restore_hydro_attempt_noexcept() noexcept {
    if(!hydro_attempt_layout_matches())std::terminate();
    impl_->quiesce_or_terminate();
    for(const auto& block:impl_->hydro_attempt->blocks) {
        for(int slot=0;slot<3;++slot)
            require_cuda_success_or_terminate(copy_fields(block->saved[slot].view(),
                block->original_slots[slot],impl_->stream.get()));
        block->owner->slots=block->original_slots;
    }
    impl_->quiesce_or_terminate();
    stage_repairs=std::move(impl_->hydro_attempt->stage_repairs);
    reflux_repairs=std::move(impl_->hydro_attempt->reflux_repairs);
    // Stage packets/registers are scratch, never an accepted field authority.
    impl_->active_amr_flux->captured_stage.reset();
    impl_->active_amr_flux->register_upload_ready=false;
}
void CudaBackend::release_hydro_attempt_noexcept() noexcept {
    if(!impl_->hydro_attempt)return;
    impl_->quiesce_or_terminate();
    impl_->hydro_attempt.reset();
}
void CudaBackend::validate_hydro_current(std::span<const backend::BackendStateAccess> currents) {
    validate_hydro_batch_accesses(currents);
    impl_->select_device();
    auto& scratch=impl_->hydro_batch;
    scratch.ensure_capacity(currents.size());
    if(currents.empty())return;
    CudaQuiescenceGuard guard{*impl_};
    for(std::size_t i=0;i<currents.size();++i) {
        auto& block=impl_->require_block(currents[i]);
        visit_eos(impl_->eos,[&](const auto& eos) {
            check_cuda(launch_cuda_validate_current(block.require_access(currents[i]),block.grid,eos,
                {impl_->launch.density_floor,impl_->launch.minimum_internal_energy,
                 impl_->launch.maximum_internal_energy},impl_->species_workspace,
                scratch.status->get()+i,impl_->stream.get()),"validate CUDA Hydro endpoint");
        });
        ++impl_->runtime_counters.kernel_count;
    }
    check_cuda(cudaMemcpyAsync(scratch.host_status.data(),scratch.status->get(),
        currents.size()*sizeof(int),cudaMemcpyDeviceToHost,impl_->stream.get()),"download Hydro endpoint status");
    impl_->runtime_counters.bytes_d2h+=currents.size()*sizeof(int);
    impl_->checked_quiesce("complete CUDA Hydro endpoint validation");guard.completed=true;
    for(std::size_t i=0;i<currents.size();++i)
        if(scratch.host_status[i])throw std::runtime_error("Distributed Device Hydro final EOS rejected");
}
} // namespace arch::cuda
