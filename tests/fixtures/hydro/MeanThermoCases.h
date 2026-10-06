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

/** Native mean EOS and point EOS are distinct interpretations of RZ data.
 * Independently integrate rho=Omega=1, r=[0,1], e0=1/64: V=1/2, W=1/3,
 * I=1/4, J=1/4, mphi=3/4, EV=17/64. Ordinary recovery of raw native U
 * gives -1/64; the explicit kappa=8/9 image recovers the positive e0.
 * This scalar Host/device fixture checks the cache contract, not FluxSweep's
 * stage/geometry wiring, a RZ limiter proof or whole-model qualification.
 */
ARCH_INLINE bool rz_native_mean_miss()
{
    const double composition[]{1.};
    const FluidVector native{1.,0.,0.,3./4.,17./64.};
    if(native.eng-.5*native.mom_w*native.mom_w/native.rho!=-1./64.
       ||arch::state::recover(native).status!=arch::state::Status::unresolved_energy)
        return false;
    const FluidVector effective{1.,0.,0.,(3./4.)*std::sqrt(8./9.),17./64.};
    if(arch::state::recover(effective).status!=arch::state::Status::valid)
        return false;
    int mean_calls=0;CountingIdealGas mean_eos(&mean_calls);
    double mean_pressure=0.,mean_speed=0.;
    FluxAdmissibility::required_mean_thermo(effective,composition,mean_eos,mean_pressure,mean_speed);
    if(mean_calls!=1||!std::isfinite(mean_pressure)||!std::isfinite(mean_speed)
       ||std::abs(mean_pressure-1./160.)>1e-13
       ||std::abs(mean_speed-std::sqrt(7./800.))>1e-13)return false;

    // The actual native SoA matches byte-for-byte, yet its cached closure EOS
    // cannot be reused as a physical point interpretation of those bytes.
    double rho[]{1.},mx[]{0.},my[]{0.},mz[]{3./4.},eng[]{17./64.},xs[]{1.};
    double cached_pressure[]{mean_pressure},cached_speed[]{mean_speed};
    unsigned char ready[]{1};
    FluxAdmissibility::MeanThermoView view{
        rho,mx,my,mz,eng,xs,cached_pressure,cached_speed,ready,1,1};
    if(view.geometry_semantics!=GridMetrics::GeometrySemantics::Existing)return false;
    view.geometry_semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    if(!view.matches(0,native,composition,1))return false;
    for(int side=0;side<2;++side) {
        double pressure=123.,speed=456.;
        if(view.query(native,composition,1,side?-1:0,side?0:-1,pressure,speed)
           ||pressure!=123.||speed!=456.)return false;
    }

    // Independently valid cold physical point at r=1/2: mphi=1/2 and E=9/64.
    // Deliberately different cached p/c make an accidental exact-state hit
    // observable without ever calling a negative-energy native point EOS.
    const FluidVector point{1.,0.,0.,.5,9./64.};
    if(arch::state::recover(point).status!=arch::state::Status::valid)return false;
    mz[0]=point.mom_w;eng[0]=point.eng;
    cached_pressure[0]=2./160.;cached_speed[0]=std::sqrt(14./800.);
    if(!view.matches(0,point,composition,1))return false;
    double pressure=321.,speed=654.;
    if(view.query(point,composition,1,0,0,pressure,speed)
       ||pressure!=321.||speed!=654.)return false;
    int face_calls=0;CountingIdealGas point_eos(&face_calls);
    FluxAdmissibility::face_thermo(point,1./64.,composition,1,point_eos,&view,0,pressure,speed);
    if(face_calls!=1||!std::isfinite(pressure)||!std::isfinite(speed)
       ||std::abs(pressure-1./160.)>1e-13
       ||std::abs(speed-std::sqrt(7./800.))>1e-13)return false;

    // HLLC's equal-state optimization queries MeanThermoView directly.
    // F(U,U) is the physical Euler flux: u_r=0 gives only radial pressure.
    int flux_calls=0;CountingIdealGas flux_eos(&flux_calls);
    FluidVector flux;double species_flux[1];
    FluxHLLC<PCMReconstruction>::compute_face_flux(point,point,composition,composition,
        1,flux_eos,0,0.,flux,species_flux,&view,0,0);
    return flux_calls==1&&flux.rho==0.&&std::abs(flux.mom_u-1./160.)<=1e-13
        &&flux.mom_v==0.&&flux.mom_w==0.&&flux.eng==0.&&species_flux[0]==0.;
}

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
    if(!rz_native_mean_miss())return false;
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
