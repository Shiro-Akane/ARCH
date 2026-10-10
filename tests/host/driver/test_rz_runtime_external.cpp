/**
 * @file test_rz_runtime_external.cpp
 * @brief Real external-source identity, body budgets and warm split coupling.
 *
 * Workflow:
 * 1. Construct real Native RZ Tree/Runtime, phased BC and bound IdealGas/EOS.
 * 2. Audit source/time/operand identities and exact whole-macro rollback.
 * 3. Run the original 24 Hydro/source cases and independent V/W body budgets.
 * 4. Exercise both real RKL2 viscosity halves around selected Hydro/source;
 *    retain original normalized integral budgets and accepted endpoint checks.
 *
 * This is linked only by the existing embedded gravity-stage contract owner.
 * No extra main, CLI, CTest or second scientific implementation is provided.
 */
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "host/driver/RzRuntimeWitness.h"

#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/DriverMacroStep.h"
#include "driver/stages/DriverStages.h"
#include "driver/stages/GravityStage.h"
#include "numerics/burnsolver/ode/ode_be-nr.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/linalg/DenseWrap.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/ExternalGravity.h"
#include "physics/gravity/NativeExternalSource.h"
#include "physics/network/aprox13/NetAprox13.h"
using rz_runtime_witness::require;

/** Actual Runtime external-source contracts; no public/native/device grant.
 * Independent V/W antiderivatives and a three-observation density fit audit
 * actual source work. Evolution, source receipts, RK and EOS stay production.
 */
namespace external_runtime_checks {
using namespace arch;
using state::StateSlot;
constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr double dt=1.e-4;
constexpr long double budget=1.e-12L;
using Integrator=driver::IntegratorSolve;
using Hydro=Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>>;
using rz_runtime_witness::bits;
using rz_runtime_witness::near;
using rz_runtime_witness::fields;
const long double pi=std::acos(-1.L);

long double power_integral(long double l,long double h,int p) {
    return (std::pow(h,p+1)-std::pow(l,p+1))/(p+1);
}
void close_budget(long double a,long double b,long double scale,const char* message) {
    require(std::isfinite(a)&&std::isfinite(b)&&scale>0.
        &&std::abs(a-b)<=budget*scale,message);
}
/** Independent residual-coordinate polynomial fitted to actual native V means.
 * Solve the 3x3 moment equations in long double; no production density/closure
 * or quadrature function supplies the reference. These smooth fixtures require
 * the uncontracted polynomial to be positive, so a positivity contraction is
 * outside this oracle and fails explicitly rather than borrowing its output.
 */
struct MomentReference {
    long double v,w,m,b,c,j,omega;
};
MomentReference moment_reference(const FluidState& s,const Grid& g,int i,int j) {
    const int begin=std::clamp(i-1,0,g.GetTotalX()-3);
    const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
    const long double mid=(l+h)/2.,dx=h-l;
    require(l>=0.&&h>l,"external reference requires a positive real annulus");
    long double a[3][4]{};
    for(int n=0;n<3;++n) {
        const long double lo=(g.GetFacePosL(begin+n)-mid)/dx;
        const long double hi=(g.GetFacePosR(begin+n)-mid)/dx;
        const long double rr=mid/dx;
        const long double volume=rr*power_integral(lo,hi,0)+power_integral(lo,hi,1);
        require(volume>0.,"external independent density observation lacks real V");
        for(int q=0;q<3;++q)
            a[n][q]=(rr*power_integral(lo,hi,q)+power_integral(lo,hi,q+1))/volume;
        a[n][3]=s.rho[g.GetIndex(begin+n,j,0)];
    }
    for(int q=0;q<3;++q) {
        int row=q;for(int n=q+1;n<3;++n)if(std::abs(a[n][q])>std::abs(a[row][q]))row=n;
        require(a[row][q]!=0.&&std::isfinite(a[row][q]),"external reference singular moment observations");
        for(int k=q;k<4;++k)std::swap(a[q][k],a[row][k]);
        const long double divisor=a[q][q];for(int k=q;k<4;++k)a[q][k]/=divisor;
        for(int n=0;n<3;++n)if(n!=q) {
            const long double factor=a[n][q];for(int k=q;k<4;++k)a[n][k]-=factor*a[q][k];
        }
    }
    const long double p[3]{a[0][3],a[1][3],a[2][3]};
    const auto density=[&](long double t){return p[0]+t*(p[1]+t*p[2]);};
    long double minimum=std::min(density(-.5L),density(.5L));
    if(p[2]!=0.) {
        const long double vertex=-p[1]/(2.*p[2]);
        if(vertex>-.5L&&vertex<.5L)minimum=std::min(minimum,density(vertex));
    }
    require(minimum>0.&&std::isfinite(minimum),"external smooth reference requires uncontracted positive density");
    const auto integral=[&](int radial_power) {
        long double total=0.;
        // Exact binomial expansion through degree five in the independent t chart.
        for(int q=0;q<3;++q)for(int k=0;k<=radial_power;++k) {
            long double choose=1.;
            for(int n=0;n<k;++n)choose*=static_cast<long double>(radial_power-n)/(n+1);
            total+=dx*p[q]*choose*std::pow(mid,radial_power-k)*std::pow(dx,k)
                *power_integral(-.5L,.5L,q+k);
        }
        return total;
    };
    MomentReference result;
    result.v=power_integral(l,h,1);result.w=power_integral(l,h,2);
    result.m=integral(1);result.b=integral(2);result.c=integral(3);
    result.j=s.mom_w[g.GetIndex(i,j,0)]*result.w;result.omega=result.j/result.c;
    require(result.m>0.&&result.c>0.&&std::isfinite(result.omega),"external reference inertia is invalid");
    close_budget(result.m,s.rho[g.GetIndex(i,j,0)]*result.v,result.m,
        "external independent polynomial did not preserve actual native density mean");
    return result;
}

struct Observer final:Numerics::IHydroSolver {
    const Hydro& actual;
    const IdealGas& eos;
    const amr::AMRControl& control;
    const Physical::Gravity::ExternalGravity& force;
    mutable std::mutex mutex;
    mutable std::array<long double,4> body{},unweighted_body{};
    mutable std::array<long double,5> outward{},unweighted_outward{};
    mutable int calls=0;
    scheduler::StageDescriptor descriptor{};
    double input_time=0.;
    bool abort_after_source=false;
    mutable bool source_reference_seen=false;
    // A test-only pre-producer defense probe; empty for every original case.
    // One actual prepared receipt is exercised, never a synthetic source frame.
    std::function<void(amr::AMRControl*,int,const FluidState&,const Grid&,double,
        const Physical::Gravity::IGravityPolicy*,const NumericsConfig&)> application_probe;
    Observer(const Hydro& h,const IdealGas& e,const amr::AMRControl& c,
        const Physical::Gravity::ExternalGravity& f):actual(h),eos(e),control(c),force(f){}
    GridMetrics::GeometrySemantics geometry_semantics() const noexcept override {return actual.geometry_semantics();}
    Numerics::HostHydroStorageContract host_storage_contract() const noexcept override {
        return actual.host_storage_contract();
    }
    /** Only observe immutable actual stage input and actual published capture;
     * one real selected producer performs every field/source/face operation.
     */
    void evaluate_patch(amr::AMRControl* ctrl,int id,const FluidState& input,const Grid& grid,double interval,
        std::vector<FluidVector>& du,std::vector<double>& ds,const Physical::Gravity::IGravityPolicy* gravity,
        const NumericsConfig& cfg,double weight=1.,void* stream=nullptr,
        const boundary::HostHydroBoundaryAuthority* walls=nullptr) const override
    {
        require(ctrl==&control&&gravity==&force&&interval==dt
            &&weight==descriptor.flux_register_weight&&walls,
            "external observer lost actual policy/domain/interval/descriptor/wall authority");
        const auto& block=ctrl->pool->GetBlock(id);
        const auto member=TimeIntegration::hydro_boundary_state_member(descriptor.input_slot);
        block.RequireNativeGeometryIdentity();
        require(&(block.*member)==&input,"external observer input is not its actual selected slot");
        // The real Runtime source/slot/wall preparation already completed.
        // A rejecting probe must leave before the selected physical producer.
        if(application_probe)application_probe(ctrl,id,input,grid,interval,gravity,cfg);
        std::array<long double,4> source{};
        for(int j=grid.Js();j<grid.Je();++j)for(int i=grid.Is();i<grid.Ie();++i) {
            const auto ref=moment_reference(input,grid,i,j);
            const long double dz=grid.GetAxialFacePosR(j)-grid.GetAxialFacePosL(j);
            const int cell=grid.GetIndex(i,j,0);
            const long double factor=2.*pi*dz*interval;
            source[0]+=factor*force.g_x*ref.m;
            source[1]+=factor*force.g_y*ref.m;
            source[2]+=factor*force.g_z*ref.b;
            source[3]+=factor*(force.g_x*input.mom_u[cell]*ref.v
                +force.g_y*input.mom_v[cell]*ref.v+force.g_z*ref.omega*ref.b);
        }
        actual.evaluate_patch(ctrl,id,input,grid,interval,du,ds,gravity,cfg,weight,stream,walls);
        std::array<long double,5> out{},raw_out{};
        const auto capture=input.boundary_flux_capture;
        require(bool(capture),"external actual Hydro did not have real Runtime capture storage");
        const int species=input.GetNumSpecies(),count_fields=6+species;
        for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side) {
            const auto& plane=capture->stage[2*axis+side];if(plane.empty())continue;
            require(block.face_neighbors[2*axis+side].count==0,
                "external Runtime capture invented an internal AMR surface");
            const int nmax=axis==0?grid.Je()-grid.Js():grid.Ie()-grid.Is();
            require(plane.size()==static_cast<std::size_t>(nmax*count_fields),"external capture layout changed");
            for(int n=0;n<nmax;++n) {
                const int i=axis==0?(side?grid.Ie()-1:grid.Is()):grid.Is()+n;
                const int j=axis==0?grid.Js()+n:(side?grid.Je()-1:grid.Js());
                const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
                const long double dz=grid.GetAxialFacePosR(j)-grid.GetAxialFacePosL(j);
                const long double r=side?h:l;
                const long double area=axis==0?2.*pi*r*dz:2.*pi*power_integral(l,h,1);
                const long double torque=axis==0?2.*pi*r*r*dz:2.*pi*power_integral(l,h,2);
                const long double factor=(side?1.L:-1.L)*interval;
                const int map[5]{0,4,3,5,6};
                for(int q=0;q<5;++q) {
                    const long double value=plane[n*count_fields+map[q]];
                    const long double amount=factor*(q==2?torque:area)*value;
                    out[q]+=amount;raw_out[q]+=amount/weight;
                }
            }
        }
        std::lock_guard lock(mutex);
        for(int q=0;q<4;++q){body[q]+=weight*source[q];unweighted_body[q]+=source[q];}
        for(int q=0;q<5;++q){outward[q]+=out[q];unweighted_outward[q]+=raw_out[q];}
        ++calls;source_reference_seen=true;
        if(abort_after_source)throw std::runtime_error("COLD_ACTUAL_SOURCE_OBSERVED");
    }
    void update_patch(const FluidState& old,const FluidState& current,FluidState& next,
        const std::vector<FluidVector>& du,const std::vector<double>& ds,const Grid& grid,
        double old_weight,double update_weight,const NumericsConfig& cfg,void* stream=nullptr) const override
    {actual.update_patch(old,current,next,du,ds,grid,old_weight,update_weight,cfg,stream);}
};

