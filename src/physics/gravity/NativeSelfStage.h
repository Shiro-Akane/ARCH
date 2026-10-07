/**
 * @file NativeSelfStage.h
 * @brief Private, nonmoving Host native self-gravity stage and write receipts.
 *
 * Workflow:
 * 1. GravityStage alone constructs after actual Runtime/macro transaction and
 *    source preparation. Reuse the real BC/domain/slot/seven-storage leases.
 * 2. Borrow exactly the ready NativeRzCandidate publication, never relabel it.
 *    Compile authentic topology rows and evaluate SAME-stage resident Phi.
 * 3. Claim each patch once; reserve momentum, each real work axis and each
 *    original Energy registration operation BEFORE its numerical write.
 * 4. Commit registration only after ApplyRegistrationPlan succeeds. Any
 *    abandonment, duplicate, stale identity or prefix failure poisons the frame.
 * 5. Join workers; check every patch/axis/route receipt and the original domain.
 *    The existing transaction owns rollback before frame destruction.
 * 6. An optional frozen internal sink inspects the SAME operations synchronously;
 *    absent sinks allocate/copy no diagnostic numerical data.
 *
 * E_registered = F_E + psi_o F_rho,
 * psi_o = sum_fragment(A_fragment/A_source)*Phi_fragment - Phi_dest_coarse.
 * Original sign, area, RK weight and dt remain with their original register.
 * This internal experiment grants no continuous field/public/Device science.
 */
#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "amr/AMRControl.h"
#include "data/GlobalDefs.h"
#include "driver/schedule/StageScheduler.h"
#include "numerics/integrator/HydroBoundaryAuthority.h"
#include "physics/gravity/GravitySource.h"
#include "physics/gravity/self/GravityWorkspace.h"
#include "physics/gravity/self/SelfGravity.h"

namespace arch::driver { class GravityStage; }
namespace Physical::Gravity {

/** Synchronous internal observations of ORIGINAL work/register operations.
 * All pointers are read-only borrows valid only during the callback. The sink
 * must copy its own compact evidence and be thread-safe under patch parallelism.
 * No observation grants field scope, numerical accuracy or Runtime authority.
 */
struct NativeSelfStageObservation {
    enum class Kind { AxisBefore, AxisAfter, EnergyReserved, EnergyApplied };
    Kind kind=Kind::AxisBefore;
    const Grid* grid=nullptr;const FluidState* input=nullptr;
    const std::vector<FluidVector>* flux=nullptr;const std::vector<FluidVector>* delta=nullptr;
    int block_id=-1,axis=-1;double dt=0.,stage_weight=0.;
    const arch::scheduler::StageDescriptor* descriptor=nullptr;
    const GravitySolveIdentity* source=nullptr;
    std::uint64_t generation=0,source_generation=0,field_generation=0;
    const GravityRefluxRowIdentity* row=nullptr;
    const amr::AmrFluxRegistrationRoute* route=nullptr;
    std::size_t operation_index=0;int flux_index=-1;
    double FE=0.,Frho=0.,psi=0.,result=0.;
};

/** One genuine private stage. Friend construction is necessary, not scientific
 * certification. Actual Runtime/transaction minting remains GravityStage's job.
 */
class NativeSelfStageFrame final {
    enum class Phase : unsigned char { Ready, Claimed, Consumed };
    struct PatchRecord {
        std::atomic<Phase> phase{Phase::Ready};
        std::array<std::atomic<Phase>,3> source{}; // momentum, radial work, axial work
        std::vector<std::size_t> energy_rows;
    };
public:
    using Observation=NativeSelfStageObservation;
    using ObservationSink=void(*)(void*,const Observation&);
    /** One move-only patch claim; borrowing lifetime ends before owner teardown. */
    class PatchReceipt final {
    public:
        PatchReceipt(const PatchReceipt&)=delete;
        PatchReceipt& operator=(const PatchReceipt&)=delete;
        /** Move a claim without reopening it or creating a new generation. */
        PatchReceipt(PatchReceipt&& other) noexcept
            : frame_(std::exchange(other.frame_,nullptr)),index_(other.index_),
              id_(other.id_),input_(other.input_),grid_(other.grid_),generation_(other.generation_) {}
        /** Poison the abandoned old receipt before adopting another claim. */
        PatchReceipt& operator=(PatchReceipt&& other) noexcept {
            if(this!=&other){abandon();frame_=std::exchange(other.frame_,nullptr);
                index_=other.index_;id_=other.id_;input_=other.input_;grid_=other.grid_;
                generation_=other.generation_;}return *this;
        }
        /** No accepted prefix survives an uncommitted or exceptional visit. */
        ~PatchReceipt(){abandon();}

