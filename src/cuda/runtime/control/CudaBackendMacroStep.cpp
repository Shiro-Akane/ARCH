/**
 * @file CudaBackendMacroStep.cpp
 * @brief Retain and restore the actual resident fields of one macro attempt.
 *
 * Workflow:
 * 1. Preflight the complete committed Current lease batch and fixed resources.
 * 2. Join the backend stream before growing reusable private backup buffers.
 * 3. Copy all three allocations, valid controls and observer/AMR surface planes
 *    through the existing D2D leaves; retain their original pointer permutation.
 * 4. Validate original storage before the caller commits the complete macro.
 * 5. On abandonment, join failed work, restore saved fields and bindings, then
 *    join restoration before releasing the same owner. Restoration failure is
 *    fatal because a partly restored scientific state cannot be published.
 *
 * Runtime owns source, ledger, boundary frame, budgets and scheduler metadata.
 * This storage service defines no physics, EOS query or numerical coefficient;
 * counters retain work actually performed, including unsuccessful attempts.
 */

#include "cuda/runtime/control/CudaBackendInternal.h"

#include <algorithm>
#include <array>
#include <utility>

namespace arch::cuda {
namespace {

/** One live compact plane and its exact allocated/valid retained extent. */
template <class Value>
struct MacroPlaneSnapshot {
    Value* pointer = nullptr;
    std::size_t elements = 0;
};

/** Actual owners and original logical bindings, separate from reusable capacity. */
struct MacroBlockSnapshot {
    backend::BackendStateAccess access{};
    CudaBlockRuntime* block = nullptr;
    CudaBackend::Impl::MacroStateScratch::Block* backup = nullptr;
    std::array<DeviceStateView, 3> slots{}, physical{};
    std::array<std::array<MacroPlaneSnapshot<boundary::ScalarBoundaryCondition>, 6>, 3>
        controls{};
    std::array<std::array<std::size_t, 6>, 3> control_capacity{};
    std::array<std::array<const boundary::ScalarBoundaryCondition*, 6>, 3>
        control_allocation{};
    std::array<MacroPlaneSnapshot<double>, 6> observer_stage{}, observer_initial{},
        amr_register{}, amr_initial{};
    std::array<int, 6> register_cells{}, register_species{}, initial_cells{}, initial_species{};
    std::array<bool, 6> observer_owned{};
    std::array<std::size_t, 6> observer_active{}, observer_stage_capacity{}, observer_initial_capacity{};
    double observer_weight = 1., observer_initial_weight = 0.;
    bool observer_save_initial = false;
};

/** Compare state allocation/shape only; stage-owned control/capture views vary. */
bool same_macro_state_allocation(DeviceStateView a, DeviceStateView b) noexcept
{
    return a.rho == b.rho && a.mom_u == b.mom_u && a.mom_v == b.mom_v
        && a.mom_w == b.mom_w && a.eng == b.eng && a.enuc_rate == b.enuc_rate
        && a.mass_fractions == b.mass_fractions && a.total_size == b.total_size
        && a.n_species == b.n_species;
}

/** Return the exact physical allocation behind an actual logical slot view. */
std::size_t macro_physical_slot(const MacroBlockSnapshot& saved, DeviceStateView view)
{
    for (std::size_t slot = 0; slot < saved.physical.size(); ++slot)
        if (same_macro_state_allocation(saved.physical[slot], view)) return slot;
    throw std::logic_error("CUDA macro slot lost its original allocation owner");
}

/** Existing DiffusionBoundaryView fixes records to actual tangential face cells. */
std::size_t macro_control_elements(const CudaBlockRuntime& block, int face)
{
    const int extent[3]{block.grid.ie - block.grid.is, block.grid.je - block.grid.js,
        block.grid.ke - block.grid.ks};
    const int direction = face / 2;
    const std::size_t cells = static_cast<std::size_t>(extent[(direction + 1) % 3])
        * static_cast<std::size_t>(extent[(direction + 2) % 3]);
    return cells * static_cast<std::size_t>(4 + block.state_storage[0].species_count);
}

/** Borrow only the actual saved shape from grow-only private state storage. */
DeviceStateView macro_backup_state(const MacroBlockSnapshot& saved, std::size_t slot)
{
    DeviceStateView view = saved.backup->states[slot]->view();
    view.total_size = saved.slots[slot].total_size;
    return view;
}

/** Enqueue an exact compact D2D plane copy; no field transfer or kernel is added. */
template <class Value>
void copy_macro_plane(Value* destination, const Value* source, std::size_t elements,
                      cudaStream_t stream, const char* operation)
{
    if (elements == 0) return;
    check_cuda(cudaMemcpyAsync(destination, source, elements * sizeof(Value),
        cudaMemcpyDeviceToDevice, stream), operation);
}

/**
 * @brief Actual resident lifetime/rollback owner, independent of Runtime policy.
 *
 * All saved buffers belong to the retained implementation and are reused only
 * after owner-stream completion. An unsuccessful snapshot never changes live
 * state. Once armed, rollback cannot be abandoned: ownership or CUDA failure
 * terminates rather than exposing a partially restored scientific state.
 */
class CudaMacroStateTransaction final : public backend::BackendMacroStateTransaction {
public:
    /** Pin the original implementation and the preflighted original bindings. */
    CudaMacroStateTransaction(std::shared_ptr<CudaBackend::Impl> owner,
                             std::vector<MacroBlockSnapshot> snapshots)
        : owner_(std::move(owner)), saved_(std::move(snapshots)),
          flux_plan_(owner_->active_amr_flux.get()),
          namespace_size_(owner_->active_resources.size())
    {
    }

