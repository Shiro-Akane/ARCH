// Scale invariance and independent analytic references; no tolerance has units.
#include <algorithm>
#include "driver/runtime/DriverRuntime.h"
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include "physics/eos/IdealGas.h"
#include "physics/gravity/GravitySource.h"
#include "numerics/flux/FluxHLL.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/FluxRoe.h"
#include "numerics/flux/FluxSW.h"
#include "numerics/flux/FluxVL.h"
#include "numerics/linalg/DenseWrap.h"
#include "core/config/ConfigValidation.h"
#include "driver/DriverUtils.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "driver/schedule/DriverControl.h"
#include "driver/schedule/StageScheduler.h"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/diffusion/DiffFunction.h"
#include "numerics/diffusion/DiffusionAMRStages.h"
#include "math/diffusion/ThermalFiveDecayCases.h"
#include "fixtures/hydro/MeanThermoCases.h"
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
void timestep_controls() {
    SimConfig config; config.io.tmax=1.; config.numerics.dt_max=.125;
    SimulationController controller(config,{});
    close(controller.calculate_next_dt(1.,1.),.125,"physical dt cap on first step");
    controller.advance(.125);
    close(controller.calculate_next_dt(.25,1.),.125,"physical dt cap on later step");
    close(controller.dt_old,.125,"cap retained in restart controller state");
    config.io.plt_dt=.1;
    SimulationController aligned(config,{});
    close(aligned.sync_dt(aligned.calculate_next_dt(1.,1.)),.1,"output alignment after cap");
    const auto identity=arch::config::StateControlIdentity(config);
    for(int control=0;control<3;++control) {
        auto changed=config;
        if(control==0) changed.numerics.dt_max=.2;
        if(control==1) changed.physics.eos_coulomb_mult=.5;
        if(control==2) changed.numerics.hll_roe_wave_speed=false;
        require(identity!=arch::config::StateControlIdentity(changed),"new control missing from restart identity");
    }
}
/** Endpoint items use the supplied physical V; they never count cumulative flux. */
void diffusion_activity_scalar_contract() {
    const auto same=DiffFlux::diffusion_energy_activity_term(.5,2.,2.);
    require(same.valid&&same.signed_energy_change==0.&&same.absolute_energy_change==0.,
        "identical energy endpoints must have zero scalar activity");
    const auto outward=DiffFlux::diffusion_energy_activity_term(.5,2.,6.);
    const auto inward=DiffFlux::diffusion_energy_activity_term(2.,3.,2.);
    require(outward.valid&&inward.valid&&outward.signed_energy_change==-2.
        &&inward.signed_energy_change==2.
        &&outward.signed_energy_change+inward.signed_energy_change==0.
        &&outward.absolute_energy_change+inward.absolute_energy_change==4.,
        "volume-weighted signed cancellation concealed nonzero endpoint activity");
    const auto overflow=DiffFlux::diffusion_energy_activity_term(2.,
        std::numeric_limits<double>::max(),0.);
    require(!overflow.valid,"nonrepresentable activity product was accepted");
    for(const double volume:{0.,-1.,std::numeric_limits<double>::infinity()})
        require(!DiffFlux::diffusion_energy_activity_term(volume,2.,1.).valid,
            "invalid true cell measure was accepted for endpoint activity");
    require(!DiffFlux::diffusion_energy_activity_term(1.,
            std::numeric_limits<double>::quiet_NaN(),1.).valid
        &&!DiffFlux::diffusion_energy_activity_term(1.,2.,
            std::numeric_limits<double>::infinity()).valid,
        "nonfinite energy endpoint was accepted for activity");
}