        /** Authenticate actual input/view/dt/bounds before any consumer mutation. */
        void require_application(const FluidState& input,const Grid& grid,
            const GridMetrics::GeometryView& geometry,double dt,const arch::state::Bounds& bounds) const {
            guard([&]{require_input();
                if(&input!=input_||&grid!=grid_||!same_double(dt,frame_->dt_)
                    ||!same_bounds(bounds,frame_->bounds_)
                    ||!same_geometry(geometry,GridMetrics::make_geometry_view(grid,
                        GridMetrics::GeometrySemantics::AxisymmetricRz)))
                    throw std::logic_error("Native self application changed actual operands");});
        }
        /** Reserve the real work axis, execute old math, publish its completion. */
        void add_flux_work(std::vector<FluidVector>& delta,
            const std::vector<FluidVector>& flux,int axis) {
            guard([&]{require_input();require_arrays(delta);
                if(axis<0||axis>=2||flux.size()!=delta.size()||&flux==&delta)
                    throw std::invalid_argument("Native self work axis/extent is invalid");
                auto& phase=frame_->patches_[index_].source[axis+1];reserve(phase);
                if(frame_->observation_sink_) {
                    observe_axis(Observation::Kind::AxisBefore,delta,flux,axis);require_input();
                }
                frame_->policy_->native_candidate_flux_work(delta,flux,*input_,*grid_,frame_->dt_,axis);
                require_input();
                if(frame_->observation_sink_) {
                    observe_axis(Observation::Kind::AxisAfter,delta,flux,axis);require_input();
                }
                consume(phase);});
        }
        /** Reserve one momentum producer and execute exactly the original leaf. */
        void add_momentum(std::vector<FluidVector>& delta) {
            guard([&]{require_input();require_arrays(delta);
                auto& phase=frame_->patches_[index_].source[0];reserve(phase);
                frame_->policy_->native_candidate_momentum(delta,*input_,*grid_,frame_->dt_);
                require_input();consume(phase);});
        }
        /** Reserve one actual Energy operation before original route application.
         * Only FE is folded; the physical hydro flux and mass flux stay intact.
         * No sign, area, stage weight or dt is applied by this scalar carrier.
         */
        double registered_energy(const amr::AmrFluxTopologyPlan& topology,
            const amr::AmrFluxRegistrationRoute& route,std::size_t operation_index,
            int flux_index,double FE,double Frho,double stage_weight) {
            double result=0.;guard([&]{require_input();require_route(topology,route);
                if(!same_double(stage_weight,frame_->stage_weight_)
                    ||!std::isfinite(FE)||!std::isfinite(Frho))
                    throw std::logic_error("Native self registration changed stage weight or flux");
                const auto found=frame_->row_index_.find({route.key,operation_index});
                if(found==frame_->row_index_.end()||operation_index>=route.plan.operations.size())
                    throw std::logic_error("Native self registration is not an original Energy operation");
                const auto row=found->second;const auto& identity=frame_->rows_->identity[row];
                if(identity.source_flux_offset!=flux_index
                    ||identity.operation!=route.plan.operations[operation_index]
                    ||identity.route_fingerprint!=route.plan.fingerprint)
                    throw std::logic_error("Native self registration changed original operation");
                reserve(frame_->energy_[row]);
                result=native_reflux_energy(FE,frame_->psi_[row],Frho);
                if(!std::isfinite(result))throw std::runtime_error("Native self registered energy is nonfinite");
                require_input();
                if(frame_->observation_sink_) {
                    frame_->energy_observations_[row]={flux_index,FE,Frho,frame_->psi_[row],result};
                    observe_energy(Observation::Kind::EnergyReserved,route,row);require_input();
                }
            });return result;
        }
        /** Consume this route only AFTER its original register Apply succeeds.
         * Precheck all Energy rows before any receipt transition; an empty
         * original route has no invented work obligation.
         */
        void commit_registration(const amr::AmrFluxRegistrationRoute& route) {
            guard([&]{require_input();require_route(*frame_->topology_,route);
                for(std::size_t ordinal=0;ordinal<route.plan.operations.size();++ordinal) {
                    if(route.plan.operations[ordinal].field!=amr::AmrField::Energy)continue;
                    const auto found=frame_->row_index_.find({route.key,ordinal});
                    if(found==frame_->row_index_.end()
                        ||frame_->energy_[found->second].load(std::memory_order_acquire)!=Phase::Claimed)
                        throw std::logic_error("Native self Energy operation was not reserved");
                }
                for(std::size_t ordinal=0;ordinal<route.plan.operations.size();++ordinal)
                    if(route.plan.operations[ordinal].field==amr::AmrField::Energy) {
                        const auto row=frame_->row_index_.at({route.key,ordinal});
                        consume(frame_->energy_[row]);
                        if(frame_->observation_sink_) {
                            observe_energy(Observation::Kind::EnergyApplied,route,row);require_input();
                        }
                    }
                require_input();});
        }
        /** Commit only after momentum, both real axes and every original row. */
        void commit() {
            guard([&]{require_input();const auto& record=frame_->patches_[index_];
                for(const auto& phase:record.source)
                    if(phase.load(std::memory_order_acquire)!=Phase::Consumed)
                        throw std::logic_error("Native self patch omitted momentum or an actual work axis");
                for(auto row:record.energy_rows)
                    if(frame_->energy_[row].load(std::memory_order_acquire)!=Phase::Consumed)
                        throw std::logic_error("Native self patch omitted original Energy registration");
                consume(frame_->patches_[index_].phase);});frame_=nullptr;
        }
    private:
        friend class NativeSelfStageFrame;
        /** Bind a successfully reserved patch without allocating numerical state. */
        PatchReceipt(const NativeSelfStageFrame& frame,std::size_t index,int id,
            const FluidState& input,const Grid& grid) noexcept
            :frame_(&frame),index_(index),id_(id),input_(&input),grid_(&grid),generation_(frame.generation_) {}
        /** All fallible consumer failures retire the whole frame permanently. */
        template<class F> void guard(F&& action) const {
            if(!frame_)throw std::logic_error("Native self receipt is not live");
            try{action();}catch(...){frame_->invalidate();throw;}
        }
        /** Match actual patch/domain/field publication and the original claim. */
        void require_input() const {
            frame_->require_live();
            if(generation_!=frame_->generation_
                ||frame_->patches_[index_].phase.load(std::memory_order_acquire)!=Phase::Claimed)
                throw std::logic_error("Native self claim is stale or already consumed");
            frame_->domain_.require_input_patch(index_,frame_->control_,id_,*input_,*grid_);
            (void)frame_->policy_->native_candidate_patch(*grid_,*input_);
        }
        /** Build a borrowed event from this exact already-authenticated claim.
         * Scalars identify the original descriptor/source, not another solve.
         */
        Observation observation(Observation::Kind kind,int axis) const noexcept {
            Observation event;event.kind=kind;event.grid=grid_;event.input=input_;
            event.block_id=id_;event.axis=axis;event.dt=frame_->dt_;
            event.stage_weight=frame_->stage_weight_;event.descriptor=&frame_->descriptor_;
            event.source=&frame_->source_;event.generation=generation_;
            event.source_generation=frame_->source_generation_;event.field_generation=frame_->field_generation_;
            return event;
        }
        /** Inspect the SAME delta/flux before or after the one original work.
         * No vector is copied and no flux/source kernel is invoked by this hook.
         */
        void observe_axis(Observation::Kind kind,const std::vector<FluidVector>& delta,
            const std::vector<FluidVector>& flux,int axis) const {
            auto event=observation(kind,axis);event.delta=&delta;event.flux=&flux;
            frame_->notify(event);
        }
        /** Match reserve/apply observations using preallocated per-row scalars.
         * E_registered=FE+psi*Frho; original A/sign/RK/dt remain in the register.
         */
        void observe_energy(Observation::Kind kind,
            const amr::AmrFluxRegistrationRoute& route,std::size_t row) const {
            const auto& identity=frame_->rows_->identity[row];
            const auto& saved=frame_->energy_observations_[row];
            auto event=observation(kind,amr::axis_value(route.key.axis));
            event.row=&identity;event.route=&route;event.operation_index=identity.operation_index;
            event.flux_index=saved.flux_index;event.FE=saved.FE;event.Frho=saved.Frho;
            event.psi=saved.psi;event.result=saved.result;frame_->notify(event);
        }
        /** Exact vector extent precedes every original source write. */
        void require_arrays(const std::vector<FluidVector>& delta) const {
            if(delta.size()!=static_cast<std::size_t>(grid_->GetTotalSize()))
                throw std::invalid_argument("Native self delta extent differs from actual input");
        }
        /** Bind the caller's route to the SAME immutable topology object. */
        void require_route(const amr::AmrFluxTopologyPlan& topology,
            const amr::AmrFluxRegistrationRoute& route) const {
            if(&topology!=frame_->topology_||route.key.source_block!=id_
                ||topology.find(id_,amr::axis_value(route.key.axis))!=&route)
                throw std::logic_error("Native self registration borrowed a foreign route");
        }
        /** CAS reserves BEFORE writes; a duplicate cannot enter numerical math. */
        static void reserve(std::atomic<Phase>& phase) {
            Phase expected=Phase::Ready;
            if(!phase.compare_exchange_strong(expected,Phase::Claimed,std::memory_order_acq_rel))
                throw std::logic_error("Native self operation was already reserved");
        }
        /** Release a successful original write; no Failed state can revive. */
        static void consume(std::atomic<Phase>& phase) {
            Phase expected=Phase::Claimed;
            if(!phase.compare_exchange_strong(expected,Phase::Consumed,std::memory_order_release))
                throw std::logic_error("Native self operation completion is inconsistent");
        }
        /** Destruction does no throwing work; abandoning poisons the stage. */
        void abandon() noexcept {if(frame_){frame_->invalidate();frame_=nullptr;}}
        const NativeSelfStageFrame* frame_=nullptr;
        std::size_t index_{};int id_{};
        const FluidState* input_{};const Grid* grid_{};std::uint64_t generation_{};
    };

