/**
 * @file NativeSelfEnergyWitness.h
 * @brief Tests-only, same-execution uniform Euler gravity energy accounting.
 *
 * Workflow:
 * 1. Bind genuine Runtime cells and save accepted-entry mass/energy, not a field.
 * 2. Own the first genuinely prepared field through the original Hydro observer.
 * 3. Inspect original before/after work and returned dU without recalculating flux.
 * 4. Close the accepted journal, advance the real clock and solve actual Current.
 * 5. Check algebra/rollback against fixed budgets; emit only compact diagnostics.
 *
 * W=.5 m.Phi is the discrete point-potential energy. Its endpoint decomposition
 * does not establish a continuous energy invariant or promote candidate fields.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "host/driver/RzRuntimeWitness.h"
#include "host/gravity/RzMaterializedSourceRecord.h"

#include "amr/elliptic/EllipticMeshAdapter.h"
#include "driver/runtime/HostHydroTransaction.h"
#include "driver/schedule/StageScheduler.h"
#include "driver/stages/DriverStages.h"
#include "physics/gravity/NativeSelfStage.h"
#include "physics/gravity/self/SelfGravity.h"

namespace arch::test {
/** Fail closed for malformed evidence; never invent a numerical value. */
inline void energy_require(bool ok,const char* why) {if(!ok)throw std::logic_error(why);}
/** Require every accumulated scale and result to remain representable. */
inline long double energy_finite(long double x) {
    energy_require(std::isfinite(x),"Private energy witness produced nonfinite evidence");return x;
}
/** Frozen ACCOUNTING budget only: no max(1), observed-error tuning or science grant.
 * State scale is sum of absolute stored extensive values participating in a
 * subtraction; operation scale is sum of absolute independently assembled terms.
 */
struct NativeEnergyBudget {
    long double error,state_scale,operation_scale,roundoff,operations,allowance;
    NativeEnergyBudget(long double e,long double state,long double operation)
        :error(energy_finite(e)),state_scale(energy_finite(state)),operation_scale(energy_finite(operation)),
         roundoff(energy_finite(64.L*std::numeric_limits<double>::epsilon()*state)),
         operations(energy_finite(1.e-12L*operation)),allowance(energy_finite(roundoff+operations)) {
        energy_require(state>=0.L&&operation>=0.L,"Private energy accounting scale is negative");
    }
    /** Enforce the already-frozen algebraic window, not a total-energy tolerance. */
    void check(const char* why) const {energy_require(std::abs(error)<=allowance,why);}
    /** Store its fixed decomposition with sufficient long-double precision. */
    void json(std::ostream& out) const {
        out<<"{\"error\":"<<error<<",\"absolute_state_scale\":"<<state_scale
           <<",\"absolute_operation_scale\":"<<operation_scale<<",\"roundoff_allowance\":"<<roundoff
           <<",\"operation_allowance\":"<<operations<<",\"allowance\":"<<allowance<<'}';
    }
};

/** Own only compact test evidence from one actual uniform Euler macro step.
 * Owner is the existing private Runtime fixture, not a substitute scheduler.
 */
