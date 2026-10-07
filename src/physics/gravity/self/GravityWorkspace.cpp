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
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "physics/gravity/self/GravityWorkspace.h"

#include "grid/GridMetrics.h"

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
    view.dim=op.base().dimension;view.semantics=op.base().semantics;
    return view;
}
}
/** Describe the accepted native cell's physical lengths in the bound chart.
 * RZ uses dr/dz; Existing retains its exact radius/theta calculation path.
 */
GravityCell gravity_cell_geometry(amr::EllipticCellBinding binding,
    const arch::elliptic::CompositePoisson& op,int cell) {
    if(cell<0 || cell>=op.size() || binding.offset<0)
        throw std::invalid_argument("Invalid gravity cell geometry descriptor");
    GravityCell result{static_cast<int>(binding.block),binding.offset,{}};
    const auto chart=workspace_chart(op);const auto native=op.center(cell);
    for(int axis=0;axis<op.base().dimension;++axis)
        result.width[axis]=chart.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
            ?GridMetrics::Rz::PhysicalSpacing(axis,op.width(cell,0),op.width(cell,1))
            :GridMetrics::PhysicalSpacing(chart.geometry,chart.dim,axis,
                op.width(cell,0),op.width(cell,1),op.width(cell,2),native[0],native[1]);
    return result;
}
/** Bind a boundary observer to the same chart; RZ is (r,0,z), not (r,phi).
 * This is an observation point only, never a point-mass source approximation.
 */
BoundaryPoint gravity_boundary_point(const arch::elliptic::CompositePoisson& op,int face) {
    if(face<0 || face>=static_cast<int>(op.faces().size()) || op.faces()[face].boundary_side<0)
        throw std::invalid_argument("Invalid gravity boundary geometry descriptor");
    const auto point=GridMetrics::PhysicalPosition(workspace_chart(op),op.faces()[face].center);
    return {{point[0],point[1],point[2]},face};
}
/** Build the original native face acceleration/work maps, without executing
 * a boundary kernel or publishing a field. RZ consumes the operator's full-ring
 * areas/volumes; Existing retains the same assembly order and coefficients.
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

namespace {
/** Require one actual native root; equal local coordinates alone do not bind it. */
void require_reflux_root(const Grid& grid,const arch::elliptic::CartesianMesh& base,
    const amr::AmrEndpoint& endpoint) {
    const auto view=GridMetrics::make_geometry_view(grid,GridMetrics::GeometrySemantics::AxisymmetricRz);
    const auto& id=grid.dyadic_identity;
    if(!id.bound||!GridMetrics::matches_identity(view)||endpoint.logical.dimension!=2
        ||endpoint.logical.level!=id.level||endpoint.logical.logical_x1!=id.logical[0]
        ||endpoint.logical.logical_x2!=id.logical[1]||endpoint.logical.logical_x3!=0)
        throw std::invalid_argument("Gravity reflux endpoint is not an authenticated native block");
    for(int axis=0;axis<2;++axis)
        if(id.root_blocks[axis]*GridMetrics::dyadic_identity_detail::axis_cells(axis)!=base.cells[axis]
            ||!GridMetrics::dyadic_identity_detail::same_binary64(id.root_lower[axis],base.origin[axis])
            ||!GridMetrics::dyadic_identity_detail::same_binary64(id.root_upper[axis],base.root_upper[axis]))
            throw std::invalid_argument("Gravity reflux root differs from the actual elliptic domain");
}
/** Resolve an ORIGINAL one-cell route box to padded native storage. Face boxes
 * permit the upper normal scratch face; cell boxes must be actual interiors. */
int reflux_box_offset(const Grid& grid,const amr::LogicalAmrBox& box,int axis,bool face) {
    if(box.extent!=std::array<std::uint32_t,3>{1,1,1}||box.first[2]!=0)
        throw std::invalid_argument("Gravity reflux box is not one native RZ cell/face");
    const int count[]{amr::BLOCK_NX,amr::BLOCK_NY};
    for(int a=0;a<2;++a)
        if(box.first[a]<0||box.first[a]>=count[a]+int(face&&a==axis))
            throw std::invalid_argument("Gravity reflux box lies outside its actual native scope");
    return grid.GetIndex(grid.Is()+box.first[0],grid.Js()+box.first[1],grid.Ks());
}
/** Validate exact source/operator snapshots before any row allocation/publication.
 * Storage is a bijection over the actual active interiors, keyed by pool ID and
 * padded offset; no floating center lookup or borrowed stage field is used. */
