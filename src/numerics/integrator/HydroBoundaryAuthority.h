/**
 * @file HydroBoundaryAuthority.h
 * @brief Borrow one real Host native-RZ Hydro input and its physical-wall frame.
 *
 * Workflow:
 * 1. Receive the actual main-thread StageBinding explicitly; never consult
 *    worker thread-local bindings inside a parallel patch evaluation.
 * 2. Locate the exact active pool block and descriptor input member; require
 *    completed Host interior/ghost ledger versions for that same handle/key.
 * 3. Capture an empty BCHandler frame at t_in=t_step+c_in*dt_step, without
 *    array copies, EOS evaluation, allocations or numerical publications.
 * 4. Before face/cache/output mutation, recheck the borrowed owners, spans,
 *    own handle/key/coherence, full descriptor and boundary frame. Only then
 *    return the flat mathematical wall flags to the selected shared producer.
 *
 * Runtime/transaction owns complete-domain identity and exclusive lifetimes.
 * This scoped borrow freezes its own patch and verifies current domain span
 * agreement; it does not snapshot all unrelated patch metadata. Callable
 * presence and an empty frame never substitute for genuine Runtime science.
 */
#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "amr/AMRControl.h"
#include "driver/schedule/StageScheduler.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"

namespace arch::boundary {

class HostHydroBoundaryDomainAuthority;

/** Noncopyable scoped authority; all borrowed owners must outlive evaluation.
 * The numerical input remains the actual pooled native U, never an effective
 * thermodynamic mean. There is no extra evolved or serialized state here.
 */
class HostHydroBoundaryAuthority {
public:
    /** Freeze the actual input after completed whole-domain boundary work.
     * Construction can reject; it never publishes fields, cache or ledger data.
     */
    HostHydroBoundaryAuthority(const BCHandler& boundary,
        const amr::AMRControl& control,int block_id,
        const scheduler::StageBinding& binding,
        const scheduler::StageDescriptor& descriptor,
        const FluidState& state,const Grid& grid)
        : boundary_(&boundary),control_(&control),binding_(&binding),
          context_(&binding.context),descriptor_(&descriptor),stage_(descriptor),
          block_id_(block_id),state_(&state),grid_(&grid)
    {
        if(!control.pool||!control.tree||block_id<0)
            throw std::logic_error("Native Hydro wall authority lacks actual AMR owners");
        pool_=control.pool.get();tree_=control.tree.get();
        const auto& active=tree_->GetActiveBlocks();
        const auto handles=control.ActiveHandles();
        if(active.empty()||handles.size()!=active.size()
            ||binding.handles.size()!=active.size())
            throw std::logic_error("Native Hydro wall authority domain extent mismatch");
        active_address_=active.data();active_size_=active.size();
        handles_address_=handles.data();handles_size_=handles.size();
        binding_handles_address_=binding.handles.data();
        binding_handles_size_=binding.handles.size();
        bool found=false;
        for(std::size_t index=0;index<active.size();++index)if(active[index]==block_id) {
            if(found)throw std::logic_error("Native Hydro wall authority repeats its pool block");
            found=true;active_index_=index;
        }
        if(!found)throw std::logic_error("Native Hydro wall authority block is not active");
        block_=&pool_->GetBlock(block_id);
        if(!block_->active||block_->id!=block_id||&block_->grid!=&grid
            ||&input_state(*block_,stage_.input_slot)!=&state)
            throw std::logic_error("Native Hydro wall authority input is not its actual pool member");
        block_->RequireNativeGeometryIdentity();
        block_active_index_=block_->active_index;level_=block_->level;
        logical_={block_->logical_x1,block_->logical_x2,block_->logical_x3};
        morton_=block_->morton_code;
        ledger_=&context_->ledger;clock_=&context_->clock;
        handle_=binding.handles[active_index_];key_={handle_,stage_.input_slot};
        start_bits_=bits(context_->step_start_time);dt_bits_=bits(context_->step_dt);
        configured_=bool(context_->configure_boundary_context);
        post_boundary_=bool(context_->post_boundary_acceptance);
        expected_time_=input_time(*context_,stage_);
        require_context();require_domain(control);
        coherence_=ledger_->inspect(key_);
        ledger_->require_readable(key_,{arch::state::ExecutionSide::Host,
            coherence_.interior.version,true,true});
        frame_=boundary_->capture_native_hydro_frame(state,grid,expected_time_);
        walls_=boundary_->native_hydro_boundary_view(frame_,state,grid,expected_time_);
    }

