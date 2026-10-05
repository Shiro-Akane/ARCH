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
            const auto center=op.center(node.cell);
            const double wr=op.width(node.cell,0),wz=op.width(node.cell,1);
            enclosed[index]=finite_ring_moment_enclosure(center[0]-.5*wr,center[0]+.5*wr,
                center[1]-.5*wz,center[1]+.5*wz,ring_density_[node.cell],node.center);
        } else enclosed[index]=combine_ring_moment_enclosures(nodes_.data(),enclosed.data(),index);
    }
    return enclosed;
}

/** Consume all finite sources at native exterior face centers.
 * A failed budget can retain diagnostic arrays, but errors remain uncertified
 * and the result is not publishable. Existing production values() stays gated.
 */
/** Derive an initial potential work target from current source and ideal B.
 * Source norm is a scale for a proposal, NOT a lower bound for final RHS.
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
    const auto source_error=bound_isolated_gravity_source(op,ring_density_,computed_source);
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
    result.initial_tolerance=std::max(atol,down(rtol*result.source_norm_lower));
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
    const auto& base=op.base();
    for(int i=0;i<op.size();++i)for(int axis=0;axis<2;++axis) {
        const auto& cell=op.cells()[i];
        const double center=op.center(i)[axis],half=.5*op.width(i,axis);
        if(!exact_root_coordinate(base.origin[axis],base.spacing[axis],
                double(cell.index[axis]),cell.level,center-half)
            ||!exact_root_coordinate(base.origin[axis],base.spacing[axis],
                double(cell.index[axis])+1.,cell.level,center+half))return result;
    }
    for(const auto& face:op.faces())if(face.boundary_side>=0) {
        const int owner=face.left>=0?face.left:face.right;
        const auto& cell=op.cells()[owner];
        for(int axis=0;axis<2;++axis) {
            const double index=double(cell.index[axis])+(axis==face.axis
                ? double(face.boundary_side%2):.5);
            if(!exact_root_coordinate(base.origin[axis],base.spacing[axis],
                index,cell.level,face.center[axis]))return result;
        }
    }
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
    result.source_error=bound_isolated_gravity_source(op,ring_density_,computed_source);
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
    result.source_error=bound_isolated_gravity_source(op,ring_density_,source);
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
    result.conditional=op.assess_boundary_residual(rhs,residual,combined,0.,
        result.native_residual_error.native_norm_upper,BoundaryErrorQuality::CertifiedAbsolute,
        rtol,atol,BoundaryResidualNormScope::RootDyadicRzWeights);
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
