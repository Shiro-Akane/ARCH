/**
 * @file GravityWorkspace.h
 * @brief Own bound geometry, resident arrays and validity for one topology epoch.
 *
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Own bound geometry, resident arrays and validity for one topology epoch.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "amr/elliptic/EllipticMeshAdapter.h"
#include "amr/flux/AmrFluxPlan.h"
#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/GravityExecution.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/self/SelfGravity.h"

namespace Physical::Gravity {
/** Original workspace geometry producers. The caller owns topology/storage
 * validation; these functions do not publish a field or qualify RZ runtime.
 */
GravityCell gravity_cell_geometry(amr::EllipticCellBinding,
    const arch::elliptic::CompositePoisson&,int cell);
BoundaryPoint gravity_boundary_point(const arch::elliptic::CompositePoisson&,int face);
/** Original face-row plan, owned by the workspace geometry producer.
 * Acceleration rows act on increasing-coordinate face gradients.
 * Potential/boundary work rows keep signed 2*A/V*(Phi_face-Phi_cell).
 * This plan does not qualify RZ source, solve or publication.
 */
struct GravityFaceRows {
    arch::multigrid::SparseStorage acceleration,potential_work,boundary_work;
    std::vector<BoundaryPoint> observers;
};
GravityFaceRows gravity_face_rows(const arch::elliptic::CompositePoisson&);
/** Gather each real cell-side into its own padded patch slot.
 * Output row (2*axis+side)*native_size+patch_offset+cell_offset selects
 * input 6*cell+2*axis+side. A shared physical face never aliases work rows.
 * This topology-only plan changes storage, not the compatible-work formula.
 */
arch::multigrid::SparseStorage gravity_patch_work_rows(
    const amr::EllipticMeshBinding&,const std::vector<int>& patch_offsets,int native_size);
/** One topology-only Energy registration row, indexed by the ORIGINAL route
 * operation. No stage potential, dt, RK coefficient or register metric is owned.
 */
struct GravityRefluxRowIdentity {
    amr::AmrFluxRouteKey route;
    amr::TopologyEpoch epoch;
    std::uint64_t topology_fingerprint=0,route_fingerprint=0;
    std::size_t operation_index=0;
    int source_flux_offset=-1,coarse_cell_index=-1;
    amr::AmrTransferOperation operation;
};
/** Geometry-only paired work rows. Row r evaluates
 * Dphi=sum_f(A_f/A_source)*Phi_value_row_f-Phi_destination_coarse,
 * plus an INDEPENDENT datum row acting on the same face-indexed boundary data.
 * The existing register alone applies +/-A_source/A_coarse, stage weight and dt.
 */
struct GravityRefluxRows {
    std::vector<GravityRefluxRowIdentity> identity;
    arch::multigrid::SparseStorage potential,boundary;
};
/** Authenticate the actual Native RZ binding/operator/route geometry, prove
 * complete dyadic face coverage, and compile immutable paired rows. Workflow:
 * validate root+UID/epoch+all storage; join integer CF cell incidences; check
 * actual fragment areas; append the target coarse potential counterterm.
 * This does not attach a consumer, solve, cache stage Phi or qualify physics.
 */
GravityRefluxRows gravity_reflux_rows(const amr::EllipticMeshBinding&,
    const arch::elliptic::CompositePoisson&,const amr::AmrFluxTopologyPlan&);

