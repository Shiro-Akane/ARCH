/**
 * @file SelfGravity.cpp
 * @brief Solve and publish one stage's self-gravitational potential and force.
 *
 * Workflow:
 * 1. Bind the active AMR topology and select periodic, Cartesian isolated,
 *    radial symmetric or multidimensional curved isolated Poisson boundaries.
 * 2. Gather the stage density, rebuild current mass moments and boundary
 *    values, then solve A(phi) = -4*pi*G*rho to a checked residual.
 * 3. Derive face gradients, cell acceleration, gravity timestep and face
 *    mass-flux work coefficients before publishing the generation identity.
 * 4. Invalidate the published field whenever density or topology changes.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "physics/gravity/self/SelfGravity.h"

#include "amr/elliptic/EllipticMeshAdapter.h"
#include "grid/GridGeometryView.h"
#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/constant/PhysicalConstants.h"
#include "physics/gravity/GravityBoundary.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/NativeSelfStage.h"
#include "physics/gravity/self/GravityUserBoundary.h"
#include "physics/gravity/self/GravityWorkspace.h"

#include "physics/boundary/UserBoundary.h"

namespace Physical::Gravity {
/** Stack metadata lease for one borrowed materialized-source callback.
 * It records publication metadata only, never a second source/field buffer.
 */
struct SelfGravity::NativeRzSourceInspectionLease {
    const Workspace* workspace;
    const NativeRzSourceInspectionView* view;
    std::uint64_t field_generation;
    GravitySolveIdentity previous_field_source;
    bool consumption_requested,observe_consumption;
    double consumption_dt,workspace_consumption_dt;
};
/** Reject source/frame/workspace drift before an authenticated callback marker.
 * Source update generation is checked separately from existing field metadata;
 * a valid pre-solve source must remain unpublished throughout inspection.
 */
void SelfGravity::require_native_source_inspection(const NativeRzSourceInspectionView& view) const {
    const auto* lease=native_source_inspection_lease_;
    if(!native_source_inspection_running_||native_source_inspection_invalidated_||!lease
        ||lease->view!=&view||work_.get()!=lease->workspace||!work_
        ||work_->scope!=GravityFieldScope::NativeRzCandidate||work_->ready
        ||work_->generation!=lease->field_generation||work_->source!=lease->previous_field_source
        ||&work_->solver.op()!=&view.op||&work_->binding!=&view.binding||!work_->ring_source
        ||&view.service_configuration!=&config_
        ||work_->solver.execution().device()
        ||host_consumption_requested_!=lease->consumption_requested
        ||work_->observe_host_consumption!=lease->observe_consumption
        ||host_consumption_dt_!=lease->consumption_dt
        ||work_->host_consumption_dt!=lease->workspace_consumption_dt
        ||work_->ring_source->materialized_ring_source_generation(view.op,view.request.identity)
            !=view.source_generation)
        throw std::logic_error("Native source inspection lost its original materialization lease");
}
/** Validate gravity boundary kind and convergence controls once. */
SelfGravity::SelfGravity(GravityConfig config):config_(std::move(config)) {
    const bool known=config_.boundary=="periodic" || config_.boundary=="isolated"
        || config_.boundary=="dirichlet" || config_.boundary=="neumann" || config_.boundary=="user";
    if (!known
        || !std::isfinite(config_.relative_tolerance) || config_.relative_tolerance<=0. || config_.relative_tolerance>=1.
        || !std::isfinite(config_.absolute_tolerance) || config_.absolute_tolerance<0. || config_.max_cycles<1)
        throw std::invalid_argument("Invalid self-gravity physical or convergence controls");
    if (config_.boundary=="user") {
        // Capture the immutable selection once; the copied callback and the
        // borrowed config/species stay live for every later run.
        const auto* selection=arch::boundary::CurrentUserBoundaries();
        if (!selection || !selection->callbacks.gravity || !selection->config || !selection->species)
            throw std::invalid_argument("User self-gravity boundary requires an active resolved gravity callback");
        user_callback_=selection->callbacks.gravity;
        user_config_=selection->config;
        user_species_=selection->species;
    }
}
/** Destroy the topology-bound resident workspace after its execution lease. */
SelfGravity::~SelfGravity()=default;
/** Return a bound workspace or fail before accessing unpublished fields. */
SelfGravity::Workspace& SelfGravity::workspace() const {
    if (!work_) throw std::logic_error("Self-gravity mesh is not bound");
    return *work_;
}
/** Bind one AMR topology epoch and establish native active-cell order. */
void SelfGravity::bind(amr::EllipticMeshBinding binding,double time) const {
    bind_impl(std::move(binding),false,0,0,time);
}
/** Bind an internal numerical candidate, never a production RZ grant.
 * A zero work cap requests actual-topology resource derivation only; explicit
 * nonzero caps and all physical/integration accuracy requirements are retained.
 */