template<class Owner> class NativeSelfEnergyWitness final {
    using Field=Physical::Gravity::NativeRzFieldInspection;
    using Observation=Physical::Gravity::NativeSelfStageObservation;
    struct Cell {
        std::size_t block;int offset,i,j;long double volume,m0,E0;double rho0,energy0;
        std::array<int,4> faces{{-1,-1,-1,-1}};
        std::array<long double,2> before{},expected{},operation{};
        std::array<int,2> phase{};bool delta_seen=false;
        long double dm=0.;
    };
    Owner& owner_;double dt_;amr::EllipticMeshBinding binding_;
    std::vector<Cell> cells_;std::optional<Field> field_;
    std::vector<std::array<const double*,7>> input_leases_;
    scheduler::StageDescriptor descriptor_{};std::mutex mutex_;
    double entry_time_;int entry_gathers_;long double boundary_energy_entry_=0.;
    double gravity_timestep_=0.;
    long double boundary_phi_=0.,boundary_energy_=0.,boundary_phi_scale_=0.,boundary_energy_scale_=0.;
    long double work_error_=0.,work_state_scale_=0.,work_operation_scale_=0.;
    std::size_t before_events_=0,after_events_=0;long double entry_resolution_=0.,max_local_work_ratio_=0.,max_local_mass_ratio_=0.;

    /** Select the exact actual slot advertised by the solved source. */
    static const FluidState& fluid(const amr::Block& block,state::StateSlot slot) {
        switch(slot) {
        case state::StateSlot::Current:return block.fluid_state;
        case state::StateSlot::Next:return block.state_next;
        case state::StateSlot::Scratch:return block.state_scratch;
        default:throw std::logic_error("Private energy source has an unknown state slot");
        }
    }
    /** Actual source/block/slot/version fences, without using private workspace. */
    void source_matches(const Field& field,state::StateSlot slot,double time) const {
        Physical::Gravity::validate_gravity_solve_identity(field.source);
        energy_require(field.source_generation>0&&field.field_generation>0
            &&field.source.inputs.size()==binding_.handles.size()
            &&binding_.handles.size()==owner_.runtime->handles().size()
            &&field.source.topology==owner_.context->ledger.active_epoch()
            &&rz_runtime_witness::bits(field.source.input_time,time)
            &&rz_runtime_witness::bits(field.source.gravitational_constant,
                arch::constants::gravity::cgs::gravitational_constant),"Private energy actual source identity mismatch");
        const auto actual=amr::bind_elliptic_mesh(owner_.control,owner_.config.grid,owner_.runtime->handles());
        energy_require(actual.cells==binding_.cells&&actual.handles==binding_.handles
            &&actual.storage.size()==binding_.storage.size()&&actual.grids==binding_.grids,
            "Private energy actual cell binding changed");
        const auto active=owner_.control.tree->GetActiveBlocks();
        for(std::size_t b=0;b<binding_.handles.size();++b) {
            const auto& input=field.source.inputs[b];
            energy_require(input.block==binding_.handles[b]&&input.block==owner_.runtime->handles()[b]
                &&input.slot==slot&&input.version.value>0&&input.storage_generation>0
                &&input.storage_generation==field.source.inputs.front().storage_generation,
                "Private energy actual source slot/handle/density lease mismatch");
            const auto& block=owner_.control.pool->GetBlock(active.at(b));
            energy_require(&block.grid==binding_.grids[b],"Private energy Grid owner changed");
            const auto coherence=owner_.context->ledger.inspect({input.block,slot});
            energy_require(coherence.interior.version==input.version,"Private energy source publication changed");
            owner_.context->ledger.require_readable({input.block,slot},
                {state::ExecutionSide::Host,input.version,true,true});
        }
        for(std::size_t n=0;n<binding_.storage.size();++n)
            energy_require(actual.storage[n].block==binding_.storage[n].block
                &&actual.storage[n].offset==binding_.storage[n].offset,"Private energy source storage ordering changed");
        energy_require(field.potential.size()==cells_.size()&&field.face_gradient.size()==field.faces.size()
            &&field.boundary_values.size()==field.faces.size(),"Private energy actual field extent mismatch");
        for(double value:field.potential)energy_finite(value);
        for(double value:field.face_gradient)energy_finite(value);
        for(double value:field.boundary_values)energy_finite(value);
    }
    /** Evaluate ORIGINAL stored potential interpolation, including real datum.
     * This is a row evaluation, not a continuous face-potential reference.
     */
    static long double face_phi(const Field& field,std::size_t f) {
        const auto& face=field.faces.at(f);
        energy_require(face.value_samples.size()==face.value_coefficients.size(),"Private energy value row extent");
        long double value=static_cast<long double>(face.value_boundary_coefficient)*field.boundary_values.at(f);
        for(std::size_t k=0;k<face.value_samples.size();++k) {
            const int n=face.value_samples[k];energy_require(n>=0&&std::size_t(n)<field.potential.size(),"Private energy value sample index");
            value+=static_cast<long double>(face.value_coefficients[k])*field.potential[std::size_t(n)];
        }
        return energy_finite(value);
    }
    /** Prove the uniform lane has exactly one actual complete face on each side.
     * CF fragments are deliberately not guessed or assigned a patch flux here.
     */
    void bind_faces() {
        for(std::size_t f=0;f<field_->faces.size();++f) {
            const auto& face=field_->faces[f];
            energy_require(face.native_bounds&&(face.axis==0||face.axis==1)&&std::isfinite(face.area)&&face.area>0.,
                "Private uniform energy face geometry invalid");
            for(int side=0;side<2;++side) {
                const int n=side?face.left:face.right;if(n<0)continue;
                energy_require(std::size_t(n)<cells_.size(),"Private energy face cell index");
                auto& cell=cells_[std::size_t(n)];const auto& grid=*binding_.grids[cell.block];
                const double rl=grid.GetFacePosL(cell.i),rh=grid.GetFacePosR(cell.i),
                    zl=grid.GetAxialFacePosL(cell.j),zh=grid.GetAxialFacePosR(cell.j);
                const double coordinate=face.axis==0?(side?rh:rl):(side?zh:zl);
                const int tangent=1-face.axis;
                energy_require(rz_runtime_witness::bits(face.center[face.axis],coordinate)
                    &&rz_runtime_witness::bits(face.fragment_lower[face.axis],coordinate)
                    &&rz_runtime_witness::bits(face.fragment_upper[face.axis],coordinate)
                    &&rz_runtime_witness::bits(face.fragment_lower[tangent],tangent?zl:rl)
                    &&rz_runtime_witness::bits(face.fragment_upper[tangent],tangent?zh:rh)
                    &&cell.faces[2*face.axis+side]==-1,"Private uniform face is fragmented or mismatched");
                cell.faces[2*face.axis+side]=static_cast<int>(f);
                const long double pi=arch::constants::math::pi;
                const long double area=face.axis==0?2.L*pi*coordinate*(static_cast<long double>(zh)-zl):
                    pi*(static_cast<long double>(rh)-rl)*(static_cast<long double>(rh)+rl);
                NativeEnergyBudget(static_cast<long double>(face.area)-area,0.,std::abs(area)).check(
                    "Private energy actual full-ring face area mismatch");
            }
        }
        for(const auto& cell:cells_)for(int f:cell.faces)energy_require(f>=0,"Private energy missing complete side");
    }
    /** Each synchronous event still belongs to this real prepared source/patch. */
    void event_matches(const Observation& event) const {
        energy_require(field_&&event.grid&&event.input&&event.flux&&event.delta&&event.descriptor&&event.source
            &&*event.source==field_->source&&event.source_generation==field_->source_generation
            &&event.field_generation==field_->field_generation&&event.generation>0
            &&scheduler::same_stage_descriptor(*event.descriptor,descriptor_)
            &&rz_runtime_witness::bits(event.dt,dt_)&&rz_runtime_witness::bits(event.stage_weight,1.)
            &&event.axis>=0&&event.axis<2,"Private energy event identity mismatch");
        const auto active=owner_.control.tree->GetActiveBlocks();
        bool found=false;
        for(std::size_t b=0;b<active.size();++b)if(active[b]==event.block_id) {
            const auto& block=owner_.control.pool->GetBlock(event.block_id);
            energy_require(event.grid==binding_.grids[b]&&event.input==&fluid(block,descriptor_.input_slot),
                "Private energy event changed real input owner");
            for(std::size_t f=0;f<7;++f)energy_require((event.input->*rz_runtime_witness::fields[f]).data()==input_leases_.at(b)[f],
                "Private energy event changed an actual seven-array input lease");found=true;
        }
        energy_require(found,"Private energy event is outside actual domain");
        source_matches(*field_,descriptor_.input_slot,owner_.context->step_start_time);
    }
public:
    /** Bind actual uniform cells and independent full-ring V from canonical faces. */
    NativeSelfEnergyWitness(Owner& owner,double dt):owner_(owner),dt_(dt),
        binding_(amr::bind_elliptic_mesh(owner.control,owner.config.grid,owner.runtime->handles())),
        entry_time_(owner.context->step_start_time),entry_gathers_(owner.execution->gathers) {
        energy_require(owner.method==scheduler::HydroMethod::Euler&&!owner.mixed
            &&(binding_.handles.size()==1||owner.homology.enabled),
            "Private energy witness is only the frozen uniform Euler lane");
        const auto active=owner.control.tree->GetActiveBlocks();
        if(owner.homology.enabled) {
            energy_require(entry_gathers_>=0&&entry_gathers_<=std::numeric_limits<int>::max()-2,
                "Homology real field counter cannot represent this two-field step");
            energy_require(binding_.storage.size()==std::size_t(owner.homology.cells),
                "Homology actual cell count differs from frozen layout");
            const auto& boundary=owner.runtime->hydro_boundary_budget();
            energy_require(boundary.size()>4,"Homology entry boundary budget missing");
            boundary_energy_entry_=energy_finite(boundary[4]);
        }
        input_leases_.resize(binding_.handles.size());
        for(std::size_t b=0;b<binding_.handles.size();++b) {
            const auto& source=owner.control.pool->GetBlock(active.at(b)).fluid_state;
            for(std::size_t f=0;f<7;++f)input_leases_[b][f]=(source.*rz_runtime_witness::fields[f]).data();
        }
        for(const auto& where:binding_.storage) {
            const auto& block=owner.control.pool->GetBlock(active.at(where.block));const auto& grid=block.grid;
            const int i=where.offset%grid.stride_y,j=where.offset/grid.stride_y;
            energy_require(i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je()
                &&grid.GetIndex(i,j,0)==where.offset,"Private energy binding is not actual active cell");
            const long double rl=grid.GetFacePosL(i),rh=grid.GetFacePosR(i),zl=grid.GetAxialFacePosL(j),zh=grid.GetAxialFacePosR(j);
            const long double V=energy_finite(arch::constants::math::pi*(rh-rl)*(rh+rl)*(zh-zl));
            energy_require(V>0.,"Private energy volume is nonpositive");
            const auto& state=block.fluid_state;
            cells_.push_back({where.block,where.offset,i,j,V,energy_finite(V*state.rho[where.offset]),
                energy_finite(V*state.eng[where.offset]),state.rho[where.offset],state.eng[where.offset]});
            entry_resolution_+=V*(std::nextafter(state.eng[where.offset],std::numeric_limits<double>::infinity())-state.eng[where.offset]);
        }
        energy_finite(entry_resolution_);
    }
    /** The original Hydro owner passes its ONLY first-stage owning field copy. */
    static void capture(void* payload,Owner& owner,Field&& field,const scheduler::StageDescriptor& descriptor) {
        energy_require(payload!=nullptr,"Private energy diagnostic payload is null");
        auto& self=*static_cast<NativeSelfEnergyWitness*>(payload);
        energy_require(&owner==&self.owner_&&!self.field_,"Private energy capture reused/foreign owner");
        self.descriptor_=descriptor;
        energy_require(scheduler::same_stage_descriptor(descriptor,scheduler::make_hydro_plan(scheduler::HydroMethod::Euler).stages.front()),
            "Private energy capture is not actual Euler descriptor");
        self.source_matches(field,descriptor.input_slot,owner.context->step_start_time);
        if(owner.homology.enabled)energy_require(owner.execution->gathers==self.entry_gathers_+1,
            "Homology Hydro capture did not consume exactly one new authentic field");
        if(owner.homology.enabled) {
            const auto* frame=owner.gravity->prepared_native_self();
            energy_require(frame!=nullptr,"Homology has no actual live Hydro gravity frame");
            self.gravity_timestep_=frame->timestep();
            energy_require(std::isfinite(self.gravity_timestep_)&&self.gravity_timestep_>0.
                &&self.dt_<=self.gravity_timestep_,"Frozen homology dt exceeds actual source gravity cap");
        }
        const auto active=owner.control.tree->GetActiveBlocks();
        for(const auto& cell:self.cells_) {
            const auto& state=fluid(owner.control.pool->GetBlock(active.at(cell.block)),descriptor.input_slot);
            energy_require(rz_runtime_witness::bits(cell.rho0,state.rho[cell.offset])
                &&rz_runtime_witness::bits(cell.energy0,state.eng[cell.offset]),
                "First real Hydro source differs from macro-entry mass/energy");
        }
        self.field_.emplace(std::move(field));self.bind_faces();
    }
    /** Return only the cap measured on the actual prepared pre-Hydro field. */
    double gravity_timestep() const {
        energy_require(owner_.homology.enabled&&field_&&gravity_timestep_>0.,
            "Homology actual gravity timestep has not been measured");return gravity_timestep_;
    }
    /** Observe original physical mass/energy flux and independently pair same Phi.
     * q=-dt sum A F_out (Phi_face-Phi_cell); A and dt appear exactly once.
     */
    static void sink(void* payload,const Observation& event) {
        energy_require(payload!=nullptr,"Private energy diagnostic payload is null");
        auto& self=*static_cast<NativeSelfEnergyWitness*>(payload);std::lock_guard<std::mutex> lock(self.mutex_);
        self.event_matches(event);
        energy_require(event.kind==Observation::Kind::AxisBefore||event.kind==Observation::Kind::AxisAfter,
            "Uniform energy diagnostic unexpectedly received a CF Energy operation");
        const auto active=self.owner_.control.tree->GetActiveBlocks();
        for(std::size_t n=0;n<self.cells_.size();++n) {
            auto& cell=self.cells_[n];if(active.at(cell.block)!=event.block_id)continue;
            energy_require(std::size_t(cell.offset)<event.delta->size(),"Private energy delta extent");
            const long double now=energy_finite(cell.volume*event.delta->at(cell.offset).eng);
            if(event.kind==Observation::Kind::AxisBefore) {
                energy_require(cell.phase[event.axis]==0,"Private energy duplicate before-axis event");
                cell.before[event.axis]=now;cell.phase[event.axis]=1;
                for(int side=0;side<2;++side) {
                    const std::size_t f=std::size_t(cell.faces[2*event.axis+side]);const auto& face=self.field_->faces[f];
                    const int index=cell.offset+(side?(event.axis==0?1:event.grid->stride_y):0);
                    energy_require(index>=0&&std::size_t(index)<event.flux->size(),"Private energy original face flux extent");
                    const long double sign=side?1.L:-1.L,A=face.area,F=event.flux->at(index).rho,
                        P=face_phi(*self.field_,f),term=energy_finite(-self.dt_*A*sign*F*(P-self.field_->potential[n]));
                    cell.expected[event.axis]+=term;cell.operation[event.axis]+=std::abs(term);
                    if(face.boundary_side>=0) {
                        const long double bp=energy_finite(self.dt_*event.stage_weight*A*sign*F*P),
                            be=energy_finite(self.dt_*event.stage_weight*A*sign*event.flux->at(index).eng);
                        self.boundary_phi_+=bp;self.boundary_phi_scale_+=std::abs(bp);
                        self.boundary_energy_+=be;self.boundary_energy_scale_+=std::abs(be);
                    }
                }
            } else {
                energy_require(cell.phase[event.axis]==1,"Private energy missing/duplicate before-axis");
                const long double error=energy_finite(now-cell.before[event.axis]-cell.expected[event.axis]);
                const NativeEnergyBudget budget(error,std::abs(now)+std::abs(cell.before[event.axis]),cell.operation[event.axis]);
                budget.check("Same-execution gravity work does not match actual face pairing");
                if(budget.allowance>0.)self.max_local_work_ratio_=std::max(self.max_local_work_ratio_,std::abs(error)/budget.allowance);
                self.work_error_+=error;self.work_state_scale_+=budget.state_scale;self.work_operation_scale_+=budget.operation_scale;
                cell.phase[event.axis]=2;
            }
        }
        if(event.kind==Observation::Kind::AxisBefore)++self.before_events_;else ++self.after_events_;
    }
    /** Record actual returned raw Euler dU.rho; no flux inversion or extra solve. */
    static void delta(void* payload,Owner& owner,int id,const FluidState& input,const Grid& grid,const std::vector<FluidVector>& du) {
        energy_require(payload!=nullptr,"Private energy diagnostic payload is null");
        auto& self=*static_cast<NativeSelfEnergyWitness*>(payload);std::lock_guard<std::mutex> lock(self.mutex_);
        energy_require(&owner==&self.owner_&&self.field_,"Private energy delta callback foreign/missing field");
        const auto active=owner.control.tree->GetActiveBlocks();
        for(auto& cell:self.cells_)if(active.at(cell.block)==id) {
            energy_require(&grid==self.binding_.grids[cell.block]&&&input==&fluid(owner.control.pool->GetBlock(id),self.descriptor_.input_slot)
                &&!cell.delta_seen&&std::size_t(cell.offset)<du.size(),"Private energy delta source/extent/duplicate");
            cell.dm=energy_finite(cell.volume*du[cell.offset].rho*self.descriptor_.flux_register_weight);cell.delta_seen=true;
        }
    }
    /** Close algebra with a genuine postCurrent field; green/skew/time are reports.
     * DeltaW=L+S, Dtotal=epsilon+(L-Q)+S; neither S==BG nor Dtotal==0 is asserted.
     */
    void publish(const Field& post,bool fault_rollback,bool prospective=false,const std::string& directory={}) {
        source_matches(post,state::StateSlot::Current,owner_.counters->t_current);
        energy_require(field_&&post.faces.size()==field_->faces.size()
            &&before_events_==2*binding_.handles.size()&&after_events_==2*binding_.handles.size()
            &&(fault_rollback||(prospective&&owner_.homology.enabled))
            &&(!owner_.homology.enabled||owner_.execution->gathers==entry_gathers_+2),
            "Private energy incomplete actual macro/post/rollback evidence");
        const auto active=owner_.control.tree->GetActiveBlocks();
        long double deltaE=0.,stateE=0.,Q=0.,Qscale=0.,W0=0.,W1=0.,L=0.,S=0.,pairscale=0.,masserror=0.,massstate=0.,massops=0.,resolution=entry_resolution_;
        for(std::size_t n=0;n<cells_.size();++n) {
            const auto& c=cells_[n];const auto& state=owner_.control.pool->GetBlock(active.at(c.block)).fluid_state;
            const long double m1=energy_finite(c.volume*state.rho[c.offset]),E1=energy_finite(c.volume*state.eng[c.offset]),dm=m1-c.m0;
            energy_require(c.phase[0]==2&&c.phase[1]==2&&c.delta_seen,"Private energy cell work/delta incomplete");
            const NativeEnergyBudget mass(dm-c.dm,std::abs(m1)+std::abs(c.m0),std::abs(c.dm));mass.check("Actual Euler mass change differs from returned dU");
            if(mass.allowance>0.)max_local_mass_ratio_=std::max(max_local_mass_ratio_,std::abs(mass.error)/mass.allowance);
            masserror+=mass.error;massstate+=mass.state_scale;massops+=mass.operation_scale;
            deltaE+=E1-c.E0;stateE+=std::abs(E1)+std::abs(c.E0);
            Q+=field_->potential[n]*c.dm;Qscale+=std::abs(field_->potential[n]*c.dm);
            W0+=.5L*c.m0*field_->potential[n];W1+=.5L*m1*post.potential[n];
            L+=.5L*(post.potential[n]+field_->potential[n])*dm;
            S+=.5L*(c.m0*post.potential[n]-m1*field_->potential[n]);
            pairscale+=.5L*(std::abs(c.m0*post.potential[n])+std::abs(m1*field_->potential[n]));
            resolution+=c.volume*(std::nextafter(state.eng[c.offset],std::numeric_limits<double>::infinity())-state.eng[c.offset]);
        }
        const auto& boundary=owner_.runtime->hydro_boundary_budget();energy_require(boundary.size()>4,"Private energy Runtime boundary budget missing");
        const long double BE=owner_.homology.enabled?boundary[4]-boundary_energy_entry_:boundary[4],
            BP=boundary_phi_,epsilon=deltaE+BE+BP+Q;
        const NativeEnergyBudget account(epsilon,stateE,std::abs(deltaE)+boundary_energy_scale_+boundary_phi_scale_+Qscale),
            boundary_check(BE-boundary_energy_,0.,boundary_energy_scale_),
            work(work_error_,work_state_scale_,work_operation_scale_),mass(masserror,massstate,massops),
            decomposition((W1-W0)-L-S,std::abs(W0)+std::abs(W1),std::abs(L)+pairscale);
        account.check("Uniform Euler same-execution energy accounting failed");boundary_check.check("Actual boundary budget differs from original FE");
        work.check("Gravity work aggregate accounting failed");mass.check("Euler aggregate mass accounting failed");decomposition.check("Endpoint point-energy decomposition failed");
        long double BG=0.;
        for(std::size_t f=0;f<post.faces.size();++f) {
            const auto& a=field_->faces[f];const auto& b=post.faces[f];
            energy_require(a.left==b.left&&a.right==b.right&&a.axis==b.axis&&a.boundary_side==b.boundary_side
                &&a.native_bounds==b.native_bounds&&a.construction==b.construction
                &&rz_runtime_witness::bits(a.area,b.area)&&a.samples==b.samples&&a.value_samples==b.value_samples
                &&rz_runtime_witness::bits(a.coefficients,b.coefficients)&&rz_runtime_witness::bits(a.value_coefficients,b.value_coefficients)
                &&rz_runtime_witness::bits(a.boundary_coefficient,b.boundary_coefficient)
                &&rz_runtime_witness::bits(a.anchor_coefficient,b.anchor_coefficient)
                &&rz_runtime_witness::bits(a.value_boundary_coefficient,b.value_boundary_coefficient),"Post field changed actual original face rows");
            for(std::size_t d=0;d<3;++d)energy_require(rz_runtime_witness::bits(a.center[d],b.center[d])
                &&rz_runtime_witness::bits(a.fragment_lower[d],b.fragment_lower[d])
                &&rz_runtime_witness::bits(a.fragment_upper[d],b.fragment_upper[d])
                &&rz_runtime_witness::bits(a.fragment_width[d],b.fragment_width[d]),"Post field changed original face geometry bits");
            if(a.boundary_side>=0) {
                const long double sign=a.boundary_side%2?1.L:-1.L;
                BG+=a.area*sign*(face_phi(post,f)*field_->face_gradient[f]-face_phi(*field_,f)*post.face_gradient[f]);
            }
        }
        BG=energy_finite(BG/(8.L*arch::constants::math::pi*field_->source.gravitational_constant));
        const long double D=energy_finite(deltaE+W1-W0+BE+BP),finite_step=energy_finite(L-Q),skew=energy_finite(S-BG);
        NativeEnergyBudget(D-(epsilon+finite_step+S),stateE+std::abs(W0)+std::abs(W1),std::abs(BE)+std::abs(BP)+Qscale+std::abs(L)+pairscale).check(
            "Whole diagnostic algebra decomposition failed");
        energy_finite(resolution);energy_finite(stateE);energy_finite(Qscale);
        // Publish only after both accepted fields and original algebra gates.
        // Legacy four-field callers additionally require their real fault rollback;
        // prospective callers retain that separate owner rather than pretending
        // this two-field batch reran it. No total-energy science gate is defined.
        const auto& destination=directory.empty()?owner_.config.io.out_dir:directory;
        std::ofstream json(destination+"/energy-diagnostic.json"),tsv(destination+"/energy-accounting.tsv");
        energy_require(bool(json)&&bool(tsv),"Cannot open accepted energy diagnostic output");
        json<<std::setprecision(std::numeric_limits<long double>::max_digits10)
            <<"{\"schema\":\"arch-private-native-self-energy-1\",\"physical_qualified\":false,\"total_energy_science\":\"UNVERIFIED\",\"accounting_checked\":true,\"actual_fields_batch\":"<<(prospective?2:4)<<",\"method\":\"Euler\",\"dt\":"<<dt_
            <<",\"G\":"<<field_->source.gravitational_constant<<",\"cells\":"<<cells_.size()
            <<",\"source_generation0\":"<<field_->source_generation<<",\"source_generation1\":"<<post.source_generation
            <<",\"field_generation0\":"<<field_->field_generation<<",\"field_generation1\":"<<post.field_generation
            <<",\"operator_revision0\":"<<field_->source.operator_revision<<",\"operator_revision1\":"<<post.source.operator_revision
            <<",\"boundary_revision0\":"<<field_->source.boundary_revision<<",\"boundary_revision1\":"<<post.source.boundary_revision
            <<",\"accuracy_revision0\":"<<field_->source.accuracy_revision<<",\"accuracy_revision1\":"<<post.source.accuracy_revision
            <<",\"relative_tolerance\":"<<owner_.config.physics.gravity.relative_tolerance
            <<",\"absolute_tolerance\":"<<owner_.config.physics.gravity.absolute_tolerance
            <<",\"max_cycles\":"<<owner_.config.physics.gravity.max_cycles
            <<",\"root_bounds\":["<<owner_.config.grid.x1_min<<','<<owner_.config.grid.x1_max<<','<<owner_.config.grid.x2_min<<','<<owner_.config.grid.x2_max<<']'
            <<",\"time0\":"<<field_->source.input_time<<",\"time1\":"<<post.source.input_time
            <<",\"axis_before_events\":"<<before_events_<<",\"axis_after_events\":"<<after_events_
            <<",\"max_local_work_budget_ratio\":"<<max_local_work_ratio_<<",\"max_local_mass_budget_ratio\":"<<max_local_mass_ratio_
            <<",\"resolution_floor_energy_ulp_sum\":"<<resolution<<",\"DeltaE\":"<<deltaE<<",\"B_E\":"<<BE<<",\"B_Phi\":"<<BP<<",\"Q\":"<<Q
            <<",\"W0\":"<<W0<<",\"W1\":"<<W1<<",\"L\":"<<L<<",\"S\":"<<S<<",\"B_G_stored_point_rows\":"<<BG
            <<",\"finite_step_L_minus_Q\":"<<finite_step<<",\"S_minus_B_G_diagnostic\":"<<skew<<",\"D_total_diagnostic\":"<<D<<",\"D_total_minus_B_G_diagnostic\":"<<D-BG
            <<",\"budget_formula\":\"64*epsilon*absolute_state_scale+1e-12*absolute_operation_scale\",\"accounting_budget\":";
        account.json(json);json<<",\"work_budget\":";work.json(json);json<<",\"mass_budget\":";mass.json(json);
        json<<",\"boundary_budget\":";boundary_check.json(json);json<<",\"point_energy_decomposition_budget\":";decomposition.json(json);
        json<<",\"input_handles\":[";
        for(std::size_t b=0;b<field_->source.inputs.size();++b) {if(b)json<<',';const auto& a=field_->source.inputs[b];const auto& z=post.source.inputs[b];
            json<<"{\"uid\":"<<a.block.uid.value<<",\"epoch\":"<<a.block.epoch.value<<",\"slot0\":"<<int(a.slot)<<",\"slot1\":"<<int(z.slot)
                <<",\"version0\":"<<a.version.value<<",\"version1\":"<<z.version.value<<",\"density_lease0\":"<<a.storage_generation<<",\"density_lease1\":"<<z.storage_generation<<'}';}
        json<<"]";
        if(prospective)json<<",\"resource_scope\":\"prospective-homology-step;whole-request-guard-external\",\"before_materialized_source_export\":\"UNKNOWN\",\"rollback_campaign\":\"retained-separate-original-four-field-owner\"";
        json<<"}\n";tsv<<std::setprecision(std::numeric_limits<long double>::max_digits10)<<"scope\tDeltaE\tB_E\tB_Phi\tQ\tepsilon_account\tDeltaW\tL\tS\tB_G\tfinite_step\tD_total\n"
            <<"stored-point-rows-diagnostic\t"<<deltaE<<'\t'<<BE<<'\t'<<BP<<'\t'<<Q<<'\t'<<epsilon<<'\t'<<W1-W0<<'\t'<<L<<'\t'<<S<<'\t'<<BG<<'\t'<<finite_step<<'\t'<<D<<'\n';
        json.flush();tsv.flush();energy_require(bool(json)&&bool(tsv),"Accepted energy diagnostic output failed");
    }
    /** Reduce the genuine two-time discrete Green identity from ORIGINAL rows.
     * Workflow: fence both actual sources/rows; assemble original face incidence;
     * reduce internal/boundary jumps and complete prescribed-boundary residual;
     * check only the fixed accounting window; publish compact accepted evidence.
     * With R_k=div(g_k)-4*pi*G*rho_k and increasing-coordinate stored gradients,
     * S-BG=(T_internal+T_boundary-T_residual)/(8*pi*G). This finite-dimensional
     * identity neither imposes S==BG nor certifies a continuous isolated field.
     */
    void publish_green_pair(const Field& post,bool prospective=false,const std::string& directory={}) {
        energy_require(!prospective||owner_.homology.enabled,"Prospective Green label has no homology input");
        source_matches(post,state::StateSlot::Current,owner_.counters->t_current);
        energy_require(field_&&post.faces.size()==field_->faces.size()
            &&before_events_==2*binding_.handles.size()&&after_events_==2*binding_.handles.size()
            &&(owner_.homology.enabled?owner_.execution->gathers==entry_gathers_+2:owner_.execution->gathers==2),
            "Private Green pair lacks two genuine fields or completed actual work");
        energy_require(field_->source.topology==post.source.topology
            &&field_->source.operator_revision==post.source.operator_revision
            &&field_->source.boundary_revision==post.source.boundary_revision
            &&field_->source.accuracy_revision==post.source.accuracy_revision
            &&rz_runtime_witness::bits(field_->source.gravitational_constant,post.source.gravitational_constant)
            &&rz_runtime_witness::bits(field_->source.input_time,entry_time_)
            &&rz_runtime_witness::bits(post.source.input_time,entry_time_+dt_),
            "Private Green pair changed topology/operator/G or genuine endpoint time");
        for(std::size_t b=0;b<field_->source.inputs.size();++b) {
            const auto& a=field_->source.inputs[b];const auto& z=post.source.inputs[b];
            energy_require(a.block==z.block&&a.slot==descriptor_.input_slot
                &&z.slot==state::StateSlot::Current&&a.version!=z.version,
                "Private Green pair did not advance the actual Current publication");
        }
        // A term's scale is frozen as the absolute ORIGINAL primitive products,
        // not abs(reduced result) and never a function of the observed mismatch.
        struct Term {
            long double value=0.,absolute=0.;
            /** Add two signed algebra terms and retain their input operation scale. */
            void add(long double a,long double b) {
                value=energy_finite(value+energy_finite(energy_finite(a)+energy_finite(b)));
                absolute=energy_finite(absolute+std::abs(a)+std::abs(b));
            }
            /** Divide both signed result and absolute scale by the positive constant. */
            void divide(long double denominator) {
                value=energy_finite(value/denominator);absolute=energy_finite(absolute/denominator);
            }
            /** Emit the value and its explicitly defined, untuned input scale. */
            void json(std::ostream& stream) const {
                stream<<"{\"value\":"<<value<<",\"absolute_operation_scale\":"<<absolute<<'}';
            }
        };
        const long double four_pi_G=energy_finite(4.L*arch::constants::math::pi*field_->source.gravitational_constant),
            denominator=energy_finite(2.L*four_pi_G);
        energy_require(denominator>0.,"Private Green pair G denominator is not positive");
        std::vector<long double> div0(cells_.size(),0.L),div1(cells_.size(),0.L),rho1(cells_.size(),0.L);
        const auto active=owner_.control.tree->GetActiveBlocks();
        Term S,BG,internal,boundary_jump,residual;
        std::array<Term,2> internal_axis{};std::array<Term,4> boundary_sides{};
        std::array<std::size_t,3> construction_counts{};
        std::size_t internal_count=0,boundary_count=0;
        long double mass_error=0.,mass_state=0.,mass_operations=0.;
        for(std::size_t n=0;n<cells_.size();++n) {
            const auto& c=cells_[n];const auto& state=owner_.control.pool->GetBlock(active.at(c.block)).fluid_state;
            energy_require(c.phase[0]==2&&c.phase[1]==2&&c.delta_seen,
                "Private Green pair cell lacks completed original work/dU evidence");
            rho1[n]=energy_finite(state.rho[c.offset]);energy_require(rho1[n]>0.,"Private Green pair post density is invalid");
            const long double m1=energy_finite(c.volume*rho1[n]);
            const NativeEnergyBudget mass(m1-c.m0-c.dm,std::abs(m1)+std::abs(c.m0),std::abs(c.dm));
            mass.check("Private Green pair actual mass change differs from original Euler dU");
            mass_error+=mass.error;mass_state+=mass.state_scale;mass_operations+=mass.operation_scale;
            S.add(.5L*c.m0*post.potential[n],-.5L*m1*field_->potential[n]);
        }
        for(std::size_t f=0;f<post.faces.size();++f) {
            const auto& a=field_->faces[f];const auto& b=post.faces[f];
            energy_require(a.left==b.left&&a.right==b.right&&a.axis==b.axis&&a.boundary_side==b.boundary_side
                &&a.native_bounds==b.native_bounds&&a.construction==b.construction
                &&rz_runtime_witness::bits(a.area,b.area)&&a.samples==b.samples&&a.value_samples==b.value_samples
                &&rz_runtime_witness::bits(a.coefficients,b.coefficients)&&rz_runtime_witness::bits(a.value_coefficients,b.value_coefficients)
                &&rz_runtime_witness::bits(a.boundary_coefficient,b.boundary_coefficient)
                &&rz_runtime_witness::bits(a.anchor_coefficient,b.anchor_coefficient)
                &&rz_runtime_witness::bits(a.value_boundary_coefficient,b.value_boundary_coefficient),
                "Private Green pair changed ORIGINAL stored face rows");
            for(std::size_t d=0;d<3;++d)energy_require(rz_runtime_witness::bits(a.center[d],b.center[d])
                &&rz_runtime_witness::bits(a.fragment_lower[d],b.fragment_lower[d])
                &&rz_runtime_witness::bits(a.fragment_upper[d],b.fragment_upper[d])
                &&rz_runtime_witness::bits(a.fragment_width[d],b.fragment_width[d]),
                "Private Green pair changed ORIGINAL native face geometry");
            const int construction=static_cast<int>(a.construction);
            energy_require(construction>=0&&construction<3&&a.axis>=0&&a.axis<2&&a.native_bounds
                &&a.area>0.&&std::isfinite(a.area),"Private Green pair malformed original face");
            ++construction_counts[std::size_t(construction)];
            const long double A=a.area,g0=field_->face_gradient[f],g1=post.face_gradient[f];
            // Original face.left receives +A*g in div; face.right receives -A*g.
            for(int side=0;side<2;++side) {
                const int n=side?a.right:a.left;if(n<0)continue;
                energy_require(std::size_t(n)<cells_.size(),"Private Green pair incidence index is outside actual cells");
                const long double sign=side?-1.L:1.L;
                div0[std::size_t(n)]=energy_finite(div0[std::size_t(n)]+sign*A*g0);
                div1[std::size_t(n)]=energy_finite(div1[std::size_t(n)]+sign*A*g1);
            }
            if(a.boundary_side<0) {
                energy_require(a.boundary_side==-1&&a.left>=0&&a.right>=0&&a.left!=a.right,
                    "Private Green pair malformed internal incidence");
                ++internal_count;
                const long double p0L=field_->potential[std::size_t(a.left)],p0R=field_->potential[std::size_t(a.right)],
                    p1L=post.potential[std::size_t(a.left)],p1R=post.potential[std::size_t(a.right)],
                    x=A*(p1L-p1R)*g0,y=-A*(p0L-p0R)*g1;
                internal.add(x,y);internal_axis[std::size_t(a.axis)].add(x,y);
            } else {
                energy_require(a.boundary_side<4&&a.boundary_side/2==a.axis
                    &&((a.boundary_side%2==1&&a.left>=0&&a.right<0)
                        ||(a.boundary_side%2==0&&a.right>=0&&a.left<0)),
                    "Private Green pair malformed physical-boundary orientation");
                ++boundary_count;const std::size_t n=std::size_t(a.left>=0?a.left:a.right);
                const long double sign=a.boundary_side%2?1.L:-1.L,
                    p0=field_->potential[n],p1=post.potential[n],pf0=face_phi(*field_,f),pf1=face_phi(post,f),
                    x=sign*A*(p1-pf1)*g0,y=-sign*A*(p0-pf0)*g1;
                boundary_jump.add(x,y);boundary_sides[std::size_t(a.boundary_side)].add(x,y);
                BG.add(sign*A*pf1*g0,-sign*A*pf0*g1);
            }
        }
        long double volume_sum=0.,residual_square0=0.,residual_square1=0.,residual_max0=0.,residual_max1=0.;
        for(std::size_t n=0;n<cells_.size();++n) {
            const auto& c=cells_[n];const long double p0=field_->potential[n],p1=post.potential[n],
                numerator0=energy_finite(div0[n]-four_pi_G*c.m0),
                numerator1=energy_finite(div1[n]-four_pi_G*c.volume*rho1[n]),
                R0=energy_finite(numerator0/c.volume),R1=energy_finite(numerator1/c.volume);
            // V*R is evaluated as its ORIGINAL extensive incidence-source residual.
            // This is complete inhomogeneous Delta residual, not homogeneous A*Phi.
            residual.add(p1*numerator0,-p0*numerator1);
            volume_sum=energy_finite(volume_sum+c.volume);
            residual_square0=energy_finite(residual_square0+c.volume*R0*R0);
            residual_square1=energy_finite(residual_square1+c.volume*R1*R1);
            residual_max0=std::max(residual_max0,std::abs(R0));residual_max1=std::max(residual_max1,std::abs(R1));
        }
        energy_require(volume_sum>0.,"Private Green pair actual total volume is invalid");
        BG.divide(denominator);internal.divide(denominator);boundary_jump.divide(denominator);residual.divide(denominator);
        for(auto& term:internal_axis)term.divide(denominator);
        for(auto& term:boundary_sides)term.divide(denominator);
        const long double gap=energy_finite(S.value-BG.value),
            reconstructed=energy_finite(internal.value+boundary_jump.value-residual.value),
            operation_scale=energy_finite(BG.absolute+internal.absolute+boundary_jump.absolute+residual.absolute);
        const NativeEnergyBudget algebra(gap-reconstructed,S.absolute,operation_scale),
            mass(mass_error,mass_state,mass_operations),work(work_error_,work_state_scale_,work_operation_scale_);
        algebra.check("Original-face discrete Green decomposition does not close");
        mass.check("Private Green pair aggregate original Euler mass accounting failed");
        work.check("Private Green pair aggregate original face-work accounting failed");
        const long double rms0=energy_finite(std::sqrt(residual_square0/volume_sum)),
            rms1=energy_finite(std::sqrt(residual_square1/volume_sum));
        // Publish only after the genuine macro/post source, all row fences and
        // fixed algebra checks pass. No flux/Phi/rho arrays or raw data are emitted.
        const auto& destination=directory.empty()?owner_.config.io.out_dir:directory;
        std::ofstream json(destination+"/green-pair-diagnostic.json");
        energy_require(bool(json),"Cannot open accepted Green pair diagnostic output");
        json<<std::setprecision(std::numeric_limits<long double>::max_digits10)
            <<"{\"schema\":\"arch-private-native-self-green-pair-1\",\"physical_qualified\":false,\"continuous_green_science\":\"UNVERIFIED\",\"algebra_checked\":true,\"actual_fields_batch\":2,\"resource_scope\":\""<<(prospective?"prospective-homology-step;whole-request-guard-external":"distinct-two-field-diagnostic-240s")<<"\",\"method\":\"Euler\",\"dt\":"<<dt_
            <<",\"G\":"<<field_->source.gravitational_constant<<",\"cells\":"<<cells_.size()<<",\"faces\":"<<post.faces.size()
            <<",\"time0\":"<<field_->source.input_time<<",\"time1\":"<<post.source.input_time
            <<",\"source_generation0\":"<<field_->source_generation<<",\"source_generation1\":"<<post.source_generation
            <<",\"field_generation0\":"<<field_->field_generation<<",\"field_generation1\":"<<post.field_generation
            <<",\"generation_scope\":\"each-separately-created-stage;actual-handle-slot-version-time-is-authoritative\""
            <<",\"operator_revision\":"<<post.source.operator_revision<<",\"boundary_revision\":"<<post.source.boundary_revision
            <<",\"accuracy_revision\":"<<post.source.accuracy_revision<<",\"relative_tolerance\":"<<owner_.config.physics.gravity.relative_tolerance
            <<",\"absolute_tolerance\":"<<owner_.config.physics.gravity.absolute_tolerance<<",\"max_cycles\":"<<owner_.config.physics.gravity.max_cycles
            <<",\"root_bounds\":["<<owner_.config.grid.x1_min<<','<<owner_.config.grid.x1_max<<','<<owner_.config.grid.x2_min<<','<<owner_.config.grid.x2_max<<']'
            <<",\"axis_before_events\":"<<before_events_<<",\"axis_after_events\":"<<after_events_
            <<",\"gradient_orientation\":\"increasing-coordinate;left-outward-plus,right-outward-minus\",\"residual_definition\":\"complete-inhomogeneous-div(g)-4*pi*G*rho\""
            <<",\"identity\":\"S-BG=internal_jump+boundary_jump-residual_cross\",\"scope\":\"original-stored-point-Phi-and-face-rows;not-continuous-Green-certificate\""
            <<",\"S\":";S.json(json);json<<",\"B_G_stored_point_rows\":";BG.json(json);
        json<<",\"internal_jump\":";internal.json(json);json<<",\"boundary_jump\":";boundary_jump.json(json);
        json<<",\"residual_cross\":";residual.json(json);
        // Same-field proof receipts remain distinct from the LD row diagnostic
        // below; writing them performs no solve, gather, norm or energy bound.
        json<<",\"field_certificate0\":";
        RzMaterializedSourceRecord::write_native_discrete_certificate(json,*field_);
        json<<",\"field_certificate1\":";
        RzMaterializedSourceRecord::write_native_discrete_certificate(json,post);
        json<<",\"S_minus_B_G\":"<<gap<<",\"reconstructed_gap\":"<<reconstructed
            <<",\"internal_faces\":"<<internal_count<<",\"physical_boundary_faces\":"<<boundary_count
            <<",\"construction_counts\":{\"TwoPoint\":"<<construction_counts[0]<<",\"PolynomialFit\":"<<construction_counts[1]<<",\"EllipticRecovery\":"<<construction_counts[2]<<'}'
            <<",\"internal_by_axis\":[";for(std::size_t a=0;a<2;++a){if(a)json<<',';internal_axis[a].json(json);}
        json<<"],\"boundary_by_side\":[";for(std::size_t a=0;a<4;++a){if(a)json<<',';boundary_sides[a].json(json);}
        json<<"],\"native_volume_sum\":"<<volume_sum<<",\"discrete_residual_rms0\":"<<rms0<<",\"discrete_residual_rms1\":"<<rms1
            <<",\"discrete_residual_linf0\":"<<residual_max0<<",\"discrete_residual_linf1\":"<<residual_max1
            <<",\"residual_norm_quality\":\"long-double-stored-row-diagnostic;not-outward-physical-certificate\",\"volume_definition\":\"full-ring-long-double-antiderivative-of-actual-canonical-cell-bounds\""
            <<",\"budget_formula\":\"64*epsilon*absolute_state_scale+1e-12*absolute_operation_scale\",\"algebra_budget\":";
        algebra.json(json);json<<",\"mass_budget\":";mass.json(json);json<<",\"work_budget\":";work.json(json);
        json<<",\"input_handles\":[";
        for(std::size_t b=0;b<field_->source.inputs.size();++b){if(b)json<<',';const auto& a=field_->source.inputs[b];const auto& z=post.source.inputs[b];
            json<<"{\"uid\":"<<a.block.uid.value<<",\"epoch\":"<<a.block.epoch.value<<",\"slot0\":"<<int(a.slot)<<",\"slot1\":"<<int(z.slot)
                <<",\"version0\":"<<a.version.value<<",\"version1\":"<<z.version.value<<",\"density_lease0\":"<<a.storage_generation<<",\"density_lease1\":"<<z.storage_generation<<'}';}
        json<<"]}\n";json.flush();energy_require(bool(json),"Accepted Green pair diagnostic output failed");
    }

};