struct Fixture {
    SimConfig config;
    SpeciesManager species;
    std::unique_ptr<IdealGas> eos;
    amr::AMRControl control{32,2};
    RunState start{};
    std::unique_ptr<SimulationController> controller;
    std::unique_ptr<boundary::ScopedUserBoundarySelection> selection;
    std::unique_ptr<BCHandler> bc;
    std::unique_ptr<driver::DriverRuntime> runtime;
    std::unique_ptr<Physical::Gravity::ExternalGravity> force;
    std::unique_ptr<Hydro> actual;
    std::unique_ptr<Observer> observer;
    std::unique_ptr<driver::GravityStage> source;
    std::optional<scheduler::StageExecutionContext> context;
    dispatch::ResolvedExecutionPlan plan{};
    driver::DriverStageWorkspace workspace;
    bool poison=false,poisoned=false;
    // Coupled-only diagnostics: the default path below remains disabled and
    // keeps every original 24-case input and Driver call unchanged.
    double actual_diffusion_fe=0.;
    int actual_diffusion_stages=0,actual_diffusion_half_calls=0;
    double configured_time=0.;
    boundary::BoundaryPurpose configured_purpose=boundary::BoundaryPurpose::Hydro;
    Fixture(int direction,bool open,double phi,dispatch::TimeIntegratorId method,bool simple=false,
        bool cold=false,double inner=1.,double radial=0.,double viscosity=0.) {
        // Freeze configuration before the real logical/compiled BC owner.
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=simple?1:(direction==0?2:1);
        config.grid.nblockx2=simple?1:(direction==0?1:2);config.grid.nblockx3=0;
        config.grid.x1_min=inner;config.grid.x1_max=inner+2.;
        config.grid.x2_min=simple?0.:-1.;config.grid.x2_max=simple?2.:1.;
        config.grid.amr_max_blocks=32;config.amr.lrefinemin=0;config.amr.lrefinemax=simple?0:1;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type=open?"outflow":"reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type=open?"outflow":"reflecting";
        config.numerics.solver_name="HLLC";config.numerics.reconstruction="pcm";
        config.numerics.time_integrator=method==dispatch::TimeIntegratorId::Euler?"euler":
            method==dispatch::TimeIntegratorId::Rk2?"rk2":"rk3";
        config.numerics.entropy_fix_coeff=0.;config.numerics.hll_roe_wave_speed=true;
        config.numerics.sml_rho=1.e-14;config.numerics.min_eint=1.e-14;config.numerics.max_eint=1.e10;
        if(cold) {
            config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="user";
            config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
        }
        config.physics.gravity.type="external";config.physics.gravity.g_x=radial;
        config.physics.gravity.g_y=0.;config.physics.gravity.g_z=phi;
        config.physics.diffusion.use_diffusion=false;config.io.tmax=1.;
        if(viscosity>0.) {
            require(std::isfinite(viscosity)&&!cold,"coupled viscosity must be a positive warm-state input");
            // Freeze the real physical controls before Tree, BC and Runtime.
            config.physics.burn.use_burn=false;
            config.physics.diffusion.use_diffusion=true;
            config.physics.diffusion.use_viscous_diffusion=true;
            config.physics.diffusion.use_thermal_diffusion=false;
            config.physics.diffusion.use_species_diffusion=false;
            config.physics.diffusion.nu_visc=viscosity;
            config.physics.diffusion.integrator="RKL2";
            config.physics.diffusion.diff_cfl=.8;
            config.physics.diffusion.max_stages=256;
        }
        species.add_species("gas0",1.,1.,1.4,3.);species.add_species("gas1",2.,1.,1.4,3.);
        eos=std::make_unique<IdealGas>(1.4,species);
        if(simple)control.tree->InitRootGrid(config,2,rz);
        else control.tree->LoadLeafGrid(config,2,{1,1,1,1,0},{0,1,0,1,static_cast<std::uint32_t>(direction==0?1:0)},
            {0,0,1,1,static_cast<std::uint32_t>(direction==0?0:1)},{0,0,0,0,0},rz);
        control.flux_register.EnsureSpecies(2);
        for(int id:control.tree->GetActiveBlocks()) {
            auto& b=control.pool->GetBlock(id);b.RequireNativeGeometryIdentity();const auto& g=b.grid;
            require(g.dyadic_identity.bound,"external actual fixture lacks authentic Native root provenance");
            for(auto* s:rz_runtime_witness::slots(b)) {
                s->stage_repairs.reset(2,state::RepairSemantics::RzVolumeAngular);
                for(int c=0;c<g.GetTotalSize();++c){s->set(c,{2.,0.,0.,0.,100.});s->X(0,c)=.6;s->X(1,c)=.4;s->enuc_rate[c]=.125+c/16.;}
                for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                    const int c=g.GetIndex(i,j,0);
                    const double l=g.GetFacePosL(i),h=g.GetFacePosR(i),r=g.GetCellCenterX(i),z=g.GetCellCenterY(j);
                    if(simple) {
                        const long double v=power_integral(l,h,1),w=power_integral(l,h,2);
                        const long double mass=cold?.875L*v+.25L*power_integral(l,h,3):2.L*v;
                        const long double inertia=cold?.875L*power_integral(l,h,3)+.25L*power_integral(l,h,5)
                            :2.L*power_integral(l,h,3);
                        const long double thermal=cold?0x1p-25L:100.L;
                        s->set(c,{double(mass/v),0.,0.,double(inertia/w),double(thermal*mass/v+.5L*inertia/v)});
                    } else {
                        // The original 24-case numerical inputs, unchanged.
                        const double rho=2.+.1*std::cos(double(pi)*(r-inner))*.1*std::cos(double(pi)*z);
                        const double ur=.03*std::sin(double(pi)*(r-inner)/2.);
                        const double uz=.02*std::sin(double(pi)*(z+1.)/2.);
                        const double wc=h<=0.?-GridMetrics::Rz::AngularReconstructionRadius(-h,-l):
                            GridMetrics::Rz::AngularReconstructionRadius(l,h);
                        const double up=.15*wc*(1.+.2*std::cos(double(pi)*z));
                        s->set(c,{rho,rho*ur,rho*uz,rho*up,12.5+.5*rho*(ur*ur+uz*uz+up*up)});
                        const double x=.6+.02*std::cos(double(pi)*z);s->X(0,c)=x;s->X(1,c)=1.-x;
                    }
                }
            }
        }
        start.time=.375;start.step=1;start.has_timestep_state=true;start.dt_old=dt;
        start.repairs.reset(2,state::RepairSemantics::RzVolumeAngular);
        controller=std::make_unique<SimulationController>(config,start);
        if(cold) {
            boundary::ResolvedUserBoundaries callbacks;
            callbacks.identity="native-external-actual-cold-quadratic-rho";
            callbacks.physical=[](const boundary::PhysicalBoundaryContext& c) {
                require(c.axis==boundary::BoundaryAxis::X1&&c.ghost_point.r_cy>0.
                    &&std::isfinite(c.time),"cold external boundary lost its actual positive point/time");
                PrimitiveData primitive;const double r=c.ghost_point.r_cy;
                primitive.rho=.875+.25*r*r;primitive.u=primitive.v=0.;primitive.w=r;
                primitive.SetTemperature(0x1p-25/3.);primitive.mass_fractions={.6,.4};
                boundary::PhysicalBoundaryData data;data.hydro=primitive;return data;
            };
            selection=std::make_unique<boundary::ScopedUserBoundarySelection>(
                std::move(callbacks),config,species);
        }
        bc=std::make_unique<BCHandler>(config,rz);bc->bind(*eos,species);
        bc->configure_stage(controller->t_current,boundary::BoundaryPurpose::Hydro);
        runtime=std::make_unique<driver::DriverRuntime>(control,*bc,config,species,*controller);
        runtime->bind_native_rz_eos(*eos);runtime->initialize_topology();
        force=std::make_unique<Physical::Gravity::ExternalGravity>(radial,0.,phi);
        actual=std::make_unique<Hydro>(*eos,rz);
        observer=std::make_unique<Observer>(*actual,*eos,control,*force);
        plan.time_integrator=method;
        if(config.physics.diffusion.use_diffusion)
            plan.diffusion_integrator=dispatch::DiffusionIntegratorId::Rkl2;
        context.emplace(runtime->stage_context());
    }
    /** Actual configure callback recreates the unique real EOS acceptance. */
    void refresh() {
        runtime->bind_native_boundary_acceptance(*context,runtime->handles());
        const auto real=context->post_boundary_acceptance;
        context->post_boundary_acceptance=[this,real](const scheduler::StageExecutionContext& frame,
            StateSlot slot,state::StateVersion version) {
            const auto stages=scheduler::supported_hydro_time_plan(hydro_method()).stages;
            if(poison&&!poisoned&&observer->descriptor.stage==static_cast<int>(stages.size())
                &&slot==stages.back().output_slot) {
                auto& b=control.pool->GetBlock(control.tree->GetActiveBlocks().back());
                const auto member=TimeIntegration::hydro_boundary_state_member(slot);
                (b.*member).eng[b.grid.GetIndex(b.grid.Is(),b.grid.Js(),0)]=-1.;poisoned=true;
            }
            real(frame,slot,version); // Never manufacture or suppress scientific acceptance.
        };
    }
    scheduler::HydroMethod hydro_method() const {
        return plan.time_integrator==dispatch::TimeIntegratorId::Euler?scheduler::HydroMethod::Euler:
            plan.time_integrator==dispatch::TimeIntegratorId::Rk2?scheduler::HydroMethod::RK2:scheduler::HydroMethod::RK3;
    }
    Integrator solve() const {
        return plan.time_integrator==dispatch::TimeIntegratorId::Euler?&SolverEuler::solve<BCHandler>:
            plan.time_integrator==dispatch::TimeIntegratorId::Rk2?&SolverRK2::solve<BCHandler>:&SolverRK3::solve<BCHandler>;
    }
    void bind_frame() {
        context->step_start_time=controller->t_current;context->step_dt=dt;
        context->boundary_start_time=controller->t_current;context->boundary_step_dt=.5*dt;
        context->configure_boundary_context=[this](double t,boundary::BoundaryPurpose purpose){
            bc->configure_stage(t,purpose);configured_time=t;configured_purpose=purpose;refresh();
        };
        context->physical_boundary_preparation=[this](StateSlot slot,double t,boundary::BoundaryPurpose purpose) {
            context->configure_boundary_context(t,purpose);runtime->ensure_fluid_ghosts(slot);
        };
        context->configure_boundary_context(controller->t_current,boundary::BoundaryPurpose::Hydro);
        runtime->bind_boundary_accounting(*context);
        require(context->hydro_flux_capture_begin&&context->hydro_flux_capture_accept,
            "actual Native built-in boundary accounting is not integrated");
        const auto real_begin=context->hydro_flux_capture_begin;
        context->hydro_flux_capture_begin=[this,real_begin](const scheduler::StageDescriptor& d) {
            observer->descriptor=d;observer->input_time=context->step_start_time+d.input_time_fraction*context->step_dt;
            require(std::isfinite(observer->input_time)&&bits(configured_time,observer->input_time)
                &&configured_purpose==boundary::BoundaryPurpose::Hydro,
                "external actual RK boundary/source time changed");
            real_begin(d);
        };
        const auto first=scheduler::supported_hydro_time_plan(hydro_method()).stages.front();
        context->hydro_flux_capture_begin(first);
        runtime->ensure_fluid_ghosts();
    }
    /** Keep the original disabled path literal; enabled coupling borrows the
     * actual Driver FE reduction before the unmodified five-segment owner.
     * The selected dt is never reduced/clamped to obtain a passing result.
     */
    void advance(const Physical::Gravity::IGravityPolicy* selected=nullptr,bool advance_time=true) {
        if(config.physics.diffusion.use_diffusion) {
            const auto candidates=driver::calculate_timestep_candidates(*runtime,workspace,*eos,&plan);
            actual_diffusion_fe=candidates.diffusion_forward_euler;
            require(std::isfinite(actual_diffusion_fe)&&actual_diffusion_fe>0.
                &&std::isfinite(candidates.hydro)&&dt<=candidates.hydro,
                "coupled original dt is outside its actual Driver Hydro/FE input contract");
            actual_diffusion_stages=DiffFunction::compute_stages(DiffFunction::RKLOrder::Second,
                .5*dt,actual_diffusion_fe,config.physics.diffusion.diff_cfl,
                config.physics.diffusion.max_stages);
            require(actual_diffusion_stages>=2,"coupled real RKL2 did not select its recurrence");
        }
        scheduler::ScopedStageBinding binding(*context,runtime->handles());
        driver::execute_driver_macro_step(*runtime,*context,observer.get(),false,
            [](driver::BurnHalf,double,state::CompletionToken)->state::CompletionToken {
                throw std::logic_error("inactive external fixture Burn was called");
            },
            [&](double half) {
                if(config.physics.diffusion.use_diffusion) {
                    require(bits(half,.5*dt),"coupled split changed the original diffusion half dt");
                    ++actual_diffusion_half_calls;
                    driver::advance_diffusion(*runtime,workspace,*context,*eos,&plan,
                        controller->step_count,half,actual_diffusion_fe);
                } else driver::advance_diffusion(*runtime,workspace,*context,*eos,&plan,
                    controller->step_count,half,1.e99);
            },
            [&](double interval){driver::advance_hydro(*runtime,workspace,*context,&plan,interval,solve(),
                selected?selected:force.get(),observer.get());},
            [](driver::CpuStage,auto&& execute){execute();});
        if(advance_time)controller->advance(dt); // Only after the actual owner accepted.
    }
    void bind_source() {
        source=std::make_unique<driver::GravityStage>(*runtime,force.get());
        require(source->active()&&source->supports_macro_step_journal(arch::state::ExecutionSide::Host),"real native external journal is inactive");
        context->hydro_preparation=source.get();
    }
    std::array<long double,6> totals() const {
        std::array<long double,6> result{};
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int c=g.GetIndex(i,j,0);const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
                const long double dz=g.GetAxialFacePosR(j)-g.GetAxialFacePosL(j);
                const long double v=2.*pi*dz*power_integral(l,h,1),w=2.*pi*dz*power_integral(l,h,2);
                result[0]+=b.fluid_state.rho[c]*v;result[1]+=b.fluid_state.eng[c]*v;
                result[2]+=b.fluid_state.mom_w[c]*w;result[3]+=std::abs(b.fluid_state.mom_w[c])*w;
                result[4]+=b.fluid_state.rho[c]*b.fluid_state.X(0,c)*v;
                result[5]+=b.fluid_state.rho[c]*b.fluid_state.X(1,c)*v;
            }
        }return result;
    }
};