struct RefluxBindingIndex {
    std::map<amr::AmrEndpoint,std::size_t> block;
    std::map<std::pair<int,int>,int> cell;
    std::vector<std::array<int,2>> local;
    std::vector<int> pool_id;
};
RefluxBindingIndex reflux_binding_index(const amr::EllipticMeshBinding& binding,
    const arch::elliptic::CompositePoisson& op,const amr::AmrFluxTopologyPlan& topology) {
    amr::validate_amr_flux_topology_plan(topology);
    const auto& base=binding.base;const auto& actual=op.base();
    if(topology.dimension!=2||topology.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||base.dimension!=2||base.geometry!=arch::elliptic::Geometry::Cylindrical
        ||base.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz||!base.native_canonical_domain
        ||actual.dimension!=base.dimension||actual.geometry!=base.geometry||actual.semantics!=base.semantics
        ||actual.native_canonical_domain!=base.native_canonical_domain||actual.cells!=base.cells
        ||actual.origin!=base.origin||actual.root_upper!=base.root_upper||actual.spacing!=base.spacing
        ||binding.cells!=op.cells()||binding.storage.size()!=binding.cells.size()
        ||binding.grids.size()!=binding.handles.size()||binding.grids.size()!=topology.active_endpoints.size()
        ||topology.native_grids.size()!=binding.grids.size()||binding.periodic[0]
        ||binding.periodic[2])
        throw std::invalid_argument("Gravity reflux requires one actual canonical native RZ binding");
    RefluxBindingIndex result;result.local.resize(binding.cells.size());result.pool_id.resize(binding.grids.size());
    for(int axis=0;axis<3;++axis)
        if(!GridMetrics::dyadic_identity_detail::same_binary64(actual.origin[axis],base.origin[axis])
            ||!GridMetrics::dyadic_identity_detail::same_binary64(actual.root_upper[axis],base.root_upper[axis])
            ||!GridMetrics::dyadic_identity_detail::same_binary64(actual.spacing[axis],base.spacing[axis]))
            throw std::invalid_argument("Gravity reflux operator root identity differs");
    for(std::size_t b=0;b<binding.grids.size();++b) {
        if(!binding.grids[b]||!amr::is_valid(binding.handles[b])||binding.handles[b].epoch!=topology.epoch)
            throw std::invalid_argument("Gravity reflux binding UID/epoch is invalid");
        const auto found=std::find_if(topology.active_endpoints.begin(),topology.active_endpoints.end(),
            [&](const auto& endpoint){return endpoint.handle==binding.handles[b];});
        if(found==topology.active_endpoints.end()||!result.block.emplace(*found,b).second)
            throw std::invalid_argument("Gravity reflux binding is not an exact active endpoint bijection");
        const int id=topology.pool_lowering.at(*found);
        const auto& grid=*binding.grids[b];
        if(grid.Ie()-grid.Is()!=amr::BLOCK_NX||grid.Je()-grid.Js()!=amr::BLOCK_NY)
            throw std::invalid_argument("Gravity reflux active block shape differs");
        require_reflux_root(grid,base,*found);
        if(grid.dyadic_identity.periodic_axial!=binding.periodic[1]
            ||!amr::flux_plan_detail::same_grid_contract(grid,amr::angular_native_grid(topology,*found),topology.semantics))
            throw std::invalid_argument("Gravity reflux native grid snapshot/rules changed");
        result.pool_id[b]=id;
    }
    std::vector<int> counts(binding.grids.size());
    for(int c=0;c<op.size();++c) {
        const auto native=binding.storage[c];
        if(native.block>=binding.grids.size())throw std::invalid_argument("Gravity reflux storage block is invalid");
        const auto& grid=*binding.grids[native.block];
        const int j=native.offset/grid.stride_y,i=native.offset%grid.stride_y;
        if(i<grid.Is()||i>=grid.Ie()||j<grid.Js()||j>=grid.Je()
            ||native.offset!=grid.GetIndex(i,j,grid.Ks()))
            throw std::invalid_argument("Gravity reflux storage is not an actual active interior");
        const auto& id=grid.dyadic_identity;const auto& logical=binding.cells[c];
        if(logical.level!=id.level||logical.index[0]!=int(id.logical[0])*amr::BLOCK_NX+i-grid.Is()
            ||logical.index[1]!=int(id.logical[1])*amr::BLOCK_NY+j-grid.Js()||logical.index[2]!=0)
            throw std::invalid_argument("Gravity reflux storage/logical mapping differs");
        const int pool_id=result.pool_id[native.block];
        if(!result.cell.emplace(std::pair{pool_id,native.offset},c).second)
            throw std::invalid_argument("Gravity reflux storage contains duplicate cells");
        result.local[c]={i,j};++counts[native.block];
    }
    for(int count:counts)if(count!=amr::BLOCK_NX*amr::BLOCK_NY)
        throw std::invalid_argument("Gravity reflux binding omits actual interior cells");
    return result;
}
/** Match the normal integer endpoints, allowing ONLY the real paired-z image.
 * Tangential CF fragments remain literal fine cells in the configured domain. */
