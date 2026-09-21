// Scale invariance and independent analytic references; no tolerance has units.
#include "physics/eos/IdealGas.h"
#include "physics/gravity/ExternalGravitySource.h"
#include "numerics/flux/FluxHLL.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/FluxRoe.h"
#include "numerics/flux/FluxSW.h"
#include "numerics/flux/FluxVL.h"
#include "numerics/linalg/DenseWrap.h"
#include "core/config/ConfigValidation.h"
#include "driver/DriverUtils.h"
#include "numerics/diffusion/DiffFlux.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void close(double actual, double expected, const char* message, double tolerance=1e-11) {
    if (!std::isfinite(actual) || std::abs(actual-expected)>tolerance*std::max(std::abs(expected),1.0)) {
        std::cerr << message << ": " << actual << " vs " << expected << '\n';
        throw std::runtime_error(message);
    }
}
FluidVector conserved(double rho, double u, double p) { return {rho,rho*u,0,0,p/0.4+0.5*rho*u*u}; }
void compare(FluidVector actual, FluidVector expected, double scale, const char* message) {
    close(actual.rho/scale,expected.rho,message); close(actual.mom_u/scale,expected.mom_u,message);
    close(actual.mom_v/scale,expected.mom_v,message); close(actual.mom_w/scale,expected.mom_w,message);
    close(actual.eng/scale,expected.eng,message);
}
template<class Flux> void check_flux(const IdealGasView& eos) {
    double x[]{1.0}, spec[1]; FluidVector reference;
    const auto left=conserved(1.0,0.3,1.0), right=conserved(0.125,-0.1,0.1);
    Flux::compute_face_flux(left,right,x,x,1,eos,0,0.1,reference,spec);
    for (double scale : {1.,1e-12,1e-20,1e-30,1e-60,1e-100}) {
        FluidVector f; Flux::compute_face_flux(scale*left,scale*right,x,x,1,eos,0,0.1,f,spec);
        compare(f,reference,scale,"flux scale invariance");
        FluxAdmissibility::limit_face(scale*left,scale*right,x,x,1,eos,0,f,spec);
        close(spec[0]/scale,f.rho/scale,"species/mass face consistency");
        Flux::compute_face_flux(scale*left,scale*left,x,x,1,eos,0,0.1,f,spec);
        // Euler analytic flux at a uniform face; independent of any solver.
        compare(f,{0.3,1.09,0,0,0.3*(left.eng+1.0)},scale,"uniform analytic flux");
    }
}
void leaves() {
    IdealGasView eos;
    check_flux<FluxHLL<PCMReconstruction>>(eos); check_flux<FluxHLLC<PCMReconstruction>>(eos);
    check_flux<FluxRoe<PCMReconstruction>>(eos); check_flux<FluxSW<PCMReconstruction>>(eos);
    check_flux<FluxVL<PCMReconstruction>>(eos);
    for (double scale : {1.,1e-12,1e-20,1e-30,1e-60,1e-100}) {
        auto u=conserved(scale,2.,3.*scale);
        const auto k=arch::state::recover(u);
        require(k.status==arch::state::Status::valid,"valid low-density state rejected");
        close(k.internal,7.5,"scale-independent internal energy");
        close(eos.get_pressure(u,nullptr)/scale,3.,"ideal-gas analytic pressure");
        FluidVector delta;
        Physical::Gravity::add_external_gravity_source_cell(u,{3.,0.,0.,true},0.5,delta);
        close(delta.mom_u/scale,1.5,"gravity at any positive density");
        close(delta.eng/scale,3.,"gravity work analytic reference");
        close(compute_limited_slope<VanLeer>(scale,2.*scale,3.*scale)/scale,0.5,"MUSCL slope scaling");
        double v[]{0.2,1.,1.1,0.3,0.8,1.}, scaled[6],l,r,sl,sr;
        for(int i=0;i<6;++i) scaled[i]=scale*v[i];
        PPMReconstruction::reconstruct_scalar_ppm(v,l,r);
        PPMReconstruction::reconstruct_scalar_ppm(scaled,sl,sr);
        close(sl/scale,l,"PPM left scale"); close(sr/scale,r,"PPM right scale");
        close(compute_cfl_cell_dt(u,3.,1,.25,1.,1.),.025,"analytic two-face CFL");
        DiffFlux::DiffusionConfigView diffusion{true,true,false,false,0.,.2,0.};
        FluidVector diffusion_flux;
        const auto face = DiffFlux::evaluate_diffusion_face(
            conserved(scale,0.,.4*scale), conserved(scale,0.,.8*scale),
            nullptr,nullptr,0,1,.5,eos,SpeciesPODView{},diffusion,
            nullptr,nullptr,nullptr,nullptr,nullptr,diffusion_flux,nullptr,1);
        require(face.valid && face.active,"low-density conduction rejected");
        close(diffusion_flux.eng/scale,-.4,"Fourier heat flux analytic reference");
        const auto invalid_face = DiffFlux::evaluate_diffusion_face(
            FluidVector{}, u,nullptr,nullptr,0,1,.5,eos,SpeciesPODView{},diffusion,
            nullptr,nullptr,nullptr,nullptr,nullptr,diffusion_flux,nullptr,1);
        require(!invalid_face.valid,"zero-density diffusion treated as inactive success");
        DenseMatrixData<2> matrix; matrix(1,1)=2.*scale; matrix(1,2)=scale;
        matrix(2,1)=scale; matrix(2,2)=3.*scale; double rhs[]{4.*scale,7.*scale};
        require(DenseLUSolver::solve<2,2>(matrix,rhs),"small nonsingular LU rejected");
        close(rhs[0],1.,"dense original-system solution x"); close(rhs[1],2.,"dense original-system solution y");
        auto repair=arch::state::apply_bounds(u,2.*scale,1.,100.);
        require(repair.status==arch::state::Status::repaired,"positive density floor not recorded");
        close(u.rho/scale,2.,"density bound"); close(u.mom_u/u.rho,2.,"floor preserves velocity");
        close(arch::state::recover(u).internal,7.5,"floor preserves thermal state");
    }
    // Original-system residuals and failure controls, not provider status alone.
    for (double scale : {1.,1e-100}) {
        DenseMatrixData<2> identity; identity(1,1)=identity(2,2)=scale;
        double zero_rhs[]{0.,0.};
        require(DenseLUSolver::solve<2,2>(identity,zero_rhs),"scaled identity zero RHS rejected");
        require(zero_rhs[0]==0. && zero_rhs[1]==0.,"zero RHS changed");
        DenseMatrixData<2> singular; singular(1,1)=singular(2,1)=scale;
        singular(1,2)=singular(2,2)=scale; double b[]{scale,scale};
        require(!DenseLUSolver::solve<2,2>(singular,b),"singular system reported success");
    }
    require(VanLeer::calc(std::numeric_limits<double>::infinity())==2.,"unbounded ratio limiter");
    for (double rho : {0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        auto bad=conserved(rho,0.,1.);
        require(!arch::state::accepted(arch::state::apply_bounds(bad,1.,1.,10.).status),"invalid state accepted");
    }
    auto unresolved=FluidVector{1.,1e10,0.,0.,5e19};
    require(arch::state::recover(unresolved).status==arch::state::Status::unresolved_energy,"cancellation hidden");
    auto hot=conserved(1.,0.,40.);
    require(arch::state::apply_bounds(hot,1e-12,1e-10,10.).status==arch::state::Status::energy_ceiling,"ceiling silently clipped");
    double bounded[]{.999,.001};
    require(arch::state::normalize_composition(bounded,2,1,.2),"floor simplex rejected");
    require(bounded[0]>=.2 && bounded[1]>=.2,"normalization undid the requested floor");
    close(bounded[0]+bounded[1],1.,"floor simplex mass closure");
    double zero[]{0.,0.}; require(!arch::state::normalize_composition(zero,2,1,1e-20),"invented empty composition");
    double mixture[]{0.3,0.7}; require(arch::state::normalize_composition(mixture,2,1,1e-20),"valid composition rejected");
    close(mixture[0]+mixture[1],1.,"composition normalization");
}
}
int main() { try { leaves(); std::cout << "Low-density analytic leaves passed through rho=1e-100\n"; }
 catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
