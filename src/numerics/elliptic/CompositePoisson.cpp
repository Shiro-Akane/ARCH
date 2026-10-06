/**
 * @file CompositePoisson.cpp
 * @brief Assemble the conservative AMR Poisson operator on native geometry.
 *
 * Workflow:
 * 1. Validate a complete set of active leaves and obtain each volume from
 *    GridMetrics, including radial and angular Jacobians.
 * 2. Build one shared flux per physical face fragment. Resolve periodic
 *    azimuth, physical Dirichlet boundaries and zero-area regularity faces.
 * 3. Fit coarse-fine and boundary gradients to a quadratic basis, then apply
 *    A(phi) = -sum_f A_f (grad phi)_f / V_i with one interface flux owner.
 * 4. Provide a unique curved-face potential for the mass-flux work operator.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

#include "numerics/elliptic/CompositePoisson.h"

#include "core/CompensatedSum.h"
#include "grid/CoordinateBoundary.h"
#include "physics/constant/PhysicalConstants.h"
#include "grid/GridMetrics.h"
#include "numerics/linalg/DenseWrap.h"

namespace arch::elliptic {
namespace {
/** Resolve a physical policy into per-side scalar boundary conditions. */
CompositeBoundary resolve_boundary(const EllipticMesh& mesh,BoundaryKind kind) {
    CompositeBoundary result;
    result.sides.fill(FaceBoundaryKind::Neumann);
    if(kind==BoundaryKind::Periodic) {
        result.sides.fill(FaceBoundaryKind::Periodic);
        result.constant_nullspace=true;
    } else if(kind==BoundaryKind::Dirichlet) {
        for(int axis=0;axis<mesh.dimension;++axis)
            for(int side=0;side<2;++side)
                result.sides[2*axis+side]=FaceBoundaryKind::Dirichlet;
    } else if(kind==BoundaryKind::RadialIsolated) {
        // One-dimensional symmetry encloses no source inside the inner radius.
        result.sides[0]=FaceBoundaryKind::Neumann;
        result.sides[1]=FaceBoundaryKind::Dirichlet;
    } else if(kind==BoundaryKind::CurvilinearIsolated) {
        // Full azimuth joins periodically. Every other nonzero-area exterior
        // face receives the free-space potential of the actual leaf mass.
        result.sides[0]=mesh.origin[0]==0. ? FaceBoundaryKind::Neumann : FaceBoundaryKind::Dirichlet;
        result.sides[1]=FaceBoundaryKind::Dirichlet;
        if(mesh.dimension==3) {
            const bool spherical=mesh.geometry==Geometry::Spherical;
            const double polar_high=mesh.origin[1]+mesh.cells[1]*mesh.spacing[1];
            result.sides[2]=spherical && mesh.origin[1]==0. ? FaceBoundaryKind::Neumann : FaceBoundaryKind::Dirichlet;
            result.sides[3]=spherical && std::abs(polar_high-arch::constants::math::pi)<1e-14
                ? FaceBoundaryKind::Neumann : FaceBoundaryKind::Dirichlet;
        }
        if (mesh.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
            // RZ has physical z boundaries, not an azimuthal periodic turn.
            result.sides[2]=FaceBoundaryKind::Dirichlet;
            result.sides[3]=FaceBoundaryKind::Dirichlet;
        } else {
            const int azimuth=mesh.dimension-1;
            result.sides[2*azimuth]=FaceBoundaryKind::Periodic;
            result.sides[2*azimuth+1]=FaceBoundaryKind::Periodic;
        }
    } else if(kind==BoundaryKind::User)
        throw std::invalid_argument("User boundary requires explicit conditions");
    else throw std::invalid_argument("Invalid composite boundary kind");
    // Every resolved legacy side publishes a consistent scalar policy so the
    // shared eliminated row can be read from one place for legacy and user data.
    for(int index=0;index<6;++index) {
        FaceBoundaryCondition condition;
        condition.kind=result.sides[index];
        if(condition.kind==FaceBoundaryKind::Dirichlet) {condition.a=1.;condition.b=0.;}
        else {condition.a=0.;condition.b=1.;}
        result.conditions[index]=condition;
    }
    return result;
}
}

/** Hash a composite level and logical cell coordinate for leaf lookup. */
std::size_t CompositeCellHash::operator()(const CompositeCell& c) const noexcept {
    std::size_t h = static_cast<std::size_t>(c.level);
    for (int i : c.index) h ^= static_cast<std::size_t>(i) + 0x9e3779b9u + (h<<6) + (h>>2);
    return h;
}
/** Return physical leaf width after dyadic refinement. */
double CompositePoisson::width(int cell, int axis) const {
    return std::ldexp(base_.spacing[axis],-cells_[cell].level);
}
/** Return the physical center of an active composite leaf. */
std::array<double,3> CompositePoisson::center(int cell) const {
    std::array<double,3> p{};
    for (int a=0;a<base_.dimension;++a) p[a]=base_.origin[a]+(cells_[cell].index[a]+0.5)*width(cell,a);
    return p;
}
/** Construct from a legacy named boundary policy; existing behavior is retained. */
CompositePoisson::CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells, BoundaryKind kind)
    : CompositePoisson(base,std::move(cells),kind,resolve_boundary(base,kind),nullptr) {}
/** Explicit per-side policy; public inputs undergo the full physical checks. */
CompositePoisson::CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
    CompositeBoundary boundary)
    : CompositePoisson(base,std::move(cells),BoundaryKind::User,std::move(boundary),nullptr) {}
/** Only CompositeMultigrid can establish controlled coarse provenance. */
CompositePoisson::CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
    BoundaryKind kind,const CompositePoisson* fine)
    : CompositePoisson(base,std::move(cells),kind,resolve_boundary(base,kind),fine) {}
CompositePoisson::CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
    CompositeBoundary boundary,const CompositePoisson* fine)
    : CompositePoisson(base,std::move(cells),BoundaryKind::User,std::move(boundary),fine) {}
