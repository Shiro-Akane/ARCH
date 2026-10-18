/**
 * @file GravityWorkspace.cpp
 * @brief Bind self-gravity data and gather plans to one AMR topology epoch.
 *
 * Workflow:
 * 1. Allocate resident density, source, boundary, potential and force arrays.
 * 2. Upload native leaf positions, physical lengths and conservative volumes;
 *    build one owner for each patch face and each coarse-fine fragment.
 * 3. Separate physical face acceleration from the curved mass-flux work
 *    coefficient, and cache physical-space boundary evaluation points.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "physics/gravity/self/GravityWorkspace.h"

#include "numerics/curvilinear/GridMetrics.h"

namespace Physical::Gravity {
namespace {
/** Build the multigrid hierarchy either from a legacy kind or an explicit
 *  per-side policy; excluded copy elision keeps the solver in place. */
arch::multigrid::CompositeMultigrid make_gravity_solver(const amr::EllipticMeshBinding& binding,
    arch::elliptic::BoundaryKind kind,const arch::elliptic::CompositeBoundary* boundary,
    const std::shared_ptr<GravityExecution>& execution) {
    if(boundary)
        return arch::multigrid::CompositeMultigrid(binding.base,binding.cells,*boundary,execution->numeric());
    return arch::multigrid::CompositeMultigrid(binding.base,binding.cells,kind,execution->numeric());
}
}
namespace {
GridMetrics::GeometryView workspace_chart(const arch::elliptic::CompositePoisson& op) {
    GridMetrics::GeometryView view{};
    view.geometry=op.base().geometry==arch::elliptic::Geometry::Cartesian
        ?GridMetrics::Geometry::Cartesian
        :(op.base().geometry==arch::elliptic::Geometry::Cylindrical
            ?GridMetrics::Geometry::Cylindrical:GridMetrics::Geometry::Spherical);
    view.dim=op.base().dimension;
    return view;
}
}
/** Describe the cell's physical lengths using its actual radius and theta. */
GravityCell gravity_cell_geometry(amr::EllipticCellBinding binding,
    const arch::elliptic::CompositePoisson& op,int cell) {
    if(cell<0 || cell>=op.size() || binding.offset<0)
        throw std::invalid_argument("Invalid gravity cell geometry descriptor");
    GravityCell result{static_cast<int>(binding.block),binding.offset,{}};
    const auto chart=workspace_chart(op);const auto native=op.center(cell);
    for(int axis=0;axis<op.base().dimension;++axis)
        result.width[axis]=GridMetrics::PhysicalSpacing(chart.geometry,chart.dim,axis,
                op.width(cell,0),op.width(cell,1),op.width(cell,2),native[0],native[1]);
    return result;
}
/** Bind a boundary observer to the same chart.
 * This is an observation point only, never a point-mass source approximation.
 */
BoundaryPoint gravity_boundary_point(const arch::elliptic::CompositePoisson& op,int face) {
    if(face<0 || face>=static_cast<int>(op.faces().size()) || op.faces()[face].boundary_side<0)
        throw std::invalid_argument("Invalid gravity boundary geometry descriptor");
    const auto chart=workspace_chart(op);
    const auto point=GridMetrics::PhysicalPosition(chart.geometry,chart.dim,op.faces()[face].center);
    return {{point[0],point[1],point[2]},face};
}
/** Build the original native face acceleration/work maps, without executing
 * a boundary kernel or publishing a field. Retain the same ordinary assembly
 * order and coefficients for the operator's actual geometry.
 */
