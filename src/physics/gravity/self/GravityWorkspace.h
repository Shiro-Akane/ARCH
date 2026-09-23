#pragma once
#include "physics/gravity/GravityExecution.h"
#include "physics/gravity/GravitySolveTypes.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "numerics/multigrid/HostCompositeMG.h"
#include <unordered_map>
namespace Physical::Gravity {
struct SelfGravity::Workspace {
    using Vector=arch::multigrid::Vector;
    template<class T> using Array=arch::multigrid::Array<T>;
    amr::EllipticMeshBinding binding;
    std::shared_ptr<GravityExecution> execution;
    arch::multigrid::HostCompositeMG solver;
    Vector density,rhs,boundary_values,face_gradient,sides,g,patch_faces,inverse_dt_squared;
    Array<GravityCell> cells;
    Array<const double*> density_pointers;
    arch::multigrid::SparseArray side_gather,patch_gather;
    Array<BoundaryTreeNode> nodes;
    Array<BoundaryMoments> moments;
    Array<BoundaryPoint> points;
    Vector volumes;
    std::vector<Array<int>> layers;
    std::vector<int> patch_offsets;
    std::vector<GravityPatchView> patches;
    std::unordered_map<const Grid*,std::size_t> lookup;
    int native_size=0;
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
    Workspace(amr::EllipticMeshBinding,arch::elliptic::BoundaryKind,std::shared_ptr<GravityExecution>);
    void require() const {
        if(!ready||!validity.matches(source,generation))throw std::logic_error("Self-gravity field is not published for this input");
    }
    const GravityPatchView& patch(const Grid& grid,const FluidState& state) const {
        require();auto it=lookup.find(&grid);
        if(it==lookup.end()||patches[it->second].density!=state.rho.data())
            throw std::logic_error("Self-gravity patch uses a different density allocation/slot");
        return patches[it->second];
    }
    void download() const {
        require();if(downloaded)return;
        auto& e=solver.execution();host_phi=e.download(solver.resident_potential());
        const auto acceleration=e.download(g);const int n=solver.op().size();
        for(int a=0;a<3;++a)host_g[a].assign(acceleration.begin()+a*n,acceleration.begin()+(a+1)*n);
        downloaded=true;
    }
};
}