void SelfGravity::bind_native_rz_candidate(amr::EllipticMeshBinding binding,
    std::uint64_t maximum_boxes,std::uint64_t maximum_work) const {
    bind_impl(std::move(binding),true,maximum_boxes,maximum_work,0.);
}
void SelfGravity::bind_impl(amr::EllipticMeshBinding binding,bool native_candidate,
    std::uint64_t maximum_boxes,std::uint64_t maximum_work,double time) const {
    if(prepared_native_self()) {
        prepared_native_self()->invalidate();
        throw std::logic_error("Native private self stage forbids synchronous mesh binding");
    }
    if(native_source_inspection_running_)
        throw std::logic_error("Native source inspection forbids synchronous mesh rebinding");
    invalidate();
    if (binding.grids.empty() || binding.grids.size()!=binding.handles.size()
        || binding.cells.size()!=binding.storage.size()) throw std::invalid_argument("Invalid gravity mesh binding");
    // Source/measure binding is distinct from the full-ring boundary,
    // native force/work and runtime publication consumer. Do not enter the
    // legacy EvaluateBoundary workspace before that full RZ path is accepted.
    if(binding.base.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz&&!native_candidate)
        throw std::logic_error("RZ self-gravity finite-ring runtime consumer is not qualified");
    if(native_candidate && (binding.base.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||binding.base.geometry!=arch::elliptic::Geometry::Cylindrical||binding.base.dimension!=2
        ||config_.boundary!="isolated"||!maximum_boxes||maximum_boxes>65536
        ||(execution_&&execution_->numeric()->device())))
        throw std::invalid_argument("Invalid or unsupported internal native RZ verification binding");
    std::size_t cell=0;
    for (std::size_t b=0;b<binding.grids.size();++b) {
        if (!binding.grids[b] || !amr::is_valid(binding.handles[b])
            || binding.handles[b].epoch!=binding.handles.front().epoch)
            throw std::invalid_argument("Invalid gravity patch binding");
        const auto& grid=*binding.grids[b];
        for (int k=grid.Ks();k<grid.Ke();++k) for (int j=grid.Js();j<grid.Je();++j) for (int i=grid.Is();i<grid.Ie();++i,++cell)
            if (cell>=binding.storage.size() || binding.storage[cell].block!=b
                || binding.storage[cell].offset!=grid.GetIndex(i,j,k))
                throw std::invalid_argument("Gravity cells do not match native active storage order");
    }
    if (cell!=binding.cells.size()) throw std::invalid_argument("Extra cells in gravity mesh binding");
    const bool user_boundary=config_.boundary=="user";
    const bool explicit_policy=config_.boundary=="dirichlet" || config_.boundary=="neumann" || user_boundary;
    if(binding.base.geometry!=arch::elliptic::Geometry::Cartesian &&
        config_.boundary!="isolated" && !explicit_policy)
        throw std::invalid_argument("Curvilinear self-gravity requires an isolated or explicit field boundary");
    auto kind=binding.base.geometry!=arch::elliptic::Geometry::Cartesian
        ? (binding.base.dimension==1 ? arch::elliptic::BoundaryKind::RadialIsolated
                                     : arch::elliptic::BoundaryKind::CurvilinearIsolated)
        : (config_.boundary=="periodic" ? arch::elliptic::BoundaryKind::Periodic
                                        : arch::elliptic::BoundaryKind::Dirichlet);
    arch::elliptic::CompositeBoundary boundary;
    if(explicit_policy) {
        kind=arch::elliptic::BoundaryKind::User;
        boundary=user_boundary
            ? gravity_user_boundary(user_callback_,*user_config_,*user_species_,binding.base,binding.periodic,time)
            : gravity_homogeneous_boundary(binding.base,binding.periodic,
                config_.boundary=="neumann" ? arch::elliptic::FaceBoundaryKind::Neumann
                                            : arch::elliptic::FaceBoundaryKind::Dirichlet);
    }
    // Periodic pairs owned by an explicit policy are a physical topology
    // property; reject any that disagrees with the AMR binding. Legacy
    // periodic/isolated gravity kinds keep their established behavior.
    if(explicit_policy) for(int axis=0;axis<binding.base.dimension;++axis)
        if((boundary.sides[2*axis]==arch::elliptic::FaceBoundaryKind::Periodic)!=binding.periodic[axis])
            throw std::invalid_argument("Self-gravity periodicity disagrees with the AMR topology");
    work_=std::make_unique<Workspace>(std::move(binding),kind,std::move(boundary),
        execution_?execution_:(execution_=make_host_gravity_execution()));
    work_->boundary_time=time;
    if(native_candidate) {
        work_->scope=GravityFieldScope::NativeRzCandidate;
        work_->ring_limits.maximum_boxes_per_leaf=maximum_boxes;
        work_->ring_source=std::make_unique<GravityBoundary>(
            work_->solver.op(),work_->binding.handles.front().epoch);
        // Derive only for an actual ring consumer. Ordinary explicit-policy
        // bindings never enter ring validation or acquire unrelated constraints.
        work_->ring_limits.maximum_leaf_evaluations=maximum_work ? maximum_work
            : work_->ring_source->full_ring_traversal_work_bound(work_->solver.op());
    }
}
/** Retire a prior gravity publication whenever its density lease changes. */
void SelfGravity::invalidate() const noexcept {
    if(prepared_native_self())prepared_native_self()->invalidate();
    if(native_source_inspection_running_)native_source_inspection_invalidated_=true;
    if(work_) { work_->ready=false; work_->downloaded=false; work_->validity.invalidate(); } }
