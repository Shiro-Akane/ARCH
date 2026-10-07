/** @file SelfGravity.h
 * Backend-independent domain-field owner. Prepare is serial; patch consumers only read a fully
 * published field. No solve is hidden in patch callbacks or output routines.
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Declare the backend-independent self-gravity domain-field owner.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "amr/flux/AmrFluxPlan.h"
#include "numerics/elliptic/CompositePoisson.h"
#include "physics/boundary/BoundaryTypes.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/IGravityPolicy.h"
#include "physics/gravity/self/GravityBoundaryDiagnostics.h"

namespace arch::driver { class GravityStage; }
namespace amr { struct EllipticMeshBinding; }
namespace arch::state { struct CompletionToken; }
namespace arch::multigrid { struct SolveReport; }
namespace Physical::Gravity {
struct GravitySolveRequest;
struct GravitySolveIdentity;
struct RingRhsAssessment;
struct RingBoundaryBudgetProposal;
class GravityExecution;
class NativeSelfStageFrame;
struct GravityRefluxRows;
struct GravityPatchView;
/** Synchronous internal view of actual materialized Host native ring source.
 * Workflow: the real GravityStage validates its Runtime -> borrow this exact
 * operator/binding/dense gathered rho -> inspect -> revalidate the Runtime.
 * Density is native V-mean rho interpreted by gravity as piecewise-constant
 * full-ring density; all cells, including any static zeros, retain their order.
 * The view owns no data and dies before budget/solve. Source generation is the
 * real ring update counter, never field publication/lease/stage generation.
 * It grants neither a solved field nor physical/native/Device qualification.
 */
struct NativeRzSourceInspectionView {
    const arch::elliptic::CompositePoisson& op;
    const amr::EllipticMeshBinding& binding;
    std::span<const double> density;
    const GravitySolveRequest& request;
    // The actual immutable service copy, not a Runtime description inferred
    // from current parameters. Derived/cache choices are not supplied here.
    const GravityConfig& service_configuration;
    std::uint64_t source_generation;
};
using NativeRzSourceInspectionSink=void(*)(void*,const NativeRzSourceInspectionView&);

/** Owning internal inspection of one completed Host native-RZ candidate.
 * Workflow: authenticate the actual published source and both generations ->
 * fence/copy the existing solved arrays and actual face metadata -> revalidate.
 * potential/acceleration use the actual composite cell order. face_gradient and
 * boundary_values use faces order; side_acceleration is [cell][2*axis+side],
 * with all six stored slots retained. The face records preserve the original
 * gradient/value stencils, coefficients and exact native fragment endpoints.
 * Source generation belongs to the materialized ring-density update; field
 * generation belongs to the solved publication. Neither substitutes for input
 * time, density slot/version/storage identities or Runtime authentication.
 * This value certifies no continuous Phi/force accuracy, physical consumer or
 * Device support. Original public/native capability gates remain unchanged.
 */
struct NativeRzFieldInspection {
    GravitySolveIdentity source;
    std::uint64_t source_generation=0,field_generation=0;
    std::vector<double> potential,face_gradient,boundary_values,side_acceleration;
    std::array<std::vector<double>,3> acceleration;
    std::vector<arch::elliptic::CompositeFace> faces;
};