/** One assembly owner for legacy/explicit policies and controlled hierarchy levels. */
CompositePoisson::CompositePoisson(CartesianMesh base, std::vector<CompositeCell> cells,
    BoundaryKind kind,CompositeBoundary boundary,const CompositePoisson* fine)
    : base_(base), kind_(kind), boundary_(std::move(boundary)),
      cells_(std::move(cells)) {
    if (!fine) validate_mesh(base_,kind==BoundaryKind::CurvilinearIsolated);
    else {
        detail::validate_mesh_geometry(base_,kind==BoundaryKind::CurvilinearIsolated);
        const auto& parent=fine->base();
        if (base_.dimension!=parent.dimension || base_.geometry!=parent.geometry ||
            base_.semantics!=parent.semantics || kind!=fine->boundary_kind())
            throw std::invalid_argument("Composite coarse derivation changes geometry/boundary");
        bool changed=false;
        for (int a=0;a<3;++a) {
            if (base_.origin[a]!=parent.origin[a])
                throw std::invalid_argument("Composite coarse derivation changes origin");
            const bool unchanged=base_.cells[a]==parent.cells[a] &&
                                 base_.spacing[a]==parent.spacing[a];
            const bool halved=a<base_.dimension && !fine->max_level() &&
                parent.cells[a]>4 && base_.cells[a]==parent.cells[a]/2 &&
                base_.spacing[a]==2.*parent.spacing[a];
            if (!unchanged && !halved)
                throw std::invalid_argument("Composite coarse derivation is not controlled dyadic coarsening");
            changed|=halved;
            if (base_.cells[a]*base_.spacing[a]!=parent.cells[a]*parent.spacing[a])
                throw std::invalid_argument("Composite coarse derivation changes domain");
        }
        if (!fine->max_level() && !changed)
            throw std::invalid_argument("Composite coarse derivation makes no progress");
    }
    if (kind != BoundaryKind::Periodic && kind != BoundaryKind::Dirichlet &&
        kind != BoundaryKind::RadialIsolated && kind != BoundaryKind::CurvilinearIsolated &&
        kind != BoundaryKind::User)
        throw std::invalid_argument("Invalid composite boundary kind");
    prepare_boundary();
    if (kind != BoundaryKind::User && (base_.geometry == Geometry::Cartesian
            ? (kind != BoundaryKind::Periodic && kind != BoundaryKind::Dirichlet)
            : (base_.dimension == 1 ? kind != BoundaryKind::RadialIsolated
                                    : kind != BoundaryKind::CurvilinearIsolated)))
        throw std::invalid_argument("Composite gravity geometry/boundary mismatch");
    if (cells_.empty() || cells_.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Composite mesh has invalid cell count");
    double base_volume=1.;
    for (int a=0;a<base_.dimension;++a) base_volume*=base_.spacing[a];
    if (!std::isfinite(base_volume) || base_volume<=0.) throw std::invalid_argument("Composite cell volume out of range");
    arch::math::CompensatedSum covered;
    for (int i=0;i<size();++i) {
        const auto& c=cells_[i];
        if (c.level<0 || c.level>15) throw std::invalid_argument("Composite refinement level out of range");
        for (int a=0;a<3;++a) {
            const auto extent=static_cast<long long>(base_.cells[a]) << (a<base_.dimension ? c.level : 0);
            if (extent>std::numeric_limits<int>::max() || c.index[a]<0 || c.index[a]>=extent)
                throw std::invalid_argument("Composite logical coordinate out of range");
        }
        if (!lookup_.emplace(c,i).second) throw std::invalid_argument("Duplicate composite leaf");
        max_level_=std::max(max_level_,c.level);
        const double fraction=std::ldexp(1.,-base_.dimension*c.level);
        double volume=base_volume*fraction;
        if (base_.geometry != Geometry::Cartesian) {
            std::array<double,3> lower=base_.origin, widths=base_.spacing;
            for(int a=0;a<base_.dimension;++a) {
                widths[a]=std::ldexp(base_.spacing[a],-c.level);
                lower[a]+=c.index[a]*widths[a];
            }
            const auto geometry=base_.geometry==Geometry::Cylindrical
                ? GridMetrics::Geometry::Cylindrical : GridMetrics::Geometry::Spherical;
            auto view=GridMetrics::make_geometry_view(geometry,base_.dimension,lower,widths);
            view.semantics=base_.semantics;
            volume=GridMetrics::CellVolume(view,0,0,0);
        }
        if (!std::isfinite(volume) || volume<=0.) throw std::invalid_argument("Composite leaf volume out of range");
        volumes_.push_back(volume);
        weights_.push_back(volume);
        covered.add(fraction);
    }
    // Dyadic volumes are exactly representable; overlap and holes are rejected.
    if (covered.value()!=base_.size()) throw std::invalid_argument("Composite leaves do not cover the domain");
    for (const auto& cell:cells_) {
        auto parent=cell;
        while (parent.level>0) {
            --parent.level;
            for (int a=0;a<base_.dimension;++a) parent.index[a]/=2;
            if (lookup_.contains(parent)) throw std::invalid_argument("Composite leaves overlap an ancestor");
        }
    }
    arch::math::CompensatedSum total_volume;
    for (double volume:volumes_) total_volume.add(volume);
    for (double& weight:weights_) weight/=total_volume.value();
    build_faces();
}
/** Validate the per-side policy and derive its nullspace and fit-shape flags.
 *  Dirichlet keeps a=1,b=0, Neumann a=0,b=1, Robin finite a>=0,b>0 and every
 *  periodic pair is mandatory. Periodicity is carried by sides; the matching
 *  coefficient pair of a periodic side is unused and never scanned below. */
void CompositePoisson::prepare_boundary() {
    for(int axis=0;axis<3;++axis) for(int side=0;side<2;++side) {
        const int index=2*axis+side;
        if(axis>=base_.dimension) continue;
        if(boundary_.sides[index]==FaceBoundaryKind::Periodic) continue;
        const auto& condition=boundary_.conditions[index];
        if(condition.kind!=boundary_.sides[index])
            throw std::invalid_argument("Composite boundary conditions disagree with sides");
        if(!std::isfinite(condition.a) || !std::isfinite(condition.b))
            throw std::invalid_argument("Composite boundary coefficients are not finite");
        if(condition.kind==FaceBoundaryKind::Dirichlet) {
            if(condition.a!=1. || condition.b!=0.)
                throw std::invalid_argument("Dirichlet boundary requires a=1,b=0");
        } else if(condition.kind==FaceBoundaryKind::Neumann) {
            if(condition.a!=0. || condition.b!=1.)
                throw std::invalid_argument("Neumann boundary requires a=0,b=1");
        } else if(condition.kind==FaceBoundaryKind::Robin) {
            if(!(condition.a>=0.) || !(condition.b>0.))
                throw std::invalid_argument("Robin boundary requires a>=0,b>0");
        } else throw std::invalid_argument("Composite boundary requires a known face kind");
    }
    for(int axis=0;axis<base_.dimension;++axis)
        if((boundary_.sides[2*axis]==FaceBoundaryKind::Periodic) !=
           (boundary_.sides[2*axis+1]==FaceBoundaryKind::Periodic))
            throw std::invalid_argument("Composite periodic boundary requires paired sides");
    periodic_only_=true;
    bool positive_a=false;
    for(int axis=0;axis<base_.dimension;++axis) for(int side=0;side<2;++side) {
        if(boundary_.sides[2*axis+side]==FaceBoundaryKind::Periodic) continue;
        periodic_only_=false;
        if(boundary_.conditions[2*axis+side].a>0.) positive_a=true;
    }
    // A positive Dirichlet/Robin weight makes the operator nonsingular; a pure
    // flux/periodic policy always leaves the constant mode free.
    if(positive_a && boundary_.constant_nullspace)
        throw std::invalid_argument("Composite positive boundary weight contradicts the constant nullspace");
    if(!positive_a) boundary_.constant_nullspace=true;
    // The 1D radial isolated fit needs its cubic regularity basis for the
    // legacy kind and for an equivalent explicit user policy.
    radial_=base_.geometry!=Geometry::Cartesian && base_.dimension==1 &&
        boundary_.sides[0]==FaceBoundaryKind::Neumann &&
        boundary_.sides[1]==FaceBoundaryKind::Dirichlet;
}
/** Find the unique active leaf covering a root-grid coordinate. */
int CompositePoisson::locate(std::array<double,3> point) const {
    for (int a=0;a<base_.dimension;++a) {
        if (boundary_.sides[2*a]==FaceBoundaryKind::Periodic &&
            boundary_.sides[2*a+1]==FaceBoundaryKind::Periodic) {
            point[a]=std::fmod(point[a],base_.cells[a]);
            if (point[a]<0.) point[a]+=base_.cells[a];
        } else if (point[a]<0. || point[a]>=base_.cells[a]) {
            throw std::invalid_argument("Point outside the nonperiodic composite domain");
        }
    }
    for (int level=0;level<=max_level_;++level) {
        CompositeCell cell; cell.level=level;
        for (int a=0;a<base_.dimension;++a) cell.index[a]=static_cast<int>(std::floor(std::ldexp(point[a],level)));
        const auto found=lookup_.find(cell);
        if (found!=lookup_.end()) return found->second;
    }
    throw std::invalid_argument("Composite mesh contains a hole");
}
/** Compute the exact physical area of one face fragment via shared Grid metrics. */
double CompositePoisson::face_area(const CompositeFace& face) const {
    if(base_.geometry==Geometry::Cartesian) return face.area;
    const int anchor=face.left>=0?face.left:face.right;
    std::array<double,3> lower=face.center,widths=base_.spacing;
    // A lower physical face is already its fragment's lower endpoint. Do not
    // subtract/add one width: that cancellation can erase a tiny positive r.
    const bool high_face=face.boundary_side<0 || (face.boundary_side%2)!=0;
    for(int a=0;a<base_.dimension;++a) {
        widths[a]=face.fragment_width[a]>0.?face.fragment_width[a]:width(anchor,a);
        lower[a]-=a==face.axis?(high_face?widths[a]:0.):0.5*widths[a];
    }
    const auto geometry=base_.geometry==Geometry::Cylindrical
        ? GridMetrics::Geometry::Cylindrical : GridMetrics::Geometry::Spherical;
    auto view=GridMetrics::make_geometry_view(geometry,base_.dimension,lower,widths);
    view.semantics=base_.semantics;
    return GridMetrics::FaceArea(view,face.axis,0,0,0,high_face);
}

/** Return physical length per native coordinate at a face center. */
double CompositePoisson::face_metric(const CompositeFace& face,int axis) const {
    if(base_.geometry==Geometry::Cartesian || axis==0
        || base_.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) return 1.;
    const auto geometry=base_.geometry==Geometry::Cylindrical
        ? GridMetrics::Geometry::Cylindrical : GridMetrics::Geometry::Spherical;
    return GridMetrics::PhysicalSpacing(geometry,base_.dimension,axis,1.,1.,1.,
        face.center[0],face.center[1]);
}

/** Create one oriented conservative face contribution per cell interface. */
void CompositePoisson::build_faces() {
    neighbors_.resize(size());
    for (int i=0;i<size();++i) for (int a=0;a<base_.dimension;++a) {
        const auto& c=cells_[i];
        const double unit=std::ldexp(1.,-c.level);
        if (boundary_.sides[2*a]!=FaceBoundaryKind::Periodic) {
            for (int side=0;side<2;++side) {
                const int edge=side ? (base_.cells[a]<<c.level)-1 : 0;
                if (c.index[a]!=edge) continue;
                // Periodic seams are internal faces. Every nonzero-area
                // Dirichlet/Neumann/Robin side publishes one physical record;
                // zero-area coordinate limits (origin/pole) stay regular.
                if (boundary_.sides[2*a+side]==FaceBoundaryKind::Periodic) continue;
                const double coordinate=base_.origin[a]+(side ? base_.cells[a]*base_.spacing[a] : 0.);
                if(GridMetrics::IsCoordinateJoin(base_.geometry,base_.dimension,a,coordinate)) continue;
                CompositeFace f;
                f.left=side ? i : -1; f.right=side ? -1 : i;
                f.axis=a; f.boundary_side=2*a+side; f.center=center(i); f.area=1.;
                // Preserve the configured physical endpoint, including a
                // positive radius smaller than a cell-center rounding unit.
                f.center[a]=coordinate;
                for(int t=0;t<base_.dimension;++t) if(t!=a) f.area*=width(i,t);
                for(int t=0;t<base_.dimension;++t) f.fragment_width[t]=width(i,t);
                f.area=face_area(f);
                if(!(f.area>0.)) continue;
                const double spacing=width(i,a)*face_metric(f,a);
                // Start from the legacy Dirichlet two-point row; the per-side
                // policy is applied once, after the shared quadratic fit.
                f.samples.push_back(i); f.coefficients.push_back((side ? -2. : 2.)/spacing);
                f.boundary_coefficient=-f.coefficients[0];
                faces_.push_back(std::move(f));
            }
            if (c.index[a]==(base_.cells[a]<<c.level)-1) continue;
        }
        std::array<double,3> point{};
        for (int t=0;t<base_.dimension;++t) point[t]=(c.index[t]+0.5)*unit;
        point[a]=(c.index[a]+1.125)*unit;
        const int adjacent=locate(point);
        if (std::abs(c.level-cells_[adjacent].level)>1) throw std::invalid_argument("Composite faces require 2:1 balance");
        const int pieces=cells_[adjacent].level>c.level ? (1<<(base_.dimension-1)) : 1;
        for (int piece=0;piece<pieces;++piece) {
            auto sample=point;
            int bit=0;
            for (int t=0;t<base_.dimension;++t) if (t!=a) {
                if (pieces>1) sample[t]=(c.index[t]+(((piece>>bit)&1) ? 0.75 : 0.25))*unit;
                ++bit;
            }
            const int j=locate(sample);
            if (std::abs(c.level-cells_[j].level)>1) throw std::invalid_argument("Composite face is not balanced");
            CompositeFace face; face.left=i; face.right=j; face.axis=a; face.area=1.;
            for (int t=0;t<base_.dimension;++t) {
                face.center[t]=base_.origin[t]+sample[t]*base_.spacing[t];
                if (t!=a) face.area*=std::min(width(i,t),width(j,t));
            }
            face.center[a]=base_.origin[a]+(c.index[a]+1.)*unit*base_.spacing[a];
            for(int t=0;t<base_.dimension;++t)
                face.fragment_width[t]=t==a?width(i,t):std::min(width(i,t),width(j,t));
            face.area=face_area(face);
            const double inverse=1./((0.5*width(i,a)+0.5*width(j,a))*face_metric(face,a));
            // grad_f(phi) = (phi_R-phi_L)/(0.5*h_L+0.5*h_R).
            face.samples={i,j}; face.coefficients={-inverse,inverse};
            faces_.push_back(std::move(face));
            neighbors_[i].push_back(j); neighbors_[j].push_back(i);
        }
    }
    for (auto& neighbors:neighbors_) {
        std::sort(neighbors.begin(),neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(),neighbors.end()),neighbors.end());
    }
    for (auto& face:faces_) {
        if (radial_ || face.boundary_side>=0 ||
            cells_[face.left].level!=cells_[face.right].level) fit_interface(face);
        if(face.boundary_side>=0) {
            // Fit the original Dirichlet row first, then transform it into the
            // eliminated a*Phi+b*dPhi/dn=c row exactly once on the Host.
            const std::vector<double> fitted=face.coefficients;
            const double fitted_qB=face.boundary_coefficient;
            fit_boundary_face_value(face,fitted.data(),fitted_qB);
            eliminate_boundary(face,fitted.data(),fitted_qB);
        } else if(base_.geometry!=Geometry::Cartesian) fit_curved_face_value(face);
    }
    // Quadratic reproduction may assign a negative self coefficient on a
    // strongly anisotropic coarse/fine fragment next to a coordinate join.
    // Recover a positive elliptic diagonal locally with the conservative
    // two-point flux on only those incident fitted faces. The same fragment
    // still owns one shared flux; no boundary budget or solver tolerance moves.
    const auto assemble_diagonal=[&] {
        diagonal_.assign(size(),0.);
        for (const auto& f:faces_) for (std::size_t k=0;k<f.samples.size();++k) {
            if (f.samples[k]==f.left) diagonal_[f.left]-=f.area*f.coefficients[k]/volumes_[f.left];
            if (f.samples[k]==f.right) diagonal_[f.right]+=f.area*f.coefficients[k]/volumes_[f.right];
        }
    };
    std::vector<unsigned char> recovered(faces_.size(),0);
    for (std::size_t pass=0;pass<=faces_.size();++pass) {
        assemble_diagonal();
        std::vector<unsigned char> invalid_mask(size(),0);
        int first_invalid=-1;
        for (int i=0;i<size();++i)
            if (!std::isfinite(diagonal_[i]) || diagonal_[i]<=0.) {
                invalid_mask[i]=1;
                if(first_invalid<0) first_invalid=i;
            }
        if (first_invalid<0) break;
        bool changed=false;
        for (std::size_t face_index=0;face_index<faces_.size();++face_index) {
            auto& f=faces_[face_index];
            if(recovered[face_index] ||
                !((f.left>=0 && invalid_mask[f.left]) ||
                  (f.right>=0 && invalid_mask[f.right]))) continue;
            const bool fitted=f.boundary_side>=0 || radial_
                || (f.left>=0 && f.right>=0 && cells_[f.left].level!=cells_[f.right].level);
            if(!fitted) continue;
            if(f.boundary_side>=0) {
                // grad_f(phi) = (phi_B-phi_C)/(h_C/2), with the
                // existing left/right orientation carried by sign.
                const int anchor=f.left>=0?f.left:f.right;
                const double inverse=2./(width(anchor,f.axis)*face_metric(f,f.axis));
                const double sign=f.left>=0?-1.:1.;
                f.samples.clear(); f.samples.push_back(anchor);
                f.coefficients.clear(); f.coefficients.push_back(sign*inverse);
                f.boundary_coefficient=-sign*inverse;
                // The recovered fragment is a Dirichlet row again, so the
                // side policy is eliminated on top of it.
                eliminate_boundary(f,f.coefficients.data(),f.boundary_coefficient);
            } else {
                // grad_f(phi) = (phi_R-phi_L)/(h_L/2+h_R/2).
                const double inverse=1./((0.5*width(f.left,f.axis)
                    +0.5*width(f.right,f.axis))*face_metric(f,f.axis));
                f.samples.clear(); f.samples.push_back(f.left);
                f.samples.push_back(f.right);
                f.coefficients.clear(); f.coefficients.push_back(-inverse);
                f.coefficients.push_back(inverse);
                f.boundary_coefficient=0.;
            }
            f.construction=FaceStencilConstruction::EllipticRecovery;
            recovered[face_index]=1; changed=true;
        }
        if(changed) continue;
        const int i=first_invalid;
        throw std::invalid_argument("Invalid composite diagonal at cell "
            +std::to_string(i)+" level "+std::to_string(cells_[i].level)
            +" logical ("+std::to_string(cells_[i].index[0])+","
            +std::to_string(cells_[i].index[1])+","
            +std::to_string(cells_[i].index[2])+") value "
            +std::to_string(diagonal_[i]));
    }
}

