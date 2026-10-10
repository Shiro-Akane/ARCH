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
#include <bit>
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
    // An actual public isolated RZ bind is the real finite-ring producer: it
    // enters the same single ring branch as the internal verification binding
    // and keeps the Workspace default ExistingPhysics scope. Only a full
    // AxisymmetricRz + Cylindrical + dimension2 isolated request qualifies.
    const bool public_ring=!native_candidate
        &&binding.base.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
        &&binding.base.geometry==arch::elliptic::Geometry::Cylindrical
        &&binding.base.dimension==2&&config_.boundary=="isolated";
    // Prescribed RZ data use the same gather -> Poisson -> gradient -> field
    // owner as (r,phi), with the operator's actual full-ring measures. They do
    // not request or inherit the isolated ring producer built above.
    if(binding.base.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz&&!native_candidate) {
        const bool prescribed=config_.boundary=="user"||config_.boundary=="dirichlet"
            ||config_.boundary=="neumann";
        if(!prescribed&&!public_ring)
            throw std::logic_error("RZ self-gravity finite-ring runtime consumer is not qualified");
    }
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
    // One shared ring initializer for both actual ring consumers: the private
    // verification binding keeps its validated explicit cap and candidate
    // scope, while the public producer reuses the existing explicit supported
    // private-route cap 65536 (the public bind itself passes 0) and keeps the
    // Workspace default ExistingPhysics scope. Only this branch derives; the
    // ordinary explicit-policy bindings never enter ring validation or acquire
    // unrelated constraints. No new constant, parameter or authority is added.
    if(native_candidate||public_ring) {
        if(native_candidate) work_->scope=GravityFieldScope::NativeRzCandidate;
        work_->ring_limits.maximum_boxes_per_leaf=native_candidate?maximum_boxes:65536;
        work_->ring_source=std::make_unique<GravityBoundary>(
            work_->solver.op(),work_->binding.handles.front().epoch);
        work_->ring_limits.maximum_leaf_evaluations=maximum_work ? maximum_work
            : work_->ring_source->full_ring_traversal_work_bound(work_->solver.op());
    }
}
/** Retire a prior gravity publication whenever its density lease changes. */
void SelfGravity::invalidate() const noexcept {
    if(prepared_native_self())prepared_native_self()->invalidate();
    if(native_source_inspection_running_)native_source_inspection_invalidated_=true;
    if(work_) { work_->ready=false; work_->downloaded=false; work_->validity.invalidate();
        work_->purpose.reset();work_->runtime_lease=nullptr; } }
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
    if(work_) {
        // Restart/retry history reset shares the original guarded owner, not field invalidation.
        if(work_->ring_source)work_->ring_source->clear_ring_memo();
        work_->solver.clear_initial_guess();
    }
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
    if((request.purpose&&!valid_gravity_field_purpose(*request.purpose))
        ||(request.runtime_lease&&!request.purpose))
        throw std::invalid_argument("Invalid gravity request purpose/issuer metadata");
    if (!work_) throw std::logic_error("Self-gravity mesh is not bound");
    const auto& identity=request.identity;
    if(request.runtime_lease)
        request.runtime_lease->require_preparation(*request.purpose,work_->binding);
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
                <<" apply_arithmetic="<<w.ring_assessment.native_residual_error.arithmetic.norm_upper
                <<" joint_construction="<<op.native_rz_prescribed_residual_construction_error(
                    e.download(w.solver.resident_potential()),w.ring.values).native_norm_upper
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
        e.run(arch::multigrid::RowsWork{w.patch_work.size,w.patch_work_gather.view(),w.work_sides.data,w.patch_work.data});
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
    if(request.runtime_lease)
        request.runtime_lease->require_preparation(*request.purpose,w.binding);
    w.purpose=request.purpose;w.runtime_lease=request.runtime_lease;
    w.validity.publish({identity,w.generation,token,w.scope,request.purpose,
        request.runtime_lease?request.runtime_lease->generation():0});
    w.ready=true; return token;
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
 * -> enclose same-op native Phi RMS/volume and copy its scalar residual proof
 * -> revalidate the same workspace/source/generations before returning owners.
 * g is the existing arithmetic average of actual gathered low/high side g;
 * no gradient, gather, quadrature, normalization or new physical field is made.
 * Prepare/bind are serial by contract; this synchronous read adds no callback
 * and grants no concurrent mutation safety or production physical qualification.
 */