/** Check genuine final Host publication and native repair identities. */
void accepted_current(Fixture& f) {
    require(!f.runtime->active_runtime_state_transaction()&&!f.force->prepared_native_external(),
        "external accepted endpoint retained a transaction/source borrow");
    for(const auto handle:f.runtime->handles()) {
        const state::StateKey key{handle,StateSlot::Current};
        const auto coherence=f.context->ledger.inspect(key);
        scheduler::detail::require_settled_destination(coherence);
        f.context->ledger.require_readable(key,{state::ExecutionSide::Host,
            coherence.interior.version,true,false});
    }
    require(f.runtime->repair_budget().semantics==state::RepairSemantics::RzVolumeAngular,
        "external accepted endpoint changed repair measure");
    for(double value:f.runtime->repair_budget().values)
        require(value==0.,"external accepted endpoint concealed floor/heating/normalization");
    for(int id:f.control.tree->GetActiveBlocks())for(auto* slot:rz_runtime_witness::slots(f.control.pool->GetBlock(id))) {
        require(slot->stage_repairs.semantics==state::RepairSemantics::RzVolumeAngular,
            "external actual stage receipt changed native measure");
        for(double value:slot->stage_repairs.values)
            require(value==0.,"external actual stage receipt concealed repair/heating");
    }
}

/** Independent actual state/owner witness, deliberately distinct from backup logic. */
struct RollbackWitness {
    Fixture& f;
    std::vector<std::pair<int,rz_runtime_witness::FieldsWitness>> values;
    std::vector<GridMetrics::DyadicGridIdentity> roots;
    driver::RuntimeStateTransaction::OwnerWitness owner;
    std::array<long double,4> body;
    double time,advice;
    int step;
    explicit RollbackWitness(Fixture& actual)
        : f(actual),owner(driver::RuntimeStateTransaction::snapshot_owner(*f.runtime,*f.context)),
          body(f.source?f.source->external_source_budget():std::array<long double,4>{}),
          time(f.controller->t_current),advice(f.controller->dt_old),step(f.controller->step_count) {
        for(int id:f.control.tree->GetActiveBlocks()) {
            auto& b=f.control.pool->GetBlock(id);values.emplace_back(id,rz_runtime_witness::FieldsWitness(b));
            roots.push_back(b.grid.dyadic_identity);
        }
    }
    void unchanged() const {
        require(driver::RuntimeStateTransaction::owner_matches(*f.runtime,*f.context,owner),
            "external rejection changed real Runtime owner/ledger/register/BC/receipt identity");
        for(std::size_t n=0;n<values.size();++n) {
            auto& block=f.control.pool->GetBlock(values[n].first);values[n].second.matches(block);
            require(GridMetrics::equal_identity(roots[n],block.grid.dyadic_identity),
                "external rejection changed actual root provenance");
        }
        require(bits(time,f.controller->t_current)&&bits(advice,f.controller->dt_old)
            &&step==f.controller->step_count&&!f.runtime->active_runtime_state_transaction()
            &&!f.force->prepared_native_external(),"external rejection changed accepted time/advice/source borrow");
        if(f.source)require(f.source->external_source_budget()==body,
            "external rejection leaked a source-budget accepted prefix");
    }
};

/** Exact buffer witness for the helper's mutable operands; compare values,
 * addresses and extents, never object padding. Marked nonzero buffers expose a
 * source/scratch write even when a zeroed production delta could conceal it.
 */
struct ApplicationBufferWitness {
    std::vector<FluidVector> du,scratch;
    std::vector<double> fractions;
    const FluidVector *du_data,*scratch_data;
    const double* fractions_data;
    ApplicationBufferWitness(const std::vector<FluidVector>& d,
        const std::vector<FluidVector>& s,const std::vector<double>& x)
        :du(d),scratch(s),fractions(x),du_data(d.data()),scratch_data(s.data()),fractions_data(x.data()){}
    static bool same(const FluidVector& a,const FluidVector& b) {
        return bits(a.rho,b.rho)&&bits(a.mom_u,b.mom_u)&&bits(a.mom_v,b.mom_v)
            &&bits(a.mom_w,b.mom_w)&&bits(a.eng,b.eng);
    }
    void unchanged(const std::vector<FluidVector>& d,const std::vector<FluidVector>& s,
        const std::vector<double>& x) const {
        require(d.data()==du_data&&s.data()==scratch_data&&x.data()==fractions_data
            &&d.size()==du.size()&&s.size()==scratch.size()&&bits(x,fractions)
            &&std::equal(d.begin(),d.end(),du.begin(),same)
            &&std::equal(s.begin(),s.end(),scratch.begin(),same),
            "external application rejection changed marked dU/scratch/X operands");
    }
};

/** Each fault changes one helper operand or one actual borrowed numeric limit.
 * These are engineering identity counterexamples, not alternative physical
 * configurations or permission to change Runtime inputs during parallel work.
 */
enum class ApplicationFault {
    ForeignInput,ForeignGrid,ViewSpacing,ViewStride,ViewChart,ViewUpper,
    ViewRoot,ViewPeriodic,DtUlp,DtHalf,BoundsDensity,BoundsMinimum,BoundsMaximum,
    NumericsDensity,NumericsMinimum,NumericsMaximum
};

/** Claim a genuine prepared patch inside the real macro, then prove every
 * application mismatch fails before even scratch/EOS traversal. A caught
 * failure cannot revive/commit that claim. Deliberate one-leaf injections have
 * no concurrent configuration writer; actual accepted owners are restored by
 * the production Host macro transaction after the explicit rejecting probe.
 */
void application_guard_rejections() {
    constexpr std::array faults{
        ApplicationFault::ForeignInput,ApplicationFault::ForeignGrid,
        ApplicationFault::ViewSpacing,ApplicationFault::ViewStride,
        ApplicationFault::ViewChart,ApplicationFault::ViewUpper,
        ApplicationFault::ViewRoot,ApplicationFault::ViewPeriodic,
        ApplicationFault::DtUlp,ApplicationFault::DtHalf,
        ApplicationFault::BoundsDensity,ApplicationFault::BoundsMinimum,
        ApplicationFault::BoundsMaximum,ApplicationFault::NumericsDensity,
        ApplicationFault::NumericsMinimum,ApplicationFault::NumericsMaximum};
    int checked=0;
    for(const auto fault:faults) {
        Fixture f(0,false,.025,dispatch::TimeIntegratorId::Euler,true);
        f.bind_source();f.bind_frame();RollbackWitness accepted(f);
        bool probe_seen=false,helper_rejected=false,failed_claim=false,commit_rejected=false;
        f.observer->application_probe=[&](amr::AMRControl* control,int id,
            const FluidState& input,const Grid& grid,double interval,
            const Physical::Gravity::IGravityPolicy* policy,const NumericsConfig& numerics) {
            require(!probe_seen&&control==&f.control&&policy==f.force.get()
                &&f.control.tree->GetActiveBlocks().size()==1
                &&f.runtime->active_runtime_state_transaction()!=nullptr,
                "external application probe did not enter its real one-leaf macro owner");
            probe_seen=true;
            const auto* frame=policy->prepared_native_external();
            require(frame!=nullptr,"external application probe has no genuine prepared frame");
            auto receipt=frame->claim_patch(control,id,input,grid,interval,*policy);
            const state::Bounds original_bounds{
                numerics.sml_rho,numerics.min_eint,numerics.max_eint};
            const auto original_geometry=GridMetrics::make_geometry_view(grid,rz);
            receipt.require_application(input,grid,original_geometry,interval,original_bounds);

            const int extent=grid.GetTotalSize();
            std::vector<FluidVector> du(extent),scratch(extent);
            std::vector<double> fractions(input.GetNumSpecies());
            for(int cell=0;cell<extent;++cell) {
                du[cell]={17.+cell,-23.-cell,31.+cell,-41.-cell,53.+cell};
                scratch[cell]={-67.-cell,79.+cell,-83.-cell,97.+cell,-101.-cell};
            }
            for(std::size_t s=0;s<fractions.size();++s)fractions[s]=113.+s;
            const ApplicationBufferWitness before(du,scratch,fractions);
            const FluidState foreign_input=input;const Grid foreign_grid=grid;
            for(auto field:fields)require(bits(input.*field,foreign_input.*field),
                "external foreign-input fixture is not a value-equal distinct owner");
            require(&foreign_input!=&input&&&foreign_grid!=&grid,
                "external application owner-negative is accidentally its original object");
            const FluidState* selected_input=&input;const Grid* selected_grid=&grid;
            auto geometry=original_geometry;auto bounds=original_bounds;double applied_dt=interval;
            // Restore the injected actual numeric owner even if an unexpected
            // exception escapes; macro rollback never owns public configuration.
            struct RestoreNumerics {
                NumericsConfig& config;state::Bounds original;
                void restore() noexcept {
                    config.sml_rho=original.density;config.min_eint=original.internal_min;
                    config.max_eint=original.internal_max;
                }
                ~RestoreNumerics(){restore();}
            } restore{f.config.numerics,original_bounds};
            const auto ulp=[](double value){return std::nextafter(value,std::numeric_limits<double>::infinity());};
            switch(fault) {
            case ApplicationFault::ForeignInput:selected_input=&foreign_input;break;
            case ApplicationFault::ForeignGrid:selected_grid=&foreign_grid;break;
            case ApplicationFault::ViewSpacing:geometry.dx2=ulp(geometry.dx2);break;
            case ApplicationFault::ViewStride:++geometry.stride_y;break;
            case ApplicationFault::ViewChart:geometry.semantics=GridMetrics::GeometrySemantics::Existing;break;
            case ApplicationFault::ViewUpper:geometry.actual_block_upper[0]=ulp(geometry.actual_block_upper[0]);break;
            case ApplicationFault::ViewRoot:geometry.dyadic_identity.root_lower[0]=ulp(geometry.dyadic_identity.root_lower[0]);break;
            case ApplicationFault::ViewPeriodic:geometry.dyadic_identity.periodic_axial=!geometry.dyadic_identity.periodic_axial;break;
            case ApplicationFault::DtUlp:applied_dt=ulp(interval);break;
            case ApplicationFault::DtHalf:applied_dt=.5*interval;break;
            case ApplicationFault::BoundsDensity:bounds.density=ulp(bounds.density);break;
            case ApplicationFault::BoundsMinimum:bounds.internal_min=ulp(bounds.internal_min);break;
            case ApplicationFault::BoundsMaximum:bounds.internal_max=ulp(bounds.internal_max);break;
            case ApplicationFault::NumericsDensity:f.config.numerics.sml_rho=ulp(f.config.numerics.sml_rho);break;
            case ApplicationFault::NumericsMinimum:f.config.numerics.min_eint=ulp(f.config.numerics.min_eint);break;
            case ApplicationFault::NumericsMaximum:f.config.numerics.max_eint=ulp(f.config.numerics.max_eint);break;
            }
            const bool config_drift=fault==ApplicationFault::NumericsDensity
                ||fault==ApplicationFault::NumericsMinimum||fault==ApplicationFault::NumericsMaximum;
            const bool view_drift=fault==ApplicationFault::ViewSpacing||fault==ApplicationFault::ViewStride
                ||fault==ApplicationFault::ViewChart||fault==ApplicationFault::ViewUpper
                ||fault==ApplicationFault::ViewRoot||fault==ApplicationFault::ViewPeriodic;
            const char* expected=config_drift?"Native external Runtime configuration changed":
                view_drift?"Native external application changed its actual native geometry view":
                "Native external application changed input, interval or physical bounds";
            try {
                Physical::Gravity::add_native_external_sources(
                    std::span<FluidVector>(du),std::span<FluidVector>(scratch),std::span<double>(fractions),
                    *selected_input,*f.eos,*selected_grid,geometry,applied_dt,bounds,receipt);
            } catch(const std::logic_error& error) {
                helper_rejected=std::string(error.what())==expected;
                if(!helper_rejected)throw;
            }
            restore.restore();
            before.unchanged(du,scratch,fractions);
            require(helper_rejected,"external application identity mismatch reached source math/write");
            try {receipt.require_application(input,grid,original_geometry,interval,original_bounds);}
            catch(const std::logic_error& error) {
                failed_claim=std::string(error.what())=="Native external patch claim is not its original generation";
                if(!failed_claim)throw;
            }
            try {receipt.commit({});}
            catch(const std::logic_error& error) {
                commit_rejected=std::string(error.what())=="Native external patch claim is not its original generation";
                if(!commit_rejected)throw;
            }
            before.unchanged(du,scratch,fractions);
            require(failed_claim&&commit_rejected,
                "external failed application claim revived or committed after operands were restored");
            throw std::logic_error("NATIVE_EXTERNAL_APPLICATION_ENGINEERING_REJECTED");
        };
        bool macro_rejected=false;
        try {f.advance();}
        catch(const std::logic_error& error) {
            macro_rejected=std::string(error.what())=="NATIVE_EXTERNAL_APPLICATION_ENGINEERING_REJECTED";
            if(!macro_rejected)throw;
        }
        require(macro_rejected&&probe_seen&&helper_rejected&&failed_claim&&commit_rejected
            &&f.observer->calls==0&&!f.observer->source_reference_seen,
            "external guard witness bypassed genuine preparation or ran the normal producer after rejection");
        accepted.unchanged();
        require(bits(f.config.numerics.sml_rho,1.e-14)&&bits(f.config.numerics.min_eint,1.e-14)
            &&bits(f.config.numerics.max_eint,1.e10),"external guard probe leaked a numeric-owner injection");
        ++checked;
    }
    std::cout<<"RZ_EXTERNAL_APPLICATION_GUARD_ENGINEERING negatives="<<checked
        <<" actual_frame=1 before_source_write=1 failed_claim=1 whole_macro_rollback=1 physical_grant=0\n";
}