class SelfGravity final : public IGravityPolicy {
public:
    explicit SelfGravity(GravityConfig config);
    ~SelfGravity();
    /** Typed discovery only; private attachment is required for consumption. */
    GravitySourceDescriptor source_descriptor() const noexcept override {
        return {GravitySourceOrigin::NativeSelfComposite,{}};
    }
    /** Bind using the actual stage/restart time; public RZ remains gated. */
    void bind(amr::EllipticMeshBinding binding, double time = 0.) const;
    // Internal CPU numerical verification only. Native candidates cannot be
    // read by normal physical patch/output consumers; public bind stays gated.
    // maximum_work=0 selects a checked full traversal budget from the actual
    // bound source tree and exterior faces. Nonzero values retain the explicit
    // resource cap, including deliberately small verification budgets. This
    // policy changes no scientific tolerance or per-leaf subdivision limit.
    void bind_native_rz_candidate(amr::EllipticMeshBinding,
        std::uint64_t maximum_boxes_per_leaf,std::uint64_t maximum_work) const;
    const RingRhsAssessment& native_rz_assessment() const;
    const std::vector<double>& native_rz_potential() const;
    const std::array<std::vector<double>,3>& native_rz_acceleration() const;
    /** Copy actual solved candidate arrays for an independent same-source
     * diagnostic; no solve, derivative reconstruction or cache is created.
     */
    NativeRzFieldInspection native_rz_field_inspection() const;
    arch::state::CompletionToken prepare(const GravitySolveRequest&) const;
    void invalidate() const noexcept;
    void clear_solver_initial_guess() const noexcept;
    void set_execution(std::shared_ptr<GravityExecution>) const;
    GravityPatchView patch_view(std::size_t block) const;
    std::size_t cell_count() const;
    double timestep(double cfl) const;
    const std::vector<double>& potential() const;
    const std::array<std::vector<double>,3>& acceleration() const;
    const arch::multigrid::SolveReport& report() const;
    double density_mean() const;
    // Wall time bounded by completion fences; no asynchronous launch timing.
    struct Timings { double source_boundary=0., poisson=0., force=0.; };
    const Timings& timings() const;
    /** Enable an internal Host receipt before preparation; ordinary runs do not track consumers. */
    void begin_host_stage_consumption(double step_dt) const;
    /** Require the exact published input and every real patch force/work consumer. */
    void require_host_stage_consumption(const GravitySolveIdentity&) const;
    /** Retire a Host receipt without publishing diagnostics or retaining borrowed state. */
    void end_host_stage_consumption() const noexcept;
    /** Sample field energy and physical-face pairs for independent accounting. */
    GravityBoundarySnapshot boundary_snapshot() const;
    void add_sources_on_patch(std::vector<FluidVector>&, const FluidState&,
        const Grid&, double, void* = nullptr) const override;
    void add_flux_work_on_patch(std::vector<FluidVector>&, const std::vector<FluidVector>&,
        const FluidState&, const Grid&, double, int) const override;
private:
    friend class arch::driver::GravityStage;
    friend class NativeSelfStageFrame;
    /** Authenticate only the friend frame's actual Host candidate publication.
     * This never changes its scope or grants the ordinary public readers.
     */
    void require_native_frame(const GravitySolveIdentity&,std::uint64_t field_generation,
        std::uint64_t source_generation) const;
    /** Retain the original Candidate publication/source checks on each visit;
     * the full immutable operator is compared once at prepare and after join.
     */
    void require_native_frame_lease(const GravitySolveIdentity&,
        std::uint64_t field_generation,std::uint64_t source_generation) const;
    /** Borrow actual resident patch arrays under the exact private publication. */
    GravityPatchView native_candidate_patch(const Grid&,const FluidState&) const;
    /** Compile/reuse authentic topology rows; evaluate stage-local Dphi using
     * resident Phi and independent datum, with no field download or new solve.
     */
    const GravityRefluxRows& native_candidate_reflux_rows(const amr::AmrFluxTopologyPlan&) const;
    /** Borrow the already fenced Host execution result; lifetime is one frame. */
    const double* native_candidate_reflux_values() const;
    /** Original source and face-work arithmetic, private candidate scope only. */
    void native_candidate_momentum(std::vector<FluidVector>&,const FluidState&,const Grid&,double) const;
    void native_candidate_flux_work(std::vector<FluidVector>&,const std::vector<FluidVector>&,
        const FluidState&,const Grid&,double,int) const;
    struct NativeRzSourceInspectionLease;
    /** Require the exact live source-only inspection and unchanged workspace.
     * Runtime authentication belongs to GravityStage, not this private check.
     */
    void require_native_source_inspection(const NativeRzSourceInspectionView&) const;
    // Only a stack GravityStage scope attaches these; no callback registry,
    // std::function, permanent density cache or public configuration exists.
    mutable NativeRzSourceInspectionSink native_source_inspection_sink_=nullptr;
    mutable void* native_source_inspection_payload_=nullptr;
    mutable bool native_source_inspection_running_=false;
    mutable bool native_source_inspection_invalidated_=false;
    mutable const NativeRzSourceInspectionLease* native_source_inspection_lease_=nullptr;
    struct Workspace;
    Workspace& workspace() const;
    /** Resolve the explicit per-side policy for the requested stage time. */
    arch::elliptic::CompositeBoundary current_boundary(const Workspace&,double) const;
    /** Rebuild the topology-bound operator after a side structure change. */
    void rebuild_boundary(arch::elliptic::CompositeBoundary) const;
    void bind_impl(amr::EllipticMeshBinding,bool native_candidate,
        std::uint64_t maximum_boxes,std::uint64_t maximum_work,double time) const;
    GravityConfig config_;
    mutable std::unique_ptr<Workspace> work_;
    mutable std::shared_ptr<GravityExecution> execution_;
    // Internal transaction observer only. It never changes force/work kernels,
    // field scope, the Poisson tolerance or the public RZ capability gate.
    mutable bool host_consumption_requested_ = false;
    mutable double host_consumption_dt_ = 0.;
    // Immutable callback selection captured at construction and kept live for
    // every run; the referenced config/species outlive the simulation scope.
    arch::boundary::GravityBoundaryFunction user_callback_;
    const SimConfig* user_config_=nullptr;
    const SpeciesManager* user_species_=nullptr;
};
}
