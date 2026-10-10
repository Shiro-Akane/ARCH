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
 * 4. The Device branch reuses that same claim/failure machine with frozen
 *    Root-issued backend access and geometry metadata plus Root recheck
 *    callbacks; no device support is claimed and no callback alone proves real
 *    Runtime authority.
 * 5. Commit only finite measured body-source budgets; an abandoned or failed
 *    claim cannot be reused. Complete-domain acceptance requires every patch.
 * 6. The owner detaches the policy borrow and invalidates this generation
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
#include <span>
#include <stdexcept>
#include <utility>

#include "driver/runtime/ComputeBackend.h"
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

    /** Frozen Root-owned Device metadata for one synchronous stage.
     * Both callbacks are Root's real Runtime/domain authority; a nonempty
     * pointer is not by itself proof that such authority exists. The two spans
     * borrow the issuer's stable metadata for the whole stage and never copy a
     * fluid state, grid object or device array. Zero/null on the Host branch.
     */
    using DeviceDomain=NativeGravityDeviceDomain;

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

    /** Move-only claim of one Root-authorized device patch; no ownership of any
     * device storage. An uncommitted destructor marks Failed and performs no
     * throwing check. Moving transfers the same claim; it never reopens Ready
     * or issues a new generation. The receipt borrows the frozen Root-owned
     * access and geometry entries at its own index and copies neither fluid
     * state nor a grid/device pointer. Receipt lifetime must be contained in
     * the frame lifetime.
     */
    class DevicePatchReceipt final {
    public:
        DevicePatchReceipt(const DevicePatchReceipt&) = delete;
        DevicePatchReceipt& operator=(const DevicePatchReceipt&) = delete;

        /** Transfer one existing claim without touching source or accounting. */
        DevicePatchReceipt(DevicePatchReceipt&& other) noexcept
            : frame_(std::exchange(other.frame_,nullptr)),index_(other.index_),
              access_(other.access_),geometry_(other.geometry_),
              generation_(other.generation_) {}

        /** Abandon the old claim before accepting a different live claim. */
        DevicePatchReceipt& operator=(DevicePatchReceipt&& other) noexcept {
            if(this!=&other) {
                abandon();
                frame_=std::exchange(other.frame_,nullptr);
                index_=other.index_;access_=other.access_;geometry_=other.geometry_;
                generation_=other.generation_;
            }
            return *this;
        }

        /** A failed/abandoned device visit cannot become an accepted receipt. */
        ~DevicePatchReceipt() { abandon(); }

        /** Return the original local (g_r,g_z,g_phi) only after real checks.
         * The view is the frame's frozen immutable source data, not a device
         * array; it must not be retained beyond the frame or reinterpreted.
         */
        const ExternalGravityView& external() const {
            require_input();
            return frame_->external_;
        }

        /** Revalidate this exact claimed device patch before any source commit.
         * Every failure permanently marks this claim Failed. Catching the
         * exception cannot revive it or retry against repaired device metadata.
         */
        void require_input() const {
            if(!frame_)throw std::logic_error("Native external device receipt is not live");
            try {
                frame_->require_device_claim(index_,generation_);
            } catch(...) {
                fail();
                throw;
            }
        }

        /** Authenticate the source helper's actual operands before its math/write.
         * Workflow: retain the live claim and the Root require_patch recheck,
         * which runs through require_input; then match the frozen
         * BackendStateAccess identity, the entire value-only geometry view, the
         * exact stage dt and the configured physical bounds. An exception
         * permanently fails this claim. No EOS, allocation, domain scan or
         * numerical tolerance is introduced by application identity.
         */
        void require_application(const arch::backend::BackendStateAccess& access,
            const GridMetrics::GeometryView& geometry,double dt,
            const arch::state::Bounds& bounds) const {
            if(!frame_)throw std::logic_error("Native external device receipt is not live");
            try {
                require_input();
                if(!NativeExternalStageFrame::same_access(access,*access_)
                    ||!NativeExternalStageFrame::same_geometry(geometry,*geometry_)
                    ||!NativeExternalStageFrame::same_double(dt,frame_->dt_)
                    ||!NativeExternalStageFrame::same_bounds(bounds,frame_->physical_bounds_))
                    throw std::logic_error("Native external device application changed access, geometry, interval or physical bounds");
            } catch(...) {
                fail();
                throw;
            }
        }

        /** Publish one finite measured budget after all patch source additions.
         * This operation intentionally may throw: stale identity or a nonfinite
         * budget cannot be hidden by noexcept termination or an accepted prefix.
         * Allocation and numerical device writes remain outside this method.
         */
        void commit(const NativeBodySourceBudget& budget) {
            require_input();
            if(!NativeExternalStageFrame::finite(budget)) {
                fail();
                throw std::runtime_error("Native external device body budget is nonfinite");
            }
            auto& record=frame_->consumption_[index_];
            record.budget=budget;
            // A final scalar live/policy check precedes publication. Complete
            // patch identity was checked above; no callback or device operation
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
                throw std::logic_error("Native external device receipt was already consumed or failed");
            }
            frame_=nullptr;
        }

    private:
        friend class NativeExternalStageFrame;

        /** Bind an already successful CAS to the frozen Root-owned span entries. */
        DevicePatchReceipt(const NativeExternalStageFrame& frame,std::size_t index) noexcept
            : frame_(&frame),index_(index),access_(&frame.device_.accesses[index]),
              geometry_(&frame.device_.geometries[index]),generation_(frame.generation_) {}

        /** Mark only our unconsumed claim Failed; never overwrite Consumed. */
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
        const arch::backend::BackendStateAccess* access_{};
        const GridMetrics::GeometryView* geometry_{};
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
     * The Device branch has no Host boundary domain and rejects before any
     * dereference instead of fabricating one.
     */
    const arch::boundary::HostHydroBoundaryDomainAuthority& boundary_domain() const {
        require_live();
        if(!domain_)
            throw std::logic_error("Native external device frame has no Host boundary domain");
        return *domain_;
    }

    /** Validate the original whole Device domain before any consumer launch.
     * Reuse the issuer's actual Runtime transaction check and return the frozen
     * patch count. This is a read-only identity check, never a consumption or
     * completion grant; each patch still needs its original move-only receipt.
     */
    std::size_t require_device_domain() const {
        require_live();
        if(domain_)
            throw std::logic_error("Native external Host frame has no Device domain");
        device_.require_domain(device_.owner);
        return patch_count_;
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
        // Reject the Device branch before any Host fluid or domain dereference.
        if(!domain_)
            throw std::logic_error("Native external Host patch claim requires the Host boundary domain");
        if(control!=control_||&actual_policy!=policy_||block_id<0
            ||control_->pool.get()!=pool_||control_->tree.get()!=tree_
            ||!same_double(dt,dt_))
            throw std::logic_error("Native external source consumer changed owner or interval");
        const auto& block=control_->pool->GetBlock(block_id);
        if(block.active_index<0
            ||static_cast<std::size_t>(block.active_index)>=patch_count_)
            throw std::logic_error("Native external source patch index is outside its domain");
        const auto index=static_cast<std::size_t>(block.active_index);
        domain_->require_input_patch(index,control,block_id,input,grid);
        Phase expected=Phase::Ready;
        if(!consumption_[index].phase.compare_exchange_strong(expected,Phase::Claimed,
            std::memory_order_acq_rel,std::memory_order_acquire))
            throw std::logic_error("Native external source patch was already claimed");
        return PatchReceipt(*this,index,block_id,input,grid);
    }

    /** Claim one measured device-source visit using Root's frozen metadata order.
     * Validate the Device branch, actual policy, captured pool/tree identity,
     * frozen access identity, frozen geometry view and exact stage interval
     * before the Ready->Claimed CAS, and run the Root require_patch callback
     * before that transition. Wrong branch, stale metadata and duplicate claims
     * reject. Work per patch is constant: this never scans the device domain,
     * and it authorizes no view, clock, ledger, storage, BC or transaction
     * change by itself.
     */
    DevicePatchReceipt claim_device_patch(std::size_t index,
        arch::backend::BackendStateAccess access,const GridMetrics::GeometryView& geometry,
        double dt,const IGravityPolicy& actual_policy) const
    {
        require_live();
        // The caller passes no control on this branch, so the captured pooled
        // owner identity is rechecked against the frame's own pool and tree.
        if(domain_||&actual_policy!=policy_
            ||control_->pool.get()!=pool_||control_->tree.get()!=tree_
            ||index>=patch_count_||!same_double(dt,dt_))
            throw std::logic_error("Native external device source consumer changed branch, owner or interval");
        if(!same_access(access,device_.accesses[index])
            ||!same_geometry(geometry,device_.geometries[index]))
            throw std::logic_error("Native external device source patch metadata is not the frozen expected entry");
        device_.require_patch(device_.owner,index,access,geometry);
        Phase expected=Phase::Ready;
        if(!consumption_[index].phase.compare_exchange_strong(expected,Phase::Claimed,
            std::memory_order_acq_rel,std::memory_order_acquire))
            throw std::logic_error("Native external device source patch was already claimed");
        return DevicePatchReceipt(*this,index);
    }

    /** Accept complete source consumption only after all patch workers join.
     * Recheck the original whole domain, require every release-published finite
     * budget, and sum in actual active order. Overflow rejects; no rescaling,
     * invented conservation allowance or partial accepted prefix is introduced.
     * Domain acceptance selects the Host authority or the Root Device callback
     * before the common loop; the Host fluid path is never dereferenced on the
     * Device branch.
     */
    NativeBodySourceBudget require_complete_consumption() const {
        require_live();
        if(domain_)
            domain_->require_complete_domain();
        else
            device_.require_domain(device_.owner);
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
          generation_(generation),
          domain_(std::make_unique<arch::boundary::HostHydroBoundaryDomainAuthority>(
              boundary,control,binding,descriptor)),
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

    /** Capture one actual, already authorized Device stage with bounded metadata.
     * This overload borrows Root-owned frozen access/geometry spans and the two
     * Root recheck callbacks. It copies no fluid state, grid object, device
     * array or Host boundary authority and needs neither BCHandler nor
     * StageBinding. A nonempty callback is not proof of real Runtime authority:
     * the actual issuer must supply the real Runtime object, only GravityStage
     * can call this constructor, and the branch stays unreachable until Root
     * integrates that issuer. No policy attachment occurs in the constructor.
     */
    NativeExternalStageFrame(const IGravityPolicy& policy,const amr::AMRControl& control,
        ExternalGravityView external,const SimConfig& configuration,double dt,
        std::uint64_t generation,const DeviceDomain& domain)
        : policy_(&policy),control_(&control),pool_(control.pool.get()),
          tree_(control.tree.get()),external_(external),
          configuration_(&configuration.physics.gravity),numerics_(&configuration.numerics),
          physical_bounds_{configuration.numerics.sml_rho,configuration.numerics.min_eint,
              configuration.numerics.max_eint},dt_(dt),
          generation_(generation),domain_(nullptr),device_(domain),
          patch_count_(domain.accesses.size()),
          consumption_(std::make_unique<Consumption[]>(patch_count_))
    {
        if(domain.owner==nullptr||!domain.require_domain||!domain.require_patch)
            throw std::invalid_argument("Native external device stage lacks Root-owned metadata callbacks");
        if(device_.accesses.empty()||device_.accesses.size()!=device_.geometries.size()
            ||patch_count_!=control.tree->GetActiveBlocks().size())
            throw std::invalid_argument("Native external device stage metadata does not match the active domain");
        if(!std::isfinite(dt_)||!(dt_>0.)||generation_==0
            ||!finite_external(external_)||!external_.enabled)
            throw std::invalid_argument("Native external device stage source data or generation is invalid");
        const auto description=policy_->source_descriptor();
        if(description.origin!=GravitySourceOrigin::NativeExternalOrthonormal
            ||!same_external(description.external,external_))
            throw std::invalid_argument("Native external device stage description disagrees with source data");
        require_configuration();
        // Actual Runtime/view/clock/ledger/storage/BC/transaction authority
        // stays with Root; require_patch runs per claim, before its CAS.
        device_.require_domain(device_.owner);
        // No device support, measured kernel budget or physical qualification
        // follows from a supplied owner or callback.
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

    /** Compare the four actual backend identity fields named by the contract.
     * Key, epoch, storage and slot are compared exactly; no tolerance, address
     * comparison or second device lookup is introduced here.
     */
    static bool same_access(const arch::backend::BackendStateAccess& first,
        const arch::backend::BackendStateAccess& second) noexcept {
        return first.block.uid.value==second.block.uid.value
            &&first.block.epoch.value==second.block.epoch.value
            &&first.storage.value==second.storage.value
            &&first.slot==second.slot;
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
        if(!domain_||generation!=generation_||index>=patch_count_
            ||consumption_[index].phase.load(std::memory_order_acquire)!=Phase::Claimed)
            throw std::logic_error("Native external patch claim is not its original generation");
        domain_->require_input_patch(index,control_,block_id,state,grid);
    }

    /** Recheck one device claim's frozen identity and run Root's recheck.
     * The frame borrows the Root-owned span entries, so the frozen access and
     * geometry passed back to the callback are exactly the claimed metadata.
     */
    void require_device_claim(std::size_t index,std::uint64_t generation) const
    {
        require_live();
        if(domain_||generation!=generation_||index>=patch_count_
            ||consumption_[index].phase.load(std::memory_order_acquire)!=Phase::Claimed)
            throw std::logic_error("Native external device patch claim is not its original generation");
        device_.require_patch(device_.owner,index,device_.accesses[index],
            device_.geometries[index]);
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
    /** Host-only prepared boundary domain; null exactly on the Device branch. */
    const std::unique_ptr<const arch::boundary::HostHydroBoundaryDomainAuthority> domain_;
    /** Zero/null callbacks with empty spans on the Host branch. */
    const DeviceDomain device_{};
    const std::size_t patch_count_;
    std::unique_ptr<Consumption[]> consumption_;
    std::atomic<bool> live_{true};
};

} // namespace Physical::Gravity