/** Original 24 off-axis cases and normalized 1e-12 budgets, now with actual
 * Runtime, real source journal and independent body + physical-surface audit.
 * Cumulative RK weights occur once; physical time advances after acceptance.
 */
void matrix_case(int direction,bool open,double phi,dispatch::TimeIntegratorId method) {
    Fixture f(direction,open,phi,method);f.bind_source();f.bind_frame();
    const auto before=f.totals();long double max_j=0.,max_m=0.,max_e=0.,max_x=0.;
    const auto stages=scheduler::supported_hydro_time_plan(f.hydro_method()).stages.size();
    double maximum_torque_register=0.;
    for(int n=0;n<10;++n) {
        f.bind_frame();f.advance();accepted_current(f);
        const auto after=f.totals();const auto body=f.source->external_source_budget();
        const auto& out=f.observer->outward;
        const long double js=before[3]+std::abs(out[2])+std::abs(body[2]);
        const long double ms=before[0]+std::abs(out[0]);
        const long double es=before[1]+std::abs(out[1])+std::abs(body[3]);
        close_budget(after[2]-before[2]+out[2],body[2],js,"external actual Runtime angular conservation failed");
        close_budget(after[0]-before[0]+out[0],0.,ms,"external actual Runtime mass conservation failed");
        close_budget(after[1]-before[1]+out[1],body[3],es,"external actual Runtime energy/source work balance failed");
        max_j=std::max(max_j,std::abs(after[2]-before[2]+out[2]-body[2])/js);
        max_m=std::max(max_m,std::abs(after[0]-before[0]+out[0])/ms);
        max_e=std::max(max_e,std::abs(after[1]-before[1]+out[1]-body[3])/es);
        for(int x=0;x<2;++x) {
            const long double scale=before[4+x]+std::abs(out[3+x]);
            close_budget(after[4+x]-before[4+x]+out[3+x],0.,scale,
                "external actual Runtime species conservation failed");
            max_x=std::max(max_x,std::abs(after[4+x]-before[4+x]+out[3+x])/scale);
        }
        // Original whole-field scales allow actual dU addition rounding; no
        // tiny-source tolerance replaces the original conservation budget.
        require(body[0]==0.&&body[1]==0.,"pure azimuthal external source added meridional momentum");
        close_budget(body[2],f.observer->body[2],js,"actual source J journal differs from independent V/W source");
        close_budget(body[3],f.observer->body[3],es,"actual source work differs from independent physical profile");
        const auto& observed=f.runtime->hydro_boundary_budget();const int map[]{0,4,3,5,6};
        require(observed.size()==8,"actual Native boundary observer has wrong species/field extent");
        const long double scales[]{ms,es,js,before[4],before[5]};
        for(int q=0;q<5;++q)close_budget(observed[map[q]],out[q],scales[q],
            "actual Native boundary budget differs from independent captured A/W plane integral");
        if(!open)for(int q=0;q<5;++q)close_budget(out[q],0.,scales[q],
            "reflecting actual Hydro leaked mass/energy/angular/species through a physical wall");
        near(f.controller->t_current,.375+(n+1)*dt,"external actual accepted physical time changed");
        require(f.controller->step_count==2+n,"external actual accepted step count changed");
        for(int id:f.control.tree->GetActiveBlocks())for(int face=0;face<4;++face)
            if(f.control.flux_register.HasData(id,face)) {
                const int count=face/2==0?amr::BLOCK_NY:amr::BLOCK_NX;
                for(int c=0;c<count;++c)maximum_torque_register=std::max(maximum_torque_register,
                    std::abs(f.control.flux_register.GetSummedFlux(id,face,c).mom_w));
            }
    }
    require(maximum_torque_register>0.&&f.observer->calls==static_cast<int>(10*stages*f.runtime->handles().size())
        &&f.observer->source_reference_seen,"external 24 case skipped actual CF/RK/source visits");
    const auto after=f.totals();const auto body=f.source->external_source_budget();
    const auto& out=f.observer->outward;
    const long double scale=before[3]+std::abs(out[2])+std::abs(body[2]);
    require(std::abs(after[2]-before[2]+out[2])/scale>budget,
        "missing-body negative did not distinguish external angular force");
    if(stages>1)require(std::abs(f.observer->unweighted_body[2]-body[2])/scale>budget,
        "wrong-RK body negative did not distinguish actual tableau weights");
    if(open) {
        require(std::abs(out[2])>budget*scale,"open external case has no genuine outward angular flux");
        if(stages>1)require(std::abs(f.observer->unweighted_outward[2]-out[2])/scale>budget,
            "wrong-RK boundary negative did not distinguish actual tableau weights");
    }
    std::cout<<std::setprecision(17)<<"RZ_EXTERNAL_ACTUAL_RUNTIME direction="<<direction<<" open="<<open
        <<" method="<<f.config.numerics.time_integrator<<" gphi="<<phi<<" steps=10 tn=.375 dt="<<dt
        <<" J="<<double(max_j)<<" M="<<double(max_m)<<" E="<<double(max_e)<<" X="<<double(max_x)
        <<" QJ="<<double(body[2])<<" QE="<<double(body[3])<<" Jout="<<double(out[2])<<'\n';
}

/** Nonuniform physical u_phi=r on the true [1,3] domain. Each cell has its own
 * native mean; the whole-domain m_phi,W=60/13 differs from m_phi,V=13/3.
 * Source work is compared to independent initial physical antiderivatives,
 * not an EOS/closure output, and the actual source consumes one Euler input.
 */
void physical_work() {
    Fixture f(0,false,.025,dispatch::TimeIntegratorId::Euler,true);f.bind_source();f.bind_frame();
    const auto initial=f.totals();
    const long double v=2.*pi*2.*power_integral(1.,3.,1);
    const long double w=2.*pi*2.*power_integral(1.,3.,2);
    close_budget(initial[2]/w,60.L/13.L,60.L/13.L,"true native whole-domain J/W reference failed");
    close_budget(2.*power_integral(1.,3.,2)/power_integral(1.,3.,1),13.L/3.L,13.L/3.L,
        "independent V physical work mean reference failed");
    long double wrong_raw_work=0.;
    for(int id:f.control.tree->GetActiveBlocks()) {
        const auto& b=f.control.pool->GetBlock(id);const auto& g=b.grid;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
            const long double cell_v=2.*pi*(g.GetAxialFacePosR(j)-g.GetAxialFacePosL(j))
                *power_integral(g.GetFacePosL(i),g.GetFacePosR(i),1);
            wrong_raw_work+=dt*f.force->g_z*b.fluid_state.mom_w[g.GetIndex(i,j,0)]*cell_v;
        }
    }
    const long double exact=dt*f.force->g_z*2.*w;
    f.advance();accepted_current(f);const auto actual=f.source->external_source_budget();
    require(actual[0]==0.&&actual[1]==0.,"pure physical work case gained meridional body momentum");
    close_budget(actual[2],exact,std::abs(exact),"true source J does not match exact physical antiderivative");
    close_budget(actual[3],exact,std::abs(exact),"true source E does not match exact physical work");
    require(std::abs(wrong_raw_work-exact)>budget*std::abs(exact),
        "raw J/W-as-point work negative became indistinguishable");
    std::cout<<"RZ_EXTERNAL_ACTUAL_WORK native_mean=60/13 physical_V_mean=13/3 actual_QE="
        <<double(actual[3])<<" wrong_raw_QE="<<double(wrong_raw_work)<<" full_domain_V="<<double(v)<<'\n';
}

/** Default/metadata alone cannot grant actual Runtime source authority. */
struct UnknownPolicy final:Physical::Gravity::IGravityPolicy {
    mutable int calls=0;
    void add_sources_on_patch(std::vector<FluidVector>&,const FluidState&,const Grid&,double,void*) const override {
        ++calls;throw std::logic_error("unknown source was evaluated");
    }
};
void rejected_source_inputs() {
    Fixture f(0,false,.025,dispatch::TimeIntegratorId::Euler);f.bind_frame();
    const RollbackWitness saved(f);
    const auto ctor_reject=[&](const Physical::Gravity::IGravityPolicy* policy,driver::GravityStage::Qualification q) {
        bool rejected=false;
        try{driver::GravityStage bad(*f.runtime,policy,q);}
        catch(const std::invalid_argument&){rejected=true;}
        require(rejected&&f.observer->calls==0,"invalid source metadata/profile reached a real flux write");
        saved.unchanged();
    };
    UnknownPolicy unknown;
    ctor_reject(&unknown,driver::GravityStage::Qualification::Production);
    require(unknown.calls==0,"unknown source was executed during metadata refusal");
    ctor_reject(f.force.get(),static_cast<driver::GravityStage::Qualification>(255));
    Physical::Gravity::ExternalGravity wrong(0.,0.,std::nextafter(.025,1.));
    ctor_reject(&wrong,driver::GravityStage::Qualification::Production);
    bool missing=false;
    try{f.advance();}
    catch(const std::invalid_argument& e){missing=std::string(e.what()).find("actual prepared source contract")!=std::string::npos;}
    require(missing&&f.observer->calls==0,"finite descriptor without actual source journal reached patch production");
    saved.unchanged();
    f.bind_source();f.bind_frame();const RollbackWitness owned(f);
    Physical::Gravity::ExternalGravity foreign(0.,0.,.025);bool refused=false;
    try{f.advance(&foreign);}
    catch(const std::logic_error& e){refused=std::string(e.what()).find("actual frame")!=std::string::npos;}
    require(refused&&f.observer->calls==0&&!foreign.prepared_native_external(),
        "foreign equal-valued source policy borrowed another owner's stage");
    owned.unchanged();
    for(int component=0;component<2;++component) {
        Fixture axis(0,false,component?.025:0.,dispatch::TimeIntegratorId::Euler,false,false,0.,component?0.:.025);
        axis.bind_frame();const RollbackWitness at_axis(axis);bool nonregular=false;
        try{axis.bind_source();}
        catch(const std::invalid_argument& e){nonregular=std::string(e.what()).find("nonregular at the axis")!=std::string::npos;}
        require(nonregular&&axis.observer->calls==0,"nonregular constant native axis vector reached production");
        at_axis.unchanged();
    }
    std::cout<<"RZ_EXTERNAL_ACTUAL_PREFLIGHT unknown=1 qualification=1 config_one_ulp=1 no_frame=1 foreign_policy=1 axis_gr_gphi=1\n";
}

