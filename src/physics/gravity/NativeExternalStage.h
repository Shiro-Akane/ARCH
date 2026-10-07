/**
 * @file NativeExternalStage.h
 * @brief Borrow one actual Host native external-source stage and its receipts.
 *
 * Workflow:
 * 1. GravityStage alone constructs this nonmoving frame after actual Runtime,
 *    configuration, chart, axis regularity and journal preflight have passed.
 * 2. Reuse the existing whole-domain boundary authority: O(N) preparation,
 *    O(1) indexed patch checks, and one O(N) recheck after workers join.
 * 3. Each patch atomically claims one source visit. Its receipt rechecks the
 *    original policy, dt, stage/slot/ghost identity and seven allocation leases.
 * 4. Commit only finite measured body-source budgets; an abandoned or failed
 *    claim cannot be reused. Complete-domain acceptance requires every patch.
 * 5. The owner detaches the policy borrow and invalidates this generation
 *    before rotation, rollback or destruction. No raw fields are copied.
 *
 * Budgets represent actual source additions:
 *   radial = sum(delta m_r,V * V), axial = sum(delta m_z,V * V),
 *   torque = sum(delta m_phi,W * W), work = sum(delta E,V * V).
 * The stage dt is already in the source increment. GravityStage applies the
 * actual RK tableau weight once to the accepted stage budget, outside this
 * header. No face-work receipt, EOS substitute or physical qualification is
 * inferred from source_descriptor(), a callback or this accounting.
 */
#pragma once

#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

#include "driver/schedule/StageScheduler.h"
#include "data/GlobalDefs.h"
#include "numerics/integrator/HydroBoundaryAuthority.h"
#include "physics/gravity/GravitySource.h"
#include "physics/gravity/IGravityPolicy.h"

namespace arch::driver { class GravityStage; }

namespace Physical::Gravity {

/** Four signed integrals measured from the actual patch dU body-source adds.
 * These values carry neither a numerical tolerance nor an RK weight. Species,
 * density, enuc and hydro face-register accounting belong to their old owners.
 */
struct NativeBodySourceBudget {
    long double radial_momentum{};
    long double axial_momentum{};
    long double torque{};
    long double work{};
};

/** Scoped nonmoving stage frame; only the real GravityStage can construct it.
 * Its lifetime includes all worker receipts and ends only after workers join.
 * Root owns actual Runtime authorization and private policy attachment. A
 * public descriptor alone cannot construct or publish this frame.
 */
class NativeExternalStageFrame final {
private:
    enum class Phase : unsigned char { Ready, Claimed, Consumed, Failed };

    /** Preallocated independent receipt slot; never moved or shared by two visits.
     * Release publication of Consumed protects the finite budget written by its
     * one claimant. The main acceptance thread reads it with acquire ordering.
     */
    struct Consumption {
        std::atomic<Phase> phase{Phase::Ready};
        NativeBodySourceBudget budget{};
    };

public:
    /** Move-only claim of one actual source patch; no ownership of field arrays.
     * An uncommitted destructor marks Failed and performs no throwing check.
     * Moving transfers the same claim; it never reopens Ready or issues a new
     * generation. Receipt lifetime must be contained in the frame lifetime.
     */
    class PatchReceipt final {
    public:
        PatchReceipt(const PatchReceipt&) = delete;
        PatchReceipt& operator=(const PatchReceipt&) = delete;

        /** Transfer one existing claim without touching source or accounting. */
        PatchReceipt(PatchReceipt&& other) noexcept
            : frame_(std::exchange(other.frame_,nullptr)),index_(other.index_),
              block_id_(other.block_id_),state_(other.state_),grid_(other.grid_),
              generation_(other.generation_) {}

        /** Abandon the old claim before accepting a different live claim. */
        PatchReceipt& operator=(PatchReceipt&& other) noexcept {
            if(this!=&other) {
                abandon();
                frame_=std::exchange(other.frame_,nullptr);
                index_=other.index_;block_id_=other.block_id_;
                state_=other.state_;grid_=other.grid_;generation_=other.generation_;
            }
            return *this;
        }

