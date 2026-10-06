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
#include <memory>

#include "numerics/elliptic/CompositePoisson.h"
#include "physics/boundary/BoundaryTypes.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/gravity/IGravityPolicy.h"
#include "physics/gravity/self/GravityBoundaryDiagnostics.h"

namespace amr { struct EllipticMeshBinding; }
namespace arch::state { struct CompletionToken; }
namespace arch::multigrid { struct SolveReport; }
namespace Physical::Gravity {
struct GravitySolveRequest;
struct GravitySolveIdentity;
struct RingRhsAssessment;
struct RingBoundaryBudgetProposal;
class GravityExecution;
struct GravityPatchView;
class SelfGravity final : public IGravityPolicy {
public:
    explicit SelfGravity(GravityConfig config);
    ~SelfGravity();
    /** Bind using the actual stage/restart time; public RZ remains gated. */
    void bind(amr::EllipticMeshBinding binding, double time = 0.) const;
    // Internal CPU numerical verification only. Native candidates cannot be
    // read by normal physical patch/output consumers; public bind stays gated.
    void bind_native_rz_candidate(amr::EllipticMeshBinding,
        std::uint64_t maximum_boxes_per_leaf,std::uint64_t maximum_work) const;
    const RingRhsAssessment& native_rz_assessment() const;
    const std::vector<double>& native_rz_potential() const;
    const std::array<std::vector<double>,3>& native_rz_acceleration() const;
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
