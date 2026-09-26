#pragma once

#include "numerics/flux/FluxHLLC.h"
#include "physics/eos/IdealGas.h"

namespace MeanThermoCases {
struct CountingIdealGas : IdealGasView {
    int* calls;
    ARCH_INLINE explicit CountingIdealGas(int* count) : calls(count) {}
    ARCH_INLINE void get_pressure_and_sound_speed(double rho, double energy,
        const double* x, double& pressure, double& speed) const {
        ++*calls;
        pressure = get_pressure_from_rho_e(rho, energy, x);
        speed = std::sqrt(1.4 * pressure / rho);
    }
};

// Independent uniform Euler flux with rho=2, u=1.5, e=3.875 and gamma=1.4.
// Readiness, every conserved component, composition, extent and both endpoint
// indices are perturbed separately. Misses must query the original EOS, while
// hits reuse a previously validated mean without a second EOS evaluation.
ARCH_INLINE bool evaluate() {
    const FluidVector state{2.,3.,0.,0.,10.};
    const double x[]{1.};
    for (int trial = 0; trial < 13; ++trial) {
        double rho[]{2.}, mx[]{3.}, my[]{0.}, mz[]{0.}, eng[]{10.}, xs[]{1.};
        double p[]{3.1}, c[]{std::sqrt(2.17)};
        unsigned char ready[]{1};
        FluxAdmissibility::MeanThermoView view{
            rho,mx,my,mz,eng,xs,p,c,ready,1,1};
        int left=0, right=-1;
        if (trial==1) { left=-1; right=0; }
        if (trial==2) ready[0]=0;
        if (trial==3) rho[0]=std::nextafter(rho[0],4.);
        if (trial==4) mx[0]=std::nextafter(mx[0],4.);
        if (trial==5) my[0]=1.;
        if (trial==6) mz[0]=1.;
        if (trial==7) eng[0]=std::nextafter(eng[0],11.);
        if (trial==8) xs[0]=std::nextafter(xs[0],0.);
        if (trial==9) view.cells=0;
        if (trial==10) view.species=2;
        if (trial==11) left=1;
        if (trial==12) view.ready=nullptr; // Device owner guarantees readiness.
        int calls=0;
        CountingIdealGas eos(&calls);
        FluidVector flux;
        double species_flux[1];
        FluxHLLC<PCMReconstruction>::compute_face_flux(
            state,state,x,x,1,eos,0,0.,flux,species_flux,&view,left,right);
        const bool hit=trial<2 || trial==12;
        if (calls!=(hit?0:1)) return false;
        if (std::abs(flux.rho-3.)>1e-13 || std::abs(flux.mom_u-7.6)>1e-13
            || flux.mom_v!=0. || flux.mom_w!=0.
            || std::abs(flux.eng-19.65)>1e-13
            || std::abs(species_flux[0]-3.)>1e-13) return false;
    }
    return true;
}
} // namespace MeanThermoCases