/** Cold variable-density actual source/EOS consumer followed by deliberate
 * discard before update. This is NOT positive cold finite-step acceptance.
 * Real analytic radial callbacks keep the density support physically smooth;
 * no ghost fallback, heat, producer replacement or false source token is used.
 */
void cold_consumption_discard() {
    Fixture f(0,false,.025,dispatch::TimeIntegratorId::Euler,true,true);f.bind_source();f.bind_frame();
    const auto& b=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
    const int cell=b.grid.GetIndex(b.grid.Is(),b.grid.Js(),0);
    require(state::recover(b.fluid_state.get(cell)).status!=state::Status::valid,
        "cold source witness does not distinguish raw point from real native thermal closure");
    const RollbackWitness before(f);f.observer->abort_after_source=true;bool discarded=false;
    try{f.advance();}
    catch(const std::runtime_error& e){discarded=std::string(e.what())=="COLD_ACTUAL_SOURCE_OBSERVED";}
    require(discarded&&f.observer->calls==1&&f.observer->source_reference_seen,
        "cold source failed before genuine point EOS/source consumption or controlled discard");
    before.unchanged();
    const long double expected=dt*f.force->g_z*2.*pi*2.*
        (.875L*power_integral(1.,3.,2)+.25L*power_integral(1.,3.,4));
    close_budget(f.observer->body[2],expected,std::abs(expected),"cold independent exact torque reference failed");
    close_budget(f.observer->body[3],expected,std::abs(expected),"cold independent exact source work reference failed");
    require(f.source->external_source_budget()==std::array<long double,4>{},
        "discarded cold source published accepted body receipts");
    std::cout<<"RZ_EXTERNAL_ACTUAL_COLD_SOURCE real_point_eos=1 raw_point_invalid=1 controlled_discard=1 cold_step_acceptance=0\n";
}

/** Reject a genuine last selected RK EOS output after an accepted prefix.
 * Poisoning is an explicit engineering fault injection, not a physics oracle.
 * The actual macro owner must preserve the old accepted source budget exactly.
 */
void late_eos_rollback() {
    Fixture f(0,false,-.025,dispatch::TimeIntegratorId::Rk3);f.bind_source();f.bind_frame();
    f.advance();accepted_current(f);f.bind_frame();const RollbackWitness before(f);
    const int old_calls=f.observer->calls;const int expected=static_cast<int>(
        scheduler::supported_hydro_time_plan(f.hydro_method()).stages.size()*f.runtime->handles().size());
    f.poison=true;bool failed=false;
    double burn_advice=.75;const double old_advice=burn_advice;
    try {
        driver::NativeMacroStepAdvice advice(*f.runtime,*f.controller,burn_advice);
        (void)f.controller->calculate_next_dt(dt,burn_advice);burn_advice=1.e99;
        f.advance();advice.commit();
    } catch(const driver::NativeBoundaryAcceptanceError& e) {
        failed=e.diagnostic.phase==RzThermodynamics::AcceptancePhase::effective_thermal
            &&e.diagnostic.status==state::Status::unresolved_energy&&e.diagnostic.inertia_mapping_valid;
        if(!failed)throw;
        std::cout<<"RZ_EXTERNAL_ACTUAL_LATE_EOS phase=effective_thermal index="<<e.diagnostic.index
            <<" actual="<<e.what()<<'\n';
    }
    require(failed&&f.poisoned&&f.observer->calls==old_calls+expected,
        "external fault did not occur after real last RK source work and genuine boundary EOS");
    before.unchanged();require(bits(burn_advice,old_advice),"external late EOS changed accepted timestep advice");
    std::cout<<"RZ_EXTERNAL_ACTUAL_ROLLBACK accepted_prefix_retained=1 pending_prefix_discarded=1 all_slots_leases_ledger_bc_time=1\n";
}

/** Independent physical-surface observer for the ACTUAL RKL2 recurrence.
 * Spatial integration uses native antiderivatives of the real captured A/W
 * planes; no GridMetrics area, diffusion producer or repair output is the
 * reference. Time weights deliberately use the selected existing RKL2
 * coefficients: this witnesses accounting/consumption, not an independent
 * approximation of the diffusion equation or a new evolution scheme.
 * B_j=mu_j B_(j-1)+nu_j B_(j-2)+h*R_j, B_0=0, where the captured R_j
 * already contains tilde_mu_j F(Y_(j-1))+gamma_j F(Y_0).
 */
struct CoupledRklWitness {
    Fixture& f;
    std::array<long double,5> outward{},previous{},older{};
    int begins=0,accepts=0,halves=0,negative_gamma=0,expected_next=1;
    bool in_half=false,nonzero_angular_flux=false;
    explicit CoupledRklWitness(Fixture& actual):f(actual){}
    /** Read all real physical surfaces in fixed leaf/axis/side/cell order. */
    std::array<long double,5> captured_rate() {
        std::array<long double,5> rates{};
        const int map[5]{0,4,3,5,6};
        for(int id:f.control.tree->GetActiveBlocks()) {
            const auto& b=f.control.pool->GetBlock(id);const auto& g=b.grid;
            const auto capture=b.fluid_state.boundary_flux_capture;
            require(bool(capture),"coupled RKL lost genuine shared capture storage");
            require(b.state_next.boundary_flux_capture==capture
                &&b.state_scratch.boundary_flux_capture==capture,
                "coupled RKL slots do not share their real observer owner");
            const int count_fields=6+b.fluid_state.GetNumSpecies();
            for(int axis=0;axis<2;++axis)for(int side=0;side<2;++side) {
                const auto& plane=capture->stage[2*axis+side];if(plane.empty())continue;
                require(b.face_neighbors[2*axis+side].count==0,
                    "coupled RKL observer invented an internal AMR surface");
                const int count=axis==0?g.Je()-g.Js():g.Ie()-g.Is();
                require(plane.size()==static_cast<std::size_t>(count*count_fields),
                    "coupled RKL real capture plane extent changed");
                for(int n=0;n<count;++n) {
                    const int i=axis==0?(side?g.Ie()-1:g.Is()):g.Is()+n;
                    const int j=axis==0?g.Js()+n:(side?g.Je()-1:g.Js());
                    const long double lo=g.GetFacePosL(i),hi=g.GetFacePosR(i);
                    const long double dz=g.GetAxialFacePosR(j)-g.GetAxialFacePosL(j);
                    const long double r=side?hi:lo;
                    const long double area=axis==0?2.*pi*r*dz:2.*pi*power_integral(lo,hi,1);
                    const long double torque=axis==0?2.*pi*r*r*dz:2.*pi*power_integral(lo,hi,2);
                    require(area>0.&&torque>0.,"coupled annular surface has no true measure");
                    for(int q=0;q<5;++q) {
                        const double value=plane[n*count_fields+map[q]];
                        require(std::isfinite(value),"coupled RKL published a nonfinite physical flux");
                        rates[q]+=(side?1.L:-1.L)*(q==2?torque:area)*value;
                        if(q==2&&value!=0.)nonzero_angular_flux=true;
                    }
                }
            }
        }
        return rates;
    }
    /** Wrap actual accounting, retaining its real ownership and failure path.
     * No callback substitutes EOS/preparation/exchange/producer acceptance.
     */
    void bind() {
        require(f.context->rkl_flux_capture_begin&&f.context->rkl_flux_capture_accept,
            "coupled Native Runtime has no real RKL accounting owner");
        const auto real_begin=f.context->rkl_flux_capture_begin;
        const auto real_accept=f.context->rkl_flux_capture_accept;
        f.context->rkl_flux_capture_begin=[this,real_begin](const scheduler::RklStageDescriptor& stage,
            const scheduler::RklPlan& plan) {
            require(plan.method==scheduler::RklMethod::RKL2&&plan.second_order
                &&plan.stages.size()==static_cast<std::size_t>(f.actual_diffusion_stages)
                &&f.actual_diffusion_stages>=2&&bits(f.context->boundary_step_dt,.5*dt)
                &&f.runtime->active_runtime_state_transaction()!=nullptr,
                "coupled diffusion bypassed the real RKL2/half/macro owner");
            if(stage.stage==1) {
                require(!in_half,"coupled RKL began a half before its previous endpoint");
                in_half=true;expected_next=1;previous={};older={};
            }
            require(in_half&&stage.stage==expected_next,
                "coupled RKL skipped or duplicated a selected stage");
            const double expected_time=f.context->boundary_start_time
                +scheduler::rkl_stage_time_fraction(plan.method,stage.stage-1,
                    static_cast<int>(plan.stages.size()))*f.context->boundary_step_dt;
            require(bits(f.configured_time,expected_time)
                &&f.configured_purpose==boundary::BoundaryPurpose::Diffusion,
                "coupled RKL did not consume actual timed Diffusion boundaries");
            real_begin(stage,plan);
            const auto coefficients=DiffFunction::get_rkl_coeffs(DiffFunction::RKLOrder::Second,
                stage.stage,static_cast<int>(plan.stages.size()));
            for(int id:f.control.tree->GetActiveBlocks()) {
                const auto capture=f.control.pool->GetBlock(id).fluid_state.boundary_flux_capture;
                require(capture&&bits(capture->weight,coefficients.tilde_mu)
                    &&bits(capture->initial_weight,stage.stage>1?coefficients.gamma:0.)
                    &&capture->save_initial==(stage.stage==1),
                    "coupled RKL actual producer capture weights changed");
            }
            if(coefficients.gamma<0.)++negative_gamma;
            ++begins;
        };
        f.context->rkl_flux_capture_accept=[this,real_accept](const scheduler::RklStageDescriptor& stage,
            const scheduler::RklPlan& plan) {
            require(in_half&&stage.stage==expected_next,
                "coupled RKL accepted an unconsumed stage");
            const double expected_time=f.context->boundary_start_time
                +scheduler::rkl_stage_time_fraction(plan.method,stage.stage,
                    static_cast<int>(plan.stages.size()))*f.context->boundary_step_dt;
            require(bits(f.configured_time,expected_time)
                &&f.configured_purpose==boundary::BoundaryPurpose::Diffusion,
                "coupled RKL output did not complete its actual timed BC/EOS");
            for(const auto handle:f.runtime->handles()) {
                const state::StateKey key{handle,stage.output_slot};
                const auto coherence=f.context->ledger.inspect(key);
                scheduler::detail::require_settled_destination(coherence);
                f.context->ledger.require_readable(key,{state::ExecutionSide::Host,
                    coherence.interior.version,true,true});
            }
            const auto rates=captured_rate();
            const auto coefficients=DiffFunction::get_rkl_coeffs(DiffFunction::RKLOrder::Second,
                stage.stage,static_cast<int>(plan.stages.size()));
            std::array<long double,5> amount{};
            for(int q=0;q<5;++q)amount[q]=f.context->boundary_step_dt*rates[q]
                +(stage.stage>1?coefficients.mu*previous[q]+coefficients.nu*older[q]:0.);
            real_accept(stage,plan);
            older=previous;previous=amount;++accepts;++expected_next;
            if(stage.stage==static_cast<int>(plan.stages.size())) {
                for(int q=0;q<5;++q)outward[q]+=amount[q];
                in_half=false;++halves;
            }
        };
    }
};

/** Warm three-module finite-step witness through the ONE actual full split.
 * B is explicitly disabled; two genuine positive-viscosity RKL2 halves surround
 * selected Hydro with its real external source journal. Independent native
 * integral balances are Delta U+B_H+B_D=Q_ext (M/species Q=0), with the original
 * normalized 1e-12 budget. Reflecting Hydro has no advective surface flux;
 * reflecting diffusion may still exchange torque/work, which is measured.
 * This is not cold/long-time/full-tensor/four-module/public RZ qualification.
 */