/** Resolve the explicit per-side policy for the requested stage time. */
arch::elliptic::CompositeBoundary SelfGravity::current_boundary(const Workspace& w,double time) const {
    if(config_.boundary=="user")
        return gravity_user_boundary(user_callback_,*user_config_,*user_species_,w.binding.base,
            w.binding.periodic,time);
    return gravity_homogeneous_boundary(w.binding.base,w.binding.periodic,
        config_.boundary=="neumann" ? arch::elliptic::FaceBoundaryKind::Neumann
                                    : arch::elliptic::FaceBoundaryKind::Dirichlet);
}
/** Rebuild the topology-bound operator after a side structure/a/b change. */
void SelfGravity::rebuild_boundary(arch::elliptic::CompositeBoundary boundary) const {
    auto& bound=*work_;
    auto binding=std::move(bound.binding);
    auto runner=bound.execution;
    // The publication counter and boundary time belong to the topology epoch,
    // not to one operator instance; a rebuild must not restart either.
    const std::uint64_t generation=bound.generation;
    const double time=bound.boundary_time;
    bound.solver.execution().fence();
    work_=std::make_unique<Workspace>(std::move(binding),arch::elliptic::BoundaryKind::User,
        std::move(boundary),std::move(runner));
    work_->generation=generation;
    work_->boundary_time=time;
}
/** Restart and uninterrupted runs must start each macro-step solve identically. */
void SelfGravity::clear_solver_initial_guess() const noexcept {
    if(prepared_native_self()){prepared_native_self()->invalidate();invalidate();return;}
    if(native_source_inspection_running_){native_source_inspection_invalidated_=true;return;}
    if(work_) work_->solver.clear_initial_guess();
}
/** Gather current density, solve A phi = -4 pi G rho_source, and publish force. */
arch::state::CompletionToken SelfGravity::prepare(const GravitySolveRequest& request) const {
    if(prepared_native_self()) {
        prepared_native_self()->invalidate();
        throw std::logic_error("Native private self stage forbids synchronous solve reentry");
    }
    if(native_source_inspection_running_)
        throw std::logic_error("Native source inspection forbids synchronous solve reentry");
    invalidate();
    // Validate the same domain dependency contract used by source caches and
    // publication before gather, moments, solve or device work can begin.
    validate_gravity_solve_identity(request.identity);
    if (!work_) throw std::logic_error("Self-gravity mesh is not bound");
    const auto& identity=request.identity;
    {
        auto& bound=*work_; const auto& op=bound.solver.op();
        if (request.blocks.size()!=bound.patches.size() || identity.inputs.size()!=bound.patches.size()
            || identity.topology!=bound.binding.handles.front().epoch || identity.gravitational_constant!=arch::constants::gravity::cgs::gravitational_constant
            || identity.operator_revision!=1 || identity.boundary_revision!=1 || identity.accuracy_revision!=1)
            throw std::logic_error("Self-gravity solve identity differs from bound mesh/configuration");
        for (std::size_t b=0;b<bound.patches.size();++b) {
            const auto& view=request.blocks[b];
            if (view.identity!=identity.inputs[b] || view.identity.block!=bound.binding.handles[b]
                || view.density.memory!=(bound.solver.execution().device()?arch::grid::FieldMemory::Device:arch::grid::FieldMemory::Host) || !view.density.data
                || view.density.layout!=amr::native_scalar_layout(*bound.binding.grids[b])
                || view.density.storage_generation!=view.identity.storage_generation
                || view.density.size!=static_cast<std::size_t>(bound.binding.grids[b]->GetTotalSize()))
                throw std::logic_error("Self-gravity density view does not match its dependency");
        }
    }
    // A datum-only change keeps the multigrid levels; a changed side structure
    // rebuilds the operator on the same AMR binding and execution.
    if(work_->explicit_boundary) {
        auto next=current_boundary(*work_,identity.input_time);
        if(!gravity_same_structure(work_->user_boundary,next)) rebuild_boundary(std::move(next));
        work_->boundary_time=identity.input_time;
    }
    auto& w=*work_; const auto& op=w.solver.op();
    using Clock=std::chrono::steady_clock;
    const auto started=Clock::now();
    auto& e=w.solver.execution();std::vector<const double*> pointers;
    for(const auto& view:request.blocks)pointers.push_back(view.density.data);
    e.copy(w.density_pointers.data,pointers.data(),sizeof(double*)*pointers.size(),arch::multigrid::Transfer::Upload);
    w.execution->run(GatherDensity{op.size(),w.cells.data,w.density_pointers.data,w.density.data});
    w.max_density=e.maximum(w.density);
    if(!std::isfinite(w.max_density))throw std::invalid_argument("Self gravity requires finite positive active density");
    w.mean=w.solver.mean(w.density);
    // A=-Laplacian: A*Phi=-4*pi*G*(rho-<rho>) only for a wholly periodic
    // gravity; every flux boundary keeps rho and relies on the shared
    // compatibility check instead of manufacturing a zero-mean source.
    const double factor=-4.*arch::constants::math::pi*arch::constants::gravity::cgs::gravitational_constant;
    if(op.periodic_boundary())e.difference_scale(w.rhs,w.density,w.mean,factor);
    else e.linear(w.rhs,factor,w.density);
    std::vector<double> native_source;
    if(w.explicit_boundary){
        // Evaluate the position/time datum c at the actual physical face
        // centers and scatter only O(surface) values onto the shared plan.
        // Homogeneous dirichlet/neumann sides keep the zero datum.
        for(std::size_t i=0;config_.boundary=="user" && i<w.boundary_faces.size();++i) {
            const auto& face=op.faces()[w.boundary_faces[i]];
            const int index=face.boundary_side;
            const auto& condition=w.user_boundary.conditions[index];
            if(condition.kind==arch::elliptic::FaceBoundaryKind::Periodic) {w.boundary_host_values[i]=0.;continue;}
            const auto data=gravity_user_sample(user_callback_,*user_config_,*user_species_,w.binding.base,
                index,w.boundary_native[i],identity.input_time);
            const auto kind=condition.kind==arch::elliptic::FaceBoundaryKind::Dirichlet
                ?arch::boundary::GravityBoundaryCondition::Dirichlet
                :(condition.kind==arch::elliptic::FaceBoundaryKind::Neumann
                    ?arch::boundary::GravityBoundaryCondition::Neumann
                    :arch::boundary::GravityBoundaryCondition::Robin);
            if(data.kind!=kind || data.a!=condition.a || data.b!=condition.b)
                throw std::invalid_argument("User gravity side structure changes within a stage");
            // A nonfinite datum must fail here, before it can be uploaded into
            // the boundary values that feed the solve and the force rows.
            if(!std::isfinite(data.c))
                throw std::invalid_argument("User gravity boundary datum is not finite");
            w.boundary_host_values[i]=data.c;
        }
        if(!w.boundary_faces.empty()) {
            e.copy(w.boundary_face_values.data,w.boundary_host_values.data(),
                sizeof(double)*w.boundary_host_values.size(),arch::multigrid::Transfer::Upload);
            w.execution->run(ScatterBoundary{static_cast<int>(w.boundary_faces.size()),
                w.boundary_face_index.data,w.boundary_face_values.data,w.boundary_values.data});
        }
        w.solver.boundary_rhs(w.rhs,w.boundary_values);
    } else if(w.ring_source) {
        e.fence();
        native_source=e.download(w.rhs);
        const auto density=e.download(w.density);
        w.ring_source->update(density,identity);
        if(native_source_inspection_sink_) {
            // Borrow the already downloaded actual gathered rho. No additional
            // gather/download/cache occurs and ordinary no-sink work is unchanged.
            const NativeRzSourceInspectionView view{op,w.binding,density,request,config_,
                w.ring_source->materialized_ring_source_generation(op,identity)};
            const NativeRzSourceInspectionLease lease{&w,&view,w.generation,w.source,
                host_consumption_requested_,w.observe_host_consumption,host_consumption_dt_,w.host_consumption_dt};
            struct RestoreInspection {
                bool& running;const NativeRzSourceInspectionLease*& lease;
                ~RestoreInspection(){running=false;lease=nullptr;}
            } restore{native_source_inspection_running_,native_source_inspection_lease_};
            native_source_inspection_invalidated_=false;
            native_source_inspection_running_=true;native_source_inspection_lease_=&lease;
            try {
                require_native_source_inspection(view);
                native_source_inspection_sink_(native_source_inspection_payload_,view);
                require_native_source_inspection(view);
            } catch(...) {
                invalidate(); // Rejection never leaves a physical field publication.
                throw;
            }
        }
        const auto proposal=w.ring_source->propose_ring_budget(op,identity,native_source,
            config_.relative_tolerance,config_.absolute_tolerance);
        if(proposal.status!=RingBudgetStatus::Proposed&&proposal.status!=RingBudgetStatus::ZeroBudget)
            throw std::runtime_error("Native RZ initial budget unavailable");
        auto control=proposal.control;
        control.maximum_boxes_per_leaf=w.ring_limits.maximum_boxes_per_leaf;
        control.maximum_leaf_evaluations=w.ring_limits.maximum_leaf_evaluations;
        w.execution->run(EvaluateRingBoundary{w.ring_source.get(),&op,&identity,&control,&w.ring});
        if(w.ring.status!=RingBoundaryStatus::Bounded) {
            std::ostringstream message;message<<"Native RZ ring boundary failed: status="
                <<int(w.ring.status)<<" target="<<std::setprecision(17)<<control.face_absolute_target
                <<" leaf="<<w.ring.leaf_evaluations<<" parent="<<w.ring.parent_evaluations;
            throw std::runtime_error(message.str());
        }
        e.copy(w.boundary_values.data,w.ring.values.data(),sizeof(double)*w.ring.values.size(),
            arch::multigrid::Transfer::Upload);
        w.solver.boundary_rhs(w.rhs,w.boundary_values);
    } else if(w.nodes.size){
        for(auto it=w.layers.rbegin();it!=w.layers.rend();++it)
            w.execution->run(UpdateMoments{it->size,it->data,w.nodes.data,w.moments.data,w.density.data,w.volumes.data});
        w.execution->run(EvaluateBoundary{w.points.size,w.points.data,w.nodes.data,w.moments.data,w.nodes.size,op.base().dimension,
            op.base().geometry==arch::elliptic::Geometry::Cartesian ? GridMetrics::Geometry::Cartesian
                : (op.base().geometry==arch::elliptic::Geometry::Cylindrical
                    ? GridMetrics::Geometry::Cylindrical:GridMetrics::Geometry::Spherical),
            arch::constants::gravity::cgs::gravitational_constant,op.base().origin[0]+op.base().cells[0]*op.base().spacing[0],w.boundary_values.data,op.base().semantics});
        w.solver.boundary_rhs(w.rhs,w.boundary_values);
    } else if(op.boundary_kind()==arch::elliptic::BoundaryKind::RadialIsolated) {
        // Spherical free-space outer value: Phi(R)=-G*M/R,
        // M=4*pi*sum_i rho_i*integral_i(r^2 dr).
        // Cylindrical Phi(R)=0 fixes the logarithmic potential gauge.
        e.fill(w.boundary_values);
        if(op.base().geometry==arch::elliptic::Geometry::Spherical) {
            const double unit_mass=e.reduce({w.density.data,nullptr,w.volumes.data,
                op.size(),arch::multigrid::ReductionKind::Product});
            const double outer=op.base().origin[0]+op.base().cells[0]*op.base().spacing[0];
            const double value=-4.*arch::constants::math::pi*arch::constants::gravity::cgs::gravitational_constant*unit_mass/outer;
            for(std::size_t f=0;f<op.faces().size();++f)
                if(op.faces()[f].boundary_side==1)
                    e.copy(w.boundary_values.data+f,&value,sizeof(value),arch::multigrid::Transfer::Upload);
        }
        w.solver.boundary_rhs(w.rhs,w.boundary_values);
    }
    // The periodic constant mode is projected only for a wholly periodic
    // operator; a pure/mixed flux boundary keeps its raw source so the shared
    // Gauss-law compatibility check can reject an unbalanced mass.
    if(op.periodic_boundary()) w.solver.project(w.rhs);
    e.fence();
    const auto source_ready=Clock::now();
    const double algebra_fraction=w.ring_source?.5:1.;
    const double algebra_rtol=config_.relative_tolerance*algebra_fraction;
    if(!(algebra_rtol>0.))throw std::runtime_error("Native algebra tolerance is not representable");
    w.report=w.solver.solve(w.rhs,{algebra_rtol,config_.absolute_tolerance*algebra_fraction,config_.max_cycles});
    if(w.report.status!=arch::multigrid::SolveStatus::Converged){
        std::ostringstream message;message<<std::setprecision(17)<<"Self-gravity Poisson solve failed: iterations="<<w.report.cycles
            <<" residual="<<w.report.residual<<" target="<<w.report.target<<" rhs="<<w.report.rhs_rms;throw std::runtime_error(message.str());}
    e.fence();
    if(w.ring_source) {
        const auto rhs=e.download(w.rhs);
        const auto first_report=w.report;int used_cycles=w.report.cycles;
        double previous_work_target=w.report.target;bool zero_cycle_refinement=false;
        // Workflow: assess the original request with ALL certified errors. A
        // fixed half-tolerance Krylov stop is only an initial work allocation;
        // construction/integration/evaluation errors can consume more than
        // half of the actual original budget. Refine the SAME resident source,
        // boundary and potential only if the complete certificate leaves a
        // positive margin. All solves share the user's original cycle budget.
        // No additional gather, boundary approximation or field publication is
        // performed here; every iterate must pass the original full proof.
        for(int refinements=0;;++refinements) {
            const auto phi=e.download(w.solver.resident_potential());
            std::vector<double> residual(op.size());op.apply(phi,residual);
            for(int i=0;i<op.size();++i)residual[i]-=rhs[i];
            w.ring_assessment=w.ring_source->assess_native_ring_rhs(op,w.ring,native_source,
                rhs,phi,residual,config_.relative_tolerance,config_.absolute_tolerance);
            const auto& assessment=w.ring_assessment.conditional;
            if(assessment.status!=arch::elliptic::BoundaryResidualStatus::ResidualTooLarge
                ||used_cycles>=config_.max_cycles||refinements>=config_.max_cycles
                ||zero_cycle_refinement)break;
            const double margin=std::nextafter(assessment.tolerance_safe-
                assessment.complete_residual_error_upper,0.);
            if(!(margin>0.)||!std::isfinite(margin))break;
            // For the SAME residual vector, ||r||native <= R*||r||stored,
            // R=sqrt(max_i(w_native_upper_i/w_stored_i)). This guides work,
            // never replaces the independent native face-operator assessment
            // (the provider's assembled sparse arithmetic can differ).
            const auto measure=op.native_rz_measure_enclosure();
            if(measure.status!=arch::elliptic::BoundaryErrorStatus::Bounded)break;
            double ratio_squared=1.;
            for(std::size_t i=0;i<measure.weight_upper.size();++i) {
                const double stored=op.norm_weights()[i];
                if(!(stored>0.)||!std::isfinite(stored))
                    throw std::runtime_error("Native refinement has invalid stored norm weight");
                ratio_squared=std::max(ratio_squared,std::nextafter(measure.weight_upper[i]/stored,
                    std::numeric_limits<double>::infinity()));
            }
            const double ratio=std::nextafter(std::sqrt(ratio_squared),
                std::numeric_limits<double>::infinity());
            const double work_target=std::nextafter(std::min(.5*margin/ratio,.5*previous_work_target),0.);
            if(!(work_target>0.)||!std::isfinite(work_target))break;
            const int remaining=config_.max_cycles-used_cycles;
            const auto refined=w.solver.solve(w.rhs,{0.,work_target,remaining});
            if(refined.cycles<0||refined.cycles>remaining)
                throw std::logic_error("Native refinement exceeded the original shared cycle budget");
            used_cycles+=refined.cycles;w.report=refined;w.report.cycles=used_cycles;
            w.report.initial_residual=first_report.initial_residual;
            previous_work_target=work_target;zero_cycle_refinement=refined.cycles==0;e.fence();
            if(refined.status!=arch::multigrid::SolveStatus::Converged)
                throw std::runtime_error("Native refinement did not converge within the original cycle budget");
            // A zero-cycle provider acceptance is still assessed once. If it
            // misses the actual native request, reject the unchanged iterate
            // without another provider call. No counter is reset or padded.
        }
        if(w.ring_assessment.conditional.status!=arch::elliptic::BoundaryResidualStatus::Accepted) {
            std::ostringstream message;message<<std::setprecision(17)
                <<"Native RZ original request rejected: status="<<int(w.ring_assessment.conditional.status)
                <<" total="<<w.ring_assessment.conditional.total_residual_upper
                <<" safe="<<w.ring_assessment.conditional.tolerance_safe
                <<" algebra="<<w.report.residual<<" algebra_target="<<w.report.target
                <<" rhs_error="<<w.ring_assessment.conditional.rhs_error_upper
                <<" residual_upper="<<w.ring_assessment.conditional.residual_norm_upper
                <<" source_error="<<w.ring_assessment.source_error.norm_upper
                <<" construction_error="<<w.ring_assessment.native_boundary_construction.native_norm_upper
                <<" boundary_error="<<w.ring_assessment.native_boundary_potential.native_norm_upper
                <<" assembly_error="<<w.ring_assessment.assembly_error.norm_upper
                <<" evaluation_error="<<w.ring_assessment.native_residual_error.native_norm_upper
                <<" complete_error="<<w.ring_assessment.native_complete_residual_error.native_norm_upper;
            throw std::runtime_error(message.str());
        }
    }
    const auto poisson_ready=Clock::now();
    w.solver.gradient(w.solver.resident_potential(),w.face_gradient,w.boundary_values);
    e.run(arch::multigrid::RowsWork{w.sides.size,w.side_gather.view(),w.face_gradient.data,w.sides.data});
    e.run(arch::multigrid::RowsWork{w.patch_faces.size,w.patch_gather.view(),w.sides.data,w.patch_faces.data});
    if(op.base().geometry!=arch::elliptic::Geometry::Cartesian) {
        e.run(arch::multigrid::RowsWork{w.work_sides.size,w.work_phi_gather.view(),
            w.solver.resident_potential().data,w.work_sides.data});
        e.run(arch::multigrid::RowsWork{w.work_sides.size,w.work_boundary_gather.view(),
            w.boundary_values.data,w.work_sides.data,1.,1.});
        e.run(arch::multigrid::RowsWork{w.patch_work_faces.size,w.patch_gather.view(),w.work_sides.data,w.patch_work_faces.data});
    }
    w.execution->run(CellAcceleration{op.size(),op.base().dimension,w.cells.data,w.sides.data,w.g.data,w.inverse_dt_squared.data});
    w.max_acceleration_ratio=e.maximum(w.inverse_dt_squared);
    if(!std::isfinite(w.max_acceleration_ratio))throw std::runtime_error("Nonfinite self-gravity force");
    e.fence();
    w.timings={std::chrono::duration<double>(source_ready-started).count(),
        std::chrono::duration<double>(poisson_ready-source_ready).count(),
        std::chrono::duration<double>(Clock::now()-poisson_ready).count()};
    for(std::size_t b=0;b<w.patches.size();++b)w.patches[b].density=pointers[b];
    if (++w.generation==0) throw std::overflow_error("Self-gravity publication counter exhausted");
    w.source=identity;
    arch::state::CompletionToken token{w.generation,arch::state::CompletionState::Complete};
    if(host_consumption_requested_)w.begin_host_consumption(host_consumption_dt_);
    w.validity.publish({identity,w.generation,token,w.scope}); w.ready=true; return token;
}
/** Internal snapshots explicitly require candidate scope. They cannot satisfy
 * ordinary physical patch/output readers or claim continuous Phi/force quality.
 */