        /** A failed/abandoned source visit cannot become an accepted receipt. */
        ~PatchReceipt() { abandon(); }

        /** Return the original local (g_r,g_z,g_phi) only after real lease checks.
         * Source mathematics borrows this immutable view for the current call;
         * it must not retain it beyond the frame or reinterpret its components.
         */
        const ExternalGravityView& external() const {
            require_input();
            return frame_->external_;
        }

        /** Revalidate this exact claimed patch before any source commit.
         * Every failure permanently marks this claim Failed. Catching the
         * exception cannot revive it or retry against repaired input storage.
         */
        void require_input() const {
            if(!frame_)throw std::logic_error("Native external receipt is not live");
            try {
                frame_->require_claim(index_,block_id_,*state_,*grid_,generation_);
            } catch(...) {
                fail();
                throw;
            }
        }

        /** Authenticate the source helper's actual operands before its math/write.
         * Workflow: retain the live claim/patch checks; match the original input
         * and Grid objects, exact stage dt and configured physical bounds; then
         * compare the entire value-only native view with this actual Grid. An
         * exception permanently fails this claim. No EOS, allocation, topology
         * scan or numerical tolerance is introduced by application identity.
         */
        void require_application(const FluidState& input,const Grid& grid,
            const GridMetrics::GeometryView& geometry,double dt,
            const arch::state::Bounds& bounds) const {
            if(!frame_)throw std::logic_error("Native external receipt is not live");
            try {
                require_input();
                if(&input!=state_||&grid!=grid_
                    ||!NativeExternalStageFrame::same_double(dt,frame_->dt_)
                    ||!NativeExternalStageFrame::same_bounds(bounds,frame_->physical_bounds_))
                    throw std::logic_error("Native external application changed input, interval or physical bounds");
                const auto actual=GridMetrics::make_geometry_view(grid,
                    GridMetrics::GeometrySemantics::AxisymmetricRz);
                if(!NativeExternalStageFrame::same_geometry(geometry,actual))
                    throw std::logic_error("Native external application changed its actual native geometry view");
            } catch(...) {
                fail();
                throw;
            }
        }

        /** Publish one finite measured budget after all patch source additions.
         * This operation intentionally may throw: stale identity or a nonfinite
         * budget cannot be hidden by noexcept termination or an accepted prefix.
         * Allocation and numerical field writes remain outside this method.
         */
        void commit(const NativeBodySourceBudget& budget) {
            require_input();
            if(!NativeExternalStageFrame::finite(budget)) {
                fail();
                throw std::runtime_error("Native external body budget is nonfinite");
            }
            auto& record=frame_->consumption_[index_];
            record.budget=budget;
            // A final scalar live/policy check precedes publication. Complete
            // patch identity was checked above; no callback or field operation
            // occurs between that check and the release transition.
            try {
                frame_->require_live();
            } catch(...) {
                fail();
                throw;
            }
            Phase expected=Phase::Claimed;
            if(!record.phase.compare_exchange_strong(expected,Phase::Consumed,
                std::memory_order_release,std::memory_order_relaxed)) {
                fail();
                throw std::logic_error("Native external receipt was already consumed or failed");
            }
            frame_=nullptr;
        }

    private:
        friend class NativeExternalStageFrame;

        /** Bind an already successful CAS to immutable actual input references. */
        PatchReceipt(const NativeExternalStageFrame& frame,std::size_t index,
            int block_id,const FluidState& state,const Grid& grid) noexcept
            : frame_(&frame),index_(index),block_id_(block_id),state_(&state),
              grid_(&grid),generation_(frame.generation_) {}