    /** Constant-work borrow of one immutable, fully preflighted stage record. */
    HostHydroBoundaryAuthority(const HostHydroBoundaryDomainAuthority&,
        std::size_t active_index,int block_id,const FluidState&,const Grid&);

    HostHydroBoundaryAuthority(const HostHydroBoundaryAuthority&)=delete;
    HostHydroBoundaryAuthority& operator=(const HostHydroBoundaryAuthority&)=delete;
    HostHydroBoundaryAuthority(HostHydroBoundaryAuthority&&)=delete;
    HostHydroBoundaryAuthority& operator=(HostHydroBoundaryAuthority&&)=delete;

    /** Return trusted flat flags only after every captured patch identity agrees.
     * The caller invokes this before dU/cache/flux-buffer mutation; null or a
     * foreign controller fails closed. Successful validation is allocation-free.
     */
    HydroBoundaryView require_view(const amr::AMRControl* control,int block_id,
        const FluidState& state,const Grid& grid) const
    {
        if(domain_)return require_domain_patch_view(control,block_id,state,grid);
        if(!control||control!=control_||block_id!=block_id_
            ||&state!=state_||&grid!=grid_)
            throw std::logic_error("Native Hydro wall authority consumer changed owner");
        require_context();require_domain(*control);
        const auto& block=pool_->GetBlock(block_id_);
        if(&block!=block_||!block.active||block.id!=block_id_
            ||block.active_index!=block_active_index_||block.level!=level_
            ||block.morton_code!=morton_
            ||std::array<std::uint32_t,3>{block.logical_x1,block.logical_x2,block.logical_x3}!=logical_
            ||&block.grid!=grid_||&input_state(block,stage_.input_slot)!=state_)
            throw std::logic_error("Native Hydro wall authority pool identity changed");
        block.RequireNativeGeometryIdentity();
        const auto current=ledger_->inspect(key_);
        if(!same_coherence(current,coherence_))
            throw std::logic_error("Native Hydro wall authority input publication changed");
        ledger_->require_readable(key_,{arch::state::ExecutionSide::Host,
            coherence_.interior.version,true,true});
        const auto result=boundary_->native_hydro_boundary_view(frame_,state,grid,expected_time_);
        if(result.reflecting!=walls_.reflecting)
            throw std::logic_error("Native Hydro wall authority physical faces changed");
        return result;
    }

private:
    friend class HostHydroBoundaryDomainAuthority;
    /** Reuse the immutable stage record while keeping the old consumer API. */
    HydroBoundaryView require_domain_patch_view(const amr::AMRControl*,int,
        const FluidState&,const Grid&) const;
    const HostHydroBoundaryDomainAuthority* domain_=nullptr;
    std::size_t domain_index_=0;
    /** Exact floating identity is independent of numerical tolerances. */
    static std::uint64_t bits(double value) noexcept {
        return std::bit_cast<std::uint64_t>(value);
    }
    /** Select the one actual pooled scheduler input member, including Scratch. */
    static const FluidState& input_state(const amr::Block& block,arch::state::StateSlot slot) {
        switch(slot) {
        case arch::state::StateSlot::Current:return block.fluid_state;
        case arch::state::StateSlot::Next:return block.state_next;
        case arch::state::StateSlot::Scratch:return block.state_scratch;
        }
        throw std::logic_error("Native Hydro wall authority has an invalid input slot");
    }
    /** Share the real Hydro scheduler equation; Dhalf boundary clocks do not
     * identify Hydro stage abscissae. A configured Runtime interval is positive;
     * an unconfigured local scientific context may have dt=0, without becoming
     * proof of a Runtime owner. Nonfinite operands/result always reject.
     */
    static double input_time(const scheduler::StageExecutionContext& context,
        const scheduler::StageDescriptor& descriptor) {
        if(!std::isfinite(context.step_start_time)||!std::isfinite(context.step_dt)
            ||!std::isfinite(descriptor.input_time_fraction)
            ||(context.configure_boundary_context&&!(context.step_dt>0.)))
            throw std::logic_error("Native Hydro wall authority has an invalid Hydro clock");
        const double result=context.step_start_time+descriptor.input_time_fraction*context.step_dt;
        if(!std::isfinite(result))
            throw std::logic_error("Native Hydro wall authority input time is not finite");
        return result;
    }
    /** Compare every descriptor field, including exact floating representations. */
    static bool same_descriptor(const scheduler::StageDescriptor& a,
        const scheduler::StageDescriptor& b) noexcept {
        return scheduler::same_stage_descriptor(a,b)
            &&bits(a.old_weight)==bits(b.old_weight)
            &&bits(a.update_weight)==bits(b.update_weight)
            &&bits(a.flux_register_weight)==bits(b.flux_register_weight)
            &&bits(a.input_time_fraction)==bits(b.input_time_fraction);
    }
    /** Compare completion and pending-transfer metadata without padding bytes. */
    static bool same_region(const arch::state::RegionCoherence& a,
        const arch::state::RegionCoherence& b) noexcept {
        return a.residency==b.residency&&a.version==b.version
            &&a.completion==b.completion&&a.pending_transfer==b.pending_transfer;
    }
    /** Both actual regions and their common source version are part of the lease. */
    static bool same_coherence(const arch::state::SlotCoherence& a,
        const arch::state::SlotCoherence& b) noexcept {
        return same_region(a.interior,b.interior)&&same_region(a.ghost,b.ghost)
            &&a.ghost_source_version==b.ghost_source_version;
    }
    /** Validate the explicitly borrowed main context; no worker TLS lookup. */
    void require_context() const {
        if(&binding_->context!=context_||&context_->ledger!=ledger_||&context_->clock!=clock_
            ||context_->side!=arch::state::ExecutionSide::Host
            ||bool(context_->configure_boundary_context)!=configured_
            ||bool(context_->post_boundary_acceptance)!=post_boundary_
            ||!post_boundary_||!same_descriptor(*descriptor_,stage_)
            ||bits(context_->step_start_time)!=start_bits_||bits(context_->step_dt)!=dt_bits_
            ||bits(input_time(*context_,*descriptor_))!=bits(expected_time_))
            throw std::logic_error("Native Hydro wall authority stage/context changed");
    }
    /** Preserve original span leases and require current complete span agreement.
     * Only this patch's original handle is snapshotted; Runtime/transaction owns
     * complete-domain publication identity and excludes concurrent retopology.
     */
    void require_domain(const amr::AMRControl& control) const {
        if(&control!=control_||control.pool.get()!=pool_||control.tree.get()!=tree_)
            throw std::logic_error("Native Hydro wall authority AMR owner changed");
        const auto& active=tree_->GetActiveBlocks();const auto handles=control.ActiveHandles();
        if(active.data()!=active_address_||active.size()!=active_size_
            ||handles.data()!=handles_address_||handles.size()!=handles_size_
            ||binding_->handles.data()!=binding_handles_address_
            ||binding_->handles.size()!=binding_handles_size_
            ||handles.size()!=active.size()||binding_->handles.size()!=active.size()
            ||active_index_>=active.size()||active[active_index_]!=block_id_
            ||handles[active_index_]!=handle_||binding_->handles[active_index_]!=handle_
            ||!amr::is_valid(handle_)||handle_.epoch!=ledger_->active_epoch())
            throw std::logic_error("Native Hydro wall authority domain lease changed");
        for(std::size_t index=0;index<handles.size();++index)
            if(handles[index]!=binding_->handles[index]||!amr::is_valid(handles[index])
                ||handles[index].epoch!=ledger_->active_epoch())
                throw std::logic_error("Native Hydro wall authority current handle spans disagree");
    }