namespace {
/** Build the constant, linear and quadratic basis for interface reproduction. */
std::array<double,10> polynomial(const std::array<double,3>& x,int dim) {
    std::array<double,10> p{}; p[0]=1.;
    for (int a=0;a<dim;++a) p[1+a]=x[a];
    int slot=1+dim;
    for (int a=0;a<dim;++a) for (int b=a;b<dim;++b) p[slot++]=x[a]*x[b];
    return p;
}
}
/** Correct a coarse-fine face stencil to reproduce quadratic gradients. */
void CompositePoisson::fit_interface(CompositeFace& f) const {
    // Minimum weighted correction of the normal two-point gradient, subject
    // to reproduction of every quadratic polynomial. Tangential offsets matter.
    const int anchor_cell=f.left>=0 ? f.left : f.right;
    std::vector<int> samples=f.samples;
    for (int depth=0;depth<(kind_==BoundaryKind::RadialIsolated?3:2);++depth) {
        const auto current=samples;
        for (int i:current) samples.insert(samples.end(),neighbors_[i].begin(),neighbors_[i].end());
        std::sort(samples.begin(),samples.end());
        samples.erase(std::unique(samples.begin(),samples.end()),samples.end());
    }
    const double scale=(f.boundary_side>=0 ? width(anchor_cell,f.axis)
        : std::max(width(f.left,f.axis),width(f.right,f.axis)))*face_metric(f,f.axis);
    const int dim=base_.dimension;
    const bool radial=kind_==BoundaryKind::RadialIsolated;
    const int terms=radial?4:1+dim+dim*(dim+1)/2;
    std::vector<std::array<double,10>> basis;
    std::vector<double> weights, initial;
    DenseMatrixData<10> gram;
    double right[10]{}; right[1+f.axis]=1.;
    if(f.boundary_side>=0) {
        gram.data[0][0]=1.;
        right[0]-=f.boundary_coefficient*scale;
    }
    for (int i:samples) {
        auto delta=center(i);
        double distance=0.;
        for (int a=0;a<dim;++a) {
            const double length=base_.cells[a]*base_.spacing[a];
            delta[a]-=f.center[a];
            if(boundary_.sides[2*a]==FaceBoundaryKind::Periodic)
                delta[a]-=std::floor(delta[a]/length+0.5)*length;
            delta[a]*=face_metric(f,a)/scale; distance+=delta[a]*delta[a];
        }
        auto p=polynomial(delta,dim);
        if(radial)p[3]=delta[0]*delta[0]*delta[0];
        const double weight=1./((1.+distance)*(1.+distance));
        double value=0.;
        for(std::size_t j=0;j<f.samples.size();++j)
            if(i==f.samples[j]) value=f.coefficients[j]*scale;
        basis.push_back(p); weights.push_back(weight); initial.push_back(value);
        for (int r=0;r<terms;++r) {
            right[r]-=value*p[r];
            for (int c=0;c<terms;++c) gram.data[r][c]+=weight*p[r]*p[c];
        }
    }
    // Minimize ||c-c0||_(W^-1)^2 subject to P*c=d. With
    // G=P*W*P^T, lambda=G^-1(d-P*c0), c=c0+W*P^T*lambda.
    const bool solved=radial ? DenseLUSolver::solve<4,10>(gram,right)
        : dim==1 ? DenseLUSolver::solve<3,10>(gram,right)
        : dim==2 ? DenseLUSolver::solve<6,10>(gram,right) : DenseLUSolver::solve<10,10>(gram,right);
    if (!solved) throw std::invalid_argument("Degenerate composite interface interpolation");
    if(f.boundary_side>=0) f.boundary_coefficient+=right[0]/scale;
    f.samples=std::move(samples); f.coefficients.resize(f.samples.size());
    double sum=0.; std::size_t anchor=0;
    for (std::size_t i=0;i<f.samples.size();++i) {
        double value=initial[i];
        for (int r=0;r<terms;++r) value+=weights[i]*basis[i][r]*right[r];
        f.coefficients[i]=value/scale;
        if (f.samples[i]==anchor_cell) anchor=i; else sum+=f.coefficients[i];
    }
    f.coefficients[anchor]=-sum-f.boundary_coefficient;
    f.construction=FaceStencilConstruction::PolynomialFit;
}

/** Fit a unique curved-face potential for both sides of the mass-flux work. */
void CompositePoisson::fit_curved_face_value(CompositeFace& face) const {
    if(face.boundary_side>=0) {
        face.value_boundary_coefficient=1.;
        return;
    }
    if(base_.dimension>1) {
        // Both cells use the same linearly reconstructed face potential in
        // the conservative mass-flux work. The Poisson face derivative keeps
        // its separately fitted quadratic interface stencil.
        const int left=face.left,right=face.right,axis=face.axis;
        const double dl=0.5*width(left,axis),dr=0.5*width(right,axis);
        face.value_samples={left,right};
        face.value_coefficients={dr/(dl+dr),dl/(dl+dr)};
        if(base_.semantics==GridMetrics::GeometrySemantics::AxisymmetricRz
            && cells_[left].level!=cells_[right].level) {
            // A coarse/fine pair is tangentially displaced. Its normal-only
            // interpolation is not Phi at this fragment's physical center.
            // Correct the same unique face value to reproduce affine r/z,
            // using the original derivative stencil's neighborhood and LU.
            const auto samples=face.samples;
            const double scale=std::max(width(left,axis),width(right,axis));
            DenseMatrixData<10> gram;
            double right_hand[10]{1.,0.,0.};
            std::vector<std::array<double,3>> basis;
            std::vector<double> weights,initial;
            for(int cell:samples) {
                const auto x=center(cell);
                const std::array<double,3> p{1.,(x[0]-face.center[0])/scale,
                    (x[1]-face.center[1])/scale};
                const double distance=p[1]*p[1]+p[2]*p[2];
                const double weight=1./((1.+distance)*(1.+distance));
                const double value=cell==left?dr/(dl+dr):(cell==right?dl/(dl+dr):0.);
                basis.push_back(p);weights.push_back(weight);initial.push_back(value);
                for(int row=0;row<3;++row) {
                    right_hand[row]-=value*p[row];
                    for(int col=0;col<3;++col)gram.data[row][col]+=weight*p[row]*p[col];
                }
            }
            if(!DenseLUSolver::solve<3,10>(gram,right_hand))
                throw std::invalid_argument("Degenerate RZ face-potential interpolation");
            face.value_samples=samples;face.value_coefficients.resize(samples.size());
            double sum=0.;std::size_t anchor=0;
            for(std::size_t k=0;k<samples.size();++k) {
                double value=initial[k];
                for(int row=0;row<3;++row)value+=weights[k]*basis[k][row]*right_hand[row];
                face.value_coefficients[k]=value;
                if(samples[k]==left)anchor=k;else sum+=value;
            }
            face.value_coefficients[anchor]=1.-sum;
        }
        return;
    }
    auto candidates=face.samples;
    const double position=face.center[0];
    std::sort(candidates.begin(),candidates.end(),[&](int left,int right) {
        const double a=std::abs(center(left)[0]-position);
        const double b=std::abs(center(right)[0]-position);
        return a==b?left<right:a<b;
    });
    candidates.resize(std::min<std::size_t>(4,candidates.size()));
    face.value_samples=candidates;
    // Lagrange interpolation reproduces cubic Phi at a shared physical
    // face. Coarse/fine neighbors consume the same Phi_f, so the resulting
    // work is a single conservative face contribution.
    for(int cell:candidates) {
        const double xi=center(cell)[0];
        double coefficient=1.;
        for(int other:candidates) if(other!=cell)
            coefficient*=(position-center(other)[0])/(xi-center(other)[0]);
        face.value_coefficients.push_back(coefficient);
    }
}

