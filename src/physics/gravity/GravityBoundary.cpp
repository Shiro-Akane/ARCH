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
#include <bit>
#include <cfenv>
#include <cmath>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <unordered_map>

#include "physics/gravity/GravityBoundary.h"

#include "grid/GridMetrics.h"

namespace Physical::Gravity {
namespace {
/** Prove an exact binary root coordinate, with no near-equality tolerance.
 * FMA product residual and TwoSum certify the two elementary operations.
 * The restricted exponent domain keeps all possible residuals representable;
 * unsupported exponents decline proof, never imply zero construction error.
 */
bool exact_root_coordinate(double origin,double root_spacing,double index,
    int level,double stored) {
    if(!std::isfinite(origin)||!std::isfinite(root_spacing)||root_spacing<=0.
        ||!std::isfinite(index)||std::abs(index)>0x1p32||level<0||level>15
        ||!std::isfinite(stored))return false;
    const auto ordinary=[](double x) {
        return x==0.||(std::ilogb(std::abs(x))>=-400&&std::ilogb(std::abs(x))<=400);
    };
    if(!ordinary(origin)||!ordinary(root_spacing)||2.*index!=std::trunc(2.*index))return false;
    const double width=std::ldexp(root_spacing,-level);
    if(std::ldexp(width,level)!=root_spacing)return false;
    const double product=index*width;
    if(!std::isfinite(product)||std::fma(index,width,-product)!=0.)return false;
    const double sum=origin+product;
    if(!std::isfinite(sum))return false;
    const double virtual_product=sum-origin;
    const double error=(origin-(sum-virtual_product))+(product-virtual_product);
    return error==0.&&sum==stored;
}
/** Prove equality to the exact configured rational-domain coordinate.
 * Canonical generation identity alone is not a roundoff certificate. For the
 * supported ordinary exponent range, TwoSum/FMA residuals prove each span,
 * ratio, product and final sum exact; otherwise ideal-root scope stays unknown.
 * Input endpoints themselves are exact at global face zero and total count.
 */
bool exact_canonical_coordinate(const arch::elliptic::EllipticMesh& base,int axis,
    std::int64_t twice_index,int level,double stored) {
    if(level<0||level>15||base.cells[axis]<=0)return false;
    const auto total=std::uint64_t(base.cells[axis])<<level;
    if(total>0x1p52||std::abs(double(twice_index))>0x1p53)return false;
    const double lower=base.origin[axis],upper=base.root_upper[axis];
    if(twice_index==0)return stored==lower;
    if(twice_index==std::int64_t(2*total))return stored==upper;
    const auto ordinary=[](double x){return x==0.||(std::isfinite(x)
        &&std::ilogb(std::abs(x))>=-400&&std::ilogb(std::abs(x))<=400);};
    if(!ordinary(lower)||!ordinary(upper)||!(upper>lower))return false;
    const double span=upper-lower,virtual_lower=span-upper;
    const double span_error=(upper-(span-virtual_lower))+(-lower-virtual_lower);
    const double numerator=double(twice_index),denominator=double(2*total);
    const double ratio=numerator/denominator,product=span*ratio;
    const double sum=lower+product,virtual_product=sum-lower;
    const double sum_error=(lower-(sum-virtual_product))+(product-virtual_product);
    return ordinary(span)&&ordinary(ratio)&&ordinary(product)&&ordinary(sum)
        &&span_error==0.&&std::fma(ratio,denominator,-numerator)==0.
        &&std::fma(span,ratio,-product)==0.&&sum_error==0.&&sum==stored;
}
/** Shared exact root source/observer geometry proof, independent of whether
 * a boundary evaluation has completed. No tolerance or coordinate replacement.
 */
bool exact_native_ring_geometry(const arch::elliptic::CompositePoisson& op) {
    if(op.base().dimension!=2
        ||op.base().semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz)return false;
    const auto& base=op.base();
    for(int i=0;i<op.size();++i)for(int axis=0;axis<2;++axis) {
        const auto& cell=op.cells()[i];
        if(base.native_canonical_domain) {
            if(!exact_canonical_coordinate(base,axis,2*std::int64_t(cell.index[axis]),
                    cell.level,op.lower(i,axis))
                ||!exact_canonical_coordinate(base,axis,2*(std::int64_t(cell.index[axis])+1),
                    cell.level,op.upper(i,axis)))return false;
        } else if(!exact_root_coordinate(base.origin[axis],base.spacing[axis],
                double(cell.index[axis]),cell.level,op.lower(i,axis))
            ||!exact_root_coordinate(base.origin[axis],base.spacing[axis],
                double(cell.index[axis])+1.,cell.level,op.upper(i,axis)))return false;
    }
    for(const auto& face:op.faces())if(face.boundary_side>=0) {
        const int owner=face.left>=0?face.left:face.right;
        const auto& cell=op.cells()[owner];
        for(int axis=0;axis<2;++axis) {
            const double index=double(cell.index[axis])+(axis==face.axis
                ? double(face.boundary_side%2):.5);
            if(base.native_canonical_domain) {
                if(!face.native_bounds||!exact_canonical_coordinate(base,axis,
                    std::int64_t(2.*index),cell.level,face.center[axis]))return false;
            } else if(!exact_root_coordinate(base.origin[axis],base.spacing[axis],
                index,cell.level,face.center[axis]))return false;
        }
    }
    return true;
}
/** Integrate a piecewise-constant curved cell into Cartesian mass moments. */
BoundaryMoments unit_cell_moments(const arch::elliptic::CompositePoisson& op,int cell,
    const std::array<double,3>& origin) {
    using arch::elliptic::Geometry;
    BoundaryMoments moments;
    const auto& base=op.base();
    moments.value[0]=op.volumes()[cell];
    if(base.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
        return finite_ring_unit_moments(op.lower(cell,0),op.upper(cell,0),op.width(cell,1));
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
/** Exact same-density rectangle union, never a bbox/mass approximation.
 * Direct four leaf siblings must tile [rl,rh]x[zl,zh] by equal shared edges.
 * Every stored edge is compared exactly. The original finite-ring integral
 * over this stored union equals its four disjoint source integrals; ideal-root
 * potential qualification remains a separate source/observer error contract.
 */
struct UniformRingQuartet { double rl,rh,zl,zh,density; };
std::optional<UniformRingQuartet> uniform_ring_quartet(
    const arch::elliptic::CompositePoisson& op,
    std::span<const BoundaryTreeNode> nodes,int index,std::span<const double> density) {
    const auto& parent=nodes[index];
    std::array<std::array<double,4>,4> edges{};
    double rho=0.;
    for(int c=0;c<4;++c) {
        const int child=parent.children[c];
        if(child<0||child>=static_cast<int>(nodes.size())||nodes[child].cell<0)return {};
        const int cell=nodes[child].cell;
        if(cell>=static_cast<int>(density.size()))return {};
        if(c==0)rho=density[cell];
        if(!std::isfinite(rho)||rho<0.||density[cell]!=rho)return {};
        edges[c]={op.lower(cell,0),op.upper(cell,0),op.lower(cell,1),op.upper(cell,1)};
    }
    for(int c=4;c<8;++c)if(parent.children[c]>=0)return {};
    const double rl=edges[0][0],rm=edges[0][1],rh=edges[3][1];
    const double zl=edges[0][2],zm=edges[0][3],zh=edges[3][3];
    if(!(rl<rm&&rm<rh&&zl<zm&&zm<zh))return {};
    for(int c=0;c<4;++c) {
        const std::array<double,4> expected{
            c&1?rm:rl,c&1?rh:rm,c&2?zm:zl,c&2?zh:zm};
        if(edges[c]!=expected)return {};
    }
    return UniformRingQuartet{rl,rh,zl,zh,rho};
}

}
/** Build a physical-space mass tree over every active leaf. */
GravityBoundary::GravityBoundary(const arch::elliptic::CompositePoisson& op,
    amr::TopologyEpoch bound_topology)
    :bound_mesh_(op.base()),bound_boundary_(op.boundary_kind()),bound_cells_(op.cells()),
     bound_topology_(bound_topology),dimension_(op.base().dimension),
     finite_ring_(op.base().semantics==GridMetrics::GeometrySemantics::AxisymmetricRz),
     reference_radius_(op.base().native_canonical_domain ? op.base().root_upper[0]
        : op.base().origin[0]+op.base().cells[0]*op.base().spacing[0]),
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
                if(finite_ring_&&op.base().native_canonical_domain&&a<2) {
                    const int level=std::max(key.level,0);
                    const std::int64_t factor=std::int64_t{1}<<std::max(-key.level,0);
                    double lower=0.,upper=0.;
                    if(!GridMetrics::canonical_dyadic_face(op.base().origin[a],op.base().root_upper[a],
                        op.base().cells[a],level,std::int64_t(key.index[a])*factor,lower)
                        ||!GridMetrics::canonical_dyadic_face(op.base().origin[a],op.base().root_upper[a],
                        op.base().cells[a],level,(std::int64_t(key.index[a])+1)*factor,upper)
                        ||!(upper>lower))throw std::logic_error("Invalid native ring tree bounds");
                    width[a]=upper-lower;
                    native[a]=lower+.5*(upper-lower);
                    node.native_lower[a]=lower;
                }
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
                if(!(finite_ring_&&op.base().native_canonical_domain&&a<2))
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
/** Return only the actual current source counter after exact source/mesh checks.
 * This pre-solve accessor requires no completed ring integration or field. A
 * forged/stale request cannot learn a replacement generation from array size.
 */
std::uint64_t GravityBoundary::materialized_ring_source_generation(
    const arch::elliptic::CompositePoisson& op,const GravitySolveIdentity& source) const {
    require_ring_operator(op);
    validate_gravity_solve_identity(source);
    if(!source_identity_||source!=*source_identity_||!source_generation_
        ||ring_density_.size()!=volumes_.size())
        throw std::logic_error("Materialized ring source identity is stale or unavailable");
    return source_generation_;
}
/** Mesh equivalence is exact and ordered, not inferred from density array size. */
void GravityBoundary::require_ring_operator(const arch::elliptic::CompositePoisson& op) const {
    const auto& mesh=op.base();
    if(!finite_ring_||mesh.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||mesh.dimension!=bound_mesh_.dimension||mesh.geometry!=bound_mesh_.geometry
        ||mesh.cells!=bound_mesh_.cells||mesh.spacing!=bound_mesh_.spacing
        ||mesh.origin!=bound_mesh_.origin||mesh.semantics!=bound_mesh_.semantics
        ||mesh.native_canonical_domain!=bound_mesh_.native_canonical_domain
        ||mesh.root_upper!=bound_mesh_.root_upper
        ||op.boundary_kind()!=bound_boundary_||op.cells()!=bound_cells_)
        throw std::logic_error("Ring boundary operator differs from bound mesh");
}
/** Derive a finite full-walk budget from this actual bound tree and surface.
 * Every exterior face restarts at the root. Each leaf is visited at most once;
 * an internal node can pay once for an exact quartet and once for its failed
 * fallback. Hence C = F * (N + 2*P), with checked integer arithmetic. This is
 * a traversal resource bound only: leaf boxes/range/kernel/AGM remain separate.
 * Source values are not inspected, so construction-time use needs no update.
 */
std::uint64_t GravityBoundary::full_ring_traversal_work_bound(
    const arch::elliptic::CompositePoisson& op) const {
    require_ring_operator(op);
    constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    const auto increment=[&](std::uint64_t& count) {
        if(count==maximum)throw std::overflow_error("Ring traversal count overflow");
        ++count;
    };
    std::uint64_t leaves=0,parents=0,faces=0;
    for(const auto& node:nodes_) {
        if(node.cell>=0)increment(leaves);
        else increment(parents);
    }
    for(const auto& face:op.faces())if(face.boundary_side>=0)increment(faces);
    if(op.size()<=0||leaves!=static_cast<std::uint64_t>(op.size())||faces==0)
        throw std::invalid_argument("Ring traversal requires actual source leaves and exterior faces");
    if(parents>(maximum-leaves)/2)
        throw std::overflow_error("Ring per-face traversal work overflow");
    const std::uint64_t per_face=leaves+2*parents;
    if(faces>maximum/per_face)
        throw std::overflow_error("Ring full traversal work overflow");
    return faces*per_face;
}
/** Enclose the current source using existing leaf geometry and parent ordering.
 * These intervals are scratch evidence, not independently updated physical data.
 */
std::vector<RingMomentEnclosure> GravityBoundary::ring_moment_enclosures(
    const arch::elliptic::CompositePoisson& op,const GravitySolveIdentity& source) const {
    require_ring_operator(op);
    if(!source_identity_||source!=*source_identity_||ring_density_.size()!=volumes_.size())
        throw std::logic_error("Ring source identity is stale or unavailable");
    std::vector<RingMomentEnclosure> enclosed(nodes_.size());
    for(int index=static_cast<int>(nodes_.size())-1;index>=0;--index) {
        const auto& node=nodes_[index];
        if(node.cell>=0) {
            enclosed[index]=finite_ring_moment_enclosure(op.lower(node.cell,0),op.upper(node.cell,0),
                op.lower(node.cell,1),op.upper(node.cell,1),ring_density_[node.cell],node.center);
        } else enclosed[index]=combine_ring_moment_enclosures(nodes_.data(),enclosed.data(),index);
    }
    return enclosed;
}

/** Consume all finite sources at native exterior face centers.
 * A failed budget can retain diagnostic arrays, but errors remain uncertified
 * and the result is not publishable. Existing production values() stays gated.
 */
/** Derive an initial potential work target from current source and ideal B.
 * Source-only fallback is a scale, not a final RHS lower bound. The optional
 * positive isolated branch proves a lower bound using native geometry/mass/B.
 * The actual ring/source/construction/assembly/A ledger must still accept
 * the original rtol/atol before any corresponding field qualification.
 */
RingBoundaryBudgetProposal GravityBoundary::propose_ring_budget(
    const arch::elliptic::CompositePoisson& op,const GravitySolveIdentity& source,
    std::span<const double> computed_source,double rtol,double atol) const {
    require_ring_operator(op);
    if(!source_identity_||source!=*source_identity_||ring_density_.size()!=volumes_.size())
        throw std::logic_error("Ring budget source identity is stale or unavailable");
    RingBoundaryBudgetProposal result;result.source=source;
    result.source_generation=source_generation_;
    if(!std::isfinite(rtol)||rtol<=0.||rtol>=1.
        ||!std::isfinite(atol)||atol<0.)return result;
    const auto source_error=bound_uncentered_gravity_source(op,ring_density_,computed_source);
    if(source_error.status!=GravitySourceBoundStatus::Bounded) {
        result.status=source_error.status==GravitySourceBoundStatus::Overflow
            ?RingBudgetStatus::Overflow:RingBudgetStatus::InvalidInput;return result;
    }
    const auto norm=op.native_rz_norm_interval(computed_source);
    const auto error_norm=op.native_rz_norm_interval(source_error.cell_bounds);
    const auto sensitivity=op.native_rz_boundary_sensitivity();
    for(auto status:{norm.status,error_norm.status,sensitivity.status})
        if(status!=arch::elliptic::BoundaryErrorStatus::Bounded) {
            result.status=status==arch::elliptic::BoundaryErrorStatus::Overflow
                ?RingBudgetStatus::Overflow:RingBudgetStatus::UncertifiedInput;return result;
        }
    const auto down=[](double v){return v>0.?std::nextafter(v,0.):0.;};
    result.source_norm_lower=down(std::max(0.,norm.lower-error_norm.upper));
    result.boundary_sensitivity_upper=sensitivity.native_norm_upper;
    // Positive isolated sources have negative Newtonian potential. If every
    // ideal B coefficient is nonnegative, no source/boundary cancellation is
    // possible. For all source/observer points, distance <= D; hence
    // -Phi >= G*M/D and |b_i| >= 4*pi*G*rho_i + (G*M/D)*sum_f B_if.
    // This is an independent initial RHS norm lower bound, not a solve result.
    if(exact_native_ring_geometry(op)) {
        using namespace finite_ring_detail;
        const auto measure=op.native_rz_measure_enclosure();
        if(measure.status==arch::elliptic::BoundaryErrorStatus::Bounded) {
            double mass=0.,outer=0.,low=std::numeric_limits<double>::infinity(),
                high=-std::numeric_limits<double>::infinity();
            for(int i=0;i<op.size();++i) {
                mass=down(mass+down(ring_density_[i]*measure.volume_lower[i]));
                outer=std::max(outer,op.upper(i,0));
                low=std::min(low,op.lower(i,1));
                high=std::max(high,op.upper(i,1));
            }
            const double radial_extent=positive_up(outer+outer);
            const double axial_extent=offset_interval(high,low).upper;
            const double distance=distance_interval(radial_extent,axial_extent).upper;
            if(!std::isfinite(mass)||!std::isfinite(distance)) {
                result.status=RingBudgetStatus::Overflow;return result;
            }
            if(distance>0.) {
                const double magnitude=down(down(source.gravitational_constant*mass)/distance);
                std::vector<double> maps(op.size(),0.);
                bool positive_maps=true;
                for(std::size_t f=0;f<op.faces().size();++f)if(op.faces()[f].boundary_side>=0) {
                    const auto map=op.native_rz_face_enclosure(f);
                    if(map.status!=arch::elliptic::BoundaryErrorStatus::Bounded) {
                        positive_maps=false;break;
                    }
                    const auto& face=op.faces()[f];
                    for(int side=0;side<2;++side) {
                        const int cell=side?face.right:face.left;if(cell<0)continue;
                        if(map.boundary_map_lower[side]<0.){positive_maps=false;break;}
                        maps[cell]=down(maps[cell]+map.boundary_map_lower[side]);
                    }
                    if(!positive_maps)break;
                }
                if(positive_maps) {
                    result.rhs_cell_magnitude_lower.resize(op.size());
                    for(int i=0;i<op.size();++i)
                        result.rhs_cell_magnitude_lower[i]=down(
                            std::max(0.,-source_error.upper[i])+down(magnitude*maps[i]));
                    const auto rhs_norm=op.native_rz_norm_interval(result.rhs_cell_magnitude_lower);
                    if(rhs_norm.status==arch::elliptic::BoundaryErrorStatus::Bounded) {
                        result.basis=RingBudgetBasis::PositiveIsolatedRhs;
                        result.mass_lower=mass;result.maximum_distance_upper=distance;
                        result.potential_magnitude_lower=magnitude;
                        result.rhs_norm_lower=rhs_norm.lower;
                    } else {
                        result.rhs_cell_magnitude_lower.clear();
                        if(rhs_norm.status==arch::elliptic::BoundaryErrorStatus::Overflow) {
                            result.status=RingBudgetStatus::Overflow;return result;
                        }
                    }
                }
            }
        }
    }
    const double initial_norm=result.basis==RingBudgetBasis::PositiveIsolatedRhs
        ?result.rhs_norm_lower:result.source_norm_lower;
    result.initial_tolerance=std::max(atol,down(rtol*initial_norm));
    if(!std::isfinite(result.initial_tolerance)
        ||!std::isfinite(result.boundary_sensitivity_upper)){
        result.status=RingBudgetStatus::Overflow;return result;}
    if(result.initial_tolerance==0.||result.boundary_sensitivity_upper<=0.) {
        result.status=RingBudgetStatus::ZeroBudget;return result;
    }
    result.control.face_absolute_target=down(down(.5*result.initial_tolerance)
        /result.boundary_sensitivity_upper);
    result.status=result.control.face_absolute_target>0.
        ?RingBudgetStatus::Proposed:RingBudgetStatus::ZeroBudget;
    return result;
}
/** Hash exact tagged key words, including BOTH relative-endpoint residuals.
 * A scalar-potential translation/reflection equivalence never changes the
 * original signed enclosure or source/field generation checks.
 */
std::size_t GravityBoundary::RingMemoHash::operator()(const RingMemoKey& key) const noexcept {
    std::size_t hash=0;
    for(const auto word:key.words)
        hash^=std::hash<std::uint64_t>{}(word)+std::size_t{0x9e3779b9}+(hash<<6)+(hash>>2);
    return hash;
}
/** Clear mathematical history only; generations, source moments and leases survive. */
void GravityBoundary::clear_ring_memo() noexcept {ring_memo_.clear();}
/** Memoize only original successful leaf/quartet enclosures, never tree authority.
 * Workflow: validate the original inputs before lookup; use exact geometry/G
 * words or proven exact axial-relative words and current quartet eligibility;
 * disable history outside nearest rounding; recheck the current precision/box
 * request; otherwise execute the original kernel and lazily admit its Bounded
 * result. Every caller still charges the original traversal/global work budget.
 * Allocation failure merely prevents admission; it cannot alter a valid result.
 */
RingPotentialEnclosure GravityBoundary::memoized_ring_potential(
    double rl,double rh,double zl,double zh,double density,double ro,double zo,double G,
    const RingEnclosureControl& control,bool quartet,RingBoundaryEvaluation& diagnostics) const {
    const bool eligible=std::isfinite(rl)&&std::isfinite(rh)&&rl>=0.&&rl<rh
        &&std::isfinite(zl)&&std::isfinite(zh)&&zl<zh
        &&std::isfinite(ro)&&ro>=0.&&std::isfinite(zo)
        &&density>0.&&std::isnormal(density)&&G>0.&&std::isnormal(G)
        &&std::isfinite(control.relative_target)&&control.relative_target>=0.
        &&std::isfinite(control.absolute_target)&&control.absolute_target>=0.
        &&control.maximum_boxes>0&&control.maximum_boxes<=65536
        &&std::fegetround()==FE_TONEAREST;
    RingMemoKey key;
    decltype(ring_memo_)::iterator found=ring_memo_.end();
    if(eligible) {
        // Mode 0 retains the exact original raw geometry. Mode 1 stores the
        // complete exact relative endpoints, canonical under axial reflection.
        // Distinct modes cannot collide, even when unused words are zero.
        key.words={0ULL,std::bit_cast<std::uint64_t>(rl),std::bit_cast<std::uint64_t>(rh),
            std::bit_cast<std::uint64_t>(ro),std::bit_cast<std::uint64_t>(G),quartet?1ULL:0ULL,
            std::bit_cast<std::uint64_t>(zl),0ULL,std::bit_cast<std::uint64_t>(zh),0ULL,
            std::bit_cast<std::uint64_t>(zo)};
        if(const auto relative=ring_memo_detail::exact_axial_relative_endpoints(zl,zh,zo)) {
            key.words[0]=1ULL;
            for(std::size_t word=0;word<relative->size();++word)
                key.words[6+word]=std::bit_cast<std::uint64_t>((*relative)[word]);
            key.words[10]=0ULL;
        }
        found=ring_memo_.find(key);
        if(found!=ring_memo_.end()) {
            RingPotentialEnclosure reused;
            const auto& entry=found->second;
            if(reuse_bounded_ring_interval(entry.interval,entry.density,density,
                entry.leaf_boxes,control,reused)) {
                ++diagnostics.memo_hits;return reused;
            }
        }
    }
    ++diagnostics.memo_misses;
    const auto original=finite_ring_potential_enclosure(rl,rh,zl,zh,density,ro,zo,G,control);
    if(eligible&&original.bound_valid&&original.status==RingIntervalStatus::Bounded
        &&finite_ring_detail::interval_finite({original.lower,original.upper})
        &&original.lower<0.&&original.upper<=0.
        &&std::isnormal(original.lower)
        &&(original.upper==0.||std::isnormal(original.upper))) {
        const RingMemoEntry entry{{original.lower,original.upper},density,original.leaf_boxes};
        if(found!=ring_memo_.end()) {
            found->second=entry;++diagnostics.memo_admissions;
        } else if(ring_memo_.size()<maximum_ring_memo_entries) {
            try {
                if(ring_memo_.emplace(key,entry).second)++diagnostics.memo_admissions;
            } catch(const std::bad_alloc&) {
                // A bounded mathematical result is independent of optional history storage.
            }
        }
    }
    return original;
}

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
    const auto moment_bounds=ring_moment_enclosures(op,source);
    const auto count=op.faces().size();
    result.values.assign(count,0.);result.lower.assign(count,0.);result.upper.assign(count,0.);
    result.errors.resize(count);
    result.far_truncation_upper.assign(count,0.);
    result.far_evaluation_width_upper.assign(count,0.);
    RingEnclosureControl leaf_control{};
    leaf_control.absolute_target=positive_down(control.face_absolute_target/op.size());
    leaf_control.maximum_boxes=control.maximum_boxes_per_leaf;
    // Current moments and quartet eligibility are always recomputed from the full source.
    // The integral memo stores only mathematical intervals, never source authority.
    std::vector<std::optional<UniformRingQuartet>> quartets(nodes_.size());
    // Same-density coalescing integrates the exact union of stored rectangles.
    // It does not promote their potential errors to ideal-root scope.
    for(int i=0;i<static_cast<int>(nodes_.size());++i)if(nodes_[i].cell<0)
            quartets[i]=uniform_ring_quartet(op,nodes_,i,ring_density_);
    bool converged=true;result.status=RingBoundaryStatus::Bounded;
    for(std::size_t face=0;face<count;++face)if(op.faces()[face].boundary_side>=0) {
        SignedInterval total{};
        for(int index=0;index<static_cast<int>(nodes_.size());) {
            const auto& node=nodes_[index];
            if(result.leaf_evaluations+result.parent_evaluations>=control.maximum_leaf_evaluations) {
                result.status=RingBoundaryStatus::WorkLimit;return result;
            }
            if(node.cell<0) {
                ++result.parent_evaluations;
                if(quartets[index]) {
                    // The parent visit pays for this exact integral first.
                    // Avoid evaluating a multipole approximation unnecessarily.
                    ++result.coalesced_parent_attempts;
                    const auto& tile=*quartets[index];
                    auto tile_control=leaf_control;
                    tile_control.absolute_target=positive_down(4.*leaf_control.absolute_target);
                    const auto enclosure=memoized_ring_potential(tile.rl,tile.rh,tile.zl,tile.zh,
                        tile.density,op.faces()[face].center[0],op.faces()[face].center[1],
                        source.gravitational_constant,tile_control,true,result);
                    result.range_evaluations+=enclosure.range_evaluations;
                    result.kernel_enclosures+=enclosure.kernel_enclosures;
                    result.agm_iterations+=enclosure.agm_iterations;
                    if(enclosure.bound_valid&&enclosure.status==RingIntervalStatus::Bounded) {
                        total=interval_sum(total,{enclosure.lower,enclosure.upper});
                        ++result.coalesced_parent_acceptances;
                        result.coalesced_native_leaves+=4;
                        result.represented_leaf_evaluations+=4;
                        index=node.end;continue;
                    }
                    // Failed exact integral falls back to the old far/descent
                    // path, with its extra evaluation charged to the same cap.
                    if(result.leaf_evaluations+result.parent_evaluations>=control.maximum_leaf_evaluations) {
                        result.status=RingBoundaryStatus::WorkLimit;return result;
                    }
                    ++result.parent_evaluations;
                }
                double tail=0.;SignedInterval evaluation{};
                const auto far=ring_node_far_enclosure(node,moment_bounds[index],
                    op.faces()[face].center[0],op.faces()[face].center[1],source.gravitational_constant,
                    &tail,&evaluation);
                const double allowance=positive_down(leaf_control.absolute_target*moment_bounds[index].leaves);
                const double halfwidth=interval_finite(far)
                    ? positive_up(.5*(far.upper-far.lower)) : std::numeric_limits<double>::infinity();
                if(interval_finite(far)&&halfwidth<=allowance) {
                    total=interval_sum(total,far);
                    ++result.parent_acceptances;
                    result.far_truncation_upper[face]=sum_up(result.far_truncation_upper[face],tail);
                    result.far_evaluation_width_upper[face]=sum_up(result.far_evaluation_width_upper[face],
                        positive_up(evaluation.upper-evaluation.lower));
                    result.represented_leaf_evaluations+=moment_bounds[index].leaves;
                    index=node.end;continue;
                }
                ++index;continue; // Budget/separation failure descends; no geometric-only opening.
            }
            ++result.leaf_evaluations;
            ++result.represented_leaf_evaluations;
            ++index;
            // Same operator-owned edge arithmetic used by unit_cell_moments;
            // lower+width could re-round an upper edge differently.
            const double rl=op.lower(node.cell,0),rh=op.upper(node.cell,0);
            const double zl=op.lower(node.cell,1),zh=op.upper(node.cell,1);
            const auto leaf=memoized_ring_potential(rl,rh,zl,zh,
                ring_density_[node.cell],op.faces()[face].center[0],
                op.faces()[face].center[1],source.gravitational_constant,leaf_control,false,result);
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
/** Certify the actual producer's source and observer coordinate identity.
 * No re-integration, coordinate replacement or physics change is performed.
 * All leaf edges and exterior face centers must equal ideal root coordinates
 * exactly. The current density/AMR/generation checks precede scope promotion.
 */
std::vector<arch::elliptic::NativeRzFacePotentialError> GravityBoundary::root_scoped_ring_errors(
    const arch::elliptic::CompositePoisson& op,const RingBoundaryEvaluation& ring) const {
    using namespace arch::elliptic;
    require_current_ring(op,ring);
    if(ring.errors.size()!=op.faces().size())throw std::invalid_argument("Missing ring face errors");
    std::vector<NativeRzFacePotentialError> result(ring.errors.size());
    for(std::size_t i=0;i<result.size();++i)result[i].error=ring.errors[i];
    if(!exact_native_ring_geometry(op))return result;
    for(std::size_t i=0;i<result.size();++i)if(op.faces()[i].boundary_side>=0
        &&ring.errors[i].quality==BoundaryErrorQuality::CertifiedAbsolute)
        result[i].scope=NativeRzPotentialScope::RootDyadicSourceAndObserver;
    return result;
}
/** Join source, boundary and actual provider arithmetic with one owner identity.
 * Workflow: require current ring -> bound actual arrays -> sum cell errors ->
 * evaluate original user rtol/atol conditionally. Construction gaps stay explicit.
 */
RingRhsAssessment GravityBoundary::assess_ring_rhs(
    const arch::elliptic::CompositePoisson& op,const RingBoundaryEvaluation& ring,
    std::span<const double> computed_source,std::span<const double> computed_rhs,
    std::span<const double> potential,std::span<const double> computed_residual,
    double rtol,double atol) const {
    using namespace arch::elliptic;
    require_current_ring(op,ring);
    RingRhsAssessment result;
    result.source=ring.source;result.source_generation=ring.source_generation;
    if(ring.source.gravitational_constant!=arch::constants::gravity::cgs::gravitational_constant)
        return result; // This physical source companion uses the authoritative shared G.
    result.source_error=bound_uncentered_gravity_source(op,ring_density_,computed_source);
    result.boundary_error=op.propagate_boundary_error(ring.errors);
    result.assembly_error=op.bound_rhs_assembly_roundoff(computed_source,ring.values,computed_rhs);
    result.residual_error=op.bound_residual_evaluation_roundoff(potential,computed_rhs,computed_residual);
    if(result.source_error.status==GravitySourceBoundStatus::Overflow
        ||result.boundary_error.status==BoundaryErrorStatus::Overflow
        ||result.assembly_error.status==BoundaryErrorStatus::Overflow
        ||result.residual_error.status==BoundaryErrorStatus::Overflow) {
        result.conditional.status=BoundaryResidualStatus::Overflow;return result;
    }
    if(result.source_error.status!=GravitySourceBoundStatus::Bounded
        ||result.boundary_error.status!=BoundaryErrorStatus::Bounded
        ||result.assembly_error.status!=BoundaryErrorStatus::Bounded
        ||result.residual_error.status!=BoundaryErrorStatus::Bounded)return result;
    auto& combined=result.combined_rhs_error;
    combined.cell_bounds.resize(op.size());
    for(int i=0;i<op.size();++i) {
        combined.cell_bounds[i]=finite_ring_detail::sum_up(
            finite_ring_detail::sum_up(result.source_error.cell_bounds[i],
                result.boundary_error.cell_bounds[i]),result.assembly_error.cell_bounds[i]);
        if(!std::isfinite(combined.cell_bounds[i])) {
            combined.status=BoundaryErrorStatus::Overflow;
            result.conditional.status=BoundaryResidualStatus::Overflow;return result;
        }
    }
    const auto norm=op.norm_interval(combined.cell_bounds);
    combined.status=norm.status;combined.norm_upper=norm.upper;
    // RHS assembly is already included cellwise: do not count it twice.
    result.conditional=op.assess_boundary_residual(computed_rhs,computed_residual,combined,
        0.,result.residual_error.norm_upper,BoundaryErrorQuality::CertifiedAbsolute,rtol,atol);
    return result;
}


/** Complete actual source/B/assembly/A/evaluation ledger in ideal native RMS.
 * Potential scope comes from this producer's checked source/observer proof.
 * Construction, integral and arithmetic errors are each counted once.
 * Acceptance certifies only the native discrete residual, not Phi/force science.
 */
RingRhsAssessment GravityBoundary::assess_native_ring_rhs(
    const arch::elliptic::CompositePoisson& op,const RingBoundaryEvaluation& ring,
    std::span<const double> source,std::span<const double> rhs,
    std::span<const double> potential,std::span<const double> residual,
    double rtol,double atol) const {
    using namespace arch::elliptic;
    require_current_ring(op,ring);
    RingRhsAssessment result;result.source=ring.source;
    result.source_generation=ring.source_generation;
    result.scope=RingRhsAssessmentScope::RootDyadicNativeOperator;
    const auto face_errors=root_scoped_ring_errors(op,ring);
    result.native_boundary_potential=op.native_rz_propagate_potential_error(face_errors);
    if(result.native_boundary_potential.status!=BoundaryErrorStatus::Bounded) {
        result.conditional.status=result.native_boundary_potential.status==BoundaryErrorStatus::UncertifiedInput
            ?BoundaryResidualStatus::UncertifiedInput:(result.native_boundary_potential.status==BoundaryErrorStatus::Overflow
                ?BoundaryResidualStatus::Overflow:BoundaryResidualStatus::InvalidInput);
        return result;
    }
    result.source_error=bound_uncentered_gravity_source(op,ring_density_,source);
    result.native_boundary_construction=op.native_rz_boundary_construction_error(ring.values);
    result.assembly_error=op.bound_rhs_assembly_roundoff(source,ring.values,rhs);
    result.native_residual_error=op.native_rz_residual_evaluation_error(potential,rhs,residual);
    if(result.source_error.status==GravitySourceBoundStatus::Overflow
        ||result.native_boundary_construction.status==BoundaryErrorStatus::Overflow
        ||result.assembly_error.status==BoundaryErrorStatus::Overflow
        ||result.native_residual_error.status==BoundaryErrorStatus::Overflow) {
        result.conditional.status=BoundaryResidualStatus::Overflow;return result;
    }
    if(result.source_error.status!=GravitySourceBoundStatus::Bounded
        ||result.native_boundary_construction.status!=BoundaryErrorStatus::Bounded
        ||result.assembly_error.status!=BoundaryErrorStatus::Bounded
        ||result.native_residual_error.status!=BoundaryErrorStatus::Bounded)return result;
    auto& combined=result.combined_rhs_error;combined.cell_bounds.resize(op.size());
    for(int i=0;i<op.size();++i) {
        combined.cell_bounds[i]=finite_ring_detail::sum_up(
            finite_ring_detail::sum_up(result.source_error.cell_bounds[i],
                result.native_boundary_construction.cell_bounds[i]),
            finite_ring_detail::sum_up(result.native_boundary_potential.cell_bounds[i],
                result.assembly_error.cell_bounds[i]));
        if(!std::isfinite(combined.cell_bounds[i])) {
            combined.status=BoundaryErrorStatus::Overflow;
            result.conditional.status=BoundaryResidualStatus::Overflow;return result;
        }
    }
    const auto norm=op.native_rz_norm_interval(combined.cell_bounds);
    combined.status=norm.status;combined.norm_upper=norm.upper;
    // Assembly already in combined RHS; A/evaluation already against computed
    // RHS. No repeated assembly or homogeneous/prescribed boundary term.
    // The exact residual contains the SAME boundary-fit coefficient in A
    // and B. Bound their joint construction against phi_anchor-datum while
    // retaining the original separate complete RHS error above for ||b||lower.
    // Source, potential integration, assembly and actual apply/subtraction
    // arithmetic stay present, each once. Physical qualification is unchanged.
    const auto construction=op.native_rz_prescribed_residual_construction_error(potential,ring.values);
    if(construction.status!=BoundaryErrorStatus::Bounded) {
        result.conditional.status=construction.status==BoundaryErrorStatus::Overflow?
            BoundaryResidualStatus::Overflow:BoundaryResidualStatus::InvalidInput;return result;
    }
    auto& complete=result.native_complete_residual_error;complete.cell_bounds.resize(op.size());
    for(int i=0;i<op.size();++i) {
        complete.cell_bounds[i]=finite_ring_detail::sum_up(
            finite_ring_detail::sum_up(result.source_error.cell_bounds[i],
                result.native_boundary_potential.cell_bounds[i]),
            finite_ring_detail::sum_up(result.assembly_error.cell_bounds[i],
                finite_ring_detail::sum_up(result.native_residual_error.arithmetic.cell_bounds[i],
                    construction.cell_bounds[i])));
        if(!std::isfinite(complete.cell_bounds[i])) {
            complete.status=BoundaryErrorStatus::Overflow;
            result.conditional.status=BoundaryResidualStatus::Overflow;return result;
        }
    }
    const auto complete_norm=op.native_rz_norm_interval(complete.cell_bounds);
    complete.status=complete_norm.status;complete.native_norm_upper=complete_norm.upper;
    result.conditional=op.assess_native_rz_correlated_residual(rhs,residual,combined,complete,rtol,atol);
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