NativeRzFieldInspection SelfGravity::native_rz_field_inspection() const {
    auto& w=workspace();
    w.require(GravityFieldScope::NativeRzCandidate);
    // Snapshot the actual request purpose/issuer, then authenticate the sealed
    // Runtime publication before any copy. Pure mathematical tags are retained
    // with zero generation/authentication and acquire no Current/Hydro rights.
    const auto purpose=w.purpose;
    const auto* const runtime_lease=w.runtime_lease;
    if(purpose&&!valid_gravity_field_purpose(*purpose))
        throw std::logic_error("Native RZ field inspection has an invalid purpose tag");
    if(runtime_lease) {
        if(!purpose)throw std::logic_error("Native RZ field inspection lost its Runtime purpose");
        w.require_runtime_purpose(*purpose);
    }
    const std::uint64_t lease_generation=runtime_lease?runtime_lease->generation():0;
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
    result.purpose=purpose;
    result.runtime_lease_generation=lease_generation;
    result.runtime_lease_authenticated=runtime_lease!=nullptr;
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
    // This is an ideal-native DISCRETE certificate for the same solved
    // publication. A weighted residual is not a continuum Phi error; copying
    // its cellwise ledger onto a separate long-double reducer would be invalid.
    const auto& assessment=w.ring_assessment;
    using arch::elliptic::BoundaryErrorStatus;
    using arch::elliptic::BoundaryResidualStatus;
    if(assessment.scope!=RingRhsAssessmentScope::RootDyadicNativeOperator
        ||assessment.conditional.status!=BoundaryResidualStatus::Accepted
        ||assessment.source_error.status!=GravitySourceBoundStatus::Bounded
        ||assessment.assembly_error.status!=BoundaryErrorStatus::Bounded
        ||assessment.native_boundary_construction.status!=BoundaryErrorStatus::Bounded
        ||assessment.native_boundary_potential.status!=BoundaryErrorStatus::Bounded
        ||assessment.native_residual_error.status!=BoundaryErrorStatus::Bounded
        ||assessment.native_residual_error.construction.status!=BoundaryErrorStatus::Bounded
        ||assessment.native_residual_error.arithmetic.status!=BoundaryErrorStatus::Bounded
        ||assessment.native_complete_residual_error.status!=BoundaryErrorStatus::Bounded)
        throw std::logic_error("Native RZ field inspection has no matching native discrete residual proof");
    // Reuse the outward-safe owners with this exact downloaded point Phi and
    // actual operator. This optional inspection adds no production norm/cache.
    result.native_potential_rms=op.native_rz_norm_interval(result.potential);
    result.native_measure=op.native_rz_measure_enclosure();
    result.conditional_residual=assessment.conditional;
    result.physical_status=assessment.physical_status;
    result.residual_norm_scope=arch::elliptic::BoundaryResidualNormScope::RootDyadicRzWeights;
    auto& errors=result.residual_error;
    errors.source_stored_upper=assessment.source_error.norm_upper;
    errors.rhs_assembly_stored_upper=assessment.assembly_error.norm_upper;
    errors.residual_arithmetic_stored_upper=assessment.native_residual_error.arithmetic.norm_upper;
    errors.boundary_construction_native_upper=assessment.native_boundary_construction.native_norm_upper;
    errors.boundary_potential_native_upper=assessment.native_boundary_potential.native_norm_upper;
    errors.operator_construction_native_upper=assessment.native_residual_error.construction.native_norm_upper;
    errors.residual_evaluation_native_upper=assessment.native_residual_error.native_norm_upper;
    errors.complete_native_upper=assessment.native_complete_residual_error.native_norm_upper;
    // Finite bounds are metadata validity, never a new acceptance threshold.
    // Zero Phi/error remains valid; neither zero nor infinity becomes a fallback.
    const auto nonnegative_finite=[](double value) {
        return std::isfinite(value)&&value>=0.;
    };
    const auto& norm=result.native_potential_rms;
    const auto& measure=result.native_measure;
    const auto& residual=result.conditional_residual;
    if(norm.status!=BoundaryErrorStatus::Bounded
        ||!nonnegative_finite(norm.lower)||!nonnegative_finite(norm.upper)
        ||norm.lower>norm.upper||measure.status!=BoundaryErrorStatus::Bounded
        ||!std::isfinite(measure.total_volume_lower)||!(measure.total_volume_lower>0.)
        ||!std::isfinite(measure.total_volume_upper)
        ||measure.total_volume_lower>measure.total_volume_upper
        ||measure.volume_lower.size()!=static_cast<std::size_t>(n)
        ||measure.volume_upper.size()!=static_cast<std::size_t>(n)
        ||measure.volume_error_upper.size()!=static_cast<std::size_t>(n)
        ||measure.weight_lower.size()!=static_cast<std::size_t>(n)
        ||measure.weight_upper.size()!=static_cast<std::size_t>(n)
        ||measure.weight_error_upper.size()!=static_cast<std::size_t>(n)
        ||!nonnegative_finite(residual.complete_residual_error_upper)
        ||!nonnegative_finite(residual.tolerance_safe)
        ||!nonnegative_finite(residual.rhs_norm_lower)
        ||!nonnegative_finite(residual.rhs_norm_upper)
        ||residual.rhs_norm_lower>residual.rhs_norm_upper
        ||!nonnegative_finite(residual.residual_norm_upper)
        ||!nonnegative_finite(residual.rhs_error_upper)
        ||!nonnegative_finite(residual.total_residual_upper)
        ||!nonnegative_finite(errors.source_stored_upper)
        ||!nonnegative_finite(errors.rhs_assembly_stored_upper)
        ||!nonnegative_finite(errors.residual_arithmetic_stored_upper)
        ||!nonnegative_finite(errors.boundary_construction_native_upper)
        ||!nonnegative_finite(errors.boundary_potential_native_upper)
        ||!nonnegative_finite(errors.operator_construction_native_upper)
        ||!nonnegative_finite(errors.residual_evaluation_native_upper)
        ||!nonnegative_finite(errors.complete_native_upper))
        throw std::runtime_error("Native RZ field inspection has invalid discrete certificate metadata");
    // Copies own storage. Recheck the original serial publication rather than
    // assuming the ring-source counter is the solved-field counter.
    execution.fence();
    w.require(GravityFieldScope::NativeRzCandidate);
    if(work_.get()!=&w||w.source!=result.source||w.generation!=result.field_generation
        ||w.purpose!=purpose||w.runtime_lease!=runtime_lease
        ||(runtime_lease&&runtime_lease->generation()!=lease_generation)
        ||w.ring_assessment.physical_status!=result.physical_status
        ||w.ring_assessment.source!=result.source
        ||w.ring_assessment.source_generation!=result.source_generation
        ||w.ring_source->materialized_ring_source_generation(op,result.source)
            !=result.source_generation)
        throw std::logic_error("Native RZ field inspection changed during its synchronous copy");
    // The same actual token and matching field stamp must still authorize the
    // requested purpose after the synchronous fence; this is not a science gate.
    if(runtime_lease)w.require_runtime_purpose(*purpose);
    return result;
}
/** Own the SAME published native source, operator geometry and solved field.
 * Workflow: authenticate the issued purpose/publication -> freeze actual
 * metadata owners -> copy the existing field once and resident rho once ->
 * compare every rho bit with both the materialized ring source and its original
 * Host allocation -> repeat publication/geometry/source checks -> return the
 * closed value. The friend issuer separately checks the full Runtime domain
 * before and after this call, including all seven arrays and actual ghosts.
 * rho is the original V-mean source, not RHS/(4*pi*G). No source reconstruction,
 * quadrature, solve, numerical tolerance, cache or live authority is created.
 */