/** Eliminate one physical side policy into an exact linear face row.
 *  With s=-1 on the lower side and s=+1 on the upper side the fitted Dirichlet
 *  row g=Q*x+qB*Phi_B and a*Phi_B+b*dPhi/dn=c give the contract elimination
 *  Phi_B=(c-b*s*Q*x)/(a+b*s*qB) and g=(a*Q*x+qB*c)/(a+b*s*qB). Storing
 *  alpha=a/D and beta=qB/D produces the difference row
 *  g=sum_i alpha*Q_i*(Phi_i-Phi_A)+beta*c-alpha*qB*Phi_A.
 *  Flux sides store the last weight directly; computing beta-alpha*qB and
 *  cancelling beta*Phi_A later would erase a small prescribed Neumann flux.
 *  Dirichlet retains its legacy difference row and zero anchor weight. */
void CompositePoisson::eliminate_boundary(CompositeFace& face,const double* fitted_coefficients,
                                          double fitted_qB) const {
    const int side=face.boundary_side&1;
    const auto& condition=boundary_.conditions[face.boundary_side];
    const double s=side?1.:-1.;
    const double denominator=condition.a+condition.b*s*fitted_qB;
    if(!std::isfinite(denominator) || denominator<=0.)
        throw std::invalid_argument("Composite boundary elimination denominator is not positive");
    const double alpha=condition.a/denominator,beta=fitted_qB/denominator;
    for(std::size_t k=0;k<face.coefficients.size();++k)
        face.coefficients[k]=alpha*fitted_coefficients[k];
    face.boundary_coefficient=beta;
    face.anchor_coefficient=condition.kind==FaceBoundaryKind::Dirichlet
        ?0.:-alpha*fitted_qB;
}

/** Interpolate the eliminated boundary potential used by the mass-flux work.
 *  Dirichlet keeps the legacy unit map; every flux side publishes
 *  Phi_B=(c-b*s*Q*x)/D through samples/coefficients/value_boundary_coefficient. */
void CompositePoisson::fit_boundary_face_value(CompositeFace& face,
    const double* fitted_coefficients,double fitted_qB) const {
    const int side=face.boundary_side&1;
    const auto& condition=boundary_.conditions[face.boundary_side];
    if(condition.kind==FaceBoundaryKind::Dirichlet) {
        face.value_samples.clear(); face.value_coefficients.clear();
        face.value_boundary_coefficient=1.;
        return;
    }
    const double s=side?1.:-1.;
    const double denominator=condition.a+condition.b*s*fitted_qB;
    face.value_samples=face.samples;
    face.value_coefficients.assign(face.samples.size(),0.);
    for(std::size_t k=0;k<face.samples.size();++k)
        face.value_coefficients[k]=-condition.b*s*fitted_coefficients[k]/denominator;
    face.value_boundary_coefficient=1./denominator;
}

/** Apply the face stencil to a cell-centered field and boundary datum. */
double CompositePoisson::face_gradient(std::span<const double> x,const CompositeFace& f,double boundary_value) const {
    const int anchor=f.left>=0 ? f.left : f.right;
    return composite_face_gradient(x.data(),anchor,f.samples.data(),f.coefficients.data(),
        f.samples.size(),f.boundary_coefficient,boundary_value,f.anchor_coefficient,
        has_flux_boundary(f));
}
/** Accumulate A u = -sum_f(area_f/V_i) grad_f(u) over oriented faces. */
void CompositePoisson::apply(std::span<const double> x,std::span<double> out) const {
    if (x.size()!=cells_.size() || out.size()!=cells_.size() || x.data()==out.data())
        throw std::invalid_argument("Composite apply requires distinct matching arrays");
    std::fill(out.begin(),out.end(),0.);
    for (const auto& f:faces_) {
        // (A phi)_cell = -sum(oriented area_f * grad_f(phi))/volume_cell.
        const double flux=f.area*face_gradient(x,f);
        if(f.left>=0) out[f.left]-=flux/volumes_[f.left];
        if(f.right>=0) out[f.right]+=flux/volumes_[f.right];
    }
}
/** Return the volume-weighted composite mean with scale-safe summation. */
double CompositePoisson::mean(std::span<const double> x) const {
    if (x.size()!=cells_.size()) throw std::invalid_argument("Composite mean extent mismatch");
    double scale=0.;
    for (double v:x) { if (!std::isfinite(v)) return std::numeric_limits<double>::infinity(); scale=std::max(scale,std::abs(v)); }
    if (scale==0.) return 0.;
    arch::math::CompensatedSum sum;
    for (int i=0;i<size();++i) sum.add(weights_[i]*(x[i]/scale));
    return scale*sum.value();
}
/** Return the volume-weighted inner product of two fields. */
double CompositePoisson::dot(std::span<const double> x,std::span<const double> y) const {
    if (x.size()!=cells_.size() || y.size()!=cells_.size()) throw std::invalid_argument("Composite dot extent mismatch");
    arch::math::CompensatedSum sum;
    for (int i=0;i<size();++i) sum.add(weights_[i]*x[i]*y[i]);
    return sum.value();
}
/** Return the volume-weighted RMS, scaling first to avoid overflow. */
double CompositePoisson::norm(std::span<const double> x) const {
    if (x.size()!=cells_.size()) throw std::invalid_argument("Composite norm extent mismatch");
    double scale=0.;
    for (double v:x) { if (!std::isfinite(v)) return std::numeric_limits<double>::infinity(); scale=std::max(scale,std::abs(v)); }
    if (scale==0.) return 0.;
    arch::math::CompensatedSum sum;
    for (int i=0;i<size();++i) { const double q=x[i]/scale; sum.add(weights_[i]*q*q); }
    return scale*std::sqrt(sum.value());
}
/** Remove the periodic constant mode; leave Dirichlet fields unchanged. */
void CompositePoisson::project(std::span<double> x) const {
    if(!boundary_.constant_nullspace) return;
    const double average=mean(x);
    for (double& v:x) v-=average;
}
/** Reject a source that no pure flux boundary can balance.
 *  The volume-weighted mean is compared against the documented FP64 roundoff
 *  bound, so accumulated roundoff is accepted while a genuine mass imbalance
 *  (in particular nonzero mass with homogeneous Neumann data) is rejected
 *  before any projection. Periodic sources keep their legacy projection. */
void CompositePoisson::validate_compatibility(std::span<const double> rhs) const {
    validate_values(rhs,cells_.size());
    if(!boundary_.constant_nullspace || periodic_only_) return;
    double scale=0.;
    for(double value:rhs) {
        if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite composite source");
        scale=std::max(scale,std::abs(value));
    }
    if(scale==0.) return;
    if(std::abs(mean(rhs))>compatibility_roundoff*scale)
        throw std::invalid_argument("Composite source violates the pure flux compatibility condition");
}
/** Move the oriented boundary datum flux of every physical side to the source.
 *  The stored boundary_coefficient is beta=qB/D, so area*beta*c is exactly the
 *  datum part of the eliminated Dirichlet/Neumann/Robin row. */
std::vector<double> CompositePoisson::effective_rhs(std::span<const double> source,
                                                  std::span<const double> boundary_values) const {
    validate_values(source,size());
    if(boundary_values.empty() && boundary_.constant_nullspace)
        return {source.begin(),source.end()};
    validate_values(boundary_values,faces_.size());
    std::vector<double> rhs(source.begin(),source.end());
    for(std::size_t i=0;i<faces_.size();++i) {
        const auto& f=faces_[i];
        const double flux=f.area*f.boundary_coefficient*boundary_values[i];
        if(f.left>=0) rhs[f.left]+=flux/volumes_[f.left];
        if(f.right>=0) rhs[f.right]-=flux/volumes_[f.right];
    }
    return rhs;
}
/** Positive FP64 upper operations; exact zeros remain zero (no budget floor). */
namespace {
double bound_up(double value) {
    return value==0. ? 0. : std::nextafter(value,std::numeric_limits<double>::infinity());
}
double bound_product(double left,double right) {
    if(left==0. || right==0.)return 0.;
    // A positive product rounded to zero is bounded by the next subnormal.
    return std::nextafter(left*right,std::numeric_limits<double>::infinity());
}
double bound_quotient(double numerator,double denominator) {
    if(numerator==0.)return 0.;
    return std::nextafter(numerator/denominator,std::numeric_limits<double>::infinity());
}
}
/**
 * Map certified absolute face-potential errors into the canonical stored B.
 * The positive sum deliberately ignores cancellation. Estimates cannot enter.
 * Source/RHS construction and the potential evaluator need separate ledgers.
 */
