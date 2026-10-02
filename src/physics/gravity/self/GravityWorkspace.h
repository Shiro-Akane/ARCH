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

#include <unordered_map>

#include "amr/elliptic/EllipticMeshAdapter.h"
#include "numerics/multigrid/CompositeMultigrid.h"
#include "physics/gravity/GravityExecution.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/self/SelfGravity.h"

namespace Physical::Gravity {
struct SelfGravity::Workspace {
    using Vector=arch::multigrid::Vector;
    template<class T> using Array=arch::multigrid::Array<T>;
    amr::EllipticMeshBinding binding;
    std::shared_ptr<GravityExecution> execution;
    // Explicit per-side policy (dirichlet/neumann/user) resolved for this
    // topology epoch; legacy periodic/isolated kinds carry no such boundary.
    arch::elliptic::CompositeBoundary user_boundary;
    bool explicit_boundary=false;
    arch::multigrid::CompositeMultigrid solver;
    Vector density,rhs,boundary_values,face_gradient,sides,work_sides,g,patch_faces,patch_work_faces,inverse_dt_squared;
    Array<GravityCell> cells;
    Array<const double*> density_pointers;
    arch::multigrid::SparseArray side_gather,work_phi_gather,work_boundary_gather,patch_gather;
    Array<BoundaryTreeNode> nodes;
    Array<BoundaryMoments> moments;
    Array<BoundaryPoint> points;
    Vector volumes;
    std::vector<Array<int>> layers;
    std::vector<int> patch_offsets;
    std::vector<GravityPatchView> patches;
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
    void require() const {
        if(!ready||!validity.matches(source,generation))throw std::logic_error("Self-gravity field is not published for this input");
    }
    /** Return a patch view only for the bound native density allocation. */
    const GravityPatchView& patch(const Grid& grid,const FluidState& state) const {
        require();auto it=lookup.find(&grid);
        if(it==lookup.end()||patches[it->second].density!=state.rho.data())
            throw std::logic_error("Self-gravity patch uses a different density allocation/slot");
        return patches[it->second];
    }
    /** Materialize potential and acceleration lazily for host output. */
    void download() const {
        require();if(downloaded)return;
        auto& e=solver.execution();host_phi=e.download(solver.resident_potential());
        const auto acceleration=e.download(g);const int n=solver.op().size();
        for(int a=0;a<3;++a)host_g[a].assign(acceleration.begin()+a*n,acceleration.begin()+(a+1)*n);
        downloaded=true;
    }
};
}