        /** Mark only our unconsumed claim Failed; never overwrite Consumed.
         * The owner must join all workers before invalidating/destroying records.
         */
        void fail() const noexcept {
            if(!frame_)return;
            Phase expected=Phase::Claimed;
            (void)frame_->consumption_[index_].phase.compare_exchange_strong(
                expected,Phase::Failed,std::memory_order_release,
                std::memory_order_relaxed);
        }

        /** Retire a moved-from/live handle without any fallible identity work. */
        void abandon() noexcept {
            fail();
            frame_=nullptr;
        }

        const NativeExternalStageFrame* frame_{};
        std::size_t index_{};
        int block_id_{};
        const FluidState* state_{};
        const Grid* grid_{};
        std::uint64_t generation_{};
    };

    NativeExternalStageFrame(const NativeExternalStageFrame&) = delete;
    NativeExternalStageFrame& operator=(const NativeExternalStageFrame&) = delete;
    NativeExternalStageFrame(NativeExternalStageFrame&&) = delete;
    NativeExternalStageFrame& operator=(NativeExternalStageFrame&&) = delete;

    /** End the metadata lifetime; the owner must already have joined workers.
     * Invalidating cannot make dangling receipt pointers safe, so destroying a
     * frame before its receipts is explicitly outside this borrowing contract.
     */
    ~NativeExternalStageFrame() { invalidate(); }

    /** Borrow the existing immutable boundary domain after source attachment.
     * Patch wall authorities can reuse it instead of preparing another domain.
     * Their own indexed checks remain mandatory; this is not scientific EOS
     * acceptance and does not perform an O(N) scan on a worker.
     */
    const arch::boundary::HostHydroBoundaryDomainAuthority& boundary_domain() const {
        require_live();
        return domain_;
    }

    /** Claim one body-source visit using the actual pool inverse active index.
     * Validate every original identity before the Ready->Claimed CAS. This
     * method checks one patch in constant work; it never scans unrelated input
     * states, invokes EOS, modifies dU or authorizes a new Runtime owner.
     */
    PatchReceipt claim_patch(const amr::AMRControl* control,int block_id,
        const FluidState& input,const Grid& grid,double dt,
        const IGravityPolicy& actual_policy) const
    {
        require_live();
        if(control!=control_||&actual_policy!=policy_||block_id<0
            ||control_->pool.get()!=pool_||control_->tree.get()!=tree_
            ||!same_double(dt,dt_))
            throw std::logic_error("Native external source consumer changed owner or interval");
        const auto& block=control_->pool->GetBlock(block_id);
        if(block.active_index<0
            ||static_cast<std::size_t>(block.active_index)>=patch_count_)
            throw std::logic_error("Native external source patch index is outside its domain");
        const auto index=static_cast<std::size_t>(block.active_index);
        domain_.require_input_patch(index,control,block_id,input,grid);
        Phase expected=Phase::Ready;
        if(!consumption_[index].phase.compare_exchange_strong(expected,Phase::Claimed,
            std::memory_order_acq_rel,std::memory_order_acquire))
            throw std::logic_error("Native external source patch was already claimed");
        return PatchReceipt(*this,index,block_id,input,grid);
    }

    /** Accept complete source consumption only after all patch workers join.
     * Recheck the original whole domain, require every release-published finite
     * budget, and sum in actual active order. Overflow rejects; no rescaling,
     * invented conservation allowance or partial accepted prefix is introduced.
     */
    NativeBodySourceBudget require_complete_consumption() const {
        require_live();
        domain_.require_complete_domain();
        NativeBodySourceBudget total{};
        for(std::size_t index=0;index<patch_count_;++index) {
            const auto& record=consumption_[index];
            if(record.phase.load(std::memory_order_acquire)!=Phase::Consumed
                ||!finite(record.budget))
                throw std::logic_error("Native external stage source consumption is incomplete");
            total.radial_momentum+=record.budget.radial_momentum;
            total.axial_momentum+=record.budget.axial_momentum;
            total.torque+=record.budget.torque;
            total.work+=record.budget.work;
            if(!finite(total))
                throw std::runtime_error("Native external complete body budget overflowed");
        }
        require_live();
        return total;
    }