struct SelfGravity::Workspace {
    using Vector=arch::multigrid::Vector;
    template<class T> using Array=arch::multigrid::Array<T>;
    // Topology-only paired rows are reused; stage values always follow the
    // exact solved generation. No whole-field download or persistent U copy.
    const amr::AmrFluxTopologyPlan* reflux_topology=nullptr;
    GravityRefluxRows reflux_rows;
    arch::multigrid::SparseArray reflux_phi_rows,reflux_datum_rows;
    Vector reflux_values;
    std::uint64_t reflux_topology_fingerprint=0,reflux_field_generation=0;
    amr::TopologyEpoch reflux_epoch{};
    amr::EllipticMeshBinding binding;
    std::shared_ptr<GravityExecution> execution;
    // Explicit per-side policy (dirichlet/neumann/user) resolved for this
    // topology epoch; legacy periodic/isolated kinds carry no such boundary.
    arch::elliptic::CompositeBoundary user_boundary;
    bool explicit_boundary=false;
    arch::multigrid::CompositeMultigrid solver;
    Vector density,rhs,boundary_values,face_gradient,sides,work_sides,g,patch_faces,patch_work,inverse_dt_squared;
    Array<GravityCell> cells;
    Array<const double*> density_pointers;
    arch::multigrid::SparseArray side_gather,work_phi_gather,work_boundary_gather,patch_gather,patch_work_gather;
    Array<BoundaryTreeNode> nodes;
    Array<BoundaryMoments> moments;
    Array<BoundaryPoint> points;
    Vector volumes;
    std::vector<Array<int>> layers;
    std::vector<int> patch_offsets;
    std::vector<GravityPatchView> patches;
    // Allocated only by an explicit Host transaction. Each patch records one
    // momentum consumer and one conservative work consumer per active axis.
    // Atomics make duplicate consumption detectable across patch executors.
    std::unique_ptr<std::atomic<unsigned>[]> host_consumption;
    bool observe_host_consumption = false;
    double host_consumption_dt = 0.;
    // O(surface) physical-face scatter plan for the position/time datum c.
    std::vector<int> boundary_faces;
    std::vector<std::array<double,3>> boundary_native;
    Array<int> boundary_face_index;
    Array<double> boundary_face_values;
    std::vector<double> boundary_host_values;
    // Lazily allocated surface observer; ordinary runs never request it.
    arch::multigrid::SparseArray boundary_sample_rows;
    Array<int> boundary_sample_faces,boundary_sample_signs;
    Vector boundary_sample_coefficients,boundary_sample_values;
    std::vector<GravityBoundaryFaceState> boundary_sample_identity;
    bool boundary_samples_bound=false;
    std::unordered_map<const Grid*,std::size_t> lookup;
    int native_size=0;
    // Stage/restart time at which the current side structure was sampled. An
    // execution swap rebinds on the same topology and must keep this time
    // instead of silently falling back to t=0.
    double boundary_time=0.;
    GravityFieldValidity validity;
    GravitySolveIdentity source;
    GravityFieldScope scope=GravityFieldScope::ExistingPhysics;
    std::optional<GravityFieldPurpose> purpose;
    const RuntimeGravitySourceLease* runtime_lease=nullptr; // Borrowed; field invalidation precedes issuer destruction.
    std::unique_ptr<GravityBoundary> ring_source;
    RingBoundaryControl ring_limits;
    RingBoundaryEvaluation ring;
    RingRhsAssessment ring_assessment;
    std::uint64_t generation=0;
    bool ready=false;
    mutable bool downloaded=false;
    mutable std::vector<double> host_phi;
    mutable std::array<std::vector<double>,3> host_g;
    arch::multigrid::SolveReport report;
    Timings timings;
    double mean=0.,max_density=0.,max_acceleration_ratio=0.;
    Workspace(amr::EllipticMeshBinding,arch::elliptic::BoundaryKind,
        arch::elliptic::CompositeBoundary,std::shared_ptr<GravityExecution>);
    /** Reject access unless the workspace holds a matching completed gravity field. */
    void require(GravityFieldScope requested=GravityFieldScope::ExistingPhysics) const {
        if(!ready||!validity.matches(source,generation,requested))throw std::logic_error("Self-gravity field is not published for this input");
    }
    /** Require an issued Runtime purpose while retaining the immutable scope.
     * Full domain validation occurs before/after solve and joined acceptance;
     * this borrowed per-patch fence adds no domain scan to worker consumption.
     */
    void require_runtime_purpose(GravityFieldPurpose expected) const {
        require(scope);
        if(!runtime_lease||purpose!=expected
            ||!validity.matches_runtime(source,generation,scope,expected,runtime_lease->generation()))
            throw std::logic_error("Gravity field lacks its actual Runtime purpose lease");
        runtime_lease->require(expected);
    }
    /** Return a patch view only for the bound native density allocation. */
    const GravityPatchView& patch(const Grid& grid,const FluidState& state) const {
        require();auto it=lookup.find(&grid);
        if(it==lookup.end()||patches[it->second].density!=state.rho.data())
            throw std::logic_error("Self-gravity patch uses a different density allocation/slot");
        return patches[it->second];
    }
    /** Borrow the exact prepared RZ patch without changing producer scope.
     * Workflow: require original field validity; resolve the original Grid pointer;
     * require its actual resident density allocation; return immutable views.
     */
    const GravityPatchView& native_patch(const Grid& grid,const FluidState& state) const {
        require(scope);
        if(solver.execution().device()
            ||solver.op().base().semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz)
            throw std::logic_error("Prepared RZ patch requires its actual Host chart");
        const auto found=lookup.find(&grid);
        if(found==lookup.end()||patches[found->second].density!=state.rho.data())
            throw std::logic_error("Native private patch changed its original density allocation");
        return patches[found->second];
    }
    /** Cache genuine geometry rows and evaluate same-stage Dphi once. */
    const GravityRefluxRows& prepare_native_reflux(const amr::AmrFluxTopologyPlan&);
    /** Allocate/reset the bounded per-patch receipt before any fluid producer runs. */
    void begin_host_consumption(double step_dt) {
        if(solver.execution().device())
            throw std::logic_error("Host gravity receipt cannot observe Device consumers");
        if(!host_consumption)
            host_consumption=std::make_unique<std::atomic<unsigned>[]>(patches.size());
        for(std::size_t b=0;b<patches.size();++b)
            host_consumption[b].store(0,std::memory_order_relaxed);
        host_consumption_dt=step_dt;
        observe_host_consumption=true;
    }
    /** A receipt certifies the prepared interval, never merely a callback visit. */
    void require_host_consumer(const Grid& grid,unsigned bit,double dt) const {
        if(!observe_host_consumption)return;
        const auto found=lookup.find(&grid);
        if(found==lookup.end()||!host_consumption||!bit
            ||!std::isfinite(dt)||!(dt>0.)||dt!=host_consumption_dt)
            throw std::logic_error("Gravity Host consumer interval/frame differs from preparation");
        if(host_consumption[found->second].load(std::memory_order_relaxed)&bit)
            throw std::logic_error("Gravity patch source/work consumed twice");
    }
    /** Record completed source algebra, rejecting duplicate patch/axis use. */
    void consume_host_patch(const Grid& grid,unsigned bit) {
        if(!observe_host_consumption)return;
        const auto found=lookup.find(&grid);
        if(found==lookup.end()||!host_consumption||!bit)
            throw std::logic_error("Gravity consumer is outside the prepared Host patch frame");
        const auto previous=host_consumption[found->second].fetch_or(bit,std::memory_order_relaxed);
        if(previous&bit)
            throw std::logic_error("Gravity patch source/work consumed twice");
    }
    /** Match the actual field publication and all completed physical source/work calls. */
    void require_host_consumption(const GravitySolveIdentity& expected) const {
        require();
        if(!observe_host_consumption||!host_consumption||source!=expected
            ||solver.execution().device())
            throw std::logic_error("Gravity Host receipt does not match the prepared publication");
        const unsigned complete=(1u<<(solver.op().base().dimension+1))-1u;
        for(std::size_t b=0;b<patches.size();++b)
            if(host_consumption[b].load(std::memory_order_relaxed)!=complete)
                throw std::logic_error("Gravity Host receipt has an unconsumed patch force/work");
    }
    /** Materialize potential and acceleration lazily for host output. */
    void download(GravityFieldScope requested=GravityFieldScope::ExistingPhysics) const {
        require(requested);if(downloaded)return;
        auto& e=solver.execution();host_phi=e.download(solver.resident_potential());
        const auto acceleration=e.download(g);const int n=solver.op().size();
        for(int a=0;a<3;++a)host_g[a].assign(acceleration.begin()+a*n,acceleration.begin()+(a+1)*n);
        downloaded=true;
    }
};
}