    const BCHandler* boundary_;
    const amr::AMRControl* control_;
    const scheduler::StageBinding* binding_;
    const scheduler::StageExecutionContext* context_;
    const scheduler::StageDescriptor* descriptor_;
    scheduler::StageDescriptor stage_;
    int block_id_;
    const FluidState* state_;
    const Grid* grid_;
    const amr::MemoryPool* pool_=nullptr;
    const amr::AmrTree* tree_=nullptr;
    const amr::Block* block_=nullptr;
    const int* active_address_=nullptr;
    std::size_t active_size_=0,active_index_=0;
    const amr::BlockHandle* handles_address_=nullptr;
    const amr::BlockHandle* binding_handles_address_=nullptr;
    std::size_t handles_size_=0,binding_handles_size_=0;
    int block_active_index_=0,level_=0;
    std::array<std::uint32_t,3> logical_{};
    std::uint64_t morton_=0;
    const arch::state::StateResidencyLedger* ledger_=nullptr;
    const scheduler::MonotonicSchedulerClock* clock_=nullptr;
    amr::BlockHandle handle_{};
    arch::state::StateKey key_{};
    arch::state::SlotCoherence coherence_{};
    std::uint64_t start_bits_=0,dt_bits_=0;
    bool configured_=false,post_boundary_=false;
    double expected_time_=0.;
    BCHandler::NativeCandidate frame_;
    HydroBoundaryView walls_{};
};
/** One real synchronous Host stage borrows the entire immutable input domain.
 * Workflow: main-thread full preflight/capture -> read-only OMP patch borrows ->
 * workers join -> explicit complete-domain recheck -> existing stage acceptance.
 * This is identity metadata only; Runtime owns scientific EOS acceptance and
 * any rollback. No source/user/configure callback executes inside this phase.
 * Supported workers may change distinct output slots and flux registers, never
 * topology, input storage, BC/context, handles or allocation leases. Concurrent
 * direct writes to these public objects remain unsupported C++ data races;
 * there is no pretend lock or cross-stage cache here. The nonmoving owner must
 * outlive every patch borrow and expire before slot rotation or BC reconfigure.
 */
class HostHydroBoundaryDomainAuthority final {
public:
    /** Preflight every actual pool input before the first worker output changes.
     * A single reserved vector owns only bounded patch metadata/empty BC frames.
     * Every fallible frame capture finishes before the OMP read phase begins.
     */
    HostHydroBoundaryDomainAuthority(const BCHandler& boundary,
        const amr::AMRControl& control,const scheduler::StageBinding& binding,
        const scheduler::StageDescriptor& descriptor)
        : boundary_(&boundary),control_(&control),binding_(&binding),
          context_(&binding.context),descriptor_(&descriptor),stage_(descriptor)
    {
        if(!control.pool||!control.tree)
            throw std::logic_error("Native Hydro wall domain lacks actual AMR owners");
        pool_=control.pool.get();tree_=control.tree.get();
        ledger_=&context_->ledger;clock_=&context_->clock;epoch_=ledger_->active_epoch();
        const auto& active=tree_->GetActiveBlocks();const auto handles=control.ActiveHandles();
        if(active.empty()||handles.size()!=active.size()||binding.handles.size()!=active.size())
            throw std::logic_error("Native Hydro wall domain extent mismatch");
        active_address_=active.data();active_size_=active.size();
        handles_address_=handles.data();handles_size_=handles.size();
        binding_handles_address_=binding.handles.data();binding_handles_size_=binding.handles.size();
        start_bits_=PatchAuthority::bits(context_->step_start_time);
        dt_bits_=PatchAuthority::bits(context_->step_dt);
        configured_=bool(context_->configure_boundary_context);
        post_boundary_=bool(context_->post_boundary_acceptance);
        expected_time_=PatchAuthority::input_time(*context_,stage_);
        require_domain_owner();
        records_.reserve(active_size_);
        for(std::size_t index=0;index<active_size_;++index) {
            const int id=active[index];const auto handle=handles[index];
            if(id<0||handle!=binding.handles[index]||!amr::is_valid(handle)||handle.epoch!=epoch_)
                throw std::logic_error("Native Hydro wall domain has an invalid input identity");
            const auto& block=pool_->GetBlock(id);
            // This actual inverse relation also rejects repeated pool IDs: a
            // single block cannot own two distinct active indices at once.
            if(!block.active||block.id!=id||block.active_index<0
                ||static_cast<std::size_t>(block.active_index)!=index)
                throw std::logic_error("Native Hydro wall domain active inverse index changed");
            block.RequireNativeGeometryIdentity();
            const auto& input=PatchAuthority::input_state(block,stage_.input_slot);
            const arch::state::StateKey key{handle,stage_.input_slot};
            const auto coherence=ledger_->inspect(key);
            ledger_->require_readable(key,{arch::state::ExecutionSide::Host,
                coherence.interior.version,true,true});
            auto frame=boundary_->capture_native_hydro_frame(input,block.grid,expected_time_);
            const auto walls=boundary_->native_hydro_boundary_view(frame,input,block.grid,expected_time_);
            records_.push_back({id,&block,&input,&block.grid,block.active_index,block.level,
                {block.logical_x1,block.logical_x2,block.logical_x3},block.morton_code,
                handle,key,coherence,std::move(frame),walls});
        }
        require_complete_domain();
    }
    HostHydroBoundaryDomainAuthority(const HostHydroBoundaryDomainAuthority&)=delete;
    HostHydroBoundaryDomainAuthority& operator=(const HostHydroBoundaryDomainAuthority&)=delete;
    HostHydroBoundaryDomainAuthority(HostHydroBoundaryDomainAuthority&&)=delete;
    HostHydroBoundaryDomainAuthority& operator=(HostHydroBoundaryDomainAuthority&&)=delete;