    NativeSelfStageFrame(const NativeSelfStageFrame&)=delete;
    NativeSelfStageFrame& operator=(const NativeSelfStageFrame&)=delete;
    NativeSelfStageFrame(NativeSelfStageFrame&&)=delete;
    NativeSelfStageFrame& operator=(NativeSelfStageFrame&&)=delete;
    /** Owner must first join workers and detach the policy before destruction. */
    ~NativeSelfStageFrame(){invalidate();}
    /** Borrow the original BC domain, not a new wall authority or EOS gate. */
    const arch::boundary::HostHydroBoundaryDomainAuthority& boundary_domain() const {
        checked([&]{require_live();});return domain_;
    }
    /** Read the original gravity stability cap from this actual live Hydro field.
     * dt_g = CFL / sqrt(max(4*pi*G*rho_max, max_a |g_a|/dx_a)).
     * The prepared source, configuration and topology lease are rechecked;
     * this private frame does not issue public native-field authority.
     */
    double timestep() const {
        double result=0.;checked([&]{require_live();
            result=policy_->field_timestep(cfl_,GravityFieldScope::NativeRzCandidate);});
        return result;
    }
    /** Own the exact prepared Hydro source/field, without another solve.
     * Workflow: authenticate this live frame and its complete actual input
     * domain -> copy the producer's same-generation field/rho/geometry once ->
     * repeat all domain, configuration and field checks before publishing.
     * This is allowed during read-only patch consumption; completion of every
     * work receipt is deliberately not fabricated as a precondition. Any
     * copy/identity failure permanently poisons the frame for macro rollback.
     */
    NativeRzSolutionInspection inspect_source_and_field() const {
        try {
            require_live();domain_.require_complete_domain();
            policy_->require_native_frame(source_,field_generation_,source_generation_);
            auto result=policy_->copy_native_rz_solution(GravityFieldPurpose::HydroStage,
                source_,field_generation_,source_generation_);
            require_live();domain_.require_complete_domain();
            policy_->require_native_frame(source_,field_generation_,source_generation_);
            return result;
        } catch(...) {invalidate();throw;}
    }
    /** Claim a real pool input once, before flux/cache/dU/register mutation. */
    PatchReceipt claim_patch(const amr::AMRControl* control,int id,const FluidState& input,
        const Grid& grid,double dt,const IGravityPolicy& policy) const {
        std::size_t index=0;checked([&]{require_live();
            if(control!=control_||&policy!=policy_||id<0||!same_double(dt,dt_))
                throw std::logic_error("Native self claim changed owner or interval");
            const auto& block=control_->pool->GetBlock(id);
            if(block.active_index<0||static_cast<std::size_t>(block.active_index)>=patch_count_)
                throw std::logic_error("Native self claim is outside its actual domain");
            index=static_cast<std::size_t>(block.active_index);
            domain_.require_input_patch(index,control,id,input,grid);
            (void)policy_->native_candidate_patch(grid,input);
            PatchReceipt::reserve(patches_[index].phase);});
        return PatchReceipt(*this,index,id,input,grid);
    }
    /** Main-thread join gate: all actual input/field/operation receipts agree. */
    void require_complete_consumption() const {
        checked([&]{require_live();domain_.require_complete_domain();
            policy_->require_native_frame(source_,field_generation_,source_generation_);
            for(std::size_t patch=0;patch<patch_count_;++patch) {
                if(patches_[patch].phase.load(std::memory_order_acquire)!=Phase::Consumed)
                    throw std::logic_error("Native self complete stage omitted an active patch");
                for(const auto& phase:patches_[patch].source)
                    if(phase.load(std::memory_order_acquire)!=Phase::Consumed)
                        throw std::logic_error("Native self complete stage omitted actual source work");
            }
            for(std::size_t row=0;row<rows_->identity.size();++row)
                if(energy_[row].load(std::memory_order_acquire)!=Phase::Consumed)
                    throw std::logic_error("Native self complete stage omitted Energy operation");
            // Read-only recheck cannot recreate a dropped/replaced cache and
            // thereby revive an earlier topology owner. Complete domain/actual
            // geometry checks above remain the original scientific identity.
            require_live();});
    }
    /** Permanent poison. Actual transaction restore and detach belong to owner. */
    void invalidate() const noexcept {live_.store(false,std::memory_order_release);}
private:
    friend class arch::driver::GravityStage;
    /** Capture actual source/field/Runtime and evaluate bounded topology work.
     * Const AMR borrow is an actual mutable Runtime object; only its preexisting
     * logically mutable topology cache is prepared here on the main thread.
     */
    NativeSelfStageFrame(const SelfGravity& policy,const BCHandler& boundary,
        const amr::AMRControl& control,const arch::scheduler::StageBinding& binding,
        const arch::scheduler::StageDescriptor& descriptor,const SimConfig& config,
        double dt,std::uint64_t generation,const GravitySolveIdentity& source,
        ObservationSink sink=nullptr,void* payload=nullptr)
        :policy_(&policy),control_(&control),configuration_(&config),gravity_(config.physics.gravity),grid_configuration_(config.grid),
          bounds_{config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint},
          dt_(dt),stage_weight_(descriptor.flux_register_weight),cfl_(config.numerics.cfl),generation_(generation),source_(source),
          field_generation_(policy.workspace().generation),
          source_generation_(policy.workspace().ring_assessment.source_generation),
          domain_(boundary,control,binding,descriptor),patch_count_(control.tree->GetActiveBlocks().size()),
          patches_(std::make_unique<PatchRecord[]>(patch_count_)),
          descriptor_(descriptor),observation_sink_(sink),observation_payload_(payload) {
        if(!observation_sink_&&observation_payload_)
            throw std::invalid_argument("Native self observation payload requires its sink");
        if(!generation_||!patch_count_||!std::isfinite(dt_)||!(dt_>0.)
            ||!same_double(dt_,binding.context.step_dt)||!std::isfinite(stage_weight_)||!(stage_weight_>0.)
            ||source_.inputs.size()!=patch_count_||source_.topology!=binding.context.ledger.active_epoch()
            ||!same_double(source_.input_time,binding.context.step_start_time
                +descriptor.input_time_fraction*binding.context.step_dt))
            throw std::invalid_argument("Native self stage source/interval/identity is invalid");
        for(std::size_t index=0;index<patch_count_;++index) {
            const auto& actual=source_.inputs[index];
            if(actual.block!=binding.handles[index]||actual.slot!=descriptor.input_slot
                ||binding.context.ledger.inspect({actual.block,actual.slot}).interior.version!=actual.version)
                throw std::logic_error("Native self source does not match actual prepared input publication");
        }
        // A real accepted-Current field cannot acquire Hydro consumption authority.
        policy_->require_runtime_purpose(GravityFieldPurpose::HydroStage);
        require_configuration();policy_->require_native_frame(source_,field_generation_,source_generation_);
        const auto& actual_topology=const_cast<amr::AMRControl&>(control).RequireFluxTopologyPlan(
            control.flux_register.GetNumSpecies(),GridMetrics::GeometrySemantics::AxisymmetricRz,-1,true);
        topology_lease_=control.FluxTopologyPlanLease();
        if(!topology_lease_||topology_lease_.get()!=&actual_topology)
            throw std::logic_error("Native self topology owner changed during construction");
        topology_=topology_lease_.get();
        epoch_=topology_->epoch;topology_fingerprint_=topology_->fingerprint;
        rows_=&policy_->native_candidate_reflux_rows(*topology_);
        psi_=policy_->native_candidate_reflux_values();
        energy_=std::make_unique<std::atomic<Phase>[]>(rows_->identity.size());
        if(observation_sink_&&!rows_->identity.empty())
            energy_observations_=std::make_unique<EnergyObservation[]>(rows_->identity.size());
        for(std::size_t row=0;row<rows_->identity.size();++row) {
            energy_[row].store(Phase::Ready,std::memory_order_relaxed);
            const auto& identity=rows_->identity[row];
            if(!row_index_.emplace(std::make_pair(identity.route,identity.operation_index),row).second)
                throw std::logic_error("Native self paired Energy row is duplicated");
            const auto& block=control.pool->GetBlock(identity.route.source_block);
            if(block.active_index<0||static_cast<std::size_t>(block.active_index)>=patch_count_)
                throw std::logic_error("Native self Energy row source is not active");
            patches_[block.active_index].energy_rows.push_back(row);
        }
    }
    /** Exact floating identity includes signed zero, independent of tolerances. */
    static bool same_double(double a,double b) noexcept {
        return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
    }
    /** Preserve original bounds as stage inputs; no lower-bound adjustment. */
    static bool same_bounds(const arch::state::Bounds& a,const arch::state::Bounds& b) noexcept {
        return same_double(a.density,b.density)&&same_double(a.internal_min,b.internal_min)
            &&same_double(a.internal_max,b.internal_max);
    }
    /** Compare all actual native view values and root/level/logical identity. */
    static bool same_geometry(const GridMetrics::GeometryView& a,const GridMetrics::GeometryView& b) noexcept {
        if(a.geometry!=b.geometry||a.dim!=b.dim||a.ng!=b.ng||a.stride_y!=b.stride_y
            ||a.stride_z!=b.stride_z||a.total_size!=b.total_size||a.semantics!=b.semantics
            ||!same_double(a.dx1,b.dx1)||!same_double(a.dx2,b.dx2)||!same_double(a.dx3,b.dx3)
            ||!same_double(a.x1_min,b.x1_min)||!same_double(a.x2_min,b.x2_min)
            ||!same_double(a.x3_min,b.x3_min)||!GridMetrics::equal_identity(a.dyadic_identity,b.dyadic_identity))return false;
        for(int axis=0;axis<2;++axis)if(!same_double(a.actual_block_upper[axis],b.actual_block_upper[axis]))return false;
        return a.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz&&a.dim==2;
    }
    /** Runtime configuration and actual service copy must remain identical. */
    void require_configuration() const {
        const auto& current=configuration_->physics.gravity;
        const auto& service=policy_->config_;
        const auto same=[&](const GravityConfig& a,const GravityConfig& b){return a==b
            &&same_double(a.g_x,b.g_x)&&same_double(a.g_y,b.g_y)&&same_double(a.g_z,b.g_z)
            &&same_double(a.relative_tolerance,b.relative_tolerance)
            &&same_double(a.absolute_tolerance,b.absolute_tolerance);};
        if(gravity_.type!="self"||!same(current,gravity_)||!same(service,gravity_)
            ||!same_double(configuration_->numerics.cfl,cfl_)
            ||configuration_->grid!=grid_configuration_
            ||!same_double(configuration_->grid.x1_min,grid_configuration_.x1_min)
            ||!same_double(configuration_->grid.x1_max,grid_configuration_.x1_max)
            ||!same_double(configuration_->grid.x2_min,grid_configuration_.x2_min)
            ||!same_double(configuration_->grid.x2_max,grid_configuration_.x2_max)
            ||!same_double(configuration_->grid.x3_min,grid_configuration_.x3_min)
            ||!same_double(configuration_->grid.x3_max,grid_configuration_.x3_max)
            ||!arch::state::valid_bounds(bounds_)||!same_bounds(bounds_,
                {configuration_->numerics.sml_rho,configuration_->numerics.min_eint,configuration_->numerics.max_eint}))
            throw std::logic_error("Native self Runtime/service configuration changed");
    }
    /** Require the same actual shared owner, not only equal topology values.
     * This detects cache invalidation/replacement without recreating it. The
     * owning lease keeps every borrowed route pointer alive until teardown.
     */
    static bool same_topology_owner(
        const std::shared_ptr<const amr::AmrFluxTopologyPlan>& first,
        const std::shared_ptr<const amr::AmrFluxTopologyPlan>& second) noexcept {
        return first&&second&&first.get()==second.get()
            &&!first.owner_before(second)&&!second.owner_before(first);
    }
    /** Require exact live private attachment and still borrowed resident field.
     * Workflow: check original liveness; read actual cache lease; reject/poison
     * changed shared ownership BEFORE dereferencing any borrowed topology; then
     * check original configuration, candidate field and paired work identities.
     */
    void require_live() const {
        if(!live_.load(std::memory_order_acquire)||policy_->prepared_native_self()!=this
            ||policy_->source_descriptor().origin!=GravitySourceOrigin::NativeSelfComposite)
            throw std::logic_error("Native self stage is retired or detached");
        const auto current_lease=control_->FluxTopologyPlanLease();
        if(!same_topology_owner(current_lease,topology_lease_)) {
            invalidate();
            throw std::logic_error("Native self topology cache owner was dropped or replaced");
        }
        require_configuration();policy_->require_native_frame_lease(source_,field_generation_,source_generation_);
        const auto& w=policy_->workspace();
        if(topology_->epoch!=epoch_||topology_->fingerprint!=topology_fingerprint_
            ||w.reflux_topology!=topology_||w.reflux_field_generation!=field_generation_
            ||&w.reflux_rows!=rows_||w.reflux_values.data!=psi_)
            throw std::logic_error("Native self paired work or topology changed");
    }
    /** Invoke the frozen sink synchronously, rejecting callback recursion.
     * The thread-local marker is only a call-stack witness, not numerical cache;
     * independent worker threads may call the thread-safe sink concurrently.
     * Receipt guards retain poisoning/rollback on every exception.
     */
    void notify(const Observation& event) const {
        static thread_local bool observing=false;
        if(observing)throw std::logic_error("Native self observation callback is reentrant");
        struct CallbackScope {
            bool& active;
            /** Mark only this thread's synchronous callback extent. */
            explicit CallbackScope(bool& flag) noexcept:active(flag){active=true;}
            /** Restore on throws; original receipt guard owns stage poison. */
            ~CallbackScope(){active=false;}
        } scope(observing);
        observation_sink_(observation_payload_,event);
    }
    /** Every checked failure is terminal even if caller catches its exception. */
    template<class F> void checked(F&& action) const {
        try{action();}catch(...){invalidate();throw;}
    }
    const SelfGravity* policy_;const amr::AMRControl* control_;const SimConfig* configuration_;
    const GravityConfig gravity_;const GridConfig grid_configuration_;const arch::state::Bounds bounds_;
    const double dt_,stage_weight_,cfl_;const std::uint64_t generation_;
    const GravitySolveIdentity source_;const std::uint64_t field_generation_,source_generation_;
    const arch::boundary::HostHydroBoundaryDomainAuthority domain_;
    const std::size_t patch_count_;std::unique_ptr<PatchRecord[]> patches_;
    // Retain the actual cache control block. The raw pointer is only an indexed
    // borrow into this lease, never independent ownership or cache authority.
    std::shared_ptr<const amr::AmrFluxTopologyPlan> topology_lease_;
    const amr::AmrFluxTopologyPlan* topology_=nullptr;amr::TopologyEpoch epoch_{};
    std::uint64_t topology_fingerprint_=0;const GravityRefluxRows* rows_=nullptr;const double* psi_=nullptr;
    std::map<std::pair<amr::AmrFluxRouteKey,std::size_t>,std::size_t> row_index_;
    std::unique_ptr<std::atomic<Phase>[]> energy_;mutable std::atomic<bool> live_{true};
    // Optional diagnostics only: no allocation/copy on the absent-sink path.
    struct EnergyObservation {int flux_index=-1;double FE=0.,Frho=0.,psi=0.,result=0.;};
    // Own bounded diagnostic metadata across callback-driven stage retirement.
    // The original boundary domain still authenticates its actual descriptor.
    const arch::scheduler::StageDescriptor descriptor_;
    const ObservationSink observation_sink_;void* const observation_payload_;
    std::unique_ptr<EnergyObservation[]> energy_observations_;
};
} // namespace Physical::Gravity