/** Throw at a genuinely reached original AxisBefore reservation; no prefix publish. */
inline void native_energy_fault(void* payload,const Physical::Gravity::NativeSelfStageObservation& event) {
    if(event.kind==Physical::Gravity::NativeSelfStageObservation::Kind::AxisBefore) {
        energy_require(payload!=nullptr,"Private energy fault payload is null");
        ++*static_cast<int*>(payload);throw std::logic_error("PRIVATE_SELF_ENERGY_AXIS_PREFIX_FAULT");
    }
}
/** An inert attachment identity for actual quiescent setter negative cases. */
inline void native_energy_noop(void*,const Physical::Gravity::NativeSelfStageObservation&) {}

/** Four genuine fields: passive macro, observed macro, actual postCurrent, fault.
 * All default CI/matrix paths remain unchanged; this explicit maintenance lane
 * has no full isolated-force, AMR-energy, RK, Device or total-energy qualification.
 */
template<class Owner> void run_native_self_energy(double dt) {
    using Stage=driver::GravityStage;using Witness=NativeSelfEnergyWitness<Owner>;
    Owner passive(scheduler::HydroMethod::Euler,false,"energy-passive");passive.advance();
    passive.stage->flush_committed_diagnostics();journal_rows(passive,1);
    energy_require(passive.execution->gathers==1,"Passive energy macro did not use one genuine field");
    const auto& passive_block=passive.control.pool->GetBlock(passive.control.tree->GetActiveBlocks().front());
    std::array<std::vector<double>,7> passive_values;
    for(std::size_t f=0;f<7;++f)passive_values[f]=passive_block.fluid_state.*rz_runtime_witness::fields[f];
    Owner observed(scheduler::HydroMethod::Euler,false,"energy-observed");Witness witness(observed,dt);
    int other=0;
    bool rejected=false;try{observed.stage->set_native_self_flux_observation(nullptr,&other);}catch(const std::logic_error&){rejected=true;}
    energy_require(rejected,"Null energy sink retained a nonnull payload");
    observed.stage->set_native_self_flux_observation(&Witness::sink,&witness);
    observed.stage->set_native_self_flux_observation(&Witness::sink,&witness); // same actual pair is idempotent
    rejected=false;try{observed.stage->set_native_self_flux_observation(&native_energy_noop,&other);}catch(const std::logic_error&){rejected=true;}
    energy_require(rejected,"Energy sink silently replaced another attached owner");
    observed.stage->set_native_self_flux_observation(nullptr,nullptr);
    observed.stage->set_native_self_flux_observation(&Witness::sink,&witness);
    observed.observer->energy_payload=&witness;observed.observer->energy_field_capture=&Witness::capture;
    observed.observer->energy_delta_capture=&Witness::delta;observed.advance();
    auto& block=observed.control.pool->GetBlock(observed.control.tree->GetActiveBlocks().front());
    for(std::size_t f=0;f<7;++f)energy_require(rz_runtime_witness::bits(block.fluid_state.*rz_runtime_witness::fields[f],passive_values[f]),
        "Optional energy observer changed accepted real seven-array bits");
    observed.stage->flush_committed_diagnostics();journal_rows(observed,1);
    energy_require(observed.execution->gathers==1&&!observed.gravity->prepared_native_self(),"Observed energy macro field count/live receipt");
    observed.context->hydro_preparation=nullptr;observed.stage.reset();
    const auto old_path=std::filesystem::path(observed.config.io.out_dir)/"native_rz_candidates.tsv";
    const auto journal_path=std::filesystem::path(observed.config.io.out_dir)/"hydro-stages.tsv";
    energy_require(!std::filesystem::exists(journal_path),"Energy journal target already exists; preserve prior evidence");
    std::filesystem::rename(old_path,journal_path);
    observed.counters->advance(dt);const double tnew=observed.counters->t_current;
    energy_require(rz_runtime_witness::bits(tnew,observed.start.time+dt)&&observed.counters->step_count==observed.start.step+1,
        "PostCurrent did not use the real accepted counter advance");
    observed.context->step_start_time=tnew;observed.context->step_dt=dt;
    observed.context->configure_boundary_context(tnew,boundary::BoundaryPurpose::Hydro);
    observed.runtime->ensure_fluid_ghosts(state::StateSlot::Current);
    observed.stage=std::make_unique<Stage>(*observed.runtime,observed.gravity.get(),Stage::Qualification::NativeRzCandidate);
    rejected=false;try{observed.stage->set_native_self_flux_observation(&native_energy_noop,&other);}catch(const std::logic_error&){rejected=true;}
    energy_require(rejected,"Source-only candidate acquired a private-Hydro observation sink");
    const rz_runtime_witness::FieldsWitness accepted(block);
    const auto saved=driver::HostHydroTransaction::snapshot_owner(*observed.runtime,*observed.context);
    RzMaterializedSourceRecord record;record.capture_call(*observed.stage,[&]{observed.stage->prepare_current(tnew,false);});
    energy_require(record.source_only_checked()&&!record.cleanup_failed()&&record.callback_count()==1,"PostCurrent source callback not authenticated");
    auto post=observed.gravity->native_rz_field_inspection();accepted.matches(block);
    energy_require(driver::HostHydroTransaction::owner_matches(*observed.runtime,*observed.context,saved)
        &&observed.execution->gathers==2&&observed.counters->step_count==2
        &&rz_runtime_witness::bits(observed.counters->t_current,tnew),"Actual postCurrent solve mutated accepted Runtime owners");
    rejected=false;try{observed.gravity->potential();}catch(const std::logic_error&){rejected=true;}
    energy_require(rejected,"PostCurrent candidate acquired public potential capability");
    Owner fault(scheduler::HydroMethod::Euler,false,"energy-axis-fault");int prefix=0;
    auto& fault_block=fault.control.pool->GetBlock(fault.control.tree->GetActiveBlocks().front());
    const rz_runtime_witness::FieldsWitness before(fault_block);
    const auto fault_owner=driver::HostHydroTransaction::snapshot_owner(*fault.runtime,*fault.context);
    fault.stage->set_native_self_flux_observation(&native_energy_fault,&prefix);
    rejected=false;try{fault.advance();}catch(const std::logic_error& e){rejected=std::string(e.what())=="PRIVATE_SELF_ENERGY_AXIS_PREFIX_FAULT";if(!rejected)throw;}
    before.matches(fault_block);
    energy_require(rejected&&prefix==1&&fault.execution->gathers==1
        &&driver::HostHydroTransaction::owner_matches(*fault.runtime,*fault.context,fault_owner)
        &&!fault.runtime->active_host_hydro_transaction()&&!fault.gravity->prepared_native_self()
        &&rz_runtime_witness::bits(fault.counters->t_current,fault.start.time)&&fault.counters->step_count==fault.start.step,
        "Real AxisBefore fault did not restore fields/leases/register/BC/ledger/clock/accounting owners");
    fault.stage->flush_committed_diagnostics();journal_rows(fault,0);
    rejected=false;try{fault.gravity->native_rz_potential();}catch(const std::logic_error&){rejected=true;}
    energy_require(rejected,"Rejected energy prefix retained a usable candidate field");
    witness.publish(post,true); // pending numerical evidence published only after all four fields/rollback
}
/** Two genuine fields for a DISTINCT Green attribution request, not a re-run of
 * the closed passive/fault matrix. Workflow: observed original macro; close its
 * journal; advance accepted clock/Current ghosts; solve actual postCurrent once;
 * fence unchanged accepted owners and reduce ORIGINAL face incidence in memory.
 * Root enforces the new whole-request 240s resource cap externally. This mode
 * does not reset/inherit the previous four-field cost or add a CI owner.
 */