const RingRhsAssessment& SelfGravity::native_rz_assessment() const {
    workspace().require(GravityFieldScope::NativeRzCandidate);return work_->ring_assessment;
}
const std::vector<double>& SelfGravity::native_rz_potential() const {
    workspace().download(GravityFieldScope::NativeRzCandidate);return work_->host_phi;
}
const std::array<std::vector<double>,3>& SelfGravity::native_rz_acceleration() const {
    workspace().download(GravityFieldScope::NativeRzCandidate);return work_->host_g;
}
/** Copy only the actual completed Host candidate and its original metadata.
 * Workflow: validate NativeRzCandidate publication/source -> fence -> download
 * existing Phi, gradient, boundary, sides and g arrays -> check representation
 * -> revalidate the same workspace/source/generations before returning owners.
 * g is the existing arithmetic average of actual gathered low/high side g;
 * no gradient, gather, quadrature, normalization or new physical field is made.
 * Prepare/bind are serial by contract; this synchronous read adds no callback
 * and grants no concurrent mutation safety or production physical qualification.
 */
NativeRzFieldInspection SelfGravity::native_rz_field_inspection() const {
    auto& w=workspace();
    w.require(GravityFieldScope::NativeRzCandidate);
    auto& execution=w.solver.execution();
    const auto& op=w.solver.op();
    if(execution.device()||!w.execution||w.execution->numeric()->device())
        throw std::logic_error("Native RZ field inspection requires Host execution");
    if(w.scope!=GravityFieldScope::NativeRzCandidate||!w.ring_source
        ||w.ring_assessment.source!=w.source||!w.ring_assessment.source_generation
        ||w.ring_source->materialized_ring_source_generation(op,w.source)
            !=w.ring_assessment.source_generation)
        throw std::logic_error("Native RZ field inspection source differs from its assessment");
    const int n=op.size();
    const auto m=op.faces().size();
    if(n<=0||n>std::numeric_limits<int>::max()/6
        ||m>static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::logic_error("Native RZ field inspection has invalid array extents");
    const auto valid_array=[](const arch::multigrid::Vector& values,int extent) {
        return values.size==extent&&values.data!=nullptr;
    };
    if(!valid_array(w.solver.resident_potential(),n)
        ||!valid_array(w.face_gradient,static_cast<int>(m))
        ||!valid_array(w.boundary_values,static_cast<int>(m))
        ||!valid_array(w.sides,6*n)||!valid_array(w.g,3*n))
        throw std::logic_error("Native RZ field inspection arrays differ from the operator");
    NativeRzFieldInspection result;
    result.source=w.source;
    result.source_generation=w.ring_assessment.source_generation;
    result.field_generation=w.generation;
    // The actual metadata is copied, not reconstructed from cell centers.
    result.faces=op.faces();
    execution.fence();
    result.potential=execution.download(w.solver.resident_potential());
    result.face_gradient=execution.download(w.face_gradient);
    result.boundary_values=execution.download(w.boundary_values);
    result.side_acceleration=execution.download(w.sides);
    const auto acceleration=execution.download(w.g);
    const auto finite_array=[](const std::vector<double>& values,std::size_t extent) {
        return values.size()==extent&&std::all_of(values.begin(),values.end(),
            [](double value){return std::isfinite(value);});
    };
    if(!finite_array(result.potential,static_cast<std::size_t>(n))
        ||!finite_array(result.face_gradient,m)||!finite_array(result.boundary_values,m)
        ||!finite_array(result.side_acceleration,static_cast<std::size_t>(6*n))
        ||!finite_array(acceleration,static_cast<std::size_t>(3*n)))
        throw std::runtime_error("Native RZ field inspection contains nonfinite or incomplete arrays");
    for(int axis=0;axis<3;++axis)
        result.acceleration[axis].assign(acceleration.begin()+axis*n,
            acceleration.begin()+(axis+1)*n);
    // Copies own storage. Recheck the original serial publication rather than
    // assuming the ring-source counter is the solved-field counter.
    execution.fence();
    w.require(GravityFieldScope::NativeRzCandidate);
    if(work_.get()!=&w||w.source!=result.source||w.generation!=result.field_generation
        ||w.ring_assessment.source!=result.source
        ||w.ring_assessment.source_generation!=result.source_generation
        ||w.ring_source->materialized_ring_source_generation(op,result.source)
            !=result.source_generation)
        throw std::logic_error("Native RZ field inspection changed during its synchronous copy");
    return result;
}
/** Switch host/device execution and rebuild resident arrays on the same topology. */
void SelfGravity::set_execution(std::shared_ptr<GravityExecution> execution) const {
    if(prepared_native_self()) {
        prepared_native_self()->invalidate();
        throw std::logic_error("Native private self stage forbids synchronous execution replacement");
    }
    if(native_source_inspection_running_)
        throw std::logic_error("Native source inspection forbids synchronous execution replacement");
    if(!execution||execution_==execution)return;
    invalidate();if(work_)work_->solver.execution().fence();execution_=std::move(execution);
    if(work_){
        auto binding=std::move(work_->binding);
        const double time=work_->boundary_time;
        const std::uint64_t generation=work_->generation;
        work_.reset();bind(std::move(binding),time);
        work_->generation=generation;
    }
}
/** Return one published patch face field for hydro source application. */
GravityPatchView SelfGravity::patch_view(std::size_t block) const {workspace().require();return work_->patches.at(block);}
/** Return active composite leaf count for diagnostics. */
std::size_t SelfGravity::cell_count() const {return workspace().solver.op().size();}
/** Bound a macro step by local density and face-acceleration timescales. */
double SelfGravity::timestep(double cfl) const {
    workspace().require();const auto& w=*work_;
    if(!std::isfinite(cfl)||cfl<=0.||cfl>1.)throw std::invalid_argument("Invalid gravity CFL");
    // dt_g = CFL / sqrt(max(4*pi*G*rho_max, max_a |g_a|/dx_a)).
    return cfl/std::sqrt(std::max(4.*arch::constants::math::pi*arch::constants::gravity::cgs::gravitational_constant*w.max_density,w.max_acceleration_ratio));
}
/** Download the accepted potential only when requested by output. */
const std::vector<double>& SelfGravity::potential() const {workspace().download();return work_->host_phi;}
/** Download accepted acceleration only when requested by output. */
const std::array<std::vector<double>,3>& SelfGravity::acceleration() const {workspace().download();return work_->host_g;}
/** Expose the accepted Poisson residual and iteration count. */
const arch::multigrid::SolveReport& SelfGravity::report() const {workspace().require();return work_->report;}
/** Expose the current volume-weighted density mean. */
double SelfGravity::density_mean() const {workspace().require();return work_->mean;}
/** Expose physical source, Poisson and force timings. */
const SelfGravity::Timings& SelfGravity::timings() const {workspace().require();return work_->timings;}
/** Open only the receipt observer; preparation still validates and solves the original request. */
void SelfGravity::begin_host_stage_consumption(double step_dt) const {
    if(prepared_native_self()) {
        prepared_native_self()->invalidate();
        throw std::logic_error("Native private self stage forbids legacy Host receipt attachment");
    }
    if(native_source_inspection_running_)
        throw std::logic_error("Native source inspection forbids synchronous consumption attachment");
    if(host_consumption_requested_)
        throw std::logic_error("Gravity Host stage receipt is already active");
    if((execution_&&execution_->numeric()->device())
        ||(work_&&work_->solver.execution().device()))
        throw std::logic_error("Host gravity receipt cannot observe Device consumers");
    if(!std::isfinite(step_dt)||!(step_dt>0.))
        throw std::invalid_argument("Gravity Host receipt requires a finite positive interval");
    host_consumption_dt_=step_dt;
    host_consumption_requested_=true;
}
/** A published field alone is insufficient: require the real force and flux-work calls. */
void SelfGravity::require_host_stage_consumption(const GravitySolveIdentity& expected) const {
    if(!host_consumption_requested_)
        throw std::logic_error("Gravity Host stage receipt is not active");
    workspace().require_host_consumption(expected);
}
/** Close the optional observer; next-stage publication remains explicitly invalidated by its owner. */
void SelfGravity::end_host_stage_consumption() const noexcept {
    // Retiring old observer metadata is noexcept, but it must not authorize a
    // second receipt system or leave a live private stage after such reentry.
    if(prepared_native_self())prepared_native_self()->invalidate();
    if(native_source_inspection_running_){native_source_inspection_invalidated_=true;return;}
    host_consumption_requested_=false;
    if(work_)work_->observe_host_consumption=false;
}
namespace {
/** Apply the original shared face momentum leaf to the actual interior.
 * Delta m_a=dt*rho*(g_low+g_high)/2; angular acceleration remains zero.
 * Caller authenticates the scope, extent, original input and field lifetime.
 */
void apply_patch_momentum(const GravityPatchView& patch,std::vector<FluidVector>& delta,
    const FluidState& state,const Grid& grid,double dt) {
    const int stride[]{1,grid.stride_y,grid.stride_z};
    for (int k=grid.Ks();k<grid.Ke();++k) for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
        const int c=grid.GetIndex(i,j,k); double* momentum[]{&delta[c].mom_u,&delta[c].mom_v,&delta[c].mom_w};
        for(int a=0;a<grid.dim;++a) *momentum[a]+=gravity_momentum(patch.faces[a][c],patch.faces[a][c+stride[a]],state.rho[c],dt);
    }
}
/** Apply original compatible work without changing dt, metric or face values.
 * Delta E=dt/2*(F_rho,low*w_low+F_rho,high*w_high).
 */
void apply_patch_flux_work(const GravityPatchView& patch,std::vector<FluidVector>& delta,
    const std::vector<FluidVector>& flux,const Grid& grid,double dt,int axis) {
    const int stride=axis==0?1:axis==1?grid.stride_y:grid.stride_z;
    for (int k=grid.Ks();k<grid.Ke();++k) for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
        const int c=grid.GetIndex(i,j,k);
        delta[c].eng+=gravity_flux_work(patch.work_faces[axis][c],patch.work_faces[axis][c+stride],flux[c].rho,flux[c+stride].rho,dt);
    }
}
} // namespace