GravityFaceRows gravity_face_rows(const arch::elliptic::CompositePoisson& op) {
    const int n=op.size();
    const bool curved=op.base().geometry!=arch::elliptic::Geometry::Cartesian;
    std::vector<std::vector<int>> columns(6*n),work_phi_columns(6*n),work_boundary_columns(6*n);
    std::vector<std::vector<double>> weights(6*n),work_phi_weights(6*n),work_boundary_weights(6*n);
    std::vector<BoundaryPoint> boundary_points;
    for(int i=0;i<static_cast<int>(op.faces().size());++i){const auto& f=op.faces()[i];
        for(int c:{f.left,f.right})if(c>=0){const int side=6*c+2*f.axis+(c==f.left?1:0);
            // Momentum consumes physical g=-grad(Phi). Cartesian keeps its
            // established area-averaged face behavior at coarse/fine sides.
            const double weighted=-f.area*op.width(c,f.axis)/op.volumes()[c];
            columns[side].push_back(i);
            weights[side].push_back(curved?-f.area:weighted);
            if(curved) {
                // Delta E_i = -dt/V_i sum_f A_f F_out,f (Phi_f-Phi_i).
                // gravity_flux_work multiplies each side by dt/2, so the
                // low/high coefficient is respectively +/-2*A_f/V_i.
                const double factor=(c==f.left?-2.:2.)*f.area/op.volumes()[c];
                work_phi_columns[side].push_back(c);
                work_phi_weights[side].push_back(-factor);
                for(std::size_t k=0;k<f.value_samples.size();++k) {
                    work_phi_columns[side].push_back(f.value_samples[k]);
                    work_phi_weights[side].push_back(factor*f.value_coefficients[k]);
                }
                if(f.value_boundary_coefficient!=0.) {
                    work_boundary_columns[side].push_back(i);
                    work_boundary_weights[side].push_back(factor*f.value_boundary_coefficient);
                }
            }}
        if(f.boundary_side>=0)
            boundary_points.push_back(gravity_boundary_point(op,i));
    }
    // Multiple refined fragments share one coarse native face. A physical
    // acceleration is its area-weighted normal gradient, not the sum of
    // fragment gradients; the work rows above retain their own A_f/V_i.
    if(curved)for(auto& side:weights) {
        double area=0.;for(double weight:side)area-=weight;
        if(area>0.)for(double& weight:side)weight/=area;
    }
    GravityFaceRows result;
    for(int i=0;i<6*n;++i) {
        result.acceleration.row(columns[i],weights[i]);
        if(curved) {
            result.potential_work.row(work_phi_columns[i],work_phi_weights[i]);
            result.boundary_work.row(work_boundary_columns[i],work_boundary_weights[i]);
        }
    }
    result.observers=std::move(boundary_points);
    return result;
}

/** Preserve compatible work as cell-side data, independently of face ownership.
 * Workflow: validate padded patch offsets; assign one unique destination for
 * each actual cell/axis/side; leave unused padded rows empty; upload through the
 * original RowsWork backend. Formula remains w=+/-2*A/V*(Phi_f-Phi_cell).
 */
arch::multigrid::SparseStorage gravity_patch_work_rows(
    const amr::EllipticMeshBinding& binding,const std::vector<int>& offsets,int native_size) {
    if(native_size<=0||native_size>std::numeric_limits<int>::max()/6
        ||binding.cells.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()/6)
        ||binding.cells.size()!=binding.storage.size()||offsets.size()!=binding.grids.size())
        throw std::invalid_argument("Gravity patch work layout has invalid extents");
    int extent=0;
    for(std::size_t b=0;b<binding.grids.size();++b) {
        const auto* grid=binding.grids[b];
        if(!grid||grid->dim<1||grid->dim>3||offsets[b]!=extent
            ||grid->GetTotalSize()<=0||grid->GetTotalSize()>native_size-extent)
            throw std::invalid_argument("Gravity patch work layout has invalid patch offsets");
        extent+=grid->GetTotalSize();
    }
    if(extent!=native_size)throw std::invalid_argument("Gravity patch work padded extent differs");
    std::vector<int> owner(6*native_size,-1);
    for(std::size_t c=0;c<binding.storage.size();++c) {
        const auto location=binding.storage[c];
        if(location.block>=binding.grids.size()||location.offset<0
            ||location.offset>=binding.grids[location.block]->GetTotalSize())
            throw std::invalid_argument("Gravity patch work cell storage is invalid");
        const auto& grid=*binding.grids[location.block];
        for(int axis=0;axis<grid.dim;++axis)for(int side=0;side<2;++side) {
            const int row=(2*axis+side)*native_size+offsets[location.block]+location.offset;
            if(owner[row]>=0)throw std::invalid_argument("Gravity patch work cell-side is duplicated");
            owner[row]=6*static_cast<int>(c)+2*axis+side;
        }
    }
    arch::multigrid::SparseStorage rows;const double one=1.;
    for(int source:owner) {
        if(source<0)rows.row({},{});else rows.row({&source,1},{&one,1});
    }
    return rows;
}

