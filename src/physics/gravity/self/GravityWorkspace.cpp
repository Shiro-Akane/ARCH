/**
 * @file GravityWorkspace.cpp
 * @brief Initialize and download topology-bound self-gravity workspace fields.
 *
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Initialize and download topology-bound self-gravity workspace fields.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#include "physics/gravity/self/GravityWorkspace.h"

namespace Physical::Gravity {
/** Allocate resident density, face and force fields for one topology epoch. */
SelfGravity::Workspace::Workspace(amr::EllipticMeshBinding value,arch::elliptic::BoundaryKind kind,
    std::shared_ptr<GravityExecution> runner):binding(std::move(value)),execution(std::move(runner)),
    solver(binding.base,binding.cells,kind,execution->numeric()) {
    auto& e=solver.execution();const auto& op=solver.op();const int n=op.size();
    density=e.array<double>(n);rhs=e.array<double>(n);boundary_values=e.array<double>(op.faces().size());
    face_gradient=e.array<double>(op.faces().size());sides=e.array<double>(6*n);g=e.array<double>(3*n);
    inverse_dt_squared=e.array<double>(n);density_pointers=e.array<const double*>(binding.grids.size());
    std::vector<GravityCell> locations;
    for(int i=0;i<n;++i){const auto b=binding.storage[i];GravityCell c{static_cast<int>(b.block),b.offset,{}};
        for(int a=0;a<op.base().dimension;++a)c.width[a]=op.width(i,a);locations.push_back(c);}
    cells=e.upload(locations);volumes=e.upload(op.volumes());
    for(std::size_t b=0;b<binding.grids.size();++b){lookup.emplace(binding.grids[b],b);patch_offsets.push_back(native_size);native_size+=binding.grids[b]->GetTotalSize();}
    patch_faces=e.array<double>(3*native_size);patches.resize(binding.grids.size());
    for(std::size_t b=0;b<patches.size();++b)for(int a=0;a<3;++a)patches[b].faces[a]=patch_faces.data+a*native_size+patch_offsets[b];
    std::vector<std::vector<int>> columns(6*n);std::vector<std::vector<double>> weights(6*n);
    std::vector<BoundaryPoint> boundary_points;
    for(int i=0;i<static_cast<int>(op.faces().size());++i){const auto& f=op.faces()[i];
        for(int c:{f.left,f.right})if(c>=0){const int side=6*c+2*f.axis+(c==f.left?1:0);
            // g_side = -sum_f (A_f * dx_cell / V_cell) * grad_f(Phi).
            columns[side].push_back(i);weights[side].push_back(-f.area*op.width(c,f.axis)/op.volumes()[c]);}
        if(f.boundary_side>=0)boundary_points.push_back({{f.center[0],f.center[1],f.center[2]},i});}
    arch::multigrid::SparseStorage side_rows,patch_rows;
    for(int i=0;i<6*n;++i)side_rows.row(columns[i],weights[i]);side_gather={e,side_rows};
    // One writer per native face, including block boundaries and coarse/fine
    // area averages. A gather avoids CUDA races between adjacent cells.
    std::vector<int> owner(3*native_size,-1);
    for(int i=0;i<n;++i){const auto b=binding.storage[i];const auto& grid=*binding.grids[b.block];const int stride[]{1,grid.stride_y,grid.stride_z};
        for(int a=0;a<grid.dim;++a)for(int s=0;s<2;++s)owner[a*native_size+patch_offsets[b.block]+b.offset+s*stride[a]]=6*i+2*a+s;}
    const double one=1.;for(int i:owner){if(i<0)patch_rows.row({},{});else patch_rows.row({&i,1},{&one,1});}patch_gather={e,patch_rows};
    if(kind==arch::elliptic::BoundaryKind::Dirichlet){GravityBoundary tree(op);nodes=e.upload(tree.nodes());moments=e.array<BoundaryMoments>(nodes.size);
        for(const auto& layer:tree.layers())layers.push_back(e.upload(layer));points=e.upload(boundary_points);}
    e.fill(boundary_values);e.fence();
}
}
