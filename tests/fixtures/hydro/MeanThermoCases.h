#pragma once

#include "numerics/flux/FluxHLL.h"
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
    // Equal conserved components do not authorize reuse after a differently
    // rounded energy recovery. A one-ulp input change must evaluate the EOS.
    {
        double rho[]{2.},mx[]{3.},my[]{0.},mz[]{0.},eng[]{10.},xs[]{1.};
        double p[]{3.1},c[]{std::sqrt(2.17)};
        unsigned char ready[]{1};
        FluxAdmissibility::MeanThermoView view{rho,mx,my,mz,eng,xs,p,c,ready,1,1};
        for(int changed=0;changed<2;++changed) {
            int calls=0; CountingIdealGas eos(&calls);
            const double e=changed?std::nextafter(3.875,4.):3.875;
            double pressure,speed;
            FluxAdmissibility::face_thermo(state,e,x,1,eos,&view,0,pressure,speed);
            if(calls!=changed) return false;
            if(changed && pressure!=eos.get_pressure_from_rho_e(2.,e,x)) return false;
        }
    }
    // Independent Davis HLL formula and stationary-contact HLLC invariant.
    // Both schemes share this method choice, including near-vacuum scales.
    for (double scale : {1.,1e-30,1e-100}) {
        IdealGasView eos;
        FluxAdmissibility::MeanThermoView view{};
        view.roe_wave_speed=false;
        FluidVector l{scale,.2*scale,0.,0.,2.52*scale};
        FluidVector r{.125*scale,-.0125*scale,0.,0.,.250625*scale};
        FluidVector actual; double species[1];
        const double sl=std::min(.2-std::sqrt(1.4),-.1-std::sqrt(1.12));
        const double sr=std::max(.2+std::sqrt(1.4),-.1+std::sqrt(1.12));
        const FluidVector fl{.2*scale,1.04*scale,0.,0.,.704*scale};
        const FluidVector fr{-.0125*scale,.10125*scale,0.,0.,-.0350625*scale};
        const FluidVector expected=(1./(sr-sl))*(sr*fl-sl*fr+(sl*sr)*(r-l));
        FluxHLL<PCMReconstruction>::compute_face_flux(l,r,x,x,1,eos,0,0.,actual,species,&view);
        if(std::abs((actual.rho-expected.rho)/scale)>1e-12
            ||std::abs((actual.mom_u-expected.mom_u)/scale)>1e-12
            ||std::abs((actual.eng-expected.eng)/scale)>1e-12) return false;
        FluxHLLC<PCMReconstruction>::compute_face_flux(
            {2.*scale,0.,0.,0.,5.*scale},{scale,0.,0.,0.,5.*scale},
            x,x,1,eos,0,0.,actual,species,&view);
        if(std::abs(actual.rho/scale)>1e-12 ||std::abs(actual.eng/scale)>1e-12
            ||std::abs(actual.mom_u/scale-2.)>1e-12) return false;
    }
    return true;
}
} // namespace MeanThermoCases