bool reflux_normal_matches(const arch::elliptic::CartesianMesh& base,bool periodic,
    int axis,int fine_level,std::int64_t fine_face,std::int64_t doubled_coarse) {
    if(fine_face==doubled_coarse)return true;
    if(axis!=1||!periodic)return false;
    const auto extent=std::int64_t(base.cells[1])<<fine_level;
    return fine_face-doubled_coarse==extent||fine_face-doubled_coarse==-extent;
}
}
/** Compile topology-only stage-paired AMR energy rows from genuine CF fragments.
 * Workflow: authenticate binding; index incident faces; resolve original Energy
 * operation; prove the full one/two-fragment integer partition; append each
 * actual Phi-value stencil weighted A_fragment/A_source; finally subtract the
 * SAME destination-coarse Phi. Boundary datum columns stay face-indexed.
 * Formula: E_registration=F_E+[Dphi(Phi)+Ddatum(c)]*F_rho. No dt, RK stage
 * weight, route sign, A_source/A_coarse or persistent Phi is inserted here.
 */
GravityRefluxRows gravity_reflux_rows(const amr::EllipticMeshBinding& binding,
    const arch::elliptic::CompositePoisson& op,const amr::AmrFluxTopologyPlan& topology) {
    const auto index=reflux_binding_index(binding,op,topology);
    std::vector<std::vector<int>> incident(op.size());
    for(int f=0;f<int(op.faces().size());++f) {
        const auto& face=op.faces()[f];
        if(face.left>=0&&face.left<op.size())incident[face.left].push_back(f);
        if(face.right>=0&&face.right<op.size())incident[face.right].push_back(f);
    }
    GravityRefluxRows result;
    for(const auto& route:topology.routes)for(std::size_t ordinal=0;ordinal<route.plan.operations.size();++ordinal) {
        const auto& operation=route.plan.operations[ordinal];
        if(operation.field!=amr::AmrField::Energy)continue;
        const int axis=amr::axis_value(operation.axis),tangent=1-axis;
        if(axis<0||axis>=2||operation.component!=-1)
            throw std::invalid_argument("Gravity reflux Energy axis/component is invalid");
        const bool fine=operation.rule==amr::RefinementRule::FineFluxContribution;
        if(!fine&&operation.rule!=amr::RefinementRule::CoarseFluxContribution)
            throw std::invalid_argument("Gravity reflux Energy operation is not a CF route");
        const auto sb=index.block.at(operation.source),db=index.block.at(operation.destination);
        const auto& sg=*binding.grids[sb];const auto& dg=*binding.grids[db];
        const int source_pool=topology.pool_lowering.at(operation.source);
        const int dest_pool=topology.pool_lowering.at(operation.destination);
        const int count=axis==0?amr::BLOCK_NX:amr::BLOCK_NY;
        const int source_normal=operation.source_box.first[axis];
        if(source_normal!=0&&source_normal!=count)
            throw std::invalid_argument("Gravity reflux source is not an actual block face");
        const bool source_high=source_normal==count,dest_high=operation.side==amr::AmrSide::Upper;
        if(source_high!=(fine?!dest_high:dest_high)
            ||operation.destination_box.first[axis]!=(dest_high?count-1:0))
            throw std::invalid_argument("Gravity reflux source/destination orientation differs");
        const int flux_offset=reflux_box_offset(sg,operation.source_box,axis,true);
        auto source_box=operation.source_box;source_box.first[axis]=source_high?count-1:0;
        const int sc=index.cell.at({source_pool,reflux_box_offset(sg,source_box,axis,false)});
        const int dc=index.cell.at({dest_pool,reflux_box_offset(dg,operation.destination_box,axis,false)});
        const auto& coarse=op.cells()[dc];const auto& source=op.cells()[sc];
        if((fine&&source.level!=coarse.level+1)||(!fine&&sc!=dc))
            throw std::invalid_argument("Gravity reflux target is not the actual destination coarse cell");
        const auto sv=GridMetrics::make_geometry_view(sg,topology.semantics);
        const auto dv=GridMetrics::make_geometry_view(dg,topology.semantics);
        const auto sl=index.local[sc],dl=index.local[dc];
        const double source_area=GridMetrics::FaceArea(sv,axis,sl[0],sl[1],sg.Ks(),source_high);
        const double coarse_area=GridMetrics::FaceArea(dv,axis,dl[0],dl[1],dg.Ks(),dest_high);
        if(!std::isfinite(source_area)||!(source_area>0.)||!std::isfinite(coarse_area)||!(coarse_area>0.)
            ||operation.weight!=(fine?source_area/coarse_area:1.))
            throw std::invalid_argument("Gravity reflux original registration metric differs");
        std::array<bool,2> covered{};int fragments=0;
        std::vector<int> phi_columns,boundary_columns;
        std::vector<double> phi_weights,boundary_weights;
        for(int f:incident[sc]) {
            const auto& face=op.faces()[f];
            if(face.axis!=axis||(source_high?face.left!=sc:face.right!=sc))continue;
            const int other=source_high?face.right:face.left;
            if(other<0||other>=op.size())continue;
            const int fc=fine?sc:other;
            if((fine&&other!=dc)||op.cells()[fc].level!=coarse.level+1)continue;
            const auto& finer=op.cells()[fc];
            const auto fine_face=std::int64_t(finer.index[axis])+(fine?source_high:!dest_high);
            const auto coarse_face=std::int64_t(coarse.index[axis])+dest_high;
            const auto segment=std::int64_t(finer.index[tangent])-2LL*coarse.index[tangent];
            if(segment<0||segment>1||covered[segment]
                ||!reflux_normal_matches(op.base(),binding.periodic[1],axis,finer.level,fine_face,2*coarse_face)
                ||!face.native_bounds||face.boundary_side!=-1||!std::isfinite(face.area)||!(face.area>0.)
                ||face.value_samples.size()!=face.value_coefficients.size())
                throw std::invalid_argument("Gravity reflux fragments do not form an exact dyadic partition");
            const double tangent_lower=op.lower(fc,tangent),tangent_upper=op.upper(fc,tangent);
            const auto& fnative=binding.storage[fc];const auto& fg=*binding.grids[fnative.block];
            const auto fl=index.local[fc];const bool fine_high=fine?source_high:!dest_high;
            const auto fv=GridMetrics::make_geometry_view(fg,topology.semantics);
            const double area=GridMetrics::FaceArea(fv,axis,fl[0],fl[1],fg.Ks(),fine_high);
            const double normal_fine=fine_high?op.upper(fc,axis):op.lower(fc,axis);
            const double normal_coarse=dest_high?op.upper(dc,axis):op.lower(dc,axis);
            if(face.fragment_lower[tangent]!=tangent_lower||face.fragment_upper[tangent]!=tangent_upper
                ||face.fragment_lower[axis]!=face.fragment_upper[axis]
                ||(face.fragment_lower[axis]!=normal_fine&&face.fragment_lower[axis]!=normal_coarse)
                ||face.area!=area||(fine&&face.area!=source_area))
                throw std::invalid_argument("Gravity reflux actual fragment/native metric differs");
            covered[segment]=true;++fragments;
            const double ratio=face.area/source_area;
            if(!std::isfinite(ratio)||!(ratio>0.))throw std::invalid_argument("Gravity reflux fragment ratio is not representable");
            for(std::size_t k=0;k<face.value_samples.size();++k) {
                const int column=face.value_samples[k];const double weight=ratio*face.value_coefficients[k];
                if(column<0||column>=op.size()||!std::isfinite(weight))
                    throw std::invalid_argument("Gravity reflux face value row is invalid");
                phi_columns.push_back(column);phi_weights.push_back(weight);
            }
            if(!std::isfinite(face.value_boundary_coefficient))
                throw std::invalid_argument("Gravity reflux boundary datum coefficient is invalid");
            if(face.value_boundary_coefficient!=0.) {
                const double weight=ratio*face.value_boundary_coefficient;
                if(!std::isfinite(weight))throw std::invalid_argument("Gravity reflux boundary datum weight overflow");
                boundary_columns.push_back(f);boundary_weights.push_back(weight);
            }
        }
        if((fine&&fragments!=1)||(!fine&&(fragments!=2||!covered[0]||!covered[1])))
            throw std::invalid_argument("Gravity reflux face coverage is incomplete");
        phi_columns.push_back(dc);phi_weights.push_back(-1.);
        result.identity.push_back({route.key,topology.epoch,topology.fingerprint,route.plan.fingerprint,
            ordinal,flux_offset,dc,operation});
        result.potential.row(phi_columns,phi_weights);result.boundary.row(boundary_columns,boundary_weights);
    }
    return result;
}