/** Recheck the same real candidate publication and materialized ring source.
 * Source, source generation and field generation are distinct exact identities;
 * this private borrow never changes the field validity/physical assessment.
 */
void SelfGravity::require_native_frame(const GravitySolveIdentity& source,
    std::uint64_t field_generation,std::uint64_t source_generation) const {
    auto& w=workspace();w.require(GravityFieldScope::NativeRzCandidate);
    if(w.solver.execution().device()||!w.execution||w.execution->numeric()->device()
        ||w.scope!=GravityFieldScope::NativeRzCandidate||w.source!=source
        ||w.generation!=field_generation||!w.ring_source||!source_generation
        ||w.ring_assessment.source!=source
        ||w.ring_assessment.source_generation!=source_generation
        ||w.ring_source->materialized_ring_source_generation(w.solver.op(),source)!=source_generation)
        throw std::logic_error("Native private self stage changed source, field or execution identity");
}
/** Retain full Candidate publication identity without rescanning op.cells.
 * Every visit still checks the original ready scope and exact source inputs.
 * Supported bind/prepare/execution mutations poison the attached private frame.
 * Constructor and joined acceptance call require_native_frame, which compares
 * the complete bound operator as well. This helper cannot mint authority.
 */
void SelfGravity::require_native_frame_lease(const GravitySolveIdentity& source,
    std::uint64_t field_generation,std::uint64_t source_generation) const {
    auto& w=workspace();w.require(GravityFieldScope::NativeRzCandidate);
    if(w.solver.execution().device()||!w.execution||w.execution->numeric()->device()
        ||w.scope!=GravityFieldScope::NativeRzCandidate||w.source!=source
        ||w.generation!=field_generation||!w.ring_source||!source_generation
        ||w.ring_assessment.source!=source
        ||w.ring_assessment.source_generation!=source_generation
        ||!w.ring_source->source_identity_||*w.ring_source->source_identity_!=source
        ||w.ring_source->source_generation_!=source_generation)
        throw std::logic_error("Native private self lease changed the original source/publication");
}
/** Return only the friend frame's original resident patch field. */
GravityPatchView SelfGravity::native_candidate_patch(const Grid& grid,const FluidState& state) const {
    return workspace().native_patch(grid,state);
}
/** Compile/evaluate genuine per-operation rows with the original sparse owner. */
const GravityRefluxRows& SelfGravity::native_candidate_reflux_rows(
    const amr::AmrFluxTopologyPlan& topology) const {
    return workspace().prepare_native_reflux(topology);
}
/** Borrow already completed Host paired values; no entire field download. */
const double* SelfGravity::native_candidate_reflux_values() const {
    auto& w=workspace();w.require(GravityFieldScope::NativeRzCandidate);
    if(w.solver.execution().device()||w.reflux_field_generation!=w.generation)
        throw std::logic_error("Native paired reflux is not the current Host field");
    return w.reflux_values.data;
}
/** Add original momentum math only after the private receipt reserved it. */
void SelfGravity::native_candidate_momentum(std::vector<FluidVector>& delta,
    const FluidState& state,const Grid& grid,double dt) const {
    apply_patch_momentum(workspace().native_patch(grid,state),delta,state,grid,dt);
}
/** Add original work math only after the private receipt reserved its axis. */
void SelfGravity::native_candidate_flux_work(std::vector<FluidVector>& delta,
    const std::vector<FluidVector>& flux,const FluidState& state,const Grid& grid,
    double dt,int axis) const {
    apply_patch_flux_work(workspace().native_patch(grid,state),delta,flux,grid,dt,axis);
}