template<class Owner> void run_native_self_green_pair(double dt) {
    using Stage=driver::GravityStage;using Witness=NativeSelfEnergyWitness<Owner>;
    Owner observed(scheduler::HydroMethod::Euler,false,"green-pair-observed");Witness witness(observed,dt);
    observed.stage->set_native_self_flux_observation(&Witness::sink,&witness);
    observed.observer->energy_payload=&witness;observed.observer->energy_field_capture=&Witness::capture;
    observed.observer->energy_delta_capture=&Witness::delta;observed.advance();
    observed.stage->flush_committed_diagnostics();journal_rows(observed,1);
    energy_require(observed.execution->gathers==1&&!observed.gravity->prepared_native_self(),
        "Green pair observed macro field count or live receipt mismatch");
    auto& block=observed.control.pool->GetBlock(observed.control.tree->GetActiveBlocks().front());
    observed.context->hydro_preparation=nullptr;observed.stage.reset();
    const auto old_path=std::filesystem::path(observed.config.io.out_dir)/"native_rz_candidates.tsv";
    const auto journal_path=std::filesystem::path(observed.config.io.out_dir)/"hydro-stages.tsv";
    energy_require(!std::filesystem::exists(journal_path),"Green pair journal target exists; preserve prior evidence");
    std::filesystem::rename(old_path,journal_path);
    observed.counters->advance(dt);const double tnew=observed.counters->t_current;
    energy_require(rz_runtime_witness::bits(tnew,observed.start.time+dt)&&observed.counters->step_count==observed.start.step+1,
        "Green pair postCurrent skipped accepted real counter advance");
    observed.context->step_start_time=tnew;observed.context->step_dt=dt;
    observed.context->configure_boundary_context(tnew,boundary::BoundaryPurpose::Hydro);
    observed.runtime->ensure_fluid_ghosts(state::StateSlot::Current);
    observed.stage=std::make_unique<Stage>(*observed.runtime,observed.gravity.get(),Stage::Qualification::NativeRzCandidate);
    const rz_runtime_witness::FieldsWitness accepted(block);
    const auto saved=driver::HostHydroTransaction::snapshot_owner(*observed.runtime,*observed.context);
    RzMaterializedSourceRecord record;record.capture_call(*observed.stage,[&]{observed.stage->prepare_current(tnew,false);});
    energy_require(record.source_only_checked()&&!record.cleanup_failed()&&record.callback_count()==1,
        "Green pair postCurrent materialized source is not authentic");
    auto post=observed.gravity->native_rz_field_inspection();accepted.matches(block);
    energy_require(driver::HostHydroTransaction::owner_matches(*observed.runtime,*observed.context,saved)
        &&observed.execution->gathers==2&&observed.counters->step_count==observed.start.step+1
        &&rz_runtime_witness::bits(observed.counters->t_current,tnew),
        "Green pair actual postCurrent solve mutated accepted fields/Runtime owners");
    bool refused=false;try{observed.gravity->potential();}catch(const std::logic_error&){refused=true;}
    energy_require(refused,"Green pair diagnostic acquired public potential capability");
    witness.publish_green_pair(post);
}