NativeRzSolutionInspection SelfGravity::copy_native_rz_solution(
    GravityFieldPurpose expected_purpose,const GravitySolveIdentity& expected_source,
    std::uint64_t expected_field_generation,std::uint64_t expected_source_generation) const {
    if(!valid_gravity_field_purpose(expected_purpose)||!expected_field_generation
        ||!expected_source_generation)
        throw std::invalid_argument("Native source/field inspection has an invalid expected publication");
    auto& w=workspace();
    w.require(GravityFieldScope::NativeRzCandidate);
    w.require_runtime_purpose(expected_purpose);
    const auto* const issuer=w.runtime_lease;
    const auto lease_generation=issuer->generation();
    const auto* const original_workspace=&w;
    const auto* const original_operator=&w.solver.op();
    const auto& op=*original_operator;
    auto& execution=w.solver.execution();
    const auto mesh=op.base();
    const auto configuration=config_;
    const auto periodic=w.binding.periodic;
    const auto same=[](double a,double b) noexcept {
        return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
    };
    // Metadata identity is exact, including signed zero. It never supplies an
    // approximate geometry, rho floor or acceptance error budget.
    const auto same_mesh=[&](const arch::elliptic::CartesianMesh& a,
        const arch::elliptic::CartesianMesh& b) {
        if(a.dimension!=b.dimension||a.cells!=b.cells||a.geometry!=b.geometry
            ||a.semantics!=b.semantics||a.native_canonical_domain!=b.native_canonical_domain)return false;
        for(int axis=0;axis<3;++axis)
            if(!same(a.origin[axis],b.origin[axis])||!same(a.spacing[axis],b.spacing[axis])
                ||!same(a.root_upper[axis],b.root_upper[axis]))return false;
        return true;
    };
    const auto same_configuration=[&](const GravityConfig& a,const GravityConfig& b) {
        return a==b&&same(a.g_x,b.g_x)&&same(a.g_y,b.g_y)&&same(a.g_z,b.g_z)
            &&same(a.relative_tolerance,b.relative_tolerance)
            &&same(a.absolute_tolerance,b.absolute_tolerance);
    };
    if(execution.device()||!w.execution||w.execution->numeric()->device()
        ||!mesh.native_canonical_domain||mesh.dimension!=2
        ||mesh.geometry!=arch::elliptic::Geometry::Cylindrical
        ||mesh.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||!same_mesh(mesh,w.binding.base)||op.size()<=0)
        throw std::logic_error("Native source/field inspection requires the actual canonical Host RZ operator");
    const auto count=static_cast<std::size_t>(op.size());
    const auto patch_count=w.binding.grids.size();
    if(!patch_count||w.binding.handles.size()!=patch_count||w.patches.size()!=patch_count
        ||expected_source.inputs.size()!=patch_count||w.binding.cells!=op.cells()
        ||w.binding.storage.size()!=count||op.volumes().size()!=count
        ||w.density.size!=op.size()||!w.density.data||!w.ring_source
        ||w.ring_source->ring_density_.size()!=count)
        throw std::logic_error("Native source/field inspection has incomplete actual source storage");
    const auto* const density_data=w.density.data;
    const auto* const ring=w.ring_source.get();
    const auto* const ring_density_address=ring->ring_density_.data();
    const auto* const binding_cells_address=w.binding.cells.data();
    const auto* const binding_storage_address=w.binding.storage.data();
    const auto* const grids_address=w.binding.grids.data();
    const auto* const handles_address=w.binding.handles.data();
    const auto* const operator_cells_address=op.cells().data();
    const auto* const faces_address=op.faces().data();
    const auto* const volumes_address=op.volumes().data();
    const auto face_count=op.faces().size();
    // Borrowed pointer values are transient fence metadata only. No Grid
    // pointer is retained by the returned owning diagnostic.
    const auto original_grids=w.binding.grids;
    std::vector<const double*> source_allocations;
    std::vector<NativeRzOwnedSourcePatch> patches;
    source_allocations.reserve(patch_count);patches.reserve(patch_count);
    for(std::size_t p=0;p<patch_count;++p) {
        if(!w.binding.grids[p]||w.binding.handles[p]!=expected_source.inputs[p].block
            ||!amr::is_valid(w.binding.handles[p])||!w.patches[p].density)
            throw std::logic_error("Native source/field inspection lost its original patch allocation");
        const auto& grid=*w.binding.grids[p];
        const auto found=w.lookup.find(&grid);
        const auto geometry=GridMetrics::make_geometry_view(grid,mesh.semantics);
        if(found==w.lookup.end()||found->second!=p||!geometry.dyadic_identity.bound
            ||geometry.geometry!=GridMetrics::Geometry::Cylindrical||geometry.dim!=2
            ||geometry.total_size!=grid.GetTotalSize()
            ||!GridMetrics::matches_identity(geometry.dyadic_identity,{grid.x1_min,grid.x2_min},
                {grid.x1_max,grid.x2_max},{grid.dx1,grid.dx2})
            ||geometry.dyadic_identity.periodic_axial!=periodic[1])
            throw std::logic_error("Native source/field inspection has incoherent actual patch geometry");
        for(int axis=0;axis<2;++axis)
            if(!same(geometry.dyadic_identity.root_lower[axis],mesh.origin[axis])
                ||!same(geometry.dyadic_identity.root_upper[axis],mesh.root_upper[axis])
                ||std::int64_t(geometry.dyadic_identity.root_blocks[axis])
                    *(axis==0?amr::BLOCK_NX:amr::BLOCK_NY)!=mesh.cells[axis])
                throw std::logic_error("Native source/field inspection changed its bound root geometry");
        source_allocations.push_back(w.patches[p].density);
        patches.push_back({w.binding.handles[p],amr::native_scalar_layout(grid),
            arch::grid::FieldMemory::Host,geometry});
    }
    // This private function accepts no source buffers supplied by the caller.
    // Every check below refers to the original resident/actual-source owners.
    const auto require_publication=[&] {
        if(work_.get()!=original_workspace)
            throw std::logic_error("Native source/field workspace changed during inspection");
        w.require(GravityFieldScope::NativeRzCandidate);
        if(&w.solver.op()!=original_operator||w.runtime_lease!=issuer||w.purpose!=expected_purpose
            ||w.source!=expected_source||!same(w.source.input_time,expected_source.input_time)
            ||!same(w.source.gravitational_constant,expected_source.gravitational_constant)
            ||w.generation!=expected_field_generation||w.ring_source.get()!=ring
            ||w.ring_assessment.source!=expected_source
            ||w.ring_assessment.source_generation!=expected_source_generation
            ||!same_configuration(config_,configuration)||!same_mesh(op.base(),mesh)
            ||!same_mesh(w.binding.base,mesh)||w.binding.periodic!=periodic
            ||w.binding.cells.size()!=count||w.binding.cells!=op.cells()||w.binding.storage.size()!=count
            ||w.binding.grids.size()!=patch_count||w.binding.grids!=original_grids
            ||w.binding.handles.size()!=patch_count
            ||w.patches.size()!=patch_count||op.size()!=static_cast<int>(count)
            ||op.volumes().size()!=count||op.faces().size()!=face_count
            ||w.density.size!=static_cast<int>(count)||w.density.data!=density_data
            ||ring->ring_density_.size()!=count||ring->ring_density_.data()!=ring_density_address
            ||w.binding.cells.data()!=binding_cells_address||w.binding.storage.data()!=binding_storage_address
            ||w.binding.grids.data()!=grids_address||w.binding.handles.data()!=handles_address
            ||op.cells().data()!=operator_cells_address||op.faces().data()!=faces_address
            ||op.volumes().data()!=volumes_address||w.solver.execution().device()
            ||!w.execution||w.execution->numeric()->device())
            throw std::logic_error("Native source/field inspection changed its original publication owners");
        w.require_runtime_purpose(expected_purpose);
        if(issuer->generation()!=lease_generation
            ||ring->materialized_ring_source_generation(op,expected_source)!=expected_source_generation)
            throw std::logic_error("Native source/field inspection changed its actual issuer/source generation");
    };
    require_publication();
    // The existing owning field copy preserves its original fences, exact
    // point arrays, original face rows and ideal-native discrete certificates.
    auto field=native_rz_field_inspection();
    if(field.source!=expected_source||field.field_generation!=expected_field_generation
        ||field.source_generation!=expected_source_generation||field.purpose!=expected_purpose
        ||!field.runtime_lease_authenticated||field.runtime_lease_generation!=lease_generation)
        throw std::logic_error("Native source/field inspection copied another field receipt");
    execution.fence();
    auto density=execution.download(w.density);
    if(density.size()!=count)
        throw std::runtime_error("Native source/field inspection omitted resident density entries");
    std::vector<NativeRzOwnedSourceCell> cells;cells.reserve(count);
    std::size_t cell=0;
    for(std::size_t p=0;p<patch_count;++p) {
        const auto& grid=*w.binding.grids[p];const auto& id=patches[p].geometry.dyadic_identity;
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i,++cell) {
            if(cell>=count)throw std::logic_error("Native source/field inspection omitted an actual cell");
            const auto storage=w.binding.storage[cell];const auto& key=op.cells()[cell];
            const int offset=grid.GetIndex(i,j,0);
            if(storage.block!=p||storage.offset!=offset||offset<0||offset>=grid.GetTotalSize()
                ||key.level!=id.level||key.index!=std::array<int,3>{
                    static_cast<int>(id.logical[0])*amr::BLOCK_NX+i-grid.Is(),
                    static_cast<int>(id.logical[1])*amr::BLOCK_NY+j-grid.Js(),0}
                ||!std::isfinite(density[cell])||!(density[cell]>0.)
                ||!same(density[cell],ring->ring_density_[cell])
                ||!same(density[cell],source_allocations[p][offset]))
                throw std::logic_error("Native source/field inspection differs from actual dense rho/storage");
            NativeRzOwnedSourceCell owned{p,offset,key,{},{},op.center(static_cast<int>(cell)),op.volumes()[cell]};
            for(int axis=0;axis<3;++axis) {
                owned.lower[axis]=op.lower(static_cast<int>(cell),axis);
                owned.upper[axis]=op.upper(static_cast<int>(cell),axis);
                if(!std::isfinite(owned.lower[axis])||!std::isfinite(owned.upper[axis])
                    ||!std::isfinite(owned.center[axis]))
                    throw std::logic_error("Native source/field inspection contains nonfinite cell geometry");
            }
            if(!(owned.upper[0]>owned.lower[0])||!(owned.upper[1]>owned.lower[1])
                ||!std::isfinite(owned.operator_volume)||!(owned.operator_volume>0.)
                ||!same(owned.lower[0],grid.GetFacePosL(i))||!same(owned.upper[0],grid.GetFacePosR(i))
                ||!same(owned.lower[1],grid.GetAxialFacePosL(j))||!same(owned.upper[1],grid.GetAxialFacePosR(j))
                ||!same(owned.center[0],grid.GetCellCenterX(i))||!same(owned.center[1],grid.GetCellCenterY(j)))
                throw std::logic_error("Native source/field inspection differs from actual operator/Grid cell geometry");
            cells.push_back(owned);
        }
    }
    if(cell!=count)throw std::logic_error("Native source/field inspection has non-native extra source cells");
    execution.fence();require_publication();
    // A second read validates the SAME allocations/geometry, not another
    // gathered source. Supported prepare/bind mutations are already excluded;
    // in-place rho drift is additionally detected bit-for-bit before return.
    for(std::size_t p=0;p<patch_count;++p) {
        const auto& grid=*w.binding.grids[p];const auto actual=GridMetrics::make_geometry_view(grid,mesh.semantics);
        const auto& frozen=patches[p].geometry;
        const auto found=w.lookup.find(&grid);
        if(found==w.lookup.end()||found->second!=p||w.patches[p].density!=source_allocations[p]
            ||w.binding.handles[p]!=patches[p].block||amr::native_scalar_layout(grid)!=patches[p].layout
            ||actual.geometry!=frozen.geometry||actual.dim!=frozen.dim||actual.ng!=frozen.ng
            ||actual.stride_y!=frozen.stride_y||actual.stride_z!=frozen.stride_z
            ||actual.total_size!=frozen.total_size||actual.semantics!=frozen.semantics
            ||!GridMetrics::equal_identity(actual.dyadic_identity,frozen.dyadic_identity)
            ||!same(actual.dx1,frozen.dx1)||!same(actual.dx2,frozen.dx2)||!same(actual.dx3,frozen.dx3)
            ||!same(actual.x1_min,frozen.x1_min)||!same(actual.x2_min,frozen.x2_min)||!same(actual.x3_min,frozen.x3_min))
            throw std::logic_error("Native source/field inspection changed original patch geometry/allocation");
        for(int axis=0;axis<2;++axis)
            if(!same(actual.actual_block_upper[axis],frozen.actual_block_upper[axis]))
                throw std::logic_error("Native source/field inspection changed actual patch endpoint bits");
    }
    for(std::size_t c=0;c<count;++c) {
        const auto& owned=cells[c];const auto current=w.binding.storage[c];
        if(current.block!=owned.block||current.offset!=owned.offset||op.cells()[c]!=owned.key
            ||!same(density[c],ring->ring_density_[c])
            ||!same(density[c],source_allocations[owned.block][owned.offset])
            ||!same(owned.operator_volume,op.volumes()[c]))
            throw std::logic_error("Native source/field inspection changed actual rho/cell ownership during copy");
        const auto center=op.center(static_cast<int>(c));
        for(int axis=0;axis<3;++axis)
            if(!same(owned.lower[axis],op.lower(static_cast<int>(c),axis))
                ||!same(owned.upper[axis],op.upper(static_cast<int>(c),axis))||!same(owned.center[axis],center[axis]))
                throw std::logic_error("Native source/field inspection changed actual cell bounds during copy");
    }
    require_publication();
    return NativeRzSolutionInspection(std::move(field),configuration,mesh,periodic,
        std::move(cells),std::move(patches),std::move(density));
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
    return field_timestep(cfl,GravityFieldScope::ExistingPhysics);
}
/** Evaluate the original stability cap under an explicitly required field scope. */
double SelfGravity::field_timestep(double cfl,GravityFieldScope scope) const {
    workspace().require(scope);const auto& w=*work_;
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
/** Expose already fence-completed phase measurements at the matching scope.
 * This diagnostic returns no field data and cannot promote a candidate into
 * physical execution; the actual Runtime/purpose owner still authenticates
 * any association of these scalar measurements with a scientific request.
 */
const SelfGravity::Timings& SelfGravity::timings(GravityFieldScope scope) const {
    workspace().require(scope);return work_->timings;
}
/** Observe the SAME completed Host ring call without a solve or array transfer.
 * Workflow: use the original ready/scope guard -> require current ring identity
 * -> copy original per-call counters/occupancy plus actual operator identity.
 * A hit saved certified interval work, not tree/source validation. Numerical
 * scope is retained and no Runtime purpose/source lease is manufactured here.
 */
SelfGravity::RingMemoObservations SelfGravity::ring_memo_observations(
    GravityFieldScope scope) const {
    auto& w=workspace();w.require(scope);
    if(scope!=GravityFieldScope::NativeRzCandidate||!w.ring_source
        ||w.solver.execution().device()||w.ring.source!=w.source
        ||w.ring.source_generation!=w.ring_assessment.source_generation)
        throw std::logic_error("Ring memo diagnostics have no matching completed Host Native scope");
    w.ring_source->require_current_ring(w.solver.op(),w.ring);
    RingMemoObservations result{};
    result.epoch=w.source.topology.value;result.source_generation=w.ring.source_generation;
    result.field_generation=w.generation;result.cells=static_cast<std::uint64_t>(w.solver.op().size());
    for(const auto& face:w.solver.op().faces())if(face.boundary_side>=0)++result.boundary_faces;
    result.memo_hits=w.ring.memo_hits;result.memo_misses=w.ring.memo_misses;
    result.memo_admissions=w.ring.memo_admissions;
    result.current_call_kernel_enclosures=w.ring.kernel_enclosures;
    result.current_call_range_evaluations=w.ring.range_evaluations;
    result.current_call_agm_iterations=w.ring.agm_iterations;
    result.entries=static_cast<std::uint64_t>(w.ring_source->ring_memo_size());
    result.capacity=GravityBoundary::maximum_ring_memo_entries;
    return result;
}
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
        delta[c].eng+=gravity_flux_work(patch.work_low[axis][c],patch.work_high[axis][c],flux[c].rho,flux[c+stride].rho,dt);
    }
}
} // namespace