/** Pair the original Energy operations with actual stage Phi and datum.
 * Workflow: validate candidate/Host scope; rebuild only when the authentic
 * topology owner/epoch/fingerprint changes; run the shared compensated sparse
 * rows against resident arrays; fence and reject every nonfinite value.
 * Formula: psi_o = sum_f(A_f/A_source)*Phi_f - Phi_destination_coarse.
 * dt, RK weight and original registration sign/area remain outside these rows.
 */
const GravityRefluxRows& SelfGravity::Workspace::prepare_native_reflux(
    const amr::AmrFluxTopologyPlan& topology) {
    require(GravityFieldScope::NativeRzCandidate);
    auto& e=solver.execution();
    if(e.device()||topology.epoch!=source.topology)
        throw std::logic_error("Native paired reflux changed backend or topology epoch");
    if(reflux_topology!=&topology||reflux_epoch!=topology.epoch
        ||reflux_topology_fingerprint!=topology.fingerprint) {
        auto actual=gravity_reflux_rows(binding,solver.op(),topology);
        if(actual.identity.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::overflow_error("Native paired reflux row count overflow");
        arch::multigrid::SparseArray phi(e,actual.potential),datum(e,actual.boundary);
        auto values=e.array<double>(static_cast<int>(actual.identity.size()));
        reflux_rows=std::move(actual);reflux_phi_rows=std::move(phi);
        reflux_datum_rows=std::move(datum);reflux_values=std::move(values);
        reflux_topology=&topology;reflux_epoch=topology.epoch;
        reflux_topology_fingerprint=topology.fingerprint;reflux_field_generation=0;
    }
    if(reflux_field_generation!=generation) {
        e.run(arch::multigrid::RowsWork{reflux_values.size,reflux_phi_rows.view(),
            solver.resident_potential().data,reflux_values.data});
        e.run(arch::multigrid::RowsWork{reflux_values.size,reflux_datum_rows.view(),
            boundary_values.data,reflux_values.data,1.,1.});
        e.fence();
        for(int row=0;row<reflux_values.size;++row)
            if(!std::isfinite(reflux_values.data[row]))
                throw std::runtime_error("Native paired reflux value is nonfinite");
        require(GravityFieldScope::NativeRzCandidate);
        reflux_field_generation=generation;
    }
    return reflux_rows;
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
