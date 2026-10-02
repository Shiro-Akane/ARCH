/**
 * @file BoundaryDiagnostics.h
 * @brief Shared capture of actual physical-face fluxes for conservation budgets.
 *
 * Workflow:
 * 1. Runtime allocates planes only for runs using case boundary callbacks.
 * 2. Hydro/diffusion capture the flux already used by divergence and reflux.
 * 3. CPU/CUDA retain the same oriented components and stage weights.
 * 4. Runtime integrates only physical surface planes with GridMetrics areas.
 *
 * Plane components are rho, three native momenta, total energy, rho*X_s,
 * and heat. Periodic joins/internal AMR interfaces have no plane. This is an
 * observer: it never repairs a flux or writes conserved simulation state.
 */
#pragma once

#include <array>
#include <vector>

#include "core/ArchPortability.h"

namespace arch::boundary {
/** Non-owning weighted stage flux and optional F(Y0) cache on each domain face. */
struct BoundaryFluxCaptureView {
    std::array<double*,6> stage{}, initial{};
    double weight = 1., initial_weight = 0.;
    bool save_initial = false;
};

/** Capture one already-computed oriented flux; weights follow its integrator. */
template<class Flux>
ARCH_INLINE void CaptureBoundaryFlux(BoundaryFluxCaptureView view, int direction,
    int i, int j, int k, int is, int ie, int js, int je, int ks, int ke,
    const Flux& flux, const double* species_flux, int species, int stride, double heat)
{
    const int position[3]{i,j,k}, lower[3]{is,js,ks}, upper[3]{ie,je,ke};
    const int side = position[direction]==lower[direction] ? 0
        : (position[direction]==upper[direction] ? 1 : -1);
    if (side<0 || !view.stage[2*direction+side]) return;
    const int a=(direction+1)%3,b=(direction+2)%3;
    if(position[a]<lower[a] || position[a]>=upper[a] || position[b]<lower[b] || position[b]>=upper[b]) return;
    const int plane=(position[a]-lower[a])+(upper[a]-lower[a])*(position[b]-lower[b]);
    const int fields=6+species;
    double* stage=view.stage[2*direction+side]+plane*fields;
    double* initial=view.initial[2*direction+side];
    if(initial) initial+=plane*fields;
    const double fixed[]{flux.rho,flux.mom_u,flux.mom_v,flux.mom_w,flux.eng};
    for(int field=0;field<fields;++field) {
        const double value=field<5 ? fixed[field] : (field==fields-1 ? heat : species_flux[(field-5)*stride]);
        if(view.save_initial && initial) initial[field]=value;
        stage[field]=view.weight*value+(initial && view.initial_weight!=0. ? view.initial_weight*initial[field] : 0.);
    }
}

/** Host-owned planes; each AMR block owns one record shared by its stage slots. */
struct BoundaryFluxCaptureStorage {
    std::array<std::vector<double>,6> stage, initial;
    double weight=1., initial_weight=0.;
    bool save_initial=false;
    BoundaryFluxCaptureView view() const {
        BoundaryFluxCaptureView result;
        for(int face=0;face<6;++face) {
            result.stage[face]=stage[face].empty()?nullptr:const_cast<double*>(stage[face].data());
            result.initial[face]=initial[face].empty()?nullptr:const_cast<double*>(initial[face].data());
        }
        result.weight=weight;result.initial_weight=initial_weight;result.save_initial=save_initial;
        return result;
    }
};
} // namespace arch::boundary