/** Allocate resident density, face and force fields for one topology epoch. */
SelfGravity::Workspace::Workspace(amr::EllipticMeshBinding value,arch::elliptic::BoundaryKind kind,
    arch::elliptic::CompositeBoundary boundary,
    std::shared_ptr<GravityExecution> runner):binding(std::move(value)),execution(std::move(runner)),
    user_boundary(std::move(boundary)),explicit_boundary(kind==arch::elliptic::BoundaryKind::User),
    solver(make_gravity_solver(binding,kind,explicit_boundary?&user_boundary:nullptr,execution)) {
    auto& e=solver.execution();const auto& op=solver.op();const int n=op.size();
    density=e.array<double>(n);rhs=e.array<double>(n);boundary_values=e.array<double>(op.faces().size());
    face_gradient=e.array<double>(op.faces().size());sides=e.array<double>(6*n);g=e.array<double>(3*n);
    const bool curved=op.base().geometry!=arch::elliptic::Geometry::Cartesian;
    if(curved)work_sides=e.array<double>(6*n);else work_sides=sides;
    inverse_dt_squared=e.array<double>(n);density_pointers=e.array<const double*>(binding.grids.size());
    std::vector<GravityCell> locations;
    for(int i=0;i<n;++i)
        locations.push_back(gravity_cell_geometry(binding.storage[i],op,i));
    cells=e.upload(locations);volumes=e.upload(op.volumes());
    const int layout_limit=std::numeric_limits<int>::max()/(curved?6:3);
    for(std::size_t b=0;b<binding.grids.size();++b) {
        const auto* grid=binding.grids[b];
        if(!grid||grid->GetTotalSize()<=0||grid->GetTotalSize()>layout_limit-native_size)
            throw std::overflow_error("Gravity padded patch layout exceeds its array range");
        lookup.emplace(grid,b);patch_offsets.push_back(native_size);native_size+=grid->GetTotalSize();
    }
    patch_faces=e.array<double>(3*native_size);
    if(curved)patch_work=e.array<double>(6*native_size);
    patches.resize(binding.grids.size());
    for(std::size_t b=0;b<patches.size();++b)for(int a=0;a<3;++a){
        patches[b].faces[a]=patch_faces.data+a*native_size+patch_offsets[b];
        const int stride=a==0?1:a==1?binding.grids[b]->stride_y:binding.grids[b]->stride_z;
        // Cartesian retains the exact original shared face arrays, including
        // the original upper-face offset. Curved work owns both cell sides.
        patches[b].work_low[a]=curved?patch_work.data+2*a*native_size+patch_offsets[b]:patches[b].faces[a];
        patches[b].work_high[a]=curved?patch_work.data+(2*a+1)*native_size+patch_offsets[b]:patches[b].faces[a]+stride;
    }
    const auto face_rows=gravity_face_rows(op);
    side_gather={e,face_rows.acceleration};
    if(curved) {
        work_phi_gather={e,face_rows.potential_work};
        work_boundary_gather={e,face_rows.boundary_work};
    }
    arch::multigrid::SparseStorage patch_rows;
    // One writer per native face, including block boundaries and coarse/fine
    // area averages. A gather avoids CUDA races between adjacent cells.
    std::vector<int> owner(3*native_size,-1);
    for(int i=0;i<n;++i){const auto b=binding.storage[i];const auto& grid=*binding.grids[b.block];const int stride[]{1,grid.stride_y,grid.stride_z};
        for(int a=0;a<grid.dim;++a)for(int s=0;s<2;++s)owner[a*native_size+patch_offsets[b.block]+b.offset+s*stride[a]]=6*i+2*a+s;}
    const double one=1.;for(int i:owner){if(i<0)patch_rows.row({},{});else patch_rows.row({&i,1},{&one,1});}patch_gather={e,patch_rows};
    if(curved)patch_work_gather={e,gravity_patch_work_rows(binding,patch_offsets,native_size)};
    if(kind==arch::elliptic::BoundaryKind::Dirichlet ||
       kind==arch::elliptic::BoundaryKind::CurvilinearIsolated){GravityBoundary tree(op);nodes=e.upload(tree.nodes());moments=e.array<BoundaryMoments>(nodes.size);
        for(const auto& layer:tree.layers())layers.push_back(e.upload(layer));points=e.upload(face_rows.observers);}
    if(explicit_boundary) {
        // One owner per physical face; the datum c is scattered onto this
        // O(surface) plan, so no full boundary vector crosses the backend.
        for(std::size_t i=0;i<op.faces().size();++i) {
            const auto& face=op.faces()[i];
            if(face.boundary_side<0) continue;
            boundary_faces.push_back(static_cast<int>(i));
            boundary_native.push_back(face.center);
        }
        boundary_host_values.assign(boundary_faces.size(),0.);
        boundary_face_index=e.array<int>(static_cast<int>(boundary_faces.size()));
        boundary_face_values=e.array<double>(static_cast<int>(boundary_faces.size()));
        if(!boundary_faces.empty())
            e.copy(boundary_face_index.data,boundary_faces.data(),sizeof(int)*boundary_faces.size(),
                arch::multigrid::Transfer::Upload);
    }
    e.fill(boundary_values);e.fence();
}
}