/** Authenticate a Runtime field purpose; direct mathematical tags cannot pass. */
void SelfGravity::require_runtime_purpose(GravityFieldPurpose purpose) const {
    workspace().require_runtime_purpose(purpose);
}

/** Require the actual solved RZ Hydro field and its original source lease.
 * Prescribed fields use the common completed publication generation. The
 * actual isolated ring producer additionally authenticates its genuine ring
 * materialization independently of its scope; a prescribed datum never
 * receives an invented ring/integration identity.
 */
void SelfGravity::require_native_frame(const GravitySolveIdentity& source,
    std::uint64_t field_generation,std::uint64_t source_generation) const {
    require_native_frame_lease(source,field_generation,source_generation);
    const auto& w=workspace();
    if(w.ring_source
        &&w.ring_source->materialized_ring_source_generation(w.solver.op(),source)!=source_generation)
        throw std::logic_error("Native self field lost its exact ring operator/source association");
}
/** Recheck the existing field/Runtime identity without copying or scanning rho.
 * The completed field generation already names the gathered prescribed source.
 * Candidate ring counters retain their original distinct diagnostic semantics.
 */
void SelfGravity::require_native_frame_lease(const GravitySolveIdentity& source,
    std::uint64_t field_generation,std::uint64_t source_generation) const {
    const auto& w=workspace();
    // The original purpose gate starts with require(scope): check the same
    // completed source/scope once before dereferencing its borrowed issuer.
    // Runtime and patch pre/post fences remain with their original owners.
    w.require_runtime_purpose(GravityFieldPurpose::HydroStage);
    if(!w.execution||w.execution->numeric().get()!=&w.solver.execution()
        ||w.solver.op().base().semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||w.source!=source||w.generation!=field_generation||!source_generation)
        throw std::logic_error("Native self lease changed its source/publication/execution");
    if(w.ring_source) {
        // The real ring producer authenticates the same ring/source/assessment
        // generation under either legitimate scope and its actual curvilinear
        // isolated chart.
        if((w.scope!=GravityFieldScope::NativeRzCandidate&&w.scope!=GravityFieldScope::ExistingPhysics)
            ||w.solver.op().boundary_kind()!=arch::elliptic::BoundaryKind::CurvilinearIsolated
            ||w.ring_assessment.source!=source
            ||w.ring_assessment.source_generation!=source_generation
            ||!w.ring_source->source_identity_||*w.ring_source->source_identity_!=source
            ||w.ring_source->source_generation_!=source_generation)
            throw std::logic_error("Native ring lease lost its materialized ring source");
    } else if(w.scope!=GravityFieldScope::ExistingPhysics||!w.explicit_boundary
        ||w.solver.op().boundary_kind()!=arch::elliptic::BoundaryKind::User
        ||source_generation!=field_generation) {
        throw std::logic_error("Prescribed RZ lease does not name its actual completed field");
    }
}
/** Return only the friend frame's original resident patch field. */
GravityPatchView SelfGravity::prepared_rz_patch(const Grid& grid,const FluidState& state) const {
    return workspace().native_patch(grid,state);
}
/** Borrow the same resident patch arrays on the explicitly requested backend
 * side. The density pointer stays a borrowed resident read of the authentic
 * source lease; this reader mints no Runtime authority of its own.
 */