/** Prospective homology endpoint: one or two REAL Euler macro steps.
 * Workflow per step: new authentic Hydro frame -> close its journal -> advance
 * real accepted clock -> true Current ghosts/EOS -> one postCurrent solve/export
 * -> original owner/ledger and algebra gates -> compact step evidence. A later
 * step receives a NEW Hydro stage, never relabels the Current field as Hydro.
 * Each step consumes two genuine fields. The first 512/1 request has a shared
 * 420s external guard; other campaign resources must be frozen by Root before
 * execution. Complete before-source export remains UNKNOWN: no fake source View
 * or additional solve fills the missing immutable Hydro-source observer API.
 */
template<class Owner,class Input> void run_native_self_homology_pair(Input input) {
    using Stage=driver::GravityStage;using Witness=NativeSelfEnergyWitness<Owner>;
    energy_require(input.enabled&&(input.cells==512||input.cells==2048)&&(input.steps==1||input.steps==2),
        "Invalid frozen homology endpoint request");
    Owner observed(scheduler::HydroMethod::Euler,false,
        "homology-"+std::to_string(input.cells)+"-steps-"+std::to_string(input.steps),input);
    const double dt=observed.step_interval;
    double minimum_hydro_cap=std::numeric_limits<double>::infinity(),
        minimum_gravity_cap=std::numeric_limits<double>::infinity();
    for(int step=0;step<input.steps;++step) {
        const auto destination=observed.config.io.out_dir+"/step-"+std::to_string(step);
        energy_require(!std::filesystem::exists(destination),"Homology step evidence exists; preserve it");
        std::filesystem::create_directories(destination);
        if(step) {
            observed.stage=std::make_unique<Stage>(*observed.runtime,observed.gravity.get(),Stage::Qualification::NativeRzSelfHydroCandidate);
            observed.context->hydro_preparation=observed.stage.get();
            observed.observer->visits.clear(); // A new genuine one-stage macro, not a generation alias.
        }
        // Use the real Native mean-EOS/halo reduction before any Hydro mutation.
        const auto caps=driver::calculate_timestep_candidates(*observed.runtime,observed.workspace,
            *observed.eos,&observed.resolved);
        energy_require(std::isfinite(caps.hydro)&&caps.hydro>0.&&dt<=caps.hydro,
            "Frozen homology dt exceeds actual Hydro CFL cap");
        minimum_hydro_cap=std::min(minimum_hydro_cap,caps.hydro);
        Witness witness(observed,dt);
        observed.stage->set_native_self_flux_observation(&Witness::sink,&witness);
        observed.observer->energy_payload=&witness;observed.observer->energy_field_capture=&Witness::capture;
        observed.observer->energy_delta_capture=&Witness::delta;observed.advance();
        minimum_gravity_cap=std::min(minimum_gravity_cap,witness.gravity_timestep());
        observed.stage->flush_committed_diagnostics();journal_rows(observed,1);
        energy_require(observed.execution->gathers==2*step+1&&!observed.gravity->prepared_native_self(),
            "Homology macro did not use exactly one new authentic field");
        observed.context->hydro_preparation=nullptr;observed.stage.reset();
        std::filesystem::rename(observed.config.io.out_dir+"/native_rz_candidates.tsv",destination+"/hydro-stages.tsv");
        observed.counters->advance(dt);const double tnew=observed.counters->t_current;
        energy_require(rz_runtime_witness::bits(tnew,(step+1)*dt)
            &&observed.counters->step_count==observed.start.step+step+1,"Homology accepted clock missed fixed endpoint fraction");
        observed.context->step_start_time=tnew;observed.context->step_dt=dt;
        observed.context->configure_boundary_context(tnew,boundary::BoundaryPurpose::Hydro);
        observed.runtime->ensure_fluid_ghosts(state::StateSlot::Current);
        observed.stage=std::make_unique<Stage>(*observed.runtime,observed.gravity.get(),Stage::Qualification::NativeRzCandidate);
        const auto active=observed.control.tree->GetActiveBlocks();
        std::vector<rz_runtime_witness::FieldsWitness> accepted;accepted.reserve(active.size());
        for(int id:active)accepted.emplace_back(observed.control.pool->GetBlock(id));
        const auto saved=driver::HostHydroTransaction::snapshot_owner(*observed.runtime,*observed.context);
        RzMaterializedSourceRecord record;record.capture_call(*observed.stage,[&]{observed.stage->prepare_current(tnew,false);});
        energy_require(record.source_only_checked()&&!record.cleanup_failed()&&record.callback_count()==1,
            "Homology postCurrent materialized source not authentic");
        record.capture_native_field(*observed.stage);const auto& post=record.native_field_receipt();
        for(std::size_t b=0;b<active.size();++b)accepted[b].matches(observed.control.pool->GetBlock(active[b]));
        energy_require(driver::HostHydroTransaction::owner_matches(*observed.runtime,*observed.context,saved)
            &&observed.execution->gathers==2*(step+1)&&observed.counters->step_count==observed.start.step+step+1
            &&rz_runtime_witness::bits(observed.counters->t_current,tnew),"Homology postCurrent changed accepted owners");
        bool refused=false;try{observed.gravity->potential();}catch(const std::logic_error&){refused=true;}
        energy_require(refused,"Homology field diagnostic acquired public capability");
        // Both reducers consume the SAME authenticated owning receipt, not a
        // second inspection/download/reduction or a duplicate solution.
        witness.publish(post,false,true,destination);witness.publish_green_pair(post,true,destination);
        std::ofstream source(destination+"/post-materialized-native-source.json");
        energy_require(bool(source),"Cannot open authentic homology source output");source<<record.json()<<'\n';
        source.flush();energy_require(bool(source),"Homology source output failed");
        observed.observer->energy_payload=nullptr;observed.observer->energy_field_capture=nullptr;
        observed.observer->energy_delta_capture=nullptr;
        observed.stage->flush_committed_diagnostics();observed.stage.reset();
        std::filesystem::rename(observed.config.io.out_dir+"/native_rz_candidates.tsv",destination+"/current-stages.tsv");
    }
    energy_require(observed.execution->gathers==2*input.steps
        &&rz_runtime_witness::bits(observed.counters->t_current,observed.endpoint_interval),
        "Homology campaign did not end at the same physical T with real fields");
    std::ofstream inputs(observed.config.io.out_dir+"/homology-inputs.json");
    energy_require(bool(inputs),"Cannot open accepted homology input evidence");
    inputs<<std::setprecision(std::numeric_limits<double>::max_digits10)
        <<"{\"schema\":\"arch-native-homology-inputs-1\",\"physical_qualified\":false,\"total_energy_science\":\"UNVERIFIED\",\"before_source_export\":\"UNKNOWN\",\"post_source_export\":\"actual-accepted-Current\",\"cells\":"<<input.cells
        <<",\"endpoint_steps\":"<<input.steps<<",\"actual_fields\":"<<observed.execution->gathers
        <<",\"resource_seconds\":"<<(input.cells==512&&input.steps==1?"420":"null")
        <<",\"resource_scope\":\"whole-request external guard; additional campaign budget must be frozen before execution\",\"rho\":1,\"G\":"<<arch::constants::gravity::cgs::gravitational_constant
        <<",\"L\":10000,\"t_start\":0,\"t_dyn\":"<<observed.dynamical_time<<",\"T\":"<<observed.endpoint_interval
        <<",\"dt\":"<<dt<<",\"t_end_actual\":"<<observed.counters->t_current<<",\"e_star\":"<<observed.specific_energy
        <<",\"velocity\":\"u_r=-r/t_dyn,u_z=-z/t_dyn,u_phi=0\",\"mean_definition\":\"independent-full-ring-V-antiderivatives\",\"physical_energy_budget\":null,\"rollback_campaign\":\"original separate four-field owner unchanged\",\"timestep_preflight\":{\"status\":\"ACTUAL_HYDRO_AND_PREPARED_GRAVITY_CAPS_CHECKED\",\"CFL\":"<<observed.config.numerics.cfl
        <<",\"minimum_hydro_cap\":"<<minimum_hydro_cap<<",\"minimum_actual_pre_hydro_gravity_cap\":"<<minimum_gravity_cap
        <<",\"extra_preflight_solves\":0,\"public_native_timestep_authority\":false}}\n";
    inputs.flush();energy_require(bool(inputs),"Homology input evidence output failed");
}

} // namespace arch::test