BoundaryRhsError CompositePoisson::propagate_boundary_error(
    std::span<const BoundaryPotentialError> errors) const
{
    BoundaryRhsError result{};
    result.cell_bounds.assign(cells_.size(),0.);
    if(errors.empty() && boundary_.constant_nullspace) {
        result.status=BoundaryErrorStatus::Bounded;result.norm_upper=0.;return result;
    }
    if(errors.size()!=faces_.size())return result;
    for(std::size_t i=0;i<faces_.size();++i) {
        const auto& f=faces_[i];
        if(f.area==0. || f.boundary_coefficient==0.)continue;
        const auto& e=errors[i];
        if(!std::isfinite(e.absolute_error) || e.absolute_error<0.)return result;
        if(e.quality!=BoundaryErrorQuality::CertifiedAbsolute) {
            result.status=BoundaryErrorStatus::UncertifiedInput;return result;
        }
        const double face=bound_product(f.area,std::abs(f.boundary_coefficient));
        const auto add=[&](int cell) {
            if(cell<0)return;
            const double coefficient=bound_quotient(face,volumes_[cell]);
            const double contribution=bound_product(coefficient,e.absolute_error);
            if(contribution!=0.)
                result.cell_bounds[cell]=bound_up(result.cell_bounds[cell]+contribution);
        };
        add(f.left);add(f.right);
    }
    double scale=0.;
    for(double value:result.cell_bounds) {
        if(!std::isfinite(value)) {
            result.status=BoundaryErrorStatus::Overflow;return result;
        }
        scale=std::max(scale,value);
    }
    if(scale==0.) {
        result.status=BoundaryErrorStatus::Bounded;result.norm_upper=0.;return result;
    }
    double square_sum=0.;
    for(std::size_t i=0;i<result.cell_bounds.size();++i) {
        const double q=bound_quotient(result.cell_bounds[i],scale);
        const double term=bound_product(weights_[i],bound_product(q,q));
        if(term!=0.)square_sum=bound_up(square_sum+term);
    }
    result.norm_upper=bound_product(scale,bound_up(std::sqrt(square_sum)));
    result.status=std::isfinite(result.norm_upper)?BoundaryErrorStatus::Bounded
        :BoundaryErrorStatus::Overflow;
    return result;
}
namespace {
double bound_down(double value) {
    return value<=0. ? 0. : std::nextafter(value,0.);
}
}
/** Interval of the exact mathematical norm of stored x and stored native weights. */
WeightedNormInterval CompositePoisson::norm_interval(std::span<const double> x) const
{
    WeightedNormInterval result{};
    if(x.size()!=cells_.size())return result;
    double scale=0.;
    for(double value:x) {
        if(!std::isfinite(value))return result;
        scale=std::max(scale,std::abs(value));
    }
    if(scale==0.) {
        result.status=BoundaryErrorStatus::Bounded;result.lower=result.upper=0.;return result;
    }
    double lo=0.,hi=0.;
    for(std::size_t i=0;i<x.size();++i) {
        const double magnitude=std::abs(x[i]);
        if(magnitude==0.)continue;
        const double qlo=bound_down(magnitude/scale),qhi=bound_quotient(magnitude,scale);
        const double lower=bound_down(weights_[i]*bound_down(qlo*qlo));
        const double upper=bound_product(weights_[i],bound_product(qhi,qhi));
        if(lower!=0.)lo=bound_down(lo+lower);
        if(upper!=0.)hi=bound_up(hi+upper);
    }
    result.lower=bound_down(scale*bound_down(std::sqrt(lo)));
    result.upper=bound_product(scale,bound_up(std::sqrt(hi)));
    result.status=std::isfinite(result.upper)?BoundaryErrorStatus::Bounded
        :BoundaryErrorStatus::Overflow;
    return result;
}
namespace {
/** Outward basic arithmetic for canonical stored-coefficient companion ledgers.
 * No physics/kernel dependency. Encloses the mathematical expression rather
 * than assuming CompensatedSum or a computed residual is exact.
 */
struct ArithmeticRange {double lo=0.,hi=0.;};
bool finite_range(ArithmeticRange a) {
    return std::isfinite(a.lo)&&std::isfinite(a.hi)&&a.lo<=a.hi;
}
ArithmeticRange range_negate(ArithmeticRange a) {return {-a.hi,-a.lo};}
ArithmeticRange range_add(ArithmeticRange a,ArithmeticRange b) {
    if(a.lo==0.&&a.hi==0.)return b;
    if(b.lo==0.&&b.hi==0.)return a;
    if(a.lo==a.hi&&b.lo==b.hi&&a.lo==-b.lo)return {};
    return {std::nextafter(a.lo+b.lo,-std::numeric_limits<double>::infinity()),
            std::nextafter(a.hi+b.hi,std::numeric_limits<double>::infinity())};
}
ArithmeticRange range_product(ArithmeticRange a,ArithmeticRange b) {
    if((a.lo==0.&&a.hi==0.)||(b.lo==0.&&b.hi==0.))return {};
    const double v[]{a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};
    double lo=v[0],hi=v[0];
    for(double x:v){lo=std::min(lo,x);hi=std::max(hi,x);}
    return {std::nextafter(lo,-std::numeric_limits<double>::infinity()),
            std::nextafter(hi,std::numeric_limits<double>::infinity())};
}
ArithmeticRange range_divide_volume(ArithmeticRange a,double volume) {
    if(a.lo==0.&&a.hi==0.)return {};
    return {std::nextafter(a.lo/volume,-std::numeric_limits<double>::infinity()),
            std::nextafter(a.hi/volume,std::numeric_limits<double>::infinity())};
}
ArithmeticRange range_square(ArithmeticRange a) {
    if(a.lo==0.&&a.hi==0.)return {};
    const double v[]{a.lo*a.lo,a.hi*a.hi};
    const double lo=a.lo<=0.&&a.hi>=0.?0.:std::min(v[0],v[1]);
    return {lo==0.?0.:std::nextafter(lo,0.),
        std::nextafter(std::max(v[0],v[1]),std::numeric_limits<double>::infinity())};
}
ArithmeticRange range_divide_positive(ArithmeticRange a,ArithmeticRange b) {
    if(!finite_range(a)||!finite_range(b)||b.lo<=0.)
        return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
    if(a.lo==0.&&a.hi==0.)return {};
    return range_product(a,{std::nextafter(1./b.hi,0.),
        std::nextafter(1./b.lo,std::numeric_limits<double>::infinity())});
}
double range_abs_upper(ArithmeticRange a) {
    return finite_range(a)?std::max(std::abs(a.lo),std::abs(a.hi))
        :std::numeric_limits<double>::infinity();
}
/** Neumann inverse proof for every matrix in the Gram enclosure.
 * C and lambda_hat are only finite witnesses from the existing DenseLUSolver.
 * q=||I-C*G||inf<1 proves ||G^-1||inf <= ||C||inf/(1-q).
 * This also bounds lambda error from the full interval equation residual,
 * including ideal coordinate/basis/Gram construction, not just LU rounding.
 */
bool enclose_gram_solution(const std::array<std::array<ArithmeticRange,6>,6>& g,
    const std::array<ArithmeticRange,6>& rhs,std::array<ArithmeticRange,6>& lambda,
    double& q,double& inverse_upper,double& error_upper) {
    DenseMatrixData<10> midpoint;
    for(int i=0;i<6;++i)for(int j=0;j<6;++j) {
        if(!finite_range(g[i][j]))return false;
        midpoint.data[i][j]=.5*g[i][j].lo+.5*g[i][j].hi;
    }
    std::array<std::array<double,6>,6> inverse{};
    for(int col=0;col<6;++col) {
        auto matrix=midpoint;double x[10]{};x[col]=1.;
        if(!DenseLUSolver::solve<6,10>(matrix,x))return false;
        for(int row=0;row<6;++row)inverse[row][col]=x[row];
    }
    auto matrix=midpoint;double approximate[10]{};
    for(int i=0;i<6;++i) {
        if(!finite_range(rhs[i]))return false;
        approximate[i]=.5*rhs[i].lo+.5*rhs[i].hi;
    }
    if(!DenseLUSolver::solve<6,10>(matrix,approximate))return false;
    q=0.;double inverse_norm=0.,residual_norm=0.;
    for(int i=0;i<6;++i) {
        double row=0.,inverse_row=0.;
        for(int j=0;j<6;++j) {
            ArithmeticRange product{};
            for(int k=0;k<6;++k)product=range_add(product,
                range_product({inverse[i][k],inverse[i][k]},g[k][j]));
            const auto defect=range_add({i==j?1.:0.,i==j?1.:0.},range_negate(product));
            row=bound_up(row+range_abs_upper(defect));
            inverse_row=bound_up(inverse_row+std::abs(inverse[i][j]));
        }
        q=std::max(q,row);inverse_norm=std::max(inverse_norm,inverse_row);
        auto residual=rhs[i];
        for(int j=0;j<6;++j)residual=range_add(residual,
            range_negate(range_product(g[i][j],{approximate[j],approximate[j]})));
        residual_norm=std::max(residual_norm,range_abs_upper(residual));
    }
    if(!std::isfinite(q)||q>=1.)return false;
    const double denominator=bound_down(1.-q);
    if(denominator<=0.)return false;
    inverse_upper=bound_quotient(inverse_norm,denominator);
    error_upper=bound_product(inverse_upper,residual_norm);
    if(!std::isfinite(error_upper)||!std::isfinite(inverse_upper))return false;
    for(int i=0;i<6;++i) {
        lambda[i]=range_add({approximate[i],approximate[i]},{-error_upper,error_upper});
        if(!finite_range(lambda[i]))return false;
    }
    return true;
}
bool finite_field(std::span<const double> x) {
    for(double v:x)if(!std::isfinite(v))return false;
    return true;
}
PoissonArithmeticError finish_arithmetic_ledger(const CompositePoisson& op,
    std::span<const ArithmeticRange> exact,std::span<const double> computed) {
    PoissonArithmeticError result{};result.cell_bounds.resize(exact.size());
    for(std::size_t i=0;i<exact.size();++i) {
        if(!finite_range(exact[i])) {result.status=BoundaryErrorStatus::Overflow;return result;}
        const double distance=std::max(std::abs(computed[i]-exact[i].lo),
                                       std::abs(computed[i]-exact[i].hi));
        result.cell_bounds[i]=bound_up(distance);
        if(!std::isfinite(result.cell_bounds[i])) {
            result.status=BoundaryErrorStatus::Overflow;return result;
        }
    }
    const auto norm=op.norm_interval(result.cell_bounds);
    result.status=norm.status;result.norm_upper=norm.upper;return result;
}
}
/** Enclose the final RZ derivative stencil relative to ideal root geometry.
 * The existing final face owns the sample set and actual recovery decision.
 * No thresholds/coefficients are altered; failure cannot become a certificate.
 */