void leaves() {
    diffusion_activity_scalar_contract();
    timestep_controls();
    require(MeanThermoCases::evaluate(),"shared mean thermodynamic view contract");
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

namespace {
/** Independent stationary ODE reference: L(q)=0 implies q(t)=q(0).
 * Used values are checked by IEEE bits; no magnitude threshold or floor.
 * The active hexadecimal cases below are exact rational arithmetic, not the
 * production recurrence repeated as an expected-value helper.
 */
void check_rkl_stationary_cell_reference() {
    namespace rkl=Numerics::Diffusion::detail;
    const auto bits=[](double value){return std::bit_cast<std::uint64_t>(value);};
    const auto equal_fluid=[&](const FluidVector& actual,const FluidVector& expected) {
        for(auto member:std::array<double FluidVector::*,5>{&FluidVector::rho,
            &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
            require(bits(actual.*member)==bits(expected.*member),"RKL exact component reference changed");
    };
    const FluidVector seed{std::nextafter(1.e7,0.),.13,-.27,.41,1.e8};
    const std::array<double,2> fraction{.37,1.-.37},zero_species{};
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    // Each non-density component evolves in turn. Density and every other
    // component are independent stationary ODEs, despite an active energy.
    for(auto member:std::array<double FluidVector::*,4>{&FluidVector::mom_u,
            &FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng}) {
        FluidVector rhs{};rhs.*member=2.;
        FluidVector first=seed;auto first_x=fraction;
        rkl::apply_first_rkl_stage_cell(first,first_x.data(),rhs,zero_species.data(),
            2,1,.125,first,first_x.data()); // actual first-stage alias
        for(auto inactive:std::array<double FluidVector::*,5>{&FluidVector::rho,
                &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
            if(inactive!=member)require(bits(first.*inactive)==bits(seed.*inactive),
                "first RKL stationary component with active neighbor changed");
        require(first.*member!=seed.*member,"first RKL active component lost its RHS");
        for(int s=0;s<2;++s)require(bits(first_x[s])==bits(fraction[s]),"first RKL stationary Xi changed");
        for(bool scaled:{false,true})for(bool second:{false,true}) {
            const auto order=second?DiffFunction::RKLOrder::Second:DiffFunction::RKLOrder::First;
            const auto coefficient=DiffFunction::get_rkl_coeffs(order,2,5);
            FluidVector older=seed;auto older_x=fraction;
            rkl::apply_recursive_rkl_stage_cell(seed,fraction.data(),seed,fraction.data(),
                older,older_x.data(),rhs,zero_species.data(),FluidVector{},zero_species.data(),
                2,1,coefficient,second,.125,scaled,older,older_x.data());
            for(auto inactive:std::array<double FluidVector::*,5>{&FluidVector::rho,
                    &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
                if(inactive!=member)require(bits(older.*inactive)==bits(seed.*inactive),
                    "recursive RKL stationary component changed under older alias");
            require(older.*member!=seed.*member,"recursive RKL active component lost its RHS");
            for(int s=0;s<2;++s)require(bits(older_x[s])==bits(fraction[s]),
                "recursive RKL stationary Xi changed under older alias");
        }
    }
    // Tiny nonzero operators have a representable result at a zero seed. An
    // exact-zero optimization must not turn these independent ODEs stationary.
    const DiffFunction::RKLCoeffs dyadic{1.5,-.5,.125,-.0625};
    for(bool scaled:{false,true})for(bool second:{false,true}) {
        const double q=scaled?rkl::evaluate_recursive_rkl_component<true>(0.,0.,0.,
            0x1p-500,0.,dyadic,second,.5):rkl::evaluate_recursive_rkl_component<false>(
            0.,0.,0.,0x1p-500,0.,dyadic,second,.5);
        require(bits(q)==bits(scaled?0x1p-503:0x1p-504),"tiny nonzero RKL RHS mistaken for stationary");
    }
    {
        const FluidVector rho_one{1.,0.,0.,0.,2.};const double x[]{0.,1.},sx[]{0x1p-500,-0x1p-500};
        double outx[2]{};FluidVector out;
        rkl::apply_first_rkl_stage_cell(rho_one,x,FluidVector{},sx,2,1,.125,out,outx);
        require(bits(outx[0])==bits(0x1p-503),"tiny represented species RHS was erased");
    }
    // First-stage coefficient and genuinely used recursive controls must not
    // hide IEEE nonfinite arithmetic behind the stationary fast path.
    for(double bad:{nan,inf}) {
        FluidVector out;double outx[2]{};
        rkl::apply_first_rkl_stage_cell(seed,fraction.data(),FluidVector{},zero_species.data(),
            2,1,bad,out,outx);
        require(!std::isfinite(out.rho)&&!std::isfinite(outx[0]),
            "nonfinite first RKL coefficient masked by stationary identity");
        for(bool scaled:{false,true})for(bool second:{false,true})for(int field=0;field<4;++field) {
            if(field==3&&!second)continue; // gamma is genuinely unused for RKL1
            auto c=dyadic;
            constexpr std::array<double DiffFunction::RKLCoeffs::*,4> controls{
                &DiffFunction::RKLCoeffs::mu,&DiffFunction::RKLCoeffs::nu,
                &DiffFunction::RKLCoeffs::tilde_mu,&DiffFunction::RKLCoeffs::gamma};
            c.*controls[field]=bad;
            const double q=scaled?rkl::evaluate_recursive_rkl_component<true>(1.,1.,1.,0.,0.,c,second,.5)
                :rkl::evaluate_recursive_rkl_component<false>(1.,1.,1.,0.,0.,c,second,.5);
            require(!std::isfinite(q),"nonfinite used RKL coefficient masked by stationary identity");
        }
        for(bool second:{false,true}) {
            require(!std::isfinite(rkl::evaluate_recursive_rkl_component<false>(
                1.,1.,1.,0.,0.,dyadic,second,bad)),"nonfinite used RKL dt masked");
            require(bits(rkl::evaluate_recursive_rkl_component<true>(1.,1.,1.,0.,0.,
                dyadic,second,bad))==bits(1.),"scaled RKL inspected mathematically unused dt");
        }
        auto first_order=dyadic;first_order.gamma=bad;
        for(bool scaled:{false,true}) {
            const double q=scaled?rkl::evaluate_recursive_rkl_component<true>(seed.rho,seed.rho,
                seed.rho,0.,bad,first_order,false,bad):rkl::evaluate_recursive_rkl_component<false>(
                seed.rho,seed.rho,seed.rho,0.,bad,first_order,false,.5);
            require(bits(q)==bits(seed.rho),"RKL1 unused gamma/initial RHS was wrongly rejected");
        }
        require(!std::isfinite(rkl::evaluate_recursive_rkl_component<false>(
            bad,bad,bad,0.,0.,dyadic,true,.5)),"nonfinite used RKL state gained stationary validity");
        require(!std::isfinite(rkl::evaluate_recursive_rkl_component<false>(
            1.,1.,1.,bad,0.,dyadic,true,.5)),"nonfinite used RKL RHS gained stationary validity");
        require(!std::isfinite(rkl::evaluate_recursive_rkl_component<false>(
            1.,1.,1.,0.,bad,dyadic,true,.5)),"nonfinite used RKL2 initial RHS gained stationary validity");
    }
    // All active operations are dyadic and exactly representable. These fixed
    // hexadecimal vectors come from independent rational evaluation of the
    // original affine scheme, making them insensitive to helper regrouping.
    const FluidVector n{8.,2.,-4.,6.,16.},p{10.,4.,-2.,8.,18.},o{6.,0.,-6.,4.,14.};
    const FluidVector lp{2.,-4.,8.,-12.,32.},ln{-2.,4.,-8.,12.,-16.};
    const DiffFunction::RKLCoeffs active{1.5,-.5,.25,-.125};
    const std::array<FluidVector,4> expected{{
        {0x1.88p+3,0x1.6p+2,0x1p+0,0x1.1p+3,0x1.8p+4},
        {0x1.8cp+3,0x1.5p+2,0x1.8p+0,0x1.fp+2,0x1.9p+4},
        {0x1.9p+3,0x1.4p+2,0x1p+1,0x1.cp+2,0x1.cp+4},
        {0x1.98p+3,0x1.2p+2,0x1.8p+1,0x1.6p+2,0x1.ep+4}}};
    {
        FluidVector alias=n;double x[]{.25,.75};const double sp[]{.375,1.625};
        rkl::apply_first_rkl_stage_cell(alias,x,lp,sp,2,1,.125,alias,x);
        equal_fluid(alias,{0x1.08p+3,0x1.8p+0,-0x1.8p+1,0x1.2p+2,0x1.4p+4});
        require(bits(x[0])==bits(131./528.)&&bits(x[1])==bits(397./528.),
            "active first RKL original rhoXi quotient/alias changed");
    }
    // RKL1's initial operator and gamma are mathematical non-inputs, including
    // fractions. Both public scaled/unscaled wrappers must retain the exact
    // stationary polynomial, without borrowing a new validity helper API.
    for(bool scaled:{false,true}) {
        auto c=active;c.gamma=inf;FluidVector alias=seed;auto x=fraction;
        const FluidVector unused{nan,nan,nan,nan,nan};const double sx[]{nan,inf};
        rkl::apply_recursive_rkl_stage_cell(seed,fraction.data(),seed,fraction.data(),
            alias,x.data(),FluidVector{},zero_species.data(),unused,sx,2,1,c,false,
            scaled?nan:.5,scaled,alias,x.data());
        equal_fluid(alias,seed);
        for(int species=0;species<2;++species)require(bits(x[species])==bits(fraction[species]),
            "RKL1 unused initial species operator poisoned stationary Xi");
    }
    int reference=0;
    for(bool scaled:{false,true})for(bool second:{false,true}) {
        FluidVector alias=o;const double xn[]{.25,.75},xp[]{.5,.5};double xo[]{.125,.875};
        const double sp[]{.375,1.625},sn[]{-.25,-1.75};
        rkl::apply_recursive_rkl_stage_cell(n,xn,p,xp,alias,xo,lp,sp,ln,sn,2,1,
            active,second,.5,scaled,alias,xo);
        equal_fluid(alias,expected[reference++]);
        // Conservation for each active species uses a separate exact rational
        // source table, with rational-to-double conversion at publication only.
        const std::array<std::array<double,2>,4> exact_x{{
            {459./784.,325./784.},{115./198.,83./198.},
            {231./400.,169./400.},{29./51.,22./51.}}};
        for(int s=0;s<2;++s)require(bits(xo[s])==bits(exact_x[reference-1][s]),
            "active RKL rhoXi quotient or older-alias arithmetic changed");
        const auto scalar=scaled?rkl::evaluate_recursive_rkl_hydro_cell<true>(n,p,o,lp,ln,active,second,.5)
            :rkl::evaluate_recursive_rkl_hydro_cell<false>(n,p,o,lp,ln,active,second,.5);
        equal_fluid(alias,scalar);
    }
    std::cout<<"RKL_STATIONARY_CELL_REFERENCE checked_nonbinary_seed=true active_hex=true alias=true physics_qualified=false\n";
}

/** Exercise the actual Host AMR scaled adapter with output==older, not only
 * the cell wrapper. Logical halos, padding, ENUC and source leases are fixed.
 * It is a stage-arithmetic fixture, not BC/EOS or full evolution qualification.
 */
void check_rkl_stationary_host_alias_reference() {
    Grid grid(2,0.,16.,0.,1.,0.,1.);grid.dim=1;grid.InitializeTopology();
    const int total=grid.GetTotalSize();
    FluidState n;n.Preallocate(total);n.InitSpecies(2);
    const FluidVector seed{std::nextafter(1.e7,0.),.13,-.27,.41,1.e8};
    for(int i=0;i<total;++i){n.set(i,seed);n.X(0,i)=.37;n.X(1,i)=1.-.37;n.enuc_rate[i]=17.+i;}
    FluidState p=n,older=n;
    std::vector<FluidVector> rhs(total,FluidVector{}),initial(total,FluidVector{});
    std::vector<double> sx(2*total,0.);
    for(int i=grid.Is();i<grid.Ie();++i)rhs[grid.GetIndex(i,grid.Js(),grid.Ks())].eng=2.;
    const auto bits=[](double q){return std::bit_cast<std::uint64_t>(q);};
    const auto arrays=[](const FluidState& q){return std::array<const double*,7>{q.rho.data(),q.mom_u.data(),
        q.mom_v.data(),q.mom_w.data(),q.eng.data(),q.mass_fractions.data(),q.enuc_rate.data()};};
    const auto n_addresses=arrays(n),p_addresses=arrays(p);
    for(bool second:{false,true}) {
        older=n;const auto addresses=arrays(older);
        const auto c=DiffFunction::get_rkl_coeffs(second?DiffFunction::RKLOrder::Second:DiffFunction::RKLOrder::First,2,5);
        Numerics::Diffusion::detail::apply_recursive_rkl_stage(n,p,older,older,rhs,sx,initial,sx,grid,c,second);
        require(arrays(older)==addresses,"RKL Host older-alias replaced an owned allocation");
        for(int i=0;i<total;++i) {
            const bool active=i>=grid.GetIndex(grid.Is(),grid.Js(),grid.Ks())&&i<grid.GetIndex(grid.Ie(),grid.Js(),grid.Ks());
            require(bits(older.rho[i])==bits(seed.rho)&&bits(older.mom_u[i])==bits(seed.mom_u)
                &&bits(older.mom_v[i])==bits(seed.mom_v)&&bits(older.mom_w[i])==bits(seed.mom_w),
                "Host scaled RKL stationary conserved field changed");
            require(bits(older.X(0,i))==bits(.37)&&bits(older.X(1,i))==bits(1.-.37)
                &&bits(older.enuc_rate[i])==bits(n.enuc_rate[i]),"Host RKL stationary Xi/ENUC changed");
            if(active)require(older.eng[i]!=seed.eng,"Host RKL active E RHS was lost");
            else require(bits(older.eng[i])==bits(seed.eng),"Host RKL changed a logical halo/padding energy");
        }
    }
    require(arrays(n)==n_addresses&&arrays(p)==p_addresses,"Host RKL replaced a const source allocation");
    for(int i=0;i<total;++i)for(const auto* source:{&n,&p}) {
        for(auto member:std::array<double FluidVector::*,5>{&FluidVector::rho,&FluidVector::mom_u,
                &FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
            require(bits(source->get(i).*member)==bits(seed.*member),"Host RKL mutated a const source value");
        require(bits(source->X(0,i))==bits(.37)&&bits(source->X(1,i))==bits(1.-.37)
            &&bits(source->enuc_rate[i])==bits(17.+i),"Host RKL mutated source Xi/ENUC");
    }
    auto alias=n;const auto addresses=arrays(alias);
    Numerics::Diffusion::detail::apply_first_rkl_stage(alias,alias,rhs,sx,grid,.125);
    require(arrays(alias)==addresses,"first Host RKL alias replaced an owned allocation");
    for(int i=0;i<total;++i)require(bits(alias.rho[i])==bits(seed.rho)
        &&bits(alias.X(0,i))==bits(.37)&&bits(alias.X(1,i))==bits(1.-.37),
        "first Host RKL alias changed stationary rho/Xi");
}
/** Reject stale output diagnostics through the actual Host stage wrapper.
 * The burn rate is inherited from the input, independently of RK weights and
 * geometry. Distinct old/output sentinels expose accidental slot provenance;
 * logical halos and padding remain the existing boundary owner's responsibility.
 */
void check_hydro_diagnostic_input() {
    const auto bits=[](double value){return std::bit_cast<std::uint64_t>(value);};
    {
        Grid grid(amr::MAX_NG,1.,2.,-1.,1.,0.,1.);
        grid.dim=1;grid.geometry="cartesian";
        grid.InitializeTopology();
        const int extent=grid.GetTotalSize();
        FluidState old,input,output;
        for(auto* state:{&old,&input,&output}) {
            state->Preallocate(extent);state->InitSpecies(0);
            for(int cell=0;cell<extent;++cell)state->set(cell,{2.,0.,0.,0.,8.});
        }
        std::vector<FluidVector> delta(extent);
        const std::vector<double> species_delta;
        for(int cell=0;cell<extent;++cell) {
            old.enuc_rate[cell]=-9000.-cell;
            input.enuc_rate[cell]=cell&1?7000.+cell:-0.;
        }
        for(const auto method:{arch::scheduler::HydroMethod::Euler,
                arch::scheduler::HydroMethod::RK2,arch::scheduler::HydroMethod::RK3}) {
            for(const auto& stage:arch::scheduler::make_hydro_plan(method).stages) {
                std::fill(output.enuc_rate.begin(),output.enuc_rate.end(),12345.);
                TimeIntegration::perform_stage_update(old,input,output,delta,species_delta,
                    grid,stage.old_weight,stage.update_weight,1e-14,1e-14,1e10);
                for(int cell=0;cell<extent;++cell) {
                    const int k=cell/grid.stride_z;
                    const int j=(cell-k*grid.stride_z)/grid.stride_y;
                    const int i=cell-k*grid.stride_z-j*grid.stride_y;
                    const bool active=i>=grid.Is()&&i<grid.Ie()
                        &&j>=grid.Js()&&j<grid.Je()&&k>=grid.Ks()&&k<grid.Ke();
                    require(bits(output.enuc_rate[cell])==bits(active?input.enuc_rate[cell]:12345.),
                        "Host Hydro diagnostic used stale/weighted output or touched its halo/padding");
                    require(bits(input.enuc_rate[cell])==bits(cell&1?7000.+cell:-0.)
                        &&bits(old.enuc_rate[cell])==bits(-9000.-cell),
                        "Host Hydro diagnostic inheritance mutated an input");
                }
            }
        }
    }
}

/** Independent RK polynomial witnesses through the public shared cell update.
 * Workflow: apply real scheduler weights to L(U)=0; check exact conserved/Xi
 * publication; then use dyadic source data and rational endpoint references.
 * These cell checks do not certify a full evolution or native thermal closure.
 */
void check_rk_conserved_polynomial_reference() {
    const auto bits=[](double x){return std::bit_cast<std::uint64_t>(x);};
    const std::array<double FluidVector::*,5> components{
        &FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,
        &FluidVector::mom_w,&FluidVector::eng};
    const auto exact=[&](const FluidVector& value,const FluidVector& expected,const char* message){
        for(const auto member:components)require(bits(value.*member)==bits(expected.*member),message);
    };
    const double q=0x1.0000000000001p+0;
    const FluidVector seed{q,q,-2.*q,.5*q,16.*q},zero{};
    const std::array<double,2> fraction{.5,.5};
    for(const auto method:{arch::scheduler::HydroMethod::Euler,
            arch::scheduler::HydroMethod::RK2,arch::scheduler::HydroMethod::RK3}) {
        const auto plan=arch::scheduler::make_hydro_plan(method);
        FluidVector current=seed;auto current_fraction=fraction;
        for(const auto& stage:plan.stages) {
            FluidVector output;std::array<double,2> output_fraction{},species_delta{};
            const auto status=TimeIntegration::update_stage_cell(seed,current,zero,
                fraction.data(),current_fraction.data(),species_delta.data(),2,1,
                stage.old_weight,stage.update_weight,1e-30,1e-30,1e300,
                output,output_fraction.data());
            require(status==arch::state::Status::valid,"RK stationary polynomial required a repair/rejection");
            exact(output,seed,"RK stationary polynomial changed a represented conserved component");
            for(int s=0;s<2;++s)require(bits(output_fraction[s])==bits(fraction[s]),
                "RK stationary polynomial changed an exact binary fraction");
            current=output;current_fraction=output_fraction;
        }
    }
    // Euler ignores old: conserved endpoint (4,1,2,3,64)+(2,2,-1,.5,8).
    const FluidVector old_state{4.,1.,-2.,.5,64.},current{6.,-1.,4.,1.5,80.};
    const std::array<double,2> old_x{.25,.75},current_x{.5,.5};
    FluidVector output;std::array<double,2> output_x{};
    const FluidVector euler_input{4.,1.,2.,3.,64.},euler_delta{2.,2.,-1.,.5,8.};
    const std::array<double,2> euler_species{.5,1.5};
    auto status=TimeIntegration::update_stage_cell(old_state,euler_input,euler_delta,
        current_x.data(),old_x.data(),euler_species.data(),2,1,0.,1.,
        1e-30,1e-30,1e300,output,output_x.data());
    require(status==arch::state::Status::valid,"Euler independent nonzero source rejected");
    exact(output,{6.,3.,1.,3.5,72.},"Euler independent dyadic source endpoint differs");
    require(bits(output_x[0])==bits(.25)&&bits(output_x[1])==bits(.75),
        "Euler source did not preserve independent species/mass pairing");
    // Heun endpoint averages old with current+delta. Species masses are
    // (1+4)/2=5/2 and (3+4)/2=7/2, at rho=6: Xi=(5/12,7/12).
    const FluidVector delta{2.,3.,-2.,.5,8.};
    const std::array<double,2> species_delta{1.,1.};
    status=TimeIntegration::update_stage_cell(old_state,current,delta,
        old_x.data(),current_x.data(),species_delta.data(),2,1,.5,.5,
        1e-30,1e-30,1e300,output,output_x.data());
    require(status==arch::state::Status::valid,"RK independent nonzero source rejected");
    exact(output,{6.,1.5,0.,1.25,76.},"RK independent rational conserved endpoint differs");
    require(bits(output_x[0])==bits(5./12.)&&bits(output_x[1])==bits(7./12.),
        "RK independent rational species endpoint differs");
    // A represented convex mean can survive even when curr-old overflows.
    // Input vectors are combined before the resulting conservative state is checked.
    const FluidVector wide_old{2.,-1e308,0.,0.,8.},wide_current{2.,1e308,0.,0.,8.};
    status=TimeIntegration::update_stage_cell(wide_old,wide_current,zero,
        nullptr,nullptr,nullptr,0,1,.5,.5,1e-30,1e-30,1e300,
        output,nullptr,{},1.,0);
    require(status==arch::state::Status::valid,"represented convex endpoint rejected after difference overflow");
    exact(output,{2.,0.,0.,0.,8.},"range-protected convex endpoint differs from exact cancellation");
    const double invalid=std::numeric_limits<double>::quiet_NaN();
    const double infinity=std::numeric_limits<double>::infinity();
    for(const auto weights:std::array<std::array<double,2>,6>{{
            {invalid,.5},{.5,infinity},{-.25,1.25},{1.25,-.25},{.25,.5},{0.,0.}}}) {
        status=TimeIntegration::update_stage_cell(seed,seed,zero,
            nullptr,nullptr,nullptr,0,1,weights[0],weights[1],1e-30,1e-30,1e300,output,nullptr);
        require(!arch::state::accepted(status),"RK malformed weights gained a stationary publication");
    }
    // Finite weights cannot hide a nonfinite required Euler trial.
    const FluidVector huge{2.,0.,0.,0.,1e308},huge_delta{0.,0.,0.,0.,1e308};
    status=TimeIntegration::update_stage_cell(seed,huge,huge_delta,
        nullptr,nullptr,nullptr,0,1,.5,.5,1e-30,1e-30,1e300,output,nullptr);
    require(!arch::state::accepted(status),"RK weighted publication concealed nonfinite curr+delta");
    std::cout<<"RK_CONSERVED_POLYNOMIAL_REFERENCE stationary_methods=3 rational_updates=2 range=1\n";
}


/** Test active complementary species with the real RKL cell adapters.
 * Workflow: gather the closed two-cell operator before any aliased stage write,
 * evolve both cells with the production coefficients, then compare the complete
 * step to independent low-degree Legendre stability polynomials. The operator
 * is L(X)_c=lambda*(X_other-X_c), with no BC, Hydro or normalization shortcut.
 * Its antisymmetric eigenvalue is -2*lambda; the symmetric mass mode is zero.
 * This arithmetic fixture does not qualify the complete DiffusionMode 5tau run.
 */
void check_rkl_active_complementary_reference() {
    namespace rkl=Numerics::Diffusion::detail;
    using Fractions=std::array<std::array<double,2>,2>;
    using Cells=std::array<FluidVector,2>;
    constexpr double lambda=1.;
    constexpr double dt=0x1p-12;
    constexpr int steps=25000;
    constexpr double window=512.*2.*std::numeric_limits<double>::epsilon();
    const long double z=-2.L*static_cast<long double>(lambda)*dt;
    const Fractions initial{{{{.37,.63}},{{.63,.37}}}};
    const std::array<long double,2> initial_mass{
        static_cast<long double>(initial[0][0])+initial[1][0],
        static_cast<long double>(initial[0][1])+initial[1][1]};
    const long double mean=(static_cast<long double>(initial[0][0])+initial[1][0])/2.L;
    const long double initial_amplitude=(static_cast<long double>(initial[1][0])-initial[0][0])/2.L;
    const auto bits=[](double value){return std::bit_cast<std::uint64_t>(value);};
    const Cells fluid_seed{{{1.,0.,0.,0.,4.},{1.,0.,0.,0.,4.}}};
    // Independent exact polynomial coefficients from P2 and P3, not the
    // producer's recurrence weights. dt and lambda are exact binary inputs.
    const auto amplification=[&](bool second,int stages) {
        if(second) return stages==2 ? 1.L+z+z*z/2.L
            : 1.L+z+z*z/2.L+z*z*z/15.L;
        return stages==2 ? 1.L+z+z*z/6.L
            : 1.L+z+5.L*z*z/24.L+5.L*z*z*z/432.L;
    };
    for(bool scaled:{false,true}) for(bool second:{false,true}) for(int stages:{2,3}) {
        const auto order=second?DiffFunction::RKLOrder::Second:DiffFunction::RKLOrder::First;
        const long double decay=amplification(second,stages);
        require(decay>0.L&&decay<1.L,"independent RKL closed-mode polynomial is unstable");
        Cells current=fluid_seed;
        Fractions current_x=initial;
        long double amplitude=initial_amplitude;
        for(int step=0;step<steps;++step) {
            const Cells state_n=current;
            const Fractions species_n=current_x;
            // All genuine operator inputs are gathered from the immutable
            // stage frame. Both species and both cells are computed separately.
            const auto rhs=[&](const Cells& u,const Fractions& x) {
                Fractions result{};
                for(int cell=0;cell<2;++cell) for(int species=0;species<2;++species)
                    result[cell][species]=lambda*(u[1-cell].rho*x[1-cell][species]
                                                -u[cell].rho*x[cell][species]);
                return result;
            };
            const Fractions initial_rhs=rhs(state_n,species_n);
            require(initial_rhs[0][0]!=0.&&initial_rhs[0][1]!=0.,
                "active RKL complementary witness became a stationary test");
            Cells older=state_n,previous=state_n;
            Fractions older_x=species_n,previous_x=species_n;
            for(int stage=1;stage<=stages;++stage) {
                const auto coefficients=DiffFunction::get_rkl_coeffs(order,stage,stages);
                const Fractions previous_rhs=rhs(previous,previous_x);
                Fractions used_previous=previous_rhs,used_initial=initial_rhs;
                if(scaled) for(int cell=0;cell<2;++cell) for(int species=0;species<2;++species) {
                    used_previous[cell][species]*=dt;
                    used_initial[cell][species]*=dt;
                }
                if(stage==1) {
                    for(int cell=0;cell<2;++cell)
                        rkl::apply_first_rkl_stage_cell(state_n[cell],species_n[cell].data(),
                            FluidVector{},used_initial[cell].data(),2,1,
                            scaled?coefficients.tilde_mu:coefficients.tilde_mu*dt,
                            previous[cell],previous_x[cell].data());
                } else {
                    // Output really aliases older. The next stage sees all
                    // outputs only after the two-cell gather and writes finish.
                    for(int cell=0;cell<2;++cell)
                        rkl::apply_recursive_rkl_stage_cell(state_n[cell],species_n[cell].data(),
                            previous[cell],previous_x[cell].data(),older[cell],older_x[cell].data(),
                            FluidVector{},used_previous[cell].data(),FluidVector{},used_initial[cell].data(),
                            2,1,coefficients,second,dt,scaled,older[cell],older_x[cell].data());
                    std::swap(previous,older);
                    std::swap(previous_x,older_x);
                }
                for(int cell=0;cell<2;++cell) {
                    require(arch::state::validate_composition(previous_x[cell].data(),2,1)
                        ==arch::state::Status::valid,"active RKL complementary simplex rejected");
                    for(auto member:std::array<double FluidVector::*,5>{&FluidVector::rho,
                            &FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
                        require(bits(previous[cell].*member)==bits(fluid_seed[cell].*member),
                            "passive active-species RKL changed stationary fluid");
                }
            }
            current=previous;
            current_x=previous_x;
            amplitude*=decay;
            for(int species=0;species<2;++species) {
                const long double mass=static_cast<long double>(current_x[0][species])+current_x[1][species];
                require(std::abs(mass-initial_mass[species])<=window*std::abs(initial_mass[species]),
                    "active closed RKL changed global species mass");
            }
            for(int cell=0;cell<2;++cell) for(int species=0;species<2;++species) {
                const long double tracer=mean+(cell?amplitude:-amplitude);
                const long double expected=species==0?tracer:initial_mass[0]-tracer;
                require(std::abs(static_cast<long double>(current_x[cell][species])-expected)<=window,
                    "active RKL decay differs from independent stability polynomial");
            }
        }
    }
    std::cout<<"RKL_ACTIVE_COMPLEMENTARY_REFERENCE routes=8 steps=25000 cells=2 species=2 "
        "simplex=core-window decay=independent-polynomial scope=cell-arithmetic-only full_5tau=false\n";
}

}

int main() {
    try { check_hydro_diagnostic_input(); check_rk_conserved_polynomial_reference(); check_rkl_stationary_cell_reference(); check_rkl_stationary_host_alias_reference(); check_rkl_active_complementary_reference(); leaves(); ThermalFiveDecayCases::run(); std::cout << "Low-density analytic leaves passed through rho=1e-100\n"; }
 catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