void warm_rkl2_external_case(bool mixed,dispatch::TimeIntegratorId method) {
    constexpr int steps=3;
    Fixture f(0,false,.025,method,!mixed,false,1.,0.,1.);
    require(!f.config.physics.burn.use_burn&&f.config.physics.diffusion.use_diffusion
        &&f.config.physics.diffusion.use_viscous_diffusion
        &&f.config.physics.diffusion.nu_visc==1.
        &&!f.config.physics.diffusion.use_thermal_diffusion
        &&!f.config.physics.diffusion.use_species_diffusion,
        "coupled physical controls were not frozen before Runtime construction");
    f.bind_source();f.bind_frame();CoupledRklWitness rkl(f);
    const auto initial=f.totals();
    const auto hydro_stages=scheduler::supported_hydro_time_plan(f.hydro_method()).stages.size();
    std::array<long double,4> maximum{};int expected_rkl_stages=0;
    for(int n=0;n<steps;++n) {
        f.bind_frame();rkl.bind();f.advance();accepted_current(f);
        expected_rkl_stages+=2*f.actual_diffusion_stages;
        require(rkl.begins==expected_rkl_stages&&rkl.accepts==expected_rkl_stages
            &&rkl.halves==2*(n+1)&&f.actual_diffusion_half_calls==2*(n+1)&&!rkl.in_half,
            "coupled macro skipped/duplicated an actual RKL2 half or stage");
        const auto after=f.totals();const auto body=f.source->external_source_budget();
        const auto& hydro=f.observer->outward;
        std::array<long double,5> surface{};
        for(int q=0;q<5;++q)surface[q]=hydro[q]+rkl.outward[q];
        const long double scales[]{initial[0]+std::abs(hydro[0])+std::abs(rkl.outward[0]),
            initial[1]+std::abs(hydro[1])+std::abs(rkl.outward[1])+std::abs(body[3]),
            initial[3]+std::abs(hydro[2])+std::abs(rkl.outward[2])+std::abs(body[2]),
            initial[4]+std::abs(hydro[3])+std::abs(rkl.outward[3]),
            initial[5]+std::abs(hydro[4])+std::abs(rkl.outward[4])};
        const int component[5]{0,1,2,4,5},field[5]{0,4,3,5,6};
        const long double sources[5]{0.,body[3],body[2],0.,0.};
        for(int q=0;q<5;++q) {
            const long double residual=after[component[q]]-initial[component[q]]+surface[q]-sources[q];
            if(!std::isfinite(residual)||std::abs(residual)>budget*scales[q])
                std::cerr<<std::setprecision(17)<<"RZ_WARM_COUPLING_FAILED mixed="<<mixed
                    <<" method="<<f.config.numerics.time_integrator<<" step="<<n+1
                    <<" component="<<q<<" residual="<<double(residual)
                    <<" scale="<<double(scales[q])<<" Hout="<<double(hydro[q])
                    <<" Dout="<<double(rkl.outward[q])<<" Q="<<double(sources[q])<<'\n';
            close_budget(residual,0.,scales[q],
                "warm actual Runtime RKL2/external/Hydro native integral balance failed");
            maximum[q<3?q:3]=std::max(maximum[q<3?q:3],std::abs(residual)/scales[q]);
            const auto& actual_hydro=f.runtime->hydro_boundary_budget();
            const auto& actual_diffusion=f.runtime->diffusion_boundary_budget();
            require(actual_hydro.size()==8&&actual_diffusion.size()==8,
                "coupled accepted boundary receipts lost actual species/field identity");
            close_budget(actual_hydro[field[q]],hydro[q],scales[q],
                "coupled accepted Hydro receipt differs from independent native A/W capture");
            close_budget(actual_diffusion[field[q]],rkl.outward[q],scales[q],
                "coupled accepted diffusion receipt differs from independent native A/W recurrence");
            close_budget(hydro[q],0.,scales[q],
                "coupled reflecting actual Hydro leaked an advective physical-wall flux");
        }
        close_budget(body[2],f.observer->body[2],scales[2],
            "coupled actual external torque differs from independent physical source");
        close_budget(body[3],f.observer->body[3],scales[1],
            "coupled actual external work differs from independent physical source");
        require(body[0]==0.&&body[1]==0.,"coupled pure azimuthal source added meridional momentum");
        near(f.controller->t_current,.375+(n+1)*dt,"coupled accepted endpoint physical time changed");
        require(f.controller->step_count==n+2&&bits(f.configured_time,f.controller->t_current)
            &&f.configured_purpose==boundary::BoundaryPurpose::Hydro,
            "coupled final actual endpoint BC/clock was not accepted");
    }
    require(rkl.nonzero_angular_flux&&rkl.negative_gamma>=2*steps
        &&f.observer->calls==static_cast<int>(steps*hydro_stages*f.runtime->handles().size())
        &&f.observer->source_reference_seen,
        "coupled witness skipped nonzero viscosity/negative-gamma/RK/source consumption");
    const auto body=f.source->external_source_budget();
    require(body[2]!=0.&&body[3]!=0.,"coupled actual journal has no azimuthal source torque/work");
    const auto after=f.totals();
    const long double jscale=initial[3]+std::abs(rkl.outward[2])+std::abs(body[2]);
    require(std::abs(after[2]-initial[2]+f.observer->outward[2]+rkl.outward[2])/jscale>budget,
        "coupled missing-source negative does not distinguish actual external torque");
    std::cout<<std::setprecision(17)<<"RZ_WARM_ACTUAL_RKL2_EXTERNAL_HYDRO mixed="<<mixed
        <<" method="<<f.config.numerics.time_integrator<<" steps="<<steps<<" tn=.375 dt="<<dt
        <<" nu="<<f.config.physics.diffusion.nu_visc<<" gphi=.025 fe="<<f.actual_diffusion_fe
        <<" RKL_begin="<<rkl.begins<<" RKL_accept="<<rkl.accepts<<" Dhalf="<<rkl.halves
        <<" gamma_negative="<<rkl.negative_gamma<<" Hydro_patch_calls="<<f.observer->calls
        <<" M="<<double(maximum[0])<<" E="<<double(maximum[1])
        <<" J="<<double(maximum[2])<<" X="<<double(maximum[3])
        <<" D_Jout="<<double(rkl.outward[2])<<" D_Eout="<<double(rkl.outward[1])
        <<" body_J="<<double(body[2])<<" body_E="<<double(body[3])
        <<" burn_enabled=0 four_module=0 public_grant=0\n";
}
/** Six bounded genuine warm cases; existing quick-lane/CLI owners are reused. */
void warm_rkl2_external_coupling() {
    for(bool mixed:{false,true})for(auto method:{dispatch::TimeIntegratorId::Euler,
        dispatch::TimeIntegratorId::Rk2,dispatch::TimeIntegratorId::Rk3})
        warm_rkl2_external_case(mixed,method);
}


/** Actual callback-free partial-cold macro, not an isolated D/2 surrogate.
 * The first annulus retains e0=2^-25 and the independent RKL1 negative witness;
 * other rings have physical e=3/2 so their true centrifugal Hydro is resolved.
 * Builtin axis/reflection/paired-periodic BC supplies real ghosts. Production
 * aprox13/BE_NR Burn preparation is called twice on the accepted attempt, but
 * all actual T remain below the original 1e9 activation threshold: no injected
 * heat/rate is allowed to make the cold thermal failure disappear.
 */