    /** Uncommitted destruction restores pinned allocations, surfaces and bindings. */
    ~CudaMacroStateTransaction() override
    {
        if (armed_ && !consumed_) rollback_noexcept();
    }

    /** Copy already preflighted owners into already allocated private buffers. */
    void save()
    {
        CudaQuiescenceGuard guard{*owner_};
        for (const auto& saved : saved_) {
            for (std::size_t slot = 0; slot < saved.slots.size(); ++slot) {
                check_cuda(copy_cuda_backend_state_slot(saved.slots[slot],
                    macro_backup_state(saved, slot), owner_->stream.get()),
                    "save CUDA macro state slot");
                for (int face = 0; face < 6; ++face)
                    copy_macro_plane(saved.backup->controls[slot][face].get(),
                        saved.controls[slot][face].pointer,
                        saved.controls[slot][face].elements, owner_->stream.get(),
                        "save CUDA macro scalar controls");
            }
            copy_surfaces(saved, false);
        }
        owner_->checked_quiesce("synchronize CUDA macro savepoint");
        guard.completed = true;
        // Construction/copy failures before this point leave live arrays intact
        // and do not acquire the namespace pin or require a rollback.
        owner_->macro_state.active = true;
        armed_ = true;
    }

    /** Check real storage ownership/permutations and prove stream completion. */
    void validate_storage() const override
    {
        require_owners();
        owner_->checked_quiesce("validate CUDA macro retained storage");
    }