NativeRzStencilEnclosure CompositePoisson::native_rz_stencil_enclosure(std::size_t index) const {
    NativeRzStencilEnclosure result;result.face_index=index;
    if(index>=faces_.size()||base_.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||base_.geometry!=Geometry::Cylindrical||base_.dimension!=2)return result;
    const auto& face=faces_[index];result.construction=face.construction;
    const int axis=face.axis,anchor=face.left>=0?face.left:face.right;
    const bool boundary=face.boundary_side>=0;
    const auto exact_width=[&](int cell,int a) {
        const double w=width(cell,a);
        return std::ldexp(w,cells_[cell].level)==base_.spacing[a]?w:0.;
    };
    const double h=exact_width(anchor,axis);
    if(h<=0.||!std::isfinite(h))return result;
    const double scale=boundary?h:std::max(exact_width(face.left,axis),exact_width(face.right,axis));
    if(scale<=0.||!std::isfinite(scale))return result;
    ArithmeticRange inverse,boundary_seed{};
    if(boundary) {
        inverse=range_divide_positive({2.,2.},{h,h});
        if(face.boundary_side%2)inverse=range_negate(inverse);
        boundary_seed=range_negate(inverse);
    } else {
        const double hl=exact_width(face.left,axis),hr=exact_width(face.right,axis);
        if(hl<=0.||hr<=0.)return result;
        inverse=range_divide_positive({1.,1.},
            range_add(range_product({.5,.5},{hl,hl}),range_product({.5,.5},{hr,hr})));
    }
    std::vector<ArithmeticRange> coefficients;
    ArithmeticRange boundary_coefficient=boundary_seed;
    if(face.construction==FaceStencilConstruction::PolynomialFit) {
        const int fine=boundary?anchor:(cells_[face.left].level>=cells_[face.right].level?face.left:face.right);
        const int scale_level=boundary?cells_[anchor].level:
            std::min(cells_[face.left].level,cells_[face.right].level);
        std::vector<std::array<ArithmeticRange,6>> basis;
        std::vector<ArithmeticRange> weights,initial;
        std::array<std::array<ArithmeticRange,6>,6> gram{};
        std::array<ArithmeticRange,6> rhs{};rhs[1+axis]={1.,1.};
        if(boundary) {
            gram[0][0]={1.,1.};
            rhs[0]=range_negate(range_product(boundary_seed,{scale,scale}));
        }
        for(int cell:face.samples) {
            std::array<ArithmeticRange,2> delta{};
            for(int a=0;a<2;++a) {
                const double w=exact_width(cell,a);if(w<=0.)return result;
                const int owner=a==axis?anchor:fine;
                const int common=std::max(cells_[cell].level,cells_[owner].level);
                // level<=15 and index<=INT_MAX imply every doubled dyadic
                // numerator and its difference fit <=48 bits. Build it in
                // int64 before exact binary scaling: no world-center subtraction.
                const std::int64_t sample=(2*std::int64_t(cells_[cell].index[a])+1)
                    << (common-cells_[cell].level);
                const std::int64_t face_numerator=a==axis?
                    2*(std::int64_t(cells_[owner].index[a])+(boundary?face.boundary_side%2:1)):
                    2*std::int64_t(cells_[owner].index[a])+1;
                const std::int64_t face_position=face_numerator << (common-cells_[owner].level);
                const double dyadic=std::ldexp(double(sample-face_position),scale_level-common-1);
                int ea=0,en=0;
                const double ma=std::frexp(base_.spacing[a],&ea),mn=std::frexp(base_.spacing[axis],&en);
                const double exact=std::ldexp(dyadic,ea-en);
                if(ma==mn&&std::isfinite(exact)&&std::ldexp(exact,en-ea)==dyadic)
                    delta[a]={exact,exact}; // shared spacing/power-of-two ratio cancels exactly
                else delta[a]=range_product({dyadic,dyadic},range_divide_positive(
                    {base_.spacing[a],base_.spacing[a]},{base_.spacing[axis],base_.spacing[axis]}));
            }
            std::array<ArithmeticRange,6> p{{{1.,1.},delta[0],delta[1],range_square(delta[0]),
                range_product(delta[0],delta[1]),range_square(delta[1])}};
            const auto denominator=range_add({1.,1.},range_add(range_square(delta[0]),range_square(delta[1])));
            const auto weight=range_divide_positive({1.,1.},range_square(denominator));
            ArithmeticRange seed{};
            if(boundary&&cell==anchor)seed=inverse;
            if(!boundary&&cell==face.left)seed=range_negate(inverse);
            if(!boundary&&cell==face.right)seed=inverse;
            seed=range_product(seed,{scale,scale});
            for(int i=0;i<6;++i) {
                rhs[i]=range_add(rhs[i],range_negate(range_product(seed,p[i])));
                for(int j=0;j<6;++j)gram[i][j]=range_add(gram[i][j],
                    range_product(weight,range_product(p[i],p[j])));
            }
            basis.push_back(p);weights.push_back(weight);initial.push_back(seed);
        }
        std::array<ArithmeticRange,6> lambda{};
        if(!enclose_gram_solution(gram,rhs,lambda,result.inverse_residual_upper,
            result.inverse_norm_upper,result.lambda_error_upper)) {
            result.status=BoundaryErrorStatus::UncertifiedInput;return result;
        }
        if(boundary)boundary_coefficient=range_add(boundary_seed,range_divide_volume(lambda[0],scale));
        for(std::size_t i=0;i<basis.size();++i) {
            auto v=initial[i];
            for(int j=0;j<6;++j)v=range_add(v,range_product(weights[i],range_product(basis[i][j],lambda[j])));
            coefficients.push_back(range_divide_volume(v,scale));
        }
        auto sum=boundary_coefficient;std::size_t anchor_index=coefficients.size();
        for(std::size_t i=0;i<coefficients.size();++i) {
            if(face.samples[i]==anchor)anchor_index=i;else sum=range_add(sum,coefficients[i]);
        }
        if(anchor_index==coefficients.size())return result;
        coefficients[anchor_index]=range_negate(sum);
    } else if(face.construction==FaceStencilConstruction::TwoPoint
        ||face.construction==FaceStencilConstruction::EllipticRecovery) {
        for(int cell:face.samples)coefficients.push_back(boundary?inverse:
            (cell==face.left?range_negate(inverse):inverse));
    } else return result;
    if(coefficients.size()!=face.coefficients.size()||!finite_range(boundary_coefficient)) {
        result.status=BoundaryErrorStatus::Overflow;return result;
    }
    result.boundary_lower=boundary_coefficient.lo;result.boundary_upper=boundary_coefficient.hi;
    result.boundary_error_upper=bound_up(std::max(std::abs(face.boundary_coefficient-boundary_coefficient.lo),
        std::abs(face.boundary_coefficient-boundary_coefficient.hi)));
    for(std::size_t i=0;i<coefficients.size();++i) {
        const auto v=coefficients[i];
        const double error=bound_up(std::max(std::abs(face.coefficients[i]-v.lo),std::abs(face.coefficients[i]-v.hi)));
        if(!finite_range(v)||!std::isfinite(error)) {
            result.status=BoundaryErrorStatus::Overflow;return result;
        }
        result.coefficient_lower.push_back(v.lo);result.coefficient_upper.push_back(v.hi);
        result.coefficient_error_upper.push_back(error);
    }
    result.status=std::isfinite(result.boundary_error_upper)?BoundaryErrorStatus::Bounded:BoundaryErrorStatus::Overflow;
    return result;
}
/** Enclose geometry and signed B from the same ideal root/leaf/face identity.
 * The 2*pi in face measures and cell volumes cancels analytically in A_f/V_i.
 * This includes actual stored area/coefficient/volume construction error;
 * floating RHS assembly is still handled by its separate arithmetic ledger.
 */
NativeRzFaceEnclosure CompositePoisson::native_rz_face_enclosure(std::size_t index) const {
    NativeRzFaceEnclosure result;result.face_index=index;
    if(index>=faces_.size()||base_.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||base_.geometry!=Geometry::Cylindrical||base_.dimension!=2)return result;
    const auto& f=faces_[index];const bool boundary=f.boundary_side>=0;
    const int anchor=f.left>=0?f.left:f.right;
    const int fine=boundary?anchor:(cells_[f.left].level>=cells_[f.right].level?f.left:f.right);
    const auto exact_width=[&](int cell,int axis) {
        const double w=width(cell,axis);
        return std::isfinite(w)&&w>0.&&std::ldexp(w,cells_[cell].level)==base_.spacing[axis]?w:0.;
    };
    std::array<ArithmeticRange,2> center{};
    for(int a=0;a<2;++a) {
        const int owner=a==f.axis?anchor:fine;const double w=exact_width(owner,a);
        if(w<=0.)return result;
        const double logical=a==f.axis?(boundary?cells_[anchor].index[a]+double(f.boundary_side%2)
            :cells_[f.left].index[a]+1.):cells_[owner].index[a]+.5;
        center[a]=range_add({base_.origin[a],base_.origin[a]},
            range_product({logical,logical},{w,w}));
        if(!finite_range(center[a])) {result.status=BoundaryErrorStatus::Overflow;return result;}
        result.center_lower[a]=center[a].lo;result.center_upper[a]=center[a].hi;
        result.center_error_upper[a]=bound_up(std::max(std::abs(f.center[a]-center[a].lo),
            std::abs(f.center[a]-center[a].hi)));
    }
    const auto radial_measure=[&](int cell) {
        const double dr=exact_width(cell,0);
        if(dr<=0.)return ArithmeticRange{0.,0.};
        const auto sum=range_add(range_product({2.,2.},{base_.origin[0],base_.origin[0]}),
            range_product({2.*cells_[cell].index[0]+1.,2.*cells_[cell].index[0]+1.},{dr,dr}));
        return range_product({dr,dr},sum); // (r_hi^2-r_lo^2), no pi.
    };
    const double dz=exact_width(fine,1);if(dz<=0.)return result;
    const auto without_pi=f.axis==0?
        range_product(range_product({2.,2.},center[0]),{dz,dz}):radial_measure(fine);
    const double infinity=std::numeric_limits<double>::infinity();
    const ArithmeticRange pi{std::nextafter(arch::constants::math::pi,-infinity),
        std::nextafter(arch::constants::math::pi,infinity)};
    const auto area=range_product(pi,without_pi);
    if(!finite_range(area)||area.lo<0.) {result.status=BoundaryErrorStatus::Overflow;return result;}
    result.area_lower=area.lo;result.area_upper=area.hi;
    result.area_error_upper=bound_up(std::max(std::abs(f.area-area.lo),std::abs(f.area-area.hi)));
    ArithmeticRange boundary_coefficient{};
    if(boundary) {
        const auto stencil=native_rz_stencil_enclosure(index);
        if(stencil.status!=BoundaryErrorStatus::Bounded) {result.status=stencil.status;return result;}
        boundary_coefficient={stencil.boundary_lower,stencil.boundary_upper};
    }
    for(int side=0;side<2;++side) {
        const int cell=side?f.right:f.left;if(cell<0)continue;
        const double height=exact_width(cell,1);if(height<=0.)return result;
        const auto volume_without_pi=range_product(radial_measure(cell),{height,height});
        const auto quotient=range_divide_positive(without_pi,volume_without_pi);
        const auto stored_quotient=range_divide_volume({f.area,f.area},volumes_[cell]);
        const auto quotient_defect=range_add(quotient,range_negate(stored_quotient));
        if(!finite_range(quotient)||!finite_range(quotient_defect)) {
            result.status=BoundaryErrorStatus::Overflow;return result;
        }
        result.area_over_volume_lower[side]=quotient.lo;result.area_over_volume_upper[side]=quotient.hi;
        result.area_over_volume_error_upper[side]=range_abs_upper(quotient_defect);
        if(!boundary)continue;
        auto map=range_product(quotient,boundary_coefficient);
        auto stored=range_divide_volume(range_product({f.area,f.area},
            {f.boundary_coefficient,f.boundary_coefficient}),volumes_[cell]);
        if(side) {map=range_negate(map);stored=range_negate(stored);}
        const auto defect=range_add(map,range_negate(stored));
        if(!finite_range(map)||!finite_range(defect)) {
            result.status=BoundaryErrorStatus::Overflow;return result;
        }
        result.boundary_map_lower[side]=map.lo;result.boundary_map_upper[side]=map.hi;
        result.boundary_map_error_upper[side]=range_abs_upper(defect);
    }
    result.status=std::isfinite(result.area_error_upper)&&finite_field(result.center_error_upper)
        ?BoundaryErrorStatus::Bounded:BoundaryErrorStatus::Overflow;
    return result;
}
/** |(B_ideal-B_stored) f_hat| with the actual final boundary map.
 * Geometry/fit construction is counted here; RHS assembly roundoff, f_hat
 * integral/source/observer error and interior A construction are not.
 */