    /** Retire this frame before rollback/slot rotation; never re-enable it.
     * Actual policy detach and generation issuance belong to GravityStage.
     * Input mutation concurrent with workers remains an unsupported data race;
     * this atomic is a liveness marker, not a lock for public scientific arrays.
     */
    void invalidate() noexcept {
        live_.store(false,std::memory_order_release);
    }

private:
    friend class arch::driver::GravityStage;

    /** Capture one actual, already authorized Host stage with bounded metadata.
     * The existing domain constructor validates actual slot/ghost publications,
     * true grid ownership, boundary time/purpose and all seven storage leases.
     * No policy attachment occurs until GravityStage accepts this constructor.
     */
    NativeExternalStageFrame(const IGravityPolicy& policy,const BCHandler& boundary,
        const amr::AMRControl& control,const arch::scheduler::StageBinding& binding,
        const arch::scheduler::StageDescriptor& descriptor,ExternalGravityView external,
        const SimConfig& configuration,double dt,std::uint64_t generation)
        : policy_(&policy),control_(&control),pool_(control.pool.get()),
          tree_(control.tree.get()),external_(external),
          configuration_(&configuration.physics.gravity),numerics_(&configuration.numerics),
          physical_bounds_{configuration.numerics.sml_rho,configuration.numerics.min_eint,
              configuration.numerics.max_eint},dt_(dt),
          generation_(generation),domain_(boundary,control,binding,descriptor),
          patch_count_(control.tree->GetActiveBlocks().size()),
          consumption_(std::make_unique<Consumption[]>(patch_count_))
    {
        if(!std::isfinite(dt_)||!(dt_>0.)||generation_==0||patch_count_==0
            ||!finite_external(external_)||!external_.enabled
            ||!same_double(dt_,binding.context.step_dt))
            throw std::invalid_argument("Native external stage source data or generation is invalid");
        const auto description=policy_->source_descriptor();
        if(description.origin!=GravitySourceOrigin::NativeExternalOrthonormal
            ||!same_external(description.external,external_))
            throw std::invalid_argument("Native external stage description disagrees with source data");
        require_configuration();
        // Actual Runtime/configuration/axis/source authority is the friend
        // caller's responsibility. No nonempty callback or enum grants it here.
    }

    /** Compare scientific identity bits, including signed zero, without a tolerance. */
    static bool same_double(double first,double second) noexcept {
        return std::bit_cast<std::uint64_t>(first)==std::bit_cast<std::uint64_t>(second);
    }

    /** Compare only the three borrowed physical limits, with original bits. */
    static bool same_bounds(const arch::state::Bounds& first,
        const arch::state::Bounds& second) noexcept {
        return same_double(first.density,second.density)
            &&same_double(first.internal_min,second.internal_min)
            &&same_double(first.internal_max,second.internal_max);
    }

    /** Compare every value field of the shared view, never structure padding.
     * The expected view is produced by the sole actual Grid/chart authority;
     * this is metadata identity, not a second coordinate/measure calculation.
     */
    static bool same_geometry(const GridMetrics::GeometryView& first,
        const GridMetrics::GeometryView& second) noexcept {
        if(first.geometry!=second.geometry||first.dim!=second.dim||first.ng!=second.ng
            ||first.stride_y!=second.stride_y||first.stride_z!=second.stride_z
            ||first.total_size!=second.total_size||first.semantics!=second.semantics
            ||!same_double(first.dx1,second.dx1)||!same_double(first.dx2,second.dx2)
            ||!same_double(first.dx3,second.dx3)||!same_double(first.x1_min,second.x1_min)
            ||!same_double(first.x2_min,second.x2_min)||!same_double(first.x3_min,second.x3_min))return false;
        const auto& a=first.dyadic_identity;const auto& b=second.dyadic_identity;
        if(a.bound!=b.bound||a.root_blocks!=b.root_blocks||a.level!=b.level
            ||a.logical!=b.logical||a.periodic_axial!=b.periodic_axial)return false;
        for(std::size_t axis=0;axis<2;++axis)
            if(!same_double(first.actual_block_upper[axis],second.actual_block_upper[axis])
                ||!same_double(a.root_lower[axis],b.root_lower[axis])
                ||!same_double(a.root_upper[axis],b.root_upper[axis]))return false;
        return true;
    }