    /** Release saved state only; Runtime publishes/commits its own metadata. */
    void commit() noexcept override
    {
        if (consumed_) return;
        if (!armed_ || !owner_->macro_state.active) std::terminate();
        owner_->macro_state.active = false;
        consumed_ = true;
    }

private:
    /** Validate actual allocation owners without inventing another identity issuer. */
    void require_owners() const
    {
        if (!armed_ || consumed_ || !owner_->macro_state.active
            || owner_->store.has_staged_transaction()
            || owner_->active_resources.size() != namespace_size_
            || owner_->active_amr_flux.get() != flux_plan_)
            throw std::logic_error("CUDA macro savepoint namespace changed");
        for (const auto& saved : saved_) {
            const auto& block = owner_->require_block(saved.access);
            if (&block != saved.block || block.handle != saved.access.block
                || block.generation != saved.access.storage)
                throw std::logic_error("CUDA macro block owner changed");
            std::array<bool, 3> seen{};
            for (std::size_t slot = 0; slot < saved.physical.size(); ++slot) {
                if (!same_macro_state_allocation(block.state_storage[slot].view(), saved.physical[slot]))
                    throw std::logic_error("CUDA macro state allocation changed");
                const auto physical = macro_physical_slot(saved, block.slots[slot]);
                if (std::exchange(seen[physical], true))
                    throw std::logic_error("CUDA macro state permutation aliases a slot");
                for (int face = 0; face < 6; ++face) {
                    const auto& controls = block.user_boundary_controls[slot][face];
                    if (controls.get() != saved.control_allocation[slot][face]
                        || controls.size() != saved.control_capacity[slot][face])
                        throw std::logic_error("CUDA macro control allocation changed");
                }
            }
            const auto& observer = block.boundary_flux_observer;
            if (observer.owned != saved.observer_owned
                || observer.active_elements != saved.observer_active)
                throw std::logic_error("CUDA macro observer layout changed");
            for (int face = 0; face < 6; ++face) {
                require_plane(observer.stage[face], saved.observer_stage[face], saved.observer_stage_capacity[face]);
                require_plane(observer.initial[face], saved.observer_initial[face], saved.observer_initial_capacity[face]);
                require_plane(block.amr_flux_register[face].values, saved.amr_register[face]);
                require_plane(block.amr_initial_flux[face].values, saved.amr_initial[face]);
                if (block.amr_flux_register[face].cell_count != saved.register_cells[face]
                    || block.amr_flux_register[face].species_count != saved.register_species[face]
                    || block.amr_initial_flux[face].cell_count != saved.initial_cells[face]
                    || block.amr_initial_flux[face].species_count != saved.initial_species[face])
                    throw std::logic_error("CUDA macro AMR surface shape changed");
            }
        }
    }

    /** A retained compact plane must still have its exact allocation/extent. */
    template <class Allocation>
    static void require_plane(const Allocation& allocation, MacroPlaneSnapshot<double> saved,
                              std::size_t capacity = std::numeric_limits<std::size_t>::max())
    {
        if (capacity == std::numeric_limits<std::size_t>::max()) capacity = saved.elements;
        if (allocation.get() != saved.pointer || allocation.size() != capacity)
            throw std::logic_error("CUDA macro surface allocation changed");
    }

    /** Save or restore observer and AMR planes through the same D2D leaf. */
    void copy_surfaces(const MacroBlockSnapshot& saved, bool restore) const
    {
        const auto copy = [&](const auto& plane, const auto& backup) {
            if (restore)
                copy_macro_plane(plane.pointer, backup.get(), plane.elements,
                    owner_->stream.get(), "restore CUDA macro surface");
            else
                copy_macro_plane(backup.get(), plane.pointer, plane.elements,
                    owner_->stream.get(), "save CUDA macro surface");
        };
        for (int face = 0; face < 6; ++face) {
            copy(saved.observer_stage[face], saved.backup->observer_stage[face]);
            copy(saved.observer_initial[face], saved.backup->observer_initial[face]);
            copy(saved.amr_register[face], saved.backup->amr_register[face]);
            copy(saved.amr_initial[face], saved.backup->amr_initial[face]);
        }
    }

    /** Drain accepted/failed work, restore exact fields/views, then drain copies. */
    void rollback_noexcept() noexcept
    {
        try {
            owner_->checked_quiesce("drain failed CUDA macro before restore");
            require_owners();
            for (const auto& saved : saved_) {
                auto& block = *saved.block;
                block.slots = saved.slots;
                for (std::size_t slot = 0; slot < saved.slots.size(); ++slot) {
                    check_cuda(copy_cuda_backend_state_slot(macro_backup_state(saved, slot),
                        saved.slots[slot], owner_->stream.get()),
                        "restore CUDA macro state slot");
                    for (int face = 0; face < 6; ++face)
                        copy_macro_plane(saved.controls[slot][face].pointer,
                            saved.backup->controls[slot][face].get(),
                            saved.controls[slot][face].elements, owner_->stream.get(),
                            "restore CUDA macro scalar controls");
                    block.state_storage[slot].capture = saved.physical[slot].capture;
                }
                copy_surfaces(saved, true);
                auto& observer = block.boundary_flux_observer;
                observer.weight = saved.observer_weight;
                observer.initial_weight = saved.observer_initial_weight;
                observer.save_initial = saved.observer_save_initial;
            }
            owner_->checked_quiesce("synchronize CUDA macro restored state");
            owner_->macro_state.active = false;
            consumed_ = true;
        } catch (...) {
            // A bad restored state must never escape as a recoverable result.
            std::terminate();
        }
    }