/** Add the midpoint face-acceleration momentum source to one native patch. */
void SelfGravity::add_sources_on_patch(std::vector<FluidVector>& delta,const FluidState& state,
    const Grid& grid,double dt,void*) const {
    const auto& patch=workspace().patch(grid,state);
    workspace().require_host_consumer(grid,1u,dt);
    apply_patch_momentum(patch,delta,state,grid,dt);
    workspace().consume_host_patch(grid,1u);
}
/** Add conservative gravity work using the hydro face mass flux. */
void SelfGravity::add_flux_work_on_patch(std::vector<FluidVector>& delta,const std::vector<FluidVector>& flux,
    const FluidState& state,const Grid& grid,double dt,int axis) const {
    if(axis<0||axis>=grid.dim)
        throw std::invalid_argument("Gravity flux work axis is outside the active patch");
    const auto& patch=workspace().patch(grid,state);
    workspace().require_host_consumer(grid,1u<<(axis+1),dt);
    apply_patch_flux_work(patch,delta,flux,grid,dt,axis);
    workspace().consume_host_patch(grid,1u<<(axis+1));
}
/** Reduce resident field energy and download only actual boundary face pairs. */
GravityBoundarySnapshot SelfGravity::boundary_snapshot() const {
    auto& w=workspace(); w.require();
    auto& execution=w.solver.execution(); const auto& op=w.solver.op();
    if(!w.boundary_samples_bound) {
        arch::multigrid::SparseStorage rows;
        std::vector<int> faces,signs; std::vector<double> datum_coefficients;
        for(int f=0;f<static_cast<int>(op.faces().size());++f) {
            const auto& face=op.faces()[f]; if(face.boundary_side<0) continue;
            const int cell=face.left>=0 ? face.left : face.right;
            const auto& leaf=op.cells()[cell]; const int axis=face.axis;
            rows.row(face.value_samples,face.value_coefficients);
            faces.push_back(f); signs.push_back((face.boundary_side&1)?1:-1);
            datum_coefficients.push_back(face.value_boundary_coefficient);
            w.boundary_sample_identity.push_back({
                {face.boundary_side,leaf.level,axis==2?leaf.index[0]:leaf.index[(axis+1)%3],
                 leaf.index[(axis+2)%3]},face.area,0.,0.});
        }
        w.boundary_sample_rows={execution,rows};
        w.boundary_sample_faces=execution.upload(faces); w.boundary_sample_signs=execution.upload(signs);
        w.boundary_sample_coefficients=execution.upload(datum_coefficients);
        w.boundary_sample_values=execution.array<double>(2*faces.size());
        w.boundary_samples_bound=true;
    }
    GravityBoundarySnapshot result;
    result.mesh=op.base(); result.G=arch::constants::gravity::cgs::gravitational_constant; result.time=w.source.input_time;
    result.potential_energy=.5*execution.reduce({w.density.data,w.solver.resident_potential().data,
        w.volumes.data,op.size(),arch::multigrid::ReductionKind::Product});
    if(!std::isfinite(result.potential_energy)) throw std::runtime_error("Nonfinite gravity field energy");
    if(w.boundary_sample_faces.size) {
        w.execution->run(GravityBoundarySample{w.boundary_sample_faces.size,w.boundary_sample_rows.view(),
            w.boundary_sample_faces.data,w.boundary_sample_signs.data,w.boundary_sample_coefficients.data,
            w.solver.resident_potential().data,w.boundary_values.data,w.face_gradient.data,
            w.boundary_sample_values.data});
        const auto pairs=execution.download(w.boundary_sample_values);
        result.faces=w.boundary_sample_identity;
        for(std::size_t i=0;i<result.faces.size();++i) {
            result.faces[i].potential=pairs[2*i]; result.faces[i].normal_gradient=pairs[2*i+1];
            if(!std::isfinite(pairs[2*i]) || !std::isfinite(pairs[2*i+1]))
                throw std::runtime_error("Nonfinite gravity boundary field");
        }
    }
    return result;
}

}