    /** Check only source-view representability; configuration/chart authority is upstream. */
    static bool finite_external(const ExternalGravityView& view) noexcept {
        return std::isfinite(view.g_x)&&std::isfinite(view.g_y)&&std::isfinite(view.g_z);
    }

    /** Require exact original configured acceleration and enabled bit. */
    static bool same_external(const ExternalGravityView& first,
        const ExternalGravityView& second) noexcept
    {
        return first.enabled==second.enabled&&same_double(first.g_x,second.g_x)
            &&same_double(first.g_y,second.g_y)&&same_double(first.g_z,second.g_z);
    }

    /** Reject unusable measured accounting without changing a physical state. */
    static bool finite(const NativeBodySourceBudget& budget) noexcept {
        return std::isfinite(budget.radial_momentum)&&std::isfinite(budget.axial_momentum)
            &&std::isfinite(budget.torque)&&std::isfinite(budget.work);
    }

    /** Check original private attachment, typed source data and scalar liveness.
     * source_descriptor is discovery data; the private attachment identifies
     * the prepared frame. Root freezes policy storage until worker completion.
     */
    void require_live() const {
        if(!live_.load(std::memory_order_acquire)
            ||policy_->prepared_native_external()!=this)
            throw std::logic_error("Native external stage frame is retired or detached");
        const auto description=policy_->source_descriptor();
        if(description.origin!=GravitySourceOrigin::NativeExternalOrthonormal
            ||!same_external(description.external,external_))
            throw std::logic_error("Native external stage source description changed");
        require_configuration();
    }

    /** Recheck the actual Runtime source inputs, not a second configurable copy. */
    void require_configuration() const {
        if(configuration_->type!="external"||!same_double(configuration_->g_x,external_.g_x)
            ||!same_double(configuration_->g_y,external_.g_y)
            ||!same_double(configuration_->g_z,external_.g_z)
            ||!arch::state::valid_bounds(physical_bounds_)
            ||!same_bounds(physical_bounds_,
                {numerics_->sml_rho,numerics_->min_eint,numerics_->max_eint}))
            throw std::logic_error("Native external Runtime configuration changed");
    }

    /** Recheck one claim's actual original input and captured generation. */
    void require_claim(std::size_t index,int block_id,const FluidState& state,
        const Grid& grid,std::uint64_t generation) const
    {
        require_live();
        if(generation!=generation_||index>=patch_count_
            ||consumption_[index].phase.load(std::memory_order_acquire)!=Phase::Claimed)
            throw std::logic_error("Native external patch claim is not its original generation");
        domain_.require_input_patch(index,control_,block_id,state,grid);
    }

    const IGravityPolicy* policy_;
    const amr::AMRControl* control_;
    const amr::MemoryPool* pool_;
    const amr::AmrTree* tree_;
    const ExternalGravityView external_;
    const GravityConfig* configuration_;
    const NumericsConfig* numerics_; // Original SimConfig owner, not a copied control set.
    const arch::state::Bounds physical_bounds_; // Immutable stage identity of its three limits.
    const double dt_;
    const std::uint64_t generation_;
    const arch::boundary::HostHydroBoundaryDomainAuthority domain_;
    const std::size_t patch_count_;
    std::unique_ptr<Consumption[]> consumption_;
    std::atomic<bool> live_{true};
};

} // namespace Physical::Gravity
