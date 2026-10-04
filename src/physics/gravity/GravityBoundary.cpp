/**
 * @file GravityBoundary.cpp
 * @brief Build isolated self-gravity boundary values from active AMR mass.
 *
 * Workflow:
 * 1. Build a physical-space tree over active leaves and cache each leaf's
 *    unit-density mass, dipole and quadrupole by native-coordinate quadrature.
 * 2. At each gravity stage, multiply cached leaf moments by current density
 *    and accumulate parent moments from children.
 * 3. Evaluate the 2D logarithmic or 3D Newton kernel at physical boundary
 *    face positions; integrate near curved leaves directly when needed.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

#include "physics/gravity/GravityBoundary.h"

#include "grid/GridMetrics.h"

namespace Physical::Gravity {
namespace {
/** Integrate a piecewise-constant curved cell into Cartesian mass moments. */
BoundaryMoments unit_cell_moments(const arch::elliptic::CompositePoisson& op,int cell,
    const std::array<double,3>& origin) {
    using arch::elliptic::Geometry;
    BoundaryMoments moments;
    const auto& base=op.base();
    moments.value[0]=op.volumes()[cell];
    if(base.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        const auto center=op.center(cell);
        return finite_ring_unit_moments(center[0]-.5*op.width(cell,0),
            center[0]+.5*op.width(cell,0),op.width(cell,1));
    }
    if(base.geometry==Geometry::Cartesian)return moments;
    const auto center=op.center(cell);
    constexpr double nodes[]{-0.7745966692414834,0.,0.7745966692414834};
    constexpr double weights[]{5./9.,8./9.,5./9.};
    double total=0.,first[3]{},second[3][3]{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<(base.dimension==3?3:1);++k) {
        std::array<double,3> native=center;
        native[0]+=.5*op.width(cell,0)*nodes[i];
        native[1]+=.5*op.width(cell,1)*nodes[j];
        if(base.dimension==3)native[2]+=.5*op.width(cell,2)*nodes[k];
        const auto geometry=base.geometry==Geometry::Cylindrical
            ? GridMetrics::Geometry::Cylindrical:GridMetrics::Geometry::Spherical;
        const auto x=GridMetrics::PhysicalPosition(geometry,base.dimension,native);
        double jacobian=native[0];
        if(base.dimension==3 && base.geometry==Geometry::Spherical)
            jacobian*=native[0]*std::sin(native[1]);
        const double w=weights[i]*weights[j]*(base.dimension==3?weights[k]:1.)*jacobian;
        total+=w;
        double d[3];for(int a=0;a<3;++a)d[a]=x[a]-origin[a];
        for(int a=0;a<3;++a) {
            first[a]+=w*d[a];
            for(int b=a;b<3;++b)second[a][b]+=w*d[a]*d[b];
        }
    }
    const double factor=moments.value[0]/total;
    for(int a=0;a<3;++a) {
        moments.value[1+a]=factor*first[a];
        for(int b=a;b<3;++b)moments.value[second_moment_index(a,b)]=factor*second[a][b];
    }
    return moments;
}
}
/** Build a physical-space mass tree over every active leaf. */
GravityBoundary::GravityBoundary(const arch::elliptic::CompositePoisson& op,
    amr::TopologyEpoch bound_topology)
    :bound_mesh_(op.base()),bound_boundary_(op.boundary_kind()),bound_cells_(op.cells()),
     bound_topology_(bound_topology),dimension_(op.base().dimension),
     finite_ring_(op.base().semantics==GridMetrics::GeometrySemantics::AxisymmetricRz),
     reference_radius_(op.base().origin[0]+op.base().cells[0]*op.base().spacing[0]),
     volumes_(op.volumes()) {
    using namespace arch::elliptic;
    if((dimension_!=2 && dimension_!=3) ||
       (dimension_==2 && op.base().geometry==Geometry::Cartesian))
        throw std::invalid_argument("Isolated multipole gravity requires 2D polar or 3D space");
    int root_level=0;
    // Round UP: a 48-cell axis needs a 64-cell tree root. Flooring this
    // depth would silently omit mass in the outermost root cells.
    for(int n=*std::max_element(op.base().cells.begin(),op.base().cells.end());n>1;n=n/2+n%2) --root_level;
    std::unordered_map<CompositeCell,int,CompositeCellHash> lookup;
    std::vector<BoundaryTreeNode> raw;
    const auto ensure=[&](CompositeCell key) {
        auto [entry,added]=lookup.emplace(key,static_cast<int>(raw.size()));
        if(added) {
            BoundaryTreeNode node;
            std::array<double,3> native{},width{};
            for(int a=0;a<3;++a) {
                width[a]=std::ldexp(op.base().spacing[a],-key.level);
                native[a]=op.base().origin[a]+(key.index[a]+0.5)*width[a];
            }
            if(finite_ring_) {
                node.center={0.,0.,native[1]};
                node.radius_squared=finite_ring_support_squared(
                    native[0]+.5*width[0],.5*width[1]);
            } else if(op.base().geometry==Geometry::Cartesian) {
                node.center=native;
                for(int a=0;a<3;++a)node.radius_squared+=0.25*width[a]*width[a];
            } else {
                const auto geometry=op.base().geometry==Geometry::Cylindrical
                    ? GridMetrics::Geometry::Cylindrical:GridMetrics::Geometry::Spherical;
                node.center=GridMetrics::PhysicalPosition(geometry,dimension_,native);
                const double outer=native[0]+0.5*width[0];
                // A conservative bound contains curved arcs, including root
                // nodes spanning a full azimuth. It controls tree opening,
                // not the physical source position or cell mass.
                const double radius=.5*width[0]+outer*.5*width[dimension_-1]
                    +(dimension_==3 ? (geometry==GridMetrics::Geometry::Spherical
                        ? outer*.5*width[1] : .5*width[1]) : 0.);
                node.radius_squared=radius*radius;
            }
            for(int a=0;a<3;++a) {
                node.native_width[a]=width[a];
                node.native_lower[a]=native[a]-0.5*width[a];
            }
            raw.push_back(node);
        }
        return entry->second;
    };
    for(int cell=0;cell<op.size();++cell) {
        auto key=op.cells()[cell];int child=ensure(key);raw[child].cell=cell;
        raw[child].unit_moments=unit_cell_moments(op,cell,raw[child].center);
        while(key.level>root_level) {
            const int octant=(key.index[0]&1)+2*(key.index[1]&1)+4*(key.index[2]&1);
            --key.level;for(int a=0;a<3;++a) key.index[a]/=2;
            const int parent=ensure(key);raw[parent].children[octant]=child;child=parent;
        }
    }
    std::function<int(int,int)> flatten=[&](int old,int depth) {
        const int index=static_cast<int>(nodes_.size());nodes_.push_back(raw[old]);
        if(static_cast<int>(layers_.size())<=depth) layers_.resize(depth+1);
        layers_[depth].push_back(index);
        for(int a=0;a<8;++a) if(raw[old].children[a]>=0) {
            const int child=flatten(raw[old].children[a],depth+1);
            nodes_[index].children[a]=child;
        }
        nodes_[index].end=static_cast<int>(nodes_.size());return index;
    };
    flatten(lookup.at({root_level,{0,0,0}}),0);
    moments_.resize(nodes_.size());
}
/** Recompute monopole, dipole and quadrupole moments from current density. */
void GravityBoundary::update(std::span<const double> density) {
    // Even a rejected attempted update retires the checked source association.
    source_identity_.reset();ring_density_.clear();
    arch::elliptic::validate_values(density,volumes_.size());
    for(double rho:density)if(rho<0.)throw std::invalid_argument("Negative isolated source density");
    std::vector<BoundaryMoments> next(nodes_.size());
    for(auto layer=layers_.rbegin();layer!=layers_.rend();++layer)for(int index:*layer) {
        const int cell=nodes_[index].cell;
        if(cell>=0) {
            for(int q=0;q<10;++q)
                next[index].value[q]=density[cell]*nodes_[index].unit_moments.value[q];
        } else next[index]=combine_boundary_moments(nodes_.data(),next.data(),index);
        for(double value:next[index].value)
            if(!std::isfinite(value))throw std::overflow_error("Nonfinite isolated mass moment");
    }
    // Prevalidated no-throw commit; keep the allocation stable for existing
    // geometry/device upload views and reference consumers.
    std::copy(next.begin(),next.end(),moments_.begin());
}
/** Cache actual source samples under the existing gravity dependency contract. */
void GravityBoundary::update(std::span<const double> density,const GravitySolveIdentity& source) {
    source_identity_.reset();ring_density_.clear();
    validate_gravity_solve_identity(source);
    if(!finite_ring_||!bound_topology_.value||source.topology!=bound_topology_)
        throw std::logic_error("Ring source topology differs from bound tree");
    if(source.gravitational_constant!=arch::constants::gravity::cgs::gravitational_constant)
        throw std::logic_error("Ring source uses a different shared gravitational constant");
    if(source_generation_==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Ring source generation exhausted");
    std::vector<double> next_density(density.begin(),density.end());
    auto next_identity=std::make_optional(source);
    update(density);
    ring_density_.swap(next_density);source_identity_=std::move(next_identity);
    ++source_generation_;
}
/** Mesh equivalence is exact and ordered, not inferred from density array size. */
void GravityBoundary::require_ring_operator(const arch::elliptic::CompositePoisson& op) const {
    const auto& mesh=op.base();
    if(!finite_ring_||mesh.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||mesh.dimension!=bound_mesh_.dimension||mesh.geometry!=bound_mesh_.geometry
        ||mesh.cells!=bound_mesh_.cells||mesh.spacing!=bound_mesh_.spacing
        ||mesh.origin!=bound_mesh_.origin||mesh.semantics!=bound_mesh_.semantics
        ||op.boundary_kind()!=bound_boundary_||op.cells()!=bound_cells_)
        throw std::logic_error("Ring boundary operator differs from bound mesh");
}
/** Consume all finite sources at native exterior face centers.
 * A failed budget can retain diagnostic arrays, but errors remain uncertified
 * and the result is not publishable. Existing production values() stays gated.
 */
RingBoundaryEvaluation GravityBoundary::ring_boundary(
    const arch::elliptic::CompositePoisson& op,const GravitySolveIdentity& source,
    const RingBoundaryControl& control) const {
    using namespace finite_ring_detail;
    require_ring_operator(op);
    if(!source_identity_||source!=*source_identity_||ring_density_.size()!=volumes_.size())
        throw std::logic_error("Ring source identity is stale or unavailable");
    if(!std::isfinite(control.face_absolute_target)||control.face_absolute_target<0.
        ||control.maximum_boxes_per_leaf==0||control.maximum_boxes_per_leaf>65536
        ||control.maximum_leaf_evaluations==0)
        throw std::invalid_argument("Invalid internal ring boundary budget");
    RingBoundaryEvaluation result;result.source=source;result.source_generation=source_generation_;
    const auto count=op.faces().size();
    result.values.assign(count,0.);result.lower.assign(count,0.);result.upper.assign(count,0.);
    result.errors.resize(count);
    RingEnclosureControl leaf_control{};
    leaf_control.absolute_target=positive_down(control.face_absolute_target/op.size());
    leaf_control.maximum_boxes=control.maximum_boxes_per_leaf;
    bool converged=true;result.status=RingBoundaryStatus::Bounded;
    for(std::size_t face=0;face<count;++face)if(op.faces()[face].boundary_side>=0) {
        SignedInterval total{};
        for(const auto& node:nodes_)if(node.cell>=0) {
            if(result.leaf_evaluations>=control.maximum_leaf_evaluations) {
                result.status=RingBoundaryStatus::WorkLimit;return result;
            }
            ++result.leaf_evaluations;
            // Same operator-owned edge arithmetic used by unit_cell_moments;
            // lower+width could re-round an upper edge differently.
            const auto center=op.center(node.cell);
            const double wr=op.width(node.cell,0),wz=op.width(node.cell,1);
            const double rl=center[0]-.5*wr,rh=center[0]+.5*wr;
            const double zl=center[1]-.5*wz,zh=center[1]+.5*wz;
            const auto leaf=finite_ring_potential_enclosure(rl,rh,zl,zh,
                ring_density_[node.cell],op.faces()[face].center[0],
                op.faces()[face].center[1],source.gravitational_constant,leaf_control);
            result.range_evaluations+=leaf.range_evaluations;
            result.kernel_enclosures+=leaf.kernel_enclosures;
            result.agm_iterations+=leaf.agm_iterations;
            if(!leaf.bound_valid) {
                result.status=RingBoundaryStatus::PrecisionLimit;return result;
            }
            if(leaf.status!=RingIntervalStatus::Bounded) {
                converged=false;
                result.status=leaf.status==RingIntervalStatus::WorkLimit?
                    RingBoundaryStatus::WorkLimit:RingBoundaryStatus::PrecisionLimit;
            }
            total=interval_sum(total,{leaf.lower,leaf.upper});
        }
        if(!interval_finite(total)) {
            result.status=RingBoundaryStatus::PrecisionLimit;return result;
        }
        result.lower[face]=total.lower;result.upper[face]=total.upper;
        result.values[face]=total.lower+.5*(total.upper-total.lower);
        const double error=positive_up(std::max(result.values[face]-total.lower,
                                                total.upper-result.values[face]));
        result.errors[face].absolute_error=error;
        if(!std::isfinite(result.values[face])||!std::isfinite(error)) {
            result.status=RingBoundaryStatus::PrecisionLimit;return result;
        }
        if(error>control.face_absolute_target) {
            converged=false;if(result.status==RingBoundaryStatus::Bounded)
                result.status=RingBoundaryStatus::PrecisionLimit;
        }
    }
    // Certification is all-or-nothing, including FP64 source reduction/budget.
    if(converged)for(auto& error:result.errors)
        error.quality=arch::elliptic::BoundaryErrorQuality::CertifiedAbsolute;
    return result;
}
void GravityBoundary::require_current_ring(const arch::elliptic::CompositePoisson& op,
    const RingBoundaryEvaluation& result) const {
    require_ring_operator(op);
    if(result.status!=RingBoundaryStatus::Bounded||!source_identity_
        ||result.source!=*source_identity_||result.source_generation!=source_generation_)
        throw std::logic_error("Ring boundary is failed, stale or from another density generation");
}
/** Evaluate isolated boundary potential at each exterior composite face. */
std::vector<double> GravityBoundary::values(const arch::elliptic::CompositePoisson& op,
                                           double G,double theta,int order) const {
    // A legacy cached tree must not consume an RZ operator as a log-kernel
    // boundary. Analytic RZ operator tests do not certify this source model.
    if(finite_ring_ || op.base().semantics==GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::invalid_argument("RZ isolated boundary unavailable: finite-ring contract pending");
    if(!std::isfinite(G) || G<=0. || !std::isfinite(theta) || theta<0. || theta>=1.
        || order<0 || order>2) throw std::invalid_argument("Invalid isolated boundary evaluation");
    std::vector<double> result(op.faces().size());
    for(std::size_t i=0;i<result.size();++i) if(op.faces()[i].boundary_side>=0) {
        const auto geometry=op.base().geometry==arch::elliptic::Geometry::Cartesian
            ? GridMetrics::Geometry::Cartesian
            : (op.base().geometry==arch::elliptic::Geometry::Cylindrical
                ? GridMetrics::Geometry::Cylindrical:GridMetrics::Geometry::Spherical);
        const auto point=GridMetrics::PhysicalPosition(geometry,dimension_,op.faces()[i].center);
        result[i]=dimension_==2
            ?isolated_log_potential(nodes_.data(),moments_.data(),static_cast<int>(nodes_.size()),
                                    point.data(),G,reference_radius_,theta,order,geometry)
            :isolated_potential(nodes_.data(),moments_.data(),static_cast<int>(nodes_.size()),
                                point.data(),G,theta,order,geometry,dimension_);
        if(!std::isfinite(result[i])) throw std::runtime_error("Nonfinite isolated boundary potential");
    }
    return result;
}
} // namespace Physical::Gravity