namespace thermal_retry_checks {
constexpr double cold_internal=0x1p-25;
using ActualBurn=Solver_BE_NR<NetAprox13,DenseMatrixData<NetAprox13::ODE_NEQ>,DenseLUSolver>;
struct AttemptRecord {double dt=0.,seconds=0.;int burn_first=0,burn_second=0,diffusion=0,hydro=0;bool rejected=false;};
struct Fixture {
    SimConfig config;SpeciesManager species;amr::AMRControl control{8,2};RunState start{};
    std::unique_ptr<IdealGas> eos;std::unique_ptr<SimulationController> counters;
    std::unique_ptr<BCHandler> bc;std::unique_ptr<driver::DriverRuntime> runtime;
    std::unique_ptr<Hydro> hydro;std::optional<scheduler::StageExecutionContext> context;
    driver::DriverStageWorkspace workspace;dispatch::ResolvedExecutionPlan plan{};
    double fe=0.,hydro_dt=0.,burn_advice=.75;std::vector<AttemptRecord> attempts;
    int active_attempt=-1;bool observed=false;FluidVector failed_value{};
    // Empty for original cases. Fatal negatives synchronously inspect the real
    // noncopyable pre-macro witnesses after rollback, while their owners live.
    std::function<void(const rz_runtime_witness::FieldsWitness&,
        const driver::RuntimeStateTransaction::OwnerWitness&)> attempt_checkpoint;
    Fixture(double minimum_step=1.e-20,bool short_final=false) {
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=config.grid.x2_min=0.;config.grid.x1_max=config.grid.x2_max=16.;
        config.grid.x1l_boundary_type="outflow";config.grid.x1r_boundary_type="reflecting";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="periodic";
        config.grid.amr_max_blocks=8;config.amr.lrefinemin=config.amr.lrefinemax=0;
        config.numerics.time_integrator="euler";config.numerics.solver_name="HLLC";
        config.numerics.reconstruction="pcm";config.numerics.dt_min=minimum_step;
        config.physics.gravity.type="none";config.physics.diffusion.use_diffusion=true;
        // A genuinely stiff viscosity keeps the cold three-stage witness
        // inside the unchanged Hydro CFL. Scaling nu and the half interval
        // inversely preserves the exact frozen angular reference recurrence.
        config.physics.diffusion.use_viscous_diffusion=true;config.physics.diffusion.nu_visc=2.;
        config.physics.diffusion.use_thermal_diffusion=config.physics.diffusion.use_species_diffusion=false;
        config.physics.diffusion.integrator="RKL1";config.physics.burn.use_burn=true;
        config.physics.burn.network_name="aprox13";config.physics.burn.odeconfig.ode_solver="BE_NR";
        config.physics.burn.odeconfig.linear_solver="DenseLU";
        config.io.tmax=short_final?.385:8.;config.io.plt_dt=config.io.chk_dt=0.;
        require(config.physics.diffusion.diff_cfl==.8&&cold_internal>config.numerics.min_eint,
            "native retry witness changed original CFL or cold thermal floor");
        for(int n=0;n<NetAprox13::NUM_SPECIES;++n)
            species.add_species(NetAprox13::SPECIES_NAMES[n],NetAprox13::AION[n],NetAprox13::ZION[n],1.4,2.);
        eos=std::make_unique<IdealGas>(1.4,species);control.tree->InitRootGrid(config,13,rz);
        control.flux_register.EnsureSpecies(13);auto& b=block();const auto& g=b.grid;
        require(control.tree->GetActiveBlocks().size()==1&&g.dx1==1.&&g.dx2==1.
            &&g.dyadic_identity.bound&&g.dyadic_identity.periodic_axial,"native retry lost actual unit root");
        for(auto* fluid:rz_runtime_witness::slots(b)) {
            fluid->stage_repairs.reset(13,state::RepairSemantics::RzVolumeAngular);
            for(int n=0;n<g.GetTotalSize();++n) {
                fluid->set(n,{1.,0.,0.,0.,1.5});fluid->enuc_rate[n]=.25+n/8.;
                for(int x=0;x<13;++x)fluid->X(x,n)=x==1?1.:0.;
            }
            for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i) {
                const long double l=g.GetFacePosL(i),h=g.GetFacePosR(i);
                const long double v=(h*h-l*l)/2.,w=(h*h*h-l*l*l)/3.,inertia=(h*h*h*h-l*l*l*l)/4.;
                const bool first=std::max(std::abs(l),std::abs(h))<=1.L;
                const long double omega=first?0.L:1.L,e=first?cold_internal:1.5L;
                fluid->set(g.GetIndex(i,j,0),{1.,0.,0.,double(omega*inertia/w),double(e+omega*omega*inertia/(2.*v))});
            }
        }
        start.time=.375;start.step=1;start.has_timestep_state=true;start.dt_old=.375;
        start.repairs.reset(13,state::RepairSemantics::RzVolumeAngular);
        counters=std::make_unique<SimulationController>(config,start);
        bc=std::make_unique<BCHandler>(config,rz);bc->bind(*eos,species);
        require(!bc->has_user(),"native retry introduced a user BC");
        bc->configure_stage(start.time,boundary::BoundaryPurpose::Hydro);
        runtime=std::make_unique<driver::DriverRuntime>(control,*bc,config,species,*counters);
        runtime->bind_native_rz_eos(*eos);runtime->initialize_topology();
        hydro=std::make_unique<Hydro>(*eos,rz);plan.time_integrator=dispatch::TimeIntegratorId::Euler;
        plan.diffusion_integrator=dispatch::DiffusionIntegratorId::Rkl1;
        context.emplace(runtime->stage_context());
        const auto actual=driver::calculate_timestep_candidates(*runtime,workspace,*eos,&plan);
        fe=actual.diffusion_forward_euler;hydro_dt=actual.hydro;
        // Full-Stokes unit-nu row: radial 8 + axial cross-row 6 + geometry 8.
        // The stiffness input nu=2 makes dt_FE=1/44; the unchanged macro .2
        // selects three stages at its first .1 half and stays inside Hydro CFL.
        near(fe,1./44.,"native retry full-Stokes FE row changed its independent reference");
        require(hydro_dt>=.2,"native retry .2 is outside true Hydro CFL; do not bypass that restriction");
    }
    amr::Block& block(){return control.pool->GetBlock(control.tree->GetActiveBlocks().front());}
    void bind_frame(double interval) {
        context->step_start_time=counters->t_current;context->step_dt=interval;
        context->boundary_start_time=counters->t_current;context->boundary_step_dt=.5*interval;
        context->configure_boundary_context=[this](double time,boundary::BoundaryPurpose purpose) {
            bc->configure_stage(time,purpose);runtime->bind_native_boundary_acceptance(*context,runtime->handles());
            const auto real=context->post_boundary_acceptance;
            context->post_boundary_acceptance=[this,real](const scheduler::StageExecutionContext& actual,
                StateSlot slot,state::StateVersion version) {
                const auto* frame=scheduler::current_rkl_completed_boundary();
                if(active_attempt==0&&frame&&frame->completed&&frame->context==&actual) {
                    const auto member=TimeIntegration::hydro_boundary_state_member(slot);
                    const auto& g=block().grid;failed_value=(block().*member).get(g.GetIndex(g.Is(),g.Js(),0));observed=true;
                }
                real(actual,slot,version);
            };
        };
        context->physical_boundary_preparation=[this](StateSlot slot,double time,boundary::BoundaryPurpose purpose) {
            context->configure_boundary_context(time,purpose);runtime->ensure_fluid_ghosts(slot);
        };
        context->configure_boundary_context(counters->t_current,boundary::BoundaryPurpose::Hydro);
        runtime->bind_boundary_accounting(*context);
        const auto one=scheduler::make_rkl_plan(scheduler::RklMethod::RKL1,1);
        context->rkl_flux_capture_begin(one.stages.front(),one);runtime->ensure_fluid_ghosts();
    }
    /** Fresh real binding/qualification, then true B-D-H-D-B. No mock operator. */
    void actual_attempt(double interval,std::uint64_t index) {
        active_attempt=static_cast<int>(attempts.size());attempts.push_back({interval});bind_frame(interval);
        const rz_runtime_witness::FieldsWitness before(block());
        const auto owner=driver::RuntimeStateTransaction::snapshot_owner(*runtime,*context);
        const auto identity=block().grid.dyadic_identity;
        const auto started=std::chrono::steady_clock::now();
        try {
            scheduler::ScopedStageBinding binding(*context,runtime->handles());
            driver::NativeMacroRetryAttempt retry(*runtime,*context,index,scheduler::RklMethod::RKL1,fe);
            const auto burn=BurnerHandle<IdealGas>::bind<ActualBurn>();
            driver::execute_driver_macro_step(*runtime,*context,hydro.get(),true,
                [&](driver::BurnHalf half,double step,state::CompletionToken token) {
                    auto& record=attempts[active_attempt];
                    if(half==driver::BurnHalf::First)++record.burn_first;else ++record.burn_second;
                    return driver::execute_burn_half(*runtime,workspace,*eos,burn,half,step,burn_advice,token);
                },
                [&](double half) {
                    ++attempts[active_attempt].diffusion;
                    driver::advance_diffusion(*runtime,workspace,*context,*eos,&plan,counters->step_count,half,fe);
                },
                [&](double full) {
                    ++attempts[active_attempt].hydro;
                    driver::advance_hydro(*runtime,workspace,*context,&plan,full,&SolverEuler::solve<BCHandler>,nullptr,hydro.get());
                },[](driver::CpuStage,auto&& call){call();});
        } catch(const driver::NativeThermalStepRejection& error) {
            attempts[active_attempt].rejected=true;
            attempts[active_attempt].seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            const auto& evidence=error.evidence();const auto& g=block().grid;
            require(evidence.pool_index==block().id&&evidence.handle==runtime->handles().front()
                &&evidence.diagnostic.i>=g.Is()&&evidence.diagnostic.i<g.Ie()
                &&evidence.diagnostic.j>=g.Js()&&evidence.diagnostic.j<g.Je()
                &&evidence.diagnostic.phase==RzThermodynamics::AcceptancePhase::effective_thermal
                &&evidence.diagnostic.status==state::Status::unresolved_energy&&evidence.diagnostic.inertia_mapping_valid,
                "native retry qualified a foreign/nonthermal/ghost refusal");
            before.matches(block());require(driver::RuntimeStateTransaction::owner_matches(*runtime,*context,owner)
                &&GridMetrics::equal_identity(identity,g.dyadic_identity)
                &&!runtime->active_runtime_state_transaction()&&!runtime->native_macro_retry_attempt(),
                "native rejected macro did not restore complete real owners");
            throw;
        } catch(...) {
            if(attempt_checkpoint)attempt_checkpoint(before,owner);
            throw;
        }
        attempts[active_attempt].seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    }
    double execute(){return driver::execute_driver_macro_attempts(*runtime,*counters,burn_advice,.2,
        [this](double interval,std::uint64_t index){actual_attempt(interval,index);});}
};
void complete_retry() {
    Fixture f;f.bind_frame(.2);const rz_runtime_witness::FieldsWitness allocations(f.block());
    const double before_time=f.counters->t_current;const int before_step=f.counters->step_count;
    const double accepted=f.execute();
    require(f.attempts.size()==2&&bits(f.attempts[0].dt,.2)&&bits(f.attempts[1].dt,.1)
        &&f.attempts[0].rejected&&!f.attempts[1].rejected&&bits(accepted,.1)&&f.observed,
        "native actual fullmacro did not reject .2 then accept the exact smaller .1");
    near(f.failed_value.mom_w,2967./6272.,"native retry independent first angular mean changed");
    near(f.failed_value.eng,cold_internal+14596739./177020928.,"native retry independent first energy mean changed");
    near(f.failed_value.eng-4.*std::pow(f.failed_value.mom_w/3.,2),cold_internal-3009439./177020928.,
        "native retry lost independent genuinely negative thermal state");
    require(f.attempts[0].burn_first==1&&f.attempts[0].burn_second==0&&f.attempts[0].diffusion==1
        &&f.attempts[0].hydro==0&&f.attempts[1].burn_first==1&&f.attempts[1].burn_second==1
        &&f.attempts[1].diffusion==2&&f.attempts[1].hydro==1,
        "native macro skipped/mocked a split owner or continued after real rejection");
    require(bits(f.counters->t_current,before_time)&&f.counters->step_count==before_step,
        "native retry accepted physical time before Driver advance");
    RzThermodynamics::validate_completed_patch_eos(f.block().fluid_state,f.block().grid,13,
        {f.config.numerics.sml_rho,f.config.numerics.min_eint,f.config.numerics.max_eint},*f.eos);
    for(const auto handle:f.runtime->handles()) {
        const auto ready=f.context->ledger.inspect({handle,StateSlot::Current});
        f.context->ledger.require_readable({handle,StateSlot::Current},
            {state::ExecutionSide::Host,ready.interior.version,true,true});
    }
    // Acceptance can legitimately rotate slots, but must retain each entire
    // original seven-array allocation group exactly once, including padding.
    std::array<bool,3> used{};
    for(const auto* fluid:rz_runtime_witness::slots(f.block())) {
        int group=-1;
        for(int old=0;old<3;++old) {
            bool exact=true;
            for(int field=0;field<7;++field)
                exact=exact&&(fluid->*fields[field]).data()==allocations.leases[old][field];
            if(exact)group=old;
        }
        require(group>=0&&!used[group],"accepted native macro changed original seven-array allocation groups");
        used[group]=true;
        require(fluid->stage_repairs.semantics==state::RepairSemantics::RzVolumeAngular,
            "accepted native macro changed receipt measure");
        for(double value:fluid->stage_repairs.values)require(value==0.,"native accepted slot concealed repair/heating");
    }
    for(double x:f.runtime->repair_budget().values)require(x==0.,"native macro retry concealed a floor/heating repair");
    for(int n=0;n<f.block().grid.GetTotalSize();++n)require(f.block().fluid_state.enuc_rate[n]==0.,
        "native inactive-temperature Burn injected energy");
    f.counters->advance(accepted);near(f.counters->t_current,before_time+accepted,"accepted native retry advanced wrong time");
    require(f.counters->step_count==before_step+1,"native retry advanced accepted step more than once");
    for(std::size_t n=0;n<f.attempts.size();++n)std::cout<<std::setprecision(17)
        <<"RZ_NATIVE_THERMAL_FULL_MACRO attempt="<<n+1<<" dt="<<f.attempts[n].dt
        <<" seconds="<<f.attempts[n].seconds<<" rejected="<<f.attempts[n].rejected
        <<" real_ode_activation_skipped=1 cold_first_annulus=1 physical_grant=0\n";
}
/** A true thermal refusal followed by dt_min termination restores Advice too. */
void minimum_and_advice() {
    Fixture f(.15);f.bind_frame(.2);const rz_runtime_witness::FieldsWitness before(f.block());
    const auto old_time=f.counters->t_current,old_dt=f.counters->dt_old,old_burn=f.burn_advice;
    const int old_step=f.counters->step_count;bool stopped=false;
    try{(void)f.execute();}catch(const std::runtime_error& error) {
        stopped=std::string(error.what()).find("Native macro thermal retry exhausted at attempt 1")!=std::string::npos;
        if(!stopped)throw;
    }
    before.matches(f.block());require(stopped&&f.attempts.size()==1&&f.attempts[0].rejected
        &&bits(old_time,f.counters->t_current)&&old_step==f.counters->step_count
        &&bits(old_dt,f.counters->dt_old)&&bits(old_burn,f.burn_advice),
        "native dt_min refusal did not retain exact accepted scalar Advice/fields");
}
/** Genuine legal first alignment shorter than dt_min; fullmacro still mandatory. */
void first_aligned_short() {
    Fixture f(.02,true);const double aligned=f.config.io.tmax-f.counters->t_current;
    require(aligned>0.&&aligned<f.config.numerics.dt_min,"native short-final fixture is not actually below dt_min");
    const double accepted=f.execute();require(bits(accepted,aligned)&&f.attempts.size()==1&&!f.attempts[0].rejected
        &&f.attempts[0].burn_first==1&&f.attempts[0].burn_second==1&&f.attempts[0].diffusion==2
        &&f.attempts[0].hydro==1,"native first aligned-short changed original sync_dt or skipped fullmacro");
}
/** Config drift after the genuine EOS binding is fatal before macro mutation. */
void eos_binding_drift() {
    Fixture f;f.bind_frame(.2);const rz_runtime_witness::FieldsWitness before(f.block());
    const auto owner=driver::RuntimeStateTransaction::snapshot_owner(*f.runtime,*f.context);
    const double original=f.config.numerics.min_eint;
    f.config.numerics.min_eint=std::nextafter(original,std::numeric_limits<double>::infinity());
    bool rejected=false;
    {scheduler::ScopedStageBinding binding(*f.context,f.runtime->handles());
        try{driver::NativeMacroRetryAttempt attempt(*f.runtime,*f.context,1,scheduler::RklMethod::RKL1,f.fe);}
        catch(const std::logic_error& error) {
            rejected=std::string(error.what())=="Native retry requires its exact quiescent macro entry";
            if(!rejected)throw;
        }}
    f.config.numerics.min_eint=original;before.matches(f.block());
    require(rejected&&driver::RuntimeStateTransaction::owner_matches(*f.runtime,*f.context,owner)
        &&!f.runtime->native_macro_retry_attempt(),"native EOS drift acquired retry permission or changed actual owners");
}
/** Forward every selected thermodynamic result to the fixture's actual IdealGas.
 * The one-shot hook is an explicitly impure engineering diagnostic. It fires
 * only after genuine RKL completion and an independently checked first active
 * unresolved-energy closure, which cannot call EOS in the normal whole gate.
 * Therefore a subsequent EOS call belongs to the real ACTIVE classifier.
 */
