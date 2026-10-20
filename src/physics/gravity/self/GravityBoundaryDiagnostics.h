/**
 * @file GravityBoundaryDiagnostics.h
 * @brief Discrete field energy and boundary exchange between published solves.
 *
 * Workflow:
 * 1. SelfGravity samples its actual eliminated face potential/normal gradient.
 * 2. Keep only physical-surface samples, with dyadic tangential face identities.
 * 3. Match equal or ancestor faces across AMR refinement/coarsening.
 * 4. Report the signed Green boundary exchange independently of fluid fluxes.
 *
 * With U_g = 1/2 integral(rho*Phi dV), Green reciprocity gives
 * W_B = 1/(8*pi*G) integral_boundary(Phi_new*d_nPhi_old
 *                                 - Phi_old*d_nPhi_new) dA.
 * Then Delta U_g = integral(Phi_avg*Delta rho dV) + W_B in the continuum.
 * A changing prescribed potential therefore carries a boundary exchange term.
 * Open mass boundaries and the chosen potential gauge also affect this term.
 * These samples describe successive published fields, including RK stage
 * fields; their signed time sequence is not a separate time integrator or a
 * claim of exact macro-step total-energy conservation.
 */
#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

#include "core/CompensatedSum.h"
#include "numerics/elliptic/CompositePoisson.h"
#include "physics/constant/PhysicalConstants.h"

namespace Physical::Gravity {
/** side, refinement level, then the two cyclic tangential cell indices. */
using GravityBoundaryFaceKey=std::array<int,4>;
struct GravityBoundaryFaceState {
    GravityBoundaryFaceKey key{};
    double area=0.,potential=0.,normal_gradient=0.;
};
/** A small Host snapshot; resident whole-domain fields remain on their backend. */
struct GravityBoundarySnapshot {
    arch::elliptic::CartesianMesh mesh{};
    double time=0.,G=0.,potential_energy=0.;
    std::vector<GravityBoundaryFaceState> faces;
};

/** Match a face to a dyadic ancestor in another physical-surface cover. */
inline const GravityBoundaryFaceState* gravity_boundary_ancestor(
    GravityBoundaryFaceKey key,
    const std::map<GravityBoundaryFaceKey,const GravityBoundaryFaceState*>& cover) {
    for (;;) {
        const auto found=cover.find(key);
        if(found!=cover.end()) return found->second;
        if(key[1]==0) return nullptr;
        --key[1]; key[2]/=2; key[3]/=2;
    }
}

/** Green exchange on the common surface partition, including AMR changes.
 * Constant fragments use their exact GridMetrics area. When one cover is
 * finer, its children provide the integration measure; each overlap is used
 * once. This is O(S log S * depth), with S the number of physical faces. */
inline double gravity_boundary_exchange(const GravityBoundarySnapshot& old,
                                         const GravityBoundarySnapshot& next) {
    if(old.G!=next.G || !(next.G>0.) || !std::isfinite(next.G)
        || old.mesh.geometry!=next.mesh.geometry || old.mesh.dimension!=next.mesh.dimension
        || old.mesh.origin!=next.mesh.origin || old.mesh.spacing!=next.mesh.spacing
        || old.mesh.cells!=next.mesh.cells)
        throw std::invalid_argument("Gravity boundary exchange requires the same physical domain and G");
    std::map<GravityBoundaryFaceKey,const GravityBoundaryFaceState*> old_cover,new_cover;
    for(const auto& face:old.faces) if(!old_cover.emplace(face.key,&face).second)
        throw std::logic_error("Duplicate old gravity boundary fragment");
    for(const auto& face:next.faces) if(!new_cover.emplace(face.key,&face).second)
        throw std::logic_error("Duplicate new gravity boundary fragment");
    arch::math::CompensatedSum sum;
    std::array<double,6> overlap{},old_area{},new_area{};
    for(const auto& face:old.faces) old_area[face.key[0]]+=face.area;
    for(const auto& face:next.faces) new_area[face.key[0]]+=face.area;
    const auto add=[&](const GravityBoundaryFaceState& first,const GravityBoundaryFaceState& last,double area) {
        sum.add(area*(last.potential*first.normal_gradient-first.potential*last.normal_gradient));
        overlap[last.key[0]]+=area;
    };
    for(const auto& last:next.faces) {
        if(const auto* first=gravity_boundary_ancestor(last.key,old_cover)) add(*first,last,last.area);
    }
    for(const auto& first:old.faces) {
        if(const auto* last=gravity_boundary_ancestor(first.key,new_cover)) {
            if(last->key[1]<first.key[1]) add(first,*last,first.area);
        }
    }
    for(int side=0;side<6;++side) {
        const double bound=512.*std::numeric_limits<double>::epsilon()
            *static_cast<double>(old.faces.size()+next.faces.size())*(old_area[side]+new_area[side]);
        if(std::abs(overlap[side]-old_area[side])>bound || std::abs(overlap[side]-new_area[side])>bound)
            throw std::logic_error("Incomplete gravity surface cover");
    }
    return sum.value()/(8.*arch::constants::math::pi*next.G);
}
} // namespace Physical::Gravity