    std::shared_ptr<CudaBackend::Impl> owner_;
    std::vector<MacroBlockSnapshot> saved_;
    CudaAmrFluxPlanRuntime* flux_plan_ = nullptr;
    std::size_t namespace_size_ = 0;
    bool armed_ = false, consumed_ = false;
};

} // namespace

/**
 * @brief Capture actual macro-entry fields after whole-batch storage preflight.
 *
 * Grow-only private buffers are allocated before any snapshot copy. The existing
 * scalar D2D copy preserves every bit in all six fields and species, including
 * full state padding. Valid scalar-control records follow their physical owner;
 * unused control capacity is never read. No scientific field reaches the Host.
 */
std::unique_ptr<backend::BackendMacroStateTransaction>
CudaBackend::begin_macro_state_transaction(
    std::span<const backend::BackendStateAccess> currents)
{
    impl_->macro_state.require_idle();
    if (currents.empty() || impl_->store.has_staged_transaction())
        throw std::invalid_argument("CUDA macro savepoint needs nonempty active Current leases");
    validate_hydro_batch_accesses(currents);
    // Existing Current validation already proves valid unique actual leases.
    // Equal cardinality with the committed store therefore requires complete
    // coverage, including every actual AMR register/reflux resource owner.
    const std::size_t committed = impl_->store.active_entries().size();
    if (currents.size() != committed || impl_->active_resources.size() != committed)
        throw std::invalid_argument("CUDA macro savepoint needs the complete committed active domain");
    std::vector<MacroBlockSnapshot> snapshots;
    snapshots.reserve(currents.size());
    // Validate every original allocation, binding and active control extent
    // before allocating or enqueuing a copy, including stale late entries.
    for (const auto current : currents) {
        auto& block = impl_->require_block(current);
        MacroBlockSnapshot saved{};
        saved.access = current;
        saved.block = &block;
        saved.slots = block.slots;
        for (std::size_t slot = 0; slot < saved.physical.size(); ++slot) {
            saved.physical[slot] = block.state_storage[slot].view();
            if (!valid_hydro_view(saved.physical[slot])
                || saved.physical[slot].total_size != block.grid.total_size
                || saved.physical[slot].n_species != impl_->species_count)
                throw std::logic_error("CUDA macro state shape disagrees with actual owner");
            for (int face = 0; face < 6; ++face) {
                const auto& controls = block.user_boundary_controls[slot][face];
                saved.control_capacity[slot][face] = controls.size();
                saved.control_allocation[slot][face] = controls.get();
            }
        }
        std::array<bool, 3> seen{};
        for (const auto view : saved.slots) {
            const std::size_t physical = macro_physical_slot(saved, view);
            if (std::exchange(seen[physical], true))
                throw std::logic_error("CUDA macro entry slots are not a permutation");
            for (int face = 0; face < 6; ++face) {
                const auto* pointer = view.diffusion_boundary.faces[face];
                if (!pointer) continue;
                const std::size_t elements = macro_control_elements(block, face);
                if (pointer != saved.control_allocation[physical][face]
                    || elements == 0 || saved.control_capacity[physical][face] < elements)
                    throw std::logic_error("CUDA macro bound scalar controls lack a valid actual extent");
                saved.controls[physical][face] = {const_cast<boundary::ScalarBoundaryCondition*>(pointer), elements};
            }
        }
        const auto& observer = block.boundary_flux_observer;
        saved.observer_owned = observer.owned;
        saved.observer_active = observer.active_elements;
        saved.observer_weight = observer.weight;
        saved.observer_initial_weight = observer.initial_weight;
        saved.observer_save_initial = observer.save_initial;
        for (int face = 0; face < 6; ++face) {
            // Preserve valid face planes only; retained capacity is not a
            // semantic shape and its unused tail has no readable payload.
            const std::size_t active = observer.owned[face] ? observer.active_elements[face] : 0;
            saved.observer_stage[face] = {observer.stage[face].get(), active};
            saved.observer_initial[face] = {observer.initial[face].get(), active};
            saved.observer_stage_capacity[face] = observer.stage[face].size();
            saved.observer_initial_capacity[face] = observer.initial[face].size();
            if (observer.owned[face] != (observer.active_elements[face] != 0)
                || observer.stage[face].size() < observer.active_elements[face]
                || observer.initial[face].size() < observer.active_elements[face])
                throw std::logic_error("CUDA macro observer has an invalid active extent");
            const auto& reg = block.amr_flux_register[face];
            const auto& initial = block.amr_initial_flux[face];
            saved.amr_register[face] = {reg.values.get(), reg.values.size()};
            saved.amr_initial[face] = {initial.values.get(), initial.values.size()};
            saved.register_cells[face] = reg.cell_count;
            saved.register_species[face] = reg.species_count;
            saved.initial_cells[face] = initial.cell_count;
            saved.initial_species[face] = initial.species_count;
            if (reg.values.size() != amr_flux_surface_scalar_count(reg.cell_count, reg.species_count)
                || initial.values.size() != amr_flux_surface_scalar_count(initial.cell_count, initial.species_count))
                throw std::logic_error("CUDA macro AMR surface allocation has an invalid shape");
        }
        snapshots.push_back(saved);
    }
    // No owner buffer is grown/replaced until all prior consumers have joined.
    impl_->checked_quiesce("prepare CUDA macro savepoint storage");
    auto& scratch = impl_->macro_state;
    while (scratch.blocks.size() < snapshots.size())
        scratch.blocks.push_back(std::make_unique<Impl::MacroStateScratch::Block>());
    for (std::size_t index = 0; index < snapshots.size(); ++index) {
        auto& saved = snapshots[index];
        saved.backup = scratch.blocks[index].get();
        for (std::size_t slot = 0; slot < saved.slots.size(); ++slot) {
            const int total = saved.slots[slot].total_size, species = saved.slots[slot].n_species;
            auto& storage = saved.backup->states[slot];
            if (!storage || storage->total_size < total || storage->species_count != species) {
                auto replacement = std::make_unique<DeviceStateStorage>();
                replacement->allocate(total, species);
                storage.swap(replacement);
            }
            for (int face = 0; face < 6; ++face)
                saved.backup->controls[slot][face].reserve(saved.controls[slot][face].elements);
        }
        for (int face = 0; face < 6; ++face) {
            saved.backup->observer_stage[face].reserve(saved.observer_stage[face].elements);
            saved.backup->observer_initial[face].reserve(saved.observer_initial[face].elements);
            saved.backup->amr_register[face].reserve(saved.amr_register[face].elements);
            saved.backup->amr_initial[face].reserve(saved.amr_initial[face].elements);
        }
    }
    auto transaction = std::make_unique<CudaMacroStateTransaction>(impl_, std::move(snapshots));
    transaction->save();
    return transaction;
}

/** Prepare physical controls capacity before the original resident savepoint.
 * Workflow:
 * 1. Authenticate ALL unique Current leases of the complete committed domain;
 *    reject staged topology or an active macro before any actual device work.
 * 2. Check actual grid/species/counts, all three physical states, the complete
 *    logical-slot permutation and every non-null original control binding.
 * 3. If any unbound plane needs growth, join this real owner stream once and
 *    reserve its requested capacity. Retain null bindings and unread capacity.
 * 4. Leave live values/pointers, state fields and all publication metadata intact.
 *
 * The BC owner supplies (4+N)*N_face counts from the original physical-face
 * selection. This service checks shape, not physical policy or callback data;
 * each later stage still evaluates and uploads its own actual producer values.
 */
void CudaBackend::prepare_boundary_control_capacity(
    std::span<const backend::BackendStateAccess> currents,
    std::span<const std::array<std::size_t,6>> face_counts) {
    impl_->macro_state.require_idle();
    if(currents.empty()||currents.size()!=face_counts.size()
        ||impl_->store.has_staged_transaction())
        throw std::invalid_argument("Boundary capacity requires actual Current domain/counts without staged topology");
    validate_hydro_batch_accesses(currents);
    const auto committed=impl_->store.active_entries().size();
    if(currents.size()!=committed||impl_->active_resources.size()!=committed)
        throw std::invalid_argument("Boundary capacity requires the complete committed Current domain");
    struct CapacityRequest {
        CudaBlockRuntime* block;
        std::array<std::size_t,6> counts;
    };
    std::vector<CapacityRequest> requests;
    requests.reserve(currents.size());
    bool grows=false;
    /** Compare allocation identity only; control/capture bindings are separate. */
    const auto same_allocation=[](DeviceStateView a,DeviceStateView b) {
        return a.rho==b.rho&&a.mom_u==b.mom_u&&a.mom_v==b.mom_v&&a.mom_w==b.mom_w
            &&a.eng==b.eng&&a.enuc_rate==b.enuc_rate&&a.mass_fractions==b.mass_fractions
            &&a.total_size==b.total_size&&a.n_species==b.n_species;
    };
    for(std::size_t index=0;index<currents.size();++index) {
        auto& block=impl_->require_block(currents[index]);
        const auto current=block.require_access(currents[index]);
        if(!valid_hydro_grid(block.grid)||!valid_hydro_view(current)
            ||current.total_size!=block.grid.total_size
            ||current.n_species!=impl_->species_count)
            throw std::logic_error("Boundary capacity requires actual complete grid/species storage");
        const auto& counts=face_counts[index];
        std::array<std::size_t,6> expected{};
        const std::size_t fields=4+static_cast<std::size_t>(current.n_species);
        for(int face=0;face<6;++face) {
            if(face/2<block.grid.dim) {
                const std::size_t cells=macro_control_elements(block,face)/fields;
                if(!cells||cells>std::numeric_limits<std::size_t>::max()/fields)
                    throw std::overflow_error("Boundary control capacity is not representable");
                expected[face]=cells*fields;
            }
            if(counts[face]&&counts[face]!=expected[face])
                throw std::invalid_argument("Boundary capacity count disagrees with the actual active face shape");
        }
        std::array<DeviceStateView,3> physical{};
        for(std::size_t owner=0;owner<physical.size();++owner) {
            physical[owner]=block.state_storage[owner].view();
            if(!valid_hydro_view(physical[owner])
                ||physical[owner].total_size!=block.grid.total_size
                ||physical[owner].n_species!=current.n_species)
                throw std::logic_error("Boundary capacity lost a physical state owner");
        }
        std::array<bool,3> seen{};
        std::array<std::array<bool,6>,3> bound{};
        for(const auto& logical:block.slots) {
            std::size_t owner=physical.size();
            for(std::size_t candidate=0;candidate<physical.size();++candidate)
                if(same_allocation(logical,physical[candidate]))owner=candidate;
            if(owner==physical.size()||seen[owner])
                throw std::logic_error("Boundary capacity logical slots are not actual physical-owner permutation");
            seen[owner]=true;
            for(int face=0;face<6;++face) {
                const auto* pointer=logical.diffusion_boundary.faces[face];
                if(!pointer)continue;
                const auto& allocation=block.user_boundary_controls[owner][face];
                if(!expected[face]||pointer!=allocation.get()||allocation.size()<expected[face])
                    throw std::logic_error("Boundary capacity found an invalid original control binding");
                bound[owner][face]=true;
            }
        }
        for(std::size_t owner=0;owner<physical.size();++owner)for(int face=0;face<6;++face) {
            const auto& allocation=block.user_boundary_controls[owner][face];
            if(counts[face]>allocation.size()) {
                if(bound[owner][face])
                    throw std::logic_error("Boundary capacity cannot replace a bound control allocation");
                grows=true;
            }
        }
        requests.push_back({&block,counts});
    }
    // Every possible failure from input/layout/frame checks precedes the first
    // device selection/join/allocation, including a malformed late domain row.
    // Empty shapes or sufficient capacity keep repeated calls metadata-only.
    if(!grows)return;
    impl_->select_device();
    impl_->checked_quiesce("prepare physical boundary control capacity");
    for(const auto& request:requests)
        for(auto& physical:request.block->user_boundary_controls)
            for(int face=0;face<6;++face)
                if(request.counts[face]>physical[face].size())
                    physical[face].reserve(request.counts[face]);
    // reserve grows only previously unbound capacity. No slot view is rebound,
    // no old valid plane is replaced and no newly allocated byte is consumed.
}


} // namespace arch::cuda