NativeRzBoundaryConstructionError CompositePoisson::native_rz_boundary_construction_error(
    std::span<const double> values) const {
    NativeRzBoundaryConstructionError result;
    if(base_.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||base_.geometry!=Geometry::Cylindrical||base_.dimension!=2
        ||values.size()!=faces_.size()||!finite_field(values))return result;
    result.cell_bounds.assign(cells_.size(),0.);
    for(std::size_t index=0;index<faces_.size();++index) {
        const auto& face=faces_[index];if(face.boundary_side<0)continue;
        const auto geometry=native_rz_face_enclosure(index);
        if(geometry.status!=BoundaryErrorStatus::Bounded) {result.status=geometry.status;return result;}
        for(int side=0;side<2;++side) {
            const int cell=side?face.right:face.left;if(cell<0)continue;
            const double term=bound_product(geometry.boundary_map_error_upper[side],std::abs(values[index]));
            result.cell_bounds[cell]=bound_up(result.cell_bounds[cell]+term);
            if(!std::isfinite(result.cell_bounds[cell])) {result.status=BoundaryErrorStatus::Overflow;return result;}
        }
    }
    const auto norm=native_rz_norm_interval(result.cell_bounds);
    result.status=norm.status;result.native_norm_upper=norm.upper;return result;
}
/** abs(B_ideal)*e_f: avoids the cross term lost by using stored B.
 * Certified errors must refer to ideal root source AND observer geometry.
 * Existing stored-coordinate ring errors cannot be implicitly promoted.
 */
/** Original ideal B enclosure summed for a unit uniform potential error.
 * No fictitious source/observer certificate or stored-weight norm is used.
 */
NativeRzBoundarySensitivity CompositePoisson::native_rz_boundary_sensitivity() const {
    NativeRzBoundarySensitivity result;
    if(base_.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||base_.geometry!=Geometry::Cylindrical||base_.dimension!=2
        ||boundary_kind()!=BoundaryKind::CurvilinearIsolated)return result;
    result.cell_coefficients.assign(cells_.size(),0.);
    for(std::size_t index=0;index<faces_.size();++index) {
        const auto& face=faces_[index];if(face.boundary_side<0)continue;
        const auto map=native_rz_face_enclosure(index);
        if(map.status!=BoundaryErrorStatus::Bounded){result.status=map.status;return result;}
        for(int side=0;side<2;++side) {
            const int cell=side?face.right:face.left;if(cell<0)continue;
            const double coefficient=std::max(std::abs(map.boundary_map_lower[side]),
                std::abs(map.boundary_map_upper[side]));
            result.cell_coefficients[cell]=bound_up(result.cell_coefficients[cell]+coefficient);
            if(!std::isfinite(result.cell_coefficients[cell])){
                result.status=BoundaryErrorStatus::Overflow;return result;}
        }
    }
    const auto norm=native_rz_norm_interval(result.cell_coefficients);
    result.status=norm.status;result.native_norm_upper=norm.upper;return result;
}
NativeRzBoundaryPotentialError CompositePoisson::native_rz_propagate_potential_error(
    std::span<const NativeRzFacePotentialError> errors) const {
    NativeRzBoundaryPotentialError result;
    if(base_.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||base_.geometry!=Geometry::Cylindrical||base_.dimension!=2
        ||errors.size()!=faces_.size())return result;
    result.cell_bounds.assign(cells_.size(),0.);
    for(std::size_t index=0;index<faces_.size();++index) {
        const auto& face=faces_[index];if(face.boundary_side<0)continue;
        const auto& input=errors[index];
        if(!std::isfinite(input.error.absolute_error)||input.error.absolute_error<0.)return result;
        if(input.error.quality!=BoundaryErrorQuality::CertifiedAbsolute
            ||input.scope!=NativeRzPotentialScope::RootDyadicSourceAndObserver) {
            result.status=BoundaryErrorStatus::UncertifiedInput;return result;
        }
        const auto map=native_rz_face_enclosure(index);
        if(map.status!=BoundaryErrorStatus::Bounded) {result.status=map.status;return result;}
        for(int side=0;side<2;++side) {
            const int cell=side?face.right:face.left;if(cell<0)continue;
            const double coefficient=std::max(std::abs(map.boundary_map_lower[side]),
                std::abs(map.boundary_map_upper[side]));
            const double term=bound_product(coefficient,input.error.absolute_error);
            result.cell_bounds[cell]=bound_up(result.cell_bounds[cell]+term);
            if(!std::isfinite(result.cell_bounds[cell])) {result.status=BoundaryErrorStatus::Overflow;return result;}
        }
    }
    const auto norm=native_rz_norm_interval(result.cell_bounds);
    result.status=norm.status;result.native_norm_upper=norm.upper;return result;
}
/** Bound (A_ideal-A_stored)*phi with the canonical anchored gradient.
 * apply() uses boundary_value=0: the -c_B*phi_anchor term belongs to A.
 * Prescribed face values belong to the separate B/RHS ledger.
 */
NativeRzOperatorConstructionError CompositePoisson::native_rz_operator_construction_error(
    std::span<const double> phi) const {
    NativeRzOperatorConstructionError result;
    if(base_.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||base_.geometry!=Geometry::Cylindrical||base_.dimension!=2
        ||phi.size()!=cells_.size()||!finite_field(phi))return result;
    result.cell_bounds.assign(cells_.size(),0.);
    for(std::size_t index=0;index<faces_.size();++index) {
        const auto& face=faces_[index];const int anchor=face.left>=0?face.left:face.right;
        const auto stencil=native_rz_stencil_enclosure(index);
        const auto geometry=native_rz_face_enclosure(index);
        if(stencil.status!=BoundaryErrorStatus::Bounded) {result.status=stencil.status;return result;}
        if(geometry.status!=BoundaryErrorStatus::Bounded) {result.status=geometry.status;return result;}
        for(int side=0;side<2;++side) {
            const int cell=side?face.right:face.left;if(cell<0)continue;
            const ArithmeticRange quotient{geometry.area_over_volume_lower[side],geometry.area_over_volume_upper[side]};
            double contribution=0.;
            for(std::size_t k=0;k<face.samples.size();++k) {
                const auto ideal=range_product(quotient,
                    {stencil.coefficient_lower[k],stencil.coefficient_upper[k]});
                const auto stored=range_divide_volume(range_product({face.area,face.area},
                    {face.coefficients[k],face.coefficients[k]}),volumes_[cell]);
                const double factor=range_abs_upper(range_add(ideal,range_negate(stored)));
                const auto difference=range_add({phi[face.samples[k]],phi[face.samples[k]]},
                    {-phi[anchor],-phi[anchor]});
                const double term=bound_product(factor,range_abs_upper(difference));
                contribution=bound_up(contribution+term);
            }
            // Geometry already encloses the signed B factor; absolute defect
            // bounds the homogeneous boundary term in A regardless of side.
            const double boundary_term=bound_product(geometry.boundary_map_error_upper[side],std::abs(phi[anchor]));
            contribution=bound_up(contribution+boundary_term);
            result.cell_bounds[cell]=bound_up(result.cell_bounds[cell]+contribution);
            if(!std::isfinite(result.cell_bounds[cell])) {result.status=BoundaryErrorStatus::Overflow;return result;}
        }
    }
    const auto norm=native_rz_norm_interval(result.cell_bounds);
    result.status=norm.status;result.native_norm_upper=norm.upper;return result;
}
/** Actual computed residual vs ideal A*phi minus the supplied computed RHS.
 * Stored apply/subtraction roundoff and native A construction are separate.
 * Ideal-vs-computed RHS and source/observer errors remain separate inputs.
 */
NativeRzResidualEvaluationError CompositePoisson::native_rz_residual_evaluation_error(
    std::span<const double> phi,std::span<const double> rhs,std::span<const double> residual) const {
    NativeRzResidualEvaluationError result;
    if(phi.size()!=cells_.size()||rhs.size()!=cells_.size()||residual.size()!=cells_.size()
        ||!finite_field(phi)||!finite_field(rhs)||!finite_field(residual))return result;
    result.construction=native_rz_operator_construction_error(phi);
    if(result.construction.status!=BoundaryErrorStatus::Bounded) {result.status=result.construction.status;return result;}
    result.arithmetic=bound_residual_evaluation_roundoff(phi,rhs,residual);
    if(result.arithmetic.status!=BoundaryErrorStatus::Bounded) {result.status=result.arithmetic.status;return result;}
    result.cell_bounds.resize(cells_.size());
    for(std::size_t i=0;i<cells_.size();++i) {
        result.cell_bounds[i]=bound_up(result.construction.cell_bounds[i]+result.arithmetic.cell_bounds[i]);
        if(!std::isfinite(result.cell_bounds[i])) {result.status=BoundaryErrorStatus::Overflow;return result;}
    }
    const auto norm=native_rz_norm_interval(result.cell_bounds);
    result.status=norm.status;result.native_norm_upper=norm.upper;return result;
}
/** Construct full-ring volume/normalization bounds from root dyadic identity.
 * V=pi*dr*(2*r_lo+dr)*dz. Using dr explicitly avoids subtraction of
 * nearly equal radial edges. Stored GridMetrics outputs are compared, never
 * assumed exact; fitted coefficients are deliberately outside this scope.
 */
NativeRzMeasureEnclosure CompositePoisson::native_rz_measure_enclosure() const {
    NativeRzMeasureEnclosure result;
    if(base_.semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz
        ||base_.geometry!=Geometry::Cylindrical||base_.dimension!=2)return result;
    const double infinity=std::numeric_limits<double>::infinity();
    const ArithmeticRange pi{std::nextafter(arch::constants::math::pi,-infinity),
        std::nextafter(arch::constants::math::pi,infinity)};
    std::vector<ArithmeticRange> volume;volume.reserve(cells_.size());
    ArithmeticRange total{};
    for(const auto& cell:cells_) {
        const double dr=std::ldexp(base_.spacing[0],-cell.level);
        const double dz=std::ldexp(base_.spacing[1],-cell.level);
        if(!std::isfinite(dr)||!std::isfinite(dz)||dr<=0.||dz<=0.
            ||std::ldexp(dr,cell.level)!=base_.spacing[0]
            ||std::ldexp(dz,cell.level)!=base_.spacing[1])return result;
        const auto radius_sum=range_add(
            range_product({2.,2.},{base_.origin[0],base_.origin[0]}),
            range_product({2.*cell.index[0]+1.,2.*cell.index[0]+1.},{dr,dr}));
        const auto v=range_product(range_product(pi,{dr,dr}),
            range_product(radius_sum,{dz,dz}));
        if(!finite_range(v)||v.lo<=0.) {
            result.status=BoundaryErrorStatus::Overflow;return result;
        }
        volume.push_back(v);total=range_add(total,v);
    }
    if(!finite_range(total)||total.lo<=0.) {
        result.status=BoundaryErrorStatus::Overflow;return result;
    }
    result.total_volume_lower=total.lo;result.total_volume_upper=total.hi;
    for(std::size_t i=0;i<volume.size();++i) {
        const auto v=volume[i];
        const double lo=std::nextafter(v.lo/total.hi,0.);
        const double hi=std::nextafter(v.hi/total.lo,infinity);
        const double ev=bound_up(std::max(std::abs(volumes_[i]-v.lo),std::abs(volumes_[i]-v.hi)));
        const double ew=bound_up(std::max(std::abs(weights_[i]-lo),std::abs(weights_[i]-hi)));
        if(!std::isfinite(hi)||!std::isfinite(ev)||!std::isfinite(ew)||lo<=0.) {
            result.status=BoundaryErrorStatus::Overflow;return result;
        }
        result.volume_lower.push_back(v.lo);result.volume_upper.push_back(v.hi);
        result.volume_error_upper.push_back(ev);
        result.weight_lower.push_back(lo);result.weight_upper.push_back(hi);
        result.weight_error_upper.push_back(ew);
    }
    result.status=BoundaryErrorStatus::Bounded;return result;
}
/** RMS enclosure with ideal native full-ring weights; zero remains exact.
 * No change to production norm/provider arrays or numerical thresholds.
 */