GravityPatchView SelfGravity::prepared_rz_device_patch(const Grid& grid,
    const double* density) const {
    return workspace().native_patch(grid,density,true);
}
/** Compile/evaluate genuine per-operation rows with the original sparse owner. */
const GravityRefluxRows& SelfGravity::prepared_rz_reflux_rows(
    const amr::AmrFluxTopologyPlan& topology) const {
    return workspace().prepare_native_reflux(topology);
}
/** Borrow already completed paired values for the requested side; no download. */
const double* SelfGravity::prepared_rz_reflux_values(bool expected_device) const {
    auto& w=workspace();w.require(w.scope);
    if(w.solver.execution().device()!=expected_device||w.reflux_field_generation!=w.generation)
        throw std::logic_error("Native paired reflux is not the current resident field for the requested side");
    return w.reflux_values.data;
}
/** Add original momentum math only after the private receipt reserved it. */
void SelfGravity::prepared_rz_momentum(std::vector<FluidVector>& delta,
    const FluidState& state,const Grid& grid,double dt) const {
    apply_patch_momentum(workspace().native_patch(grid,state),delta,state,grid,dt);
}
/** Add original work math only after the private receipt reserved its axis. */
void SelfGravity::prepared_rz_flux_work(std::vector<FluidVector>& delta,
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