    /** Check every original input after all workers join, before publication.
     * This explicit fallible gate also runs after numerical failure; callers
     * preserve their first numerical exception while still performing this
     * ownership check. No throwing destructor or scientific acceptance occurs.
     */
    void require_complete_domain() const {
        require_domain_owner();
        for(std::size_t index=0;index<records_.size();++index)
            (void)require_record(index,records_[index].id,*records_[index].state,*records_[index].grid);
        require_domain_owner();
    }

private:
    friend class HostHydroBoundaryAuthority;
    using PatchAuthority=HostHydroBoundaryAuthority;
    /** One immutable actual patch/key/lease record; no numerical array copy. */
    struct Record {
        int id;
        const amr::Block* block;
        const FluidState* state;
        const Grid* grid;
        int active_index,level;
        std::array<std::uint32_t,3> logical;
        std::uint64_t morton;
        amr::BlockHandle handle;
        arch::state::StateKey key;
        arch::state::SlotCoherence coherence;
        BCHandler::NativeCandidate frame;
        HydroBoundaryView walls;
    };
    /** Shared constant-sized owners/spans/clock/descriptor remain unchanged. */
    void require_domain_owner() const {
        if(!control_||control_->pool.get()!=pool_||control_->tree.get()!=tree_
            ||&binding_->context!=context_||&context_->ledger!=ledger_||&context_->clock!=clock_
            ||context_->side!=arch::state::ExecutionSide::Host||ledger_->active_epoch()!=epoch_
            ||bool(context_->configure_boundary_context)!=configured_
            ||bool(context_->post_boundary_acceptance)!=post_boundary_||!post_boundary_
            ||!PatchAuthority::same_descriptor(*descriptor_,stage_)
            ||PatchAuthority::bits(context_->step_start_time)!=start_bits_
            ||PatchAuthority::bits(context_->step_dt)!=dt_bits_
            ||PatchAuthority::bits(PatchAuthority::input_time(*context_,*descriptor_))
                !=PatchAuthority::bits(expected_time_))
            throw std::logic_error("Native Hydro wall domain stage/context changed");
        const auto& active=tree_->GetActiveBlocks();const auto handles=control_->ActiveHandles();
        if(active.data()!=active_address_||active.size()!=active_size_
            ||handles.data()!=handles_address_||handles.size()!=handles_size_
            ||binding_->handles.data()!=binding_handles_address_
            ||binding_->handles.size()!=binding_handles_size_
            ||handles.size()!=active.size()||binding_->handles.size()!=active.size())
            throw std::logic_error("Native Hydro wall domain owner/span lease changed");
    }
    /** Recheck one actual record in constant work, using its known loop index. */
    HydroBoundaryView require_record(std::size_t index,int id,
        const FluidState& state,const Grid& grid) const {
        if(index>=records_.size())
            throw std::logic_error("Native Hydro wall domain patch index is outside its lease");
        const auto& record=records_[index];
        const auto& active=tree_->GetActiveBlocks();const auto handles=control_->ActiveHandles();
        if(id!=record.id||&state!=record.state||&grid!=record.grid
            ||active[index]!=record.id||handles[index]!=record.handle
            ||binding_->handles[index]!=record.handle||!amr::is_valid(record.handle)
            ||record.handle.epoch!=epoch_)
            throw std::logic_error("Native Hydro wall domain patch/key changed");
        const auto& block=pool_->GetBlock(id);
        if(&block!=record.block||!block.active||block.id!=id
            ||block.active_index!=record.active_index||block.level!=record.level
            ||block.morton_code!=record.morton
            ||std::array<std::uint32_t,3>{block.logical_x1,block.logical_x2,block.logical_x3}!=record.logical
            ||&block.grid!=record.grid||&PatchAuthority::input_state(block,stage_.input_slot)!=record.state)
            throw std::logic_error("Native Hydro wall domain actual pool input changed");
        block.RequireNativeGeometryIdentity();
        if(!PatchAuthority::same_coherence(ledger_->inspect(record.key),record.coherence))
            throw std::logic_error("Native Hydro wall domain input publication changed");
        ledger_->require_readable(record.key,{arch::state::ExecutionSide::Host,
            record.coherence.interior.version,true,true});
        const auto result=boundary_->native_hydro_boundary_view(record.frame,state,grid,expected_time_);
        if(result.reflecting!=record.walls.reflecting)
            throw std::logic_error("Native Hydro wall domain physical faces changed");
        return result;
    }
    /** Patch entry repeats constant-size shared owners plus its own full lease. */
    HydroBoundaryView require_patch_view(std::size_t index,const amr::AMRControl* control,
        int id,const FluidState& state,const Grid& grid) const {
        if(!control||control!=control_)
            throw std::logic_error("Native Hydro wall domain consumer changed controller");
        require_domain_owner();
        return require_record(index,id,state,grid);
    }
    const BCHandler* boundary_;
    const amr::AMRControl* control_;
    const scheduler::StageBinding* binding_;
    const scheduler::StageExecutionContext* context_;
    const scheduler::StageDescriptor* descriptor_;
    scheduler::StageDescriptor stage_;
    const amr::MemoryPool* pool_=nullptr;
    const amr::AmrTree* tree_=nullptr;
    const arch::state::StateResidencyLedger* ledger_=nullptr;
    const scheduler::MonotonicSchedulerClock* clock_=nullptr;
    const int* active_address_=nullptr;
    std::size_t active_size_=0;
    const amr::BlockHandle* handles_address_=nullptr;
    const amr::BlockHandle* binding_handles_address_=nullptr;
    std::size_t handles_size_=0,binding_handles_size_=0;
    amr::TopologyEpoch epoch_{};
    std::uint64_t start_bits_=0,dt_bits_=0;
    bool configured_=false,post_boundary_=false;
    double expected_time_=0.;
    std::vector<Record> records_;
};

/** Borrow a fully preflighted stage record without scanning other patches. */
inline HostHydroBoundaryAuthority::HostHydroBoundaryAuthority(
    const HostHydroBoundaryDomainAuthority& domain,std::size_t active_index,
    int block_id,const FluidState& state,const Grid& grid)
    : domain_(&domain),domain_index_(active_index),boundary_(nullptr),control_(nullptr),
      binding_(nullptr),context_(nullptr),descriptor_(nullptr),block_id_(-1),
      state_(nullptr),grid_(nullptr)
{
    (void)domain.require_patch_view(active_index,domain.control_,
        block_id,state,grid);
}

/** The original mathematical consumer keeps one exact per-patch entry gate. */
inline HydroBoundaryView HostHydroBoundaryAuthority::require_domain_patch_view(
    const amr::AMRControl* control,int block_id,const FluidState& state,const Grid& grid) const {
    return domain_->require_patch_view(domain_index_,control,block_id,state,grid);
}

} // namespace arch::boundary