WeightedNormInterval CompositePoisson::native_rz_norm_interval(std::span<const double> x) const {
    WeightedNormInterval result;
    if(x.size()!=cells_.size()||!finite_field(x))return result;
    const auto measure=native_rz_measure_enclosure();
    if(measure.status!=BoundaryErrorStatus::Bounded) {result.status=measure.status;return result;}
    double scale=0.;for(double value:x)scale=std::max(scale,std::abs(value));
    if(scale==0.) {result.status=BoundaryErrorStatus::Bounded;result.lower=result.upper=0.;return result;}
    double lo=0.,hi=0.;
    for(std::size_t i=0;i<x.size();++i) {
        const double magnitude=std::abs(x[i]);if(magnitude==0.)continue;
        const double qlo=bound_down(magnitude/scale),qhi=bound_quotient(magnitude,scale);
        const double lower=bound_down(measure.weight_lower[i]*bound_down(qlo*qlo));
        const double upper=bound_product(measure.weight_upper[i],bound_product(qhi,qhi));
        if(lower!=0.)lo=bound_down(lo+lower);
        if(upper!=0.)hi=bound_up(hi+upper);
    }
    result.lower=bound_down(scale*bound_down(std::sqrt(lo)));
    result.upper=bound_product(scale,bound_up(std::sqrt(hi)));
    result.status=std::isfinite(result.upper)?BoundaryErrorStatus::Bounded:BoundaryErrorStatus::Overflow;
    return result;
}
/** Companion certificate for the actual constant-mode projection.
 * The supplied output may come from the scalar or execution provider path;
 * no assumption is made about reduction/subtraction arithmetic being exact.
 * Conditional stored-weight scope: P_w is not an ideal geometric projection.
 */
PoissonArithmeticError CompositePoisson::bound_constant_mode_projection_roundoff(
    std::span<const double> input,std::span<const double> computed) const {
    if(input.size()!=cells_.size()||computed.size()!=cells_.size()
        ||!finite_field(input)||!finite_field(computed))return {};
    ArithmeticRange average{};
    if(boundary_.constant_nullspace)
        for(std::size_t i=0;i<input.size();++i)
            average=range_add(average,range_product({weights_[i],weights_[i]},
                {input[i],input[i]}));
    std::vector<ArithmeticRange> exact;exact.reserve(input.size());
    for(double value:input)
        exact.push_back(range_add({value,value},range_negate(average)));
    return finish_arithmetic_ledger(*this,exact,computed);
}
/** Bound actual RHS array against exact stored source+B*boundary expression.
 * It does not certify the physical source or construction of native B.
 */
PoissonArithmeticError CompositePoisson::bound_rhs_assembly_roundoff(
    std::span<const double> source,std::span<const double> boundary_values,
    std::span<const double> computed_rhs) const {
    if(source.size()!=cells_.size()||computed_rhs.size()!=cells_.size()
        ||!finite_field(source)||!finite_field(computed_rhs))return {};
    const bool no_boundary=boundary_values.empty()&&boundary_.constant_nullspace;
    if(!no_boundary&&(boundary_values.size()!=faces_.size()||!finite_field(boundary_values)))
        return {};
    std::vector<ArithmeticRange> exact;exact.reserve(source.size());
    for(double value:source)exact.push_back({value,value});
    if(!no_boundary)for(std::size_t i=0;i<faces_.size();++i) {
        const auto& f=faces_[i];
        if(f.area==0.||f.boundary_coefficient==0.)continue;
        const auto flux=range_product(range_product({f.area,f.area},
            {f.boundary_coefficient,f.boundary_coefficient}),
            {boundary_values[i],boundary_values[i]});
        if(f.left>=0)exact[f.left]=range_add(exact[f.left],range_divide_volume(flux,volumes_[f.left]));
        if(f.right>=0)exact[f.right]=range_add(exact[f.right],
            range_negate(range_divide_volume(flux,volumes_[f.right])));
    }
    return finish_arithmetic_ledger(*this,exact,computed_rhs);
}
/** Bound computed residual against canonical mathematical A*potential-rhs.
 * The scalar/provider residual and compensated gradient may have rounded;
 * neither their arithmetic nor their equality is assumed by this ledger.
 */
PoissonArithmeticError CompositePoisson::bound_residual_evaluation_roundoff(
    std::span<const double> potential,std::span<const double> rhs,
    std::span<const double> computed_residual) const {
    if(potential.size()!=cells_.size()||rhs.size()!=cells_.size()
        ||computed_residual.size()!=cells_.size()||!finite_field(potential)
        ||!finite_field(rhs)||!finite_field(computed_residual))return {};
    std::vector<ArithmeticRange> exact(cells_.size());
    for(const auto& f:faces_) {
        if(f.area==0.)continue;
        const int anchor=f.left>=0?f.left:f.right;
        ArithmeticRange gradient{};
        for(std::size_t k=0;k<f.samples.size();++k) {
            const double value=potential[f.samples[k]],base=potential[anchor];
            const auto difference=range_add({value,value},range_negate({base,base}));
            gradient=range_add(gradient,
                range_product({f.coefficients[k],f.coefficients[k]},difference));
        }
        gradient=range_add(gradient,range_product({f.boundary_coefficient,f.boundary_coefficient},
            {-potential[anchor],-potential[anchor]}));
        const auto flux=range_product({f.area,f.area},gradient);
        if(f.left>=0)exact[f.left]=range_add(exact[f.left],
            range_negate(range_divide_volume(flux,volumes_[f.left])));
        if(f.right>=0)exact[f.right]=range_add(exact[f.right],range_divide_volume(flux,volumes_[f.right]));
    }
    for(std::size_t i=0;i<exact.size();++i)exact[i]=range_add(exact[i],{-rhs[i],-rhs[i]});
    return finish_arithmetic_ledger(*this,exact,computed_residual);
}
/**
 * Conditional original-RHS acceptance, without modifying solver tolerance.
 * E_b includes certified face propagation plus certified RHS assembly error.
 * ||b_exact|| >= max(0, ||b_hat||_lower-E_b).
 * Residual upper + E_b <= max(atol,rtol*that lower bound) is sufficient.
 */
BoundaryResidualAssessment CompositePoisson::assess_boundary_residual(
    std::span<const double> rhs,std::span<const double> residual,
    const BoundaryRhsError& face_error,double assembly_error,double evaluation_error,
    BoundaryErrorQuality quality,double rtol,double atol,BoundaryResidualNormScope scope) const
{
    BoundaryResidualAssessment result{};
    if(scope!=BoundaryResidualNormScope::StoredNativeWeights
        &&scope!=BoundaryResidualNormScope::RootDyadicRzWeights)return result;
    const auto valid=[](double value){return std::isfinite(value)&&value>=0.;};
    if(!valid(rtol)||!valid(atol)||!valid(assembly_error)||!valid(evaluation_error)
        ||face_error.cell_bounds.size()!=cells_.size()
        ||rhs.size()!=cells_.size()||residual.size()!=cells_.size())return result;
    if(quality!=BoundaryErrorQuality::CertifiedAbsolute
        ||face_error.status==BoundaryErrorStatus::UncertifiedInput) {
        result.status=BoundaryResidualStatus::UncertifiedInput;return result;
    }
    if(face_error.status==BoundaryErrorStatus::Overflow) {
        result.status=BoundaryResidualStatus::Overflow;return result;
    }
    if(face_error.status!=BoundaryErrorStatus::Bounded||!valid(face_error.norm_upper))return result;
    for(double value:face_error.cell_bounds)if(!valid(value))return result;
    // The same acceptance mathematics serves both scopes; root scope must
    // also re-norm the cell error ledger with ideal weights, never a stored norm.
    const bool native=scope==BoundaryResidualNormScope::RootDyadicRzWeights;
    const auto rhs_norm=native?native_rz_norm_interval(rhs):norm_interval(rhs);
    const auto residual_norm=native?native_rz_norm_interval(residual):norm_interval(residual);
    const auto error_norm=native?native_rz_norm_interval(face_error.cell_bounds)
        :WeightedNormInterval{BoundaryErrorStatus::Bounded,0.,face_error.norm_upper};
    if(error_norm.status!=BoundaryErrorStatus::Bounded) {
        result.status=error_norm.status==BoundaryErrorStatus::Overflow
            ?BoundaryResidualStatus::Overflow:BoundaryResidualStatus::InvalidInput;return result;
    }
    if(rhs_norm.status==BoundaryErrorStatus::Overflow||residual_norm.status==BoundaryErrorStatus::Overflow) {
        result.status=BoundaryResidualStatus::Overflow;return result;
    }
    if(rhs_norm.status!=BoundaryErrorStatus::Bounded||residual_norm.status!=BoundaryErrorStatus::Bounded)return result;
    result.rhs_norm_lower=rhs_norm.lower;result.rhs_norm_upper=rhs_norm.upper;
    result.rhs_error_upper=assembly_error==0.?error_norm.upper:
        bound_up(error_norm.upper+assembly_error);
    result.residual_norm_upper=evaluation_error==0.?residual_norm.upper:
        bound_up(residual_norm.upper+evaluation_error);
    const double exact_rhs_lower=bound_down(rhs_norm.lower-result.rhs_error_upper);
    result.tolerance_safe=std::max(atol,bound_down(rtol*exact_rhs_lower));
    result.total_residual_upper=result.rhs_error_upper==0.?result.residual_norm_upper:
        bound_up(result.residual_norm_upper+result.rhs_error_upper);
    if(!std::isfinite(result.rhs_error_upper)||!std::isfinite(result.residual_norm_upper)
        ||!std::isfinite(result.total_residual_upper)||!std::isfinite(result.tolerance_safe)) {
        result.status=BoundaryResidualStatus::Overflow;return result;
    }
    result.status=result.total_residual_upper<=result.tolerance_safe?
        BoundaryResidualStatus::Accepted:BoundaryResidualStatus::ResidualTooLarge;
    return result;
}
} // namespace arch::elliptic