struct ClassificationEosProbe {
    const IdealGas& actual;
    mutable std::function<void()> on_pressure;
    double get_temperature(double rho,double energy,const double* fractions) const {
        return actual.get_temperature(rho,energy,fractions);
    }
    double get_pressure(const FluidVector& value,const double* fractions) const {
        if(on_pressure)on_pressure();
        return actual.get_pressure(value,fractions);
    }
    double get_sound_speed(const FluidVector& value,double pressure,const double* fractions) const {
        return actual.get_sound_speed(value,pressure,fractions);
    }
};
enum class ClassificationFault { DiffusionCoefficient,ClockToken,LaterActiveDensity };

/** Real selected-EOS classification must not mint retry permission after an
 * owner drift, and its later nonthermal error must retain exact patch identity.
 * Workflow: unchanged cold fixture -> actual B/D/RKL output -> one synchronous
 * diagnostic -> fatal exit -> real macro/Advice rollback of all existing owners.
 * The density fault is a later cell in the SAME actual patch, not a fabricated
 * second patch or an independent scientific negative-density reference.
 */
void classification_post_fence_rejections() {
    for(const auto fault:{ClassificationFault::DiffusionCoefficient,
        ClassificationFault::ClockToken,ClassificationFault::LaterActiveDensity}) {
        // The actual borrowed probe must outlive Runtime, including exceptional exits.
        std::optional<ClassificationEosProbe> probe;
        Fixture f;probe.emplace(ClassificationEosProbe{*f.eos,{}});
        f.runtime->bind_native_rz_eos(*probe);
        bool owners_restored=false;int checkpoints=0;
        f.attempt_checkpoint=[&](const auto& fields,const auto& owner) {
            ++checkpoints;fields.matches(f.block());
            owners_restored=driver::RuntimeStateTransaction::owner_matches(*f.runtime,*f.context,owner)
                &&!f.runtime->active_runtime_state_transaction()&&!f.runtime->native_macro_retry_attempt();
            require(owners_restored,"classifier fatal exit did not restore actual live noncopyable owners");
        };
        const double original_nu=f.config.physics.diffusion.nu_visc;
        // Configuration is not a transaction-owned array/scalar. Restore the
        // test's deliberate public-config perturbation independently on all exits.
        struct RestoreCoefficient {
            double& value;double original;
            ~RestoreCoefficient(){value=original;}
        } restore{f.config.physics.diffusion.nu_visc,original_nu};
        const double accepted_time=f.counters->t_current,accepted_advice=f.burn_advice,
            accepted_old_dt=f.counters->dt_old;
        const int accepted_step=f.counters->step_count;
        bool fired=false,fatal=false,wrapped=false;
        int pressure_calls=0,expected_index=-1,expected_i=-1,expected_j=-1;
        StateSlot failed_slot=StateSlot::Current;
        state::StateVersion failed_version{};amr::BlockHandle failed_handle{};
        std::uint64_t token_before=0,token_after=0;
        probe->on_pressure=[&] {
            const auto* frame=scheduler::current_rkl_completed_boundary();
            if(fired||!frame||!frame->completed||frame->context!=&*f.context)return;
            require(f.active_attempt==0&&f.attempts.size()==1
                &&f.attempts.front().burn_first==1&&f.attempts.front().diffusion==1
                &&f.attempts.front().hydro==0&&f.runtime->active_runtime_state_transaction(),
                "thermal classifier injection lacks its genuine cold macro/RKL prefix");
            auto& block=f.block();const auto& grid=block.grid;
            const auto member=TimeIntegration::hydro_boundary_state_member(frame->slot);
            auto& output=block.*member;
            const int first=grid.GetIndex(grid.Is(),grid.Js(),0);
            const auto read=[&](int n){return output.get(n);};
            const state::Bounds bounds{f.config.numerics.sml_rho,
                f.config.numerics.min_eint,f.config.numerics.max_eint};
            const auto closure=RzThermodynamics::make_cell_supported(read,first,
                GridMetrics::make_geometry_view(grid,rz),grid.Is(),
                std::clamp(grid.Is()-1,0,grid.GetTotalX()-3),bounds);
            // The new full-tensor timestep selects three RKL1 stages. Its
            // first two cold means are valid; inject only after the same
            // genuine final-stage thermal refusal reaches classification.
            if(closure.status==state::Status::valid)return;
            require(closure.inertia_mapping_valid&&closure.status==state::Status::unresolved_energy,
                "classifier probe fired before the actual first unresolved active closure");
            // Both the mandatory gate and classification test the first cell
            // before EOS. Only classification continues past that real failure.
            require(f.runtime->native_macro_retry_attempt()!=nullptr
                &&frame->handles.size()==1&&frame->handles.front()==f.runtime->handles().front(),
                "classifier probe used a foreign Runtime/handle frame");
            fired=true;++pressure_calls;failed_slot=frame->slot;failed_version=frame->version;
            failed_handle=frame->handles.front();
            if(fault==ClassificationFault::DiffusionCoefficient) {
                f.config.physics.diffusion.nu_visc=std::nextafter(original_nu,
                    std::numeric_limits<double>::infinity());
                require(!bits(f.config.physics.diffusion.nu_visc,original_nu),
                    "classifier coefficient injection did not change its actual owner");
            } else if(fault==ClassificationFault::ClockToken) {
                token_before=f.context->clock.last_token();
                token_after=f.context->clock.next_completion().value;
                require(token_after==token_before+1&&token_after!=frame->completion.value,
                    "classifier clock injection did not invalidate the actual completion fence");
            } else {
                // The first valid later cell has already built its immutable
                // density closure before its pressure call. Poison a still-later
                // actual active cell, then let the genuine gate reject it.
                expected_i=grid.Is()+2;expected_j=grid.Js();
                require(expected_i<grid.Ie(),"classifier later-cell negative lacks actual support");
                expected_index=grid.GetIndex(expected_i,expected_j,0);
                require(output.rho[expected_index]>0.,"classifier candidate was invalid before injection");
                output.rho[expected_index]=-1.;
            }
        };
        try{(void)f.execute();}
        catch(const driver::NativeBoundaryAcceptanceError& error) {
            require(fault==ClassificationFault::LaterActiveDensity,
                "owner drift was misreported as a scientific acceptance failure");
            wrapped=error.pool_index==f.block().id&&error.handle==failed_handle
                &&error.slot==failed_slot&&error.version==failed_version
                &&error.diagnostic.phase==RzThermodynamics::AcceptancePhase::provisional
                &&error.diagnostic.status==state::Status::nonpositive_density
                &&error.diagnostic.index==expected_index&&error.diagnostic.i==expected_i
                &&error.diagnostic.j==expected_j&&error.diagnostic.node==-1
                &&!error.diagnostic.inertia_mapping_valid;
            require(wrapped,"later actual nonthermal cell lost original phase/status/pool/UID/slot/version");
            fatal=true;
        } catch(const driver::NativeThermalStepRejection&) {
            require(false,"engineering classifier fault escaped as a qualified thermal retry");
        } catch(const std::logic_error&) {
            require(fault!=ClassificationFault::LaterActiveDensity,
                "later nonthermal cell lost its typed scientific diagnostic");
            fatal=true;
        }
        // Rollback does not own config; only undo this intentional diagnostic.
        f.config.physics.diffusion.nu_visc=original_nu;
        require(fired&&pressure_calls==1&&fatal&&checkpoints==1&&f.attempts.size()==1
            &&!f.attempts.front().rejected&&f.attempts.front().burn_first==1
            &&f.attempts.front().burn_second==0&&f.attempts.front().diffusion==1
            &&f.attempts.front().hydro==0&&owners_restored,
            "classifier engineering rejection retried/skipped the genuine first B/D prefix");
        require(!f.runtime->active_runtime_state_transaction()&&!f.runtime->native_macro_retry_attempt()
            &&bits(f.counters->t_current,accepted_time)&&f.counters->step_count==accepted_step
            &&bits(f.counters->dt_old,accepted_old_dt)&&bits(f.burn_advice,accepted_advice),
            "classifier fatal rejection failed exact leases/fields/ledger/register/clock/BC/Advice rollback");
        // Drop the genuine borrowed diagnostic EOS before its local lifetime ends.
        f.runtime->bind_native_rz_eos(*f.eos);
        std::cout<<"RZ_NATIVE_THERMAL_CLASSIFIER_DEFENSE fault="<<static_cast<int>(fault)
            <<" attempts=1 real_prefix_B_D=1 complete_owner_rollback=1 same_patch="
            <<(fault==ClassificationFault::LaterActiveDensity)<<" physical_grant=0\n";
    }
}

void run(){complete_retry();minimum_and_advice();first_aligned_short();eos_binding_drift();classification_post_fence_rejections();}
} // namespace thermal_retry_checks

/** A genuine Native Runtime reader must reject newly published Current means
 * until the same Runtime completes BC/exchange/EOS and publishes their ghosts.
 * This uses the existing warm owner/EOS; it does not fake ghost certificates,
 * fill boundaries inside the reader or grant a private/public gravity route.
 */
void native_jeans_ghost_preflight()
{
    Fixture f(0,false,0.,dispatch::TimeIntegratorId::Euler,true);
    int calls=0;
    f.control.tree->SetJeansEvaluator([&](const FluidVector& u,const double* x,
        const GridMetrics::GeometryView& geometry,int i,int j) {
        ++calls;
        const double p=f.eos->get_pressure(u,x);
        const double c=f.eos->get_sound_speed(u,p,x);
        return JeansDiagnostics::evaluate_cell(u.rho,c*c,geometry,i,j);
    });
    const auto initial=f.runtime->evaluate_current_jeans_resolution();
    require(initial.size()==f.runtime->handles().size()&&calls==amr::BLOCK_NX*amr::BLOCK_NY,
        "Native Current Jeans reader missed actual accepted cells");
    scheduler::publish_completed_interior(*f.context,f.runtime->handles(),StateSlot::Current);
    const auto ready=f.context->ledger.inspect({f.runtime->handles().front(),StateSlot::Current});
    f.context->ledger.require_readable({f.runtime->handles().front(),StateSlot::Current},
        {state::ExecutionSide::Host,ready.interior.version,true,false});
    require(ready.ghost.residency==state::StateResidency::Invalid,
        "Native stale-ghost witness did not invalidate actual ghost publication");
    auto& block=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
    const rz_runtime_witness::FieldsWitness untouched(block);
    const auto token=f.context->clock.last_token();const int before=calls;
    bool rejected=false;
    try{(void)f.runtime->evaluate_current_jeans_resolution();}
    catch(const std::logic_error&){rejected=true;}
    require(rejected&&calls==before&&f.context->clock.last_token()==token,
        "Native Jeans missing-ghost reader entered EOS or secretly filled/published boundaries");
    untouched.matches(block);
    f.runtime->ensure_fluid_ghosts();
    const auto completed=f.runtime->evaluate_current_jeans_resolution();
    require(completed.size()==initial.size()&&calls==before+amr::BLOCK_NX*amr::BLOCK_NY,
        "Native Jeans failed after actual Runtime ghost/EOS completion");
    std::cout<<"RZ_NATIVE_JEANS_GHOST_PREFLIGHT_PASS\n";
}

void run() {
    native_jeans_ghost_preflight();
    for(int direction=0;direction<2;++direction)for(bool open:{false,true})
        for(double phi:{-.025,.025})for(auto method:{dispatch::TimeIntegratorId::Euler,
            dispatch::TimeIntegratorId::Rk2,dispatch::TimeIntegratorId::Rk3})
            matrix_case(direction,open,phi,method);
    physical_work();rejected_source_inputs();cold_consumption_discard();late_eos_rollback();
    application_guard_rejections();
    warm_rkl2_external_coupling();
    thermal_retry_checks::run();
}
} // namespace external_runtime_checks

/** Existing embedded owner calls this body once; it grants no public route. */
void run_native_rz_runtime_external_contract() {
    external_runtime_checks::run();
}
