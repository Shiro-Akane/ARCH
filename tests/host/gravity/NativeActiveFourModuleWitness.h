/**
 * @file NativeActiveFourModuleWitness.h
 * @brief One real warm reacting Native RZ macro; activity evidence only.
 *
 * Workflow:
 * 1. Load the caller's actual Helmholtz table and register the real aprox13
 *    C/O species. Sample the frozen Gaussian T field at the shared eight
 *    physical Init points and integrate the sole native V/W cell averages.
 * 2. Bind the actual Tree, BC/EOS, Runtime, ledger, RK2 Self stage and journal.
 * 3. Execute the unchanged B/2-D/2-H-D/2-B/2 in one real Host transaction.
 *    Record each burn half's NEW ENUC and composition separately. Observe
 *    genuine RKL2 callbacks and both actual private Self work-axis consumers.
 * 4. Require an accepted physical endpoint and publish compact activity
 *    diagnostics. No dynamic AMR, restart, continuous-field accuracy or
 *    macro fluid-plus-gravity energy qualification follows from the single-root
 *    entry. The separate dynamic entry uses the same operations and budgets.
 *
 * Q_B,h = sum_cells V*rho*ENUC_h*(dt/2). The two ENUC publications are
 * distinct; the last rate is never multiplied by the complete macro dt.
 * Thermal flux probes call the same production DiffFlux on immutable actual
 * half inputs, but are labelled probes rather than actual interior observers.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "host/driver/RzRuntimeWitness.h"

#include "amr/AMRControl.h"
#include "core/problem/InitialStateConversion.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/schedule/StageScheduler.h"
#include "driver/stages/DriverMacroStep.h"
#include "driver/stages/DriverStages.h"
#include "driver/stages/GravityStage.h"
#include "numerics/burnsolver/ode/ode_bd.h"
#include "numerics/diffusion/DiffFlux.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/linalg/DenseWrap.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "physics/eos/HelmEos.h"
#include "physics/gravity/GravityExecution.h"
#include "physics/gravity/NativeSelfStage.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/network/aprox13/NetAprox13.h"

namespace native_active_four_module {
using namespace arch;
using rz_runtime_witness::bits;
using rz_runtime_witness::require;
constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr int species_count=NetAprox13::NUM_SPECIES;
constexpr double macro_dt=1.e-12; // Prospective frozen SNIa tiny-time input.
constexpr long double integral_budget=1.e-12L; // Original native normalized budget.
using Burn=Solver_BD<NetAprox13,DenseMatrixData<NetAprox13::ODE_NEQ>,DenseLUSolver>;
using Hydro=Numerics::HydroSolverImpl<HelmEos,FluxHLLC<MusclReconstruction<McLimiter>>>;

/** Real Current active-cell snapshot; no ghost or second physical state owner. */
struct CellBefore {
    int index=0,block_id=-1,level=0;
    amr::BlockHandle handle{};
    std::array<std::uint32_t,3> logical{};
    double enuc=0.;
    FluidVector fluid{};
    std::array<double,species_count> fractions{};
};
/** Separate measurable half-step evidence; no reconstructed ODE/source integral. */
struct HalfRecord {
    int calls=0,changed_energy=0,changed_composition=0,nonzero_enuc=0;
    int rkl_begin=0,rkl_accept=0,rkl_stages=0;
    long double delta_energy=0.,energy_scale=0.,nuclear_integral=0.,composition_l1=0.;
    std::array<long double,species_count> species_delta{};
    double maximum_flux_probe=0.,maximum_enuc=0.;
};
/** Original operation evidence, copied only from the synchronous real receipt. */
struct RegistrationVisit {
    Physical::Gravity::GravityRefluxRowIdentity row{};
    int reserved=0,applied=0,flux_index=-1;
    double FE=0.,Frho=0.,psi=0.,result=0.;
};
/** Accepted native journal expectation; Current stage0 remains a separate purpose. */
struct JournalVisit {
    int stage=0;double time=0.;std::uint64_t epoch=0,lease=0,source_generation=0;
    std::size_t cells=0;
};
/** Owning scalar copies of actual synchronous source work, never borrowed Phi. */
struct SourceVisit {
    scheduler::StageDescriptor descriptor{};
    std::uint64_t field_generation=0,source_generation=0;
    double input_time=0.;
    std::array<int,2> axes{};
    std::array<double,3> solver_seconds{};
    // Scalar copy captured while this exact prepared field is still ready.
    Physical::Gravity::SelfGravity::RingMemoObservations ring_memo{};
    Physical::Gravity::GravitySolveIdentity source{};
    std::uint64_t lease=0,topology_fingerprint=0;
    std::map<int,std::array<int,2>> patch_axes,patch_before;
    std::map<std::tuple<int,int,std::size_t>,RegistrationVisit> registration;
};
/** Integral diagnostics use each real native V/W measure and current rho*X. */
struct Totals {
    long double mass=0.,angular=0.,energy=0.;
    std::array<long double,species_count> species{};
};

/** Test-only EMPTY initialization selector. The approved IO TU owns its loader
 * and every file/provenance operation. Shared stage owners gain no IO symbol
 * dependency: a null callback follows the original root/model Init exactly.
 * The loader runs after the actual independent Helm owner exists and before
 * Controller/BC/Runtime, and receives the actual EMPTY control and RunState.
 */
struct EmptyInitialization {
    using Loader=void (*)(void*,amr::AMRControl&,RunState&,const SimConfig&,
        const SpeciesManager&,HelmEos&);
    Loader load=nullptr;
    void* payload=nullptr;
};

/** Shared maintained activity owner. The original entry has one real root;
 * the separately frozen dynamic entry starts with two roots before a true regrid.
 * Empty initial CF routes are asserted, not mistaken for AMR coverage.
 */
class Owner {
public:
    SimConfig config{};
    SpeciesManager species;
    amr::AMRControl control;
    const bool dynamic;
    RunState start{};
    std::unique_ptr<HelmEos> eos;
    std::unique_ptr<SimulationController> controller;
    std::unique_ptr<BCHandler> bc;
    std::unique_ptr<driver::DriverRuntime> runtime;
    std::unique_ptr<Physical::Gravity::SelfGravity> gravity;
    std::unique_ptr<driver::GravityStage> gravity_stage;
    std::unique_ptr<Hydro> hydro;
    std::optional<scheduler::StageExecutionContext> context;
    driver::DriverStageWorkspace workspace;
    dispatch::ResolvedExecutionPlan plan{};
    std::array<HalfRecord,2> burns{},diffusions{};
    std::mutex source_mutex;
    std::map<int,SourceVisit> sources;
    std::vector<JournalVisit> accepted_journal;
    double forward_euler=0.,burn_advice=DriverBurn::INACTIVE_LIMITER_CANDIDATE;
    int active_diffusion=-1,hydro_calls=0;
    double macro_wall_seconds=0.;

    /** Freeze physical/numerical inputs before any handler, EOS or Runtime.
     * Optional checkpoint construction creates its own species/Helm owner and
     * EMPTY Tree, restores real IO means without calling model Init, then shares
     * the sole Runtime/BC/source binding path. endpoint_time is frozen before
     * controller construction; it does not mutate an in-flight run contract.
     * T=1e9+(3e9-1e9)*exp(-((r-3)^2+z^2)/(2*.5^2)); rho=1e7,
     * X_C12=X_O16=.5, u_r=u_z=u_phi=0. V/W Init remains shared.
     * Only the dynamic variant uses rho=1e7*(1+.1*exp(-((r-1.7)/.25)^2-(z/.5)^2)).
     */
    explicit Owner(const std::string& table,bool dynamic_mode=false,
        double endpoint_time=0.,EmptyInitialization initialization={},
        const std::string& output_directory={})
        :control(dynamic_mode?32:8,2),dynamic(dynamic_mode) {
        require(!table.empty(),"Active four-module requires an explicit actual Helm table path");
        config.grid.dim=2;config.grid.geometry="cylindrical";
        config.grid.nblockx1=dynamic?2:1;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=1.;config.grid.x1_max=5.;
        config.grid.x2_min=-2.;config.grid.x2_max=2.;config.grid.amr_max_blocks=dynamic?32:8;
        config.grid.x1l_boundary_type=config.grid.x1r_boundary_type="outflow";
        config.grid.x2l_boundary_type=config.grid.x2r_boundary_type="outflow";
        config.amr.lrefinemin=0;config.amr.lrefinemax=dynamic?1:0;
        if(dynamic) {
            config.amr.refine_var="DENS";config.amr.refine_on_rho=true;
            config.amr.refine_threshold=.1;config.amr.derefine_threshold=.02;
        }
        config.numerics.solver_name="HLLC";config.numerics.reconstruction="muscl";
        config.numerics.limiter="mc";config.numerics.time_integrator="RK2";
        config.numerics.cfl=.3;config.numerics.dt_init=macro_dt;
        config.numerics.dt_min=1.e-20;config.numerics.tstep_change_factor=1.;
        // Original SNIa scientific bounds, Coulomb correction and ODE controls.
        config.numerics.sml_rho=1.e-12;config.numerics.min_eint=1.e-10;
        config.numerics.max_eint=1.e21;
        config.physics.eos_type="helmholtz";config.physics.eos_table_path=table;
        config.physics.eos_coulomb_mult=1.;
        auto& burn=config.physics.burn;
        burn.use_burn=true;burn.network_name="aprox13";
        burn.nuclearTempMin=8.e8;burn.nuclearDensMin=1.e-10;
        // Maintained SNIa value controls only future fractional-energy dt advice.
        // This fixed tiny macro does not exercise adaptive reaction-limited dt;
        // no ENUC/BD/EOS integral is scaled or bypassed by this advice parameter.
        burn.smallt=1.e5;burn.smallx=1.e-20;burn.enucDtFactor=1.e30;
        burn.use_nse=false;burn.nse_auto=false;
        burn.odeconfig.ode_solver="bd";burn.odeconfig.linear_solver="DenseLU";
        burn.odeconfig.rtol=1.e-4;burn.odeconfig.atol=1.e-8;
        auto& diffusion=config.physics.diffusion;
        diffusion.use_diffusion=diffusion.use_thermal_diffusion=true;
        diffusion.use_viscous_diffusion=diffusion.use_species_diffusion=false;
        diffusion.integrator="RKL2";diffusion.diff_cfl=.8;
        // Zero overrides select actual Helm stellar conduction, not constants.
        diffusion.nu_visc=diffusion.alpha_therm=diffusion.D_spec=0.;
        config.physics.gravity.type="self";config.physics.gravity.boundary="isolated";
        config.physics.gravity.relative_tolerance=1.e-10;
        config.physics.gravity.absolute_tolerance=0.;config.physics.gravity.max_cycles=200;
        config.io.tmax=endpoint_time==0.?(dynamic?2.*macro_dt:macro_dt):endpoint_time;
        require(std::isfinite(config.io.tmax)&&config.io.tmax>0.,"Invalid frozen activity endpoint");config.io.plt_dt=config.io.chk_dt=-1.;
        config.io.plt_dstep=config.io.chk_dstep=-1;
        config.io.out_dir=output_directory.empty()?(dynamic?"native-active-four-module-amr":"native-active-four-module"):output_directory;
        if(!output_directory.empty())config.io.base_name="native-active";
        config.io.restart=initialization.load!=nullptr;
        NetAprox13::RegisterSpecies(species);
        require(species.count()==species_count,"Active four-module lost real aprox13 layout");
        eos=std::make_unique<HelmEos>(table,&species,config.physics.eos_coulomb_mult);
        if(!initialization.load) {
        control.tree->InitRootGrid(config,species_count,native);
        control.flux_register.EnsureSpecies(species_count);
        require(control.tree->GetActiveBlocks().size()==static_cast<std::size_t>(dynamic?2:1),
            "Activity witness lost its actual configured root domain");
        for(int block_id:control.tree->GetActiveBlocks()) {
            auto& block=control.pool->GetBlock(block_id);block.RequireNativeGeometryIdentity();const auto& grid=block.grid;
            require(grid.Ie()-grid.Is()==16&&grid.Je()-grid.Js()==16,
                "Active four-module has invalid actual N16 annular support");
            // The single-root first halo is [0,.25]; the dynamic two-root first
            // halo begins at .5. All actual shared Gauss radii must be positive.
            for(int i=0;i<grid.GetTotalX();++i) {
                const double lower=grid.GetFacePosL(i),upper=grid.GetFacePosR(i);
                require(std::isfinite(lower)&&std::isfinite(upper)&&lower>=0.&&upper>lower,
                    "Active four-module has invalid actual radial cell support");
            }
            for(const auto& sample:GridMetrics::Rz::CellAverageSamples(grid.GetFacePosL(0),
                grid.GetFacePosR(0),grid.GetAxialFacePosL(0),grid.GetAxialFacePosR(0)))
                require(std::isfinite(sample.radius)&&sample.radius>0.,
                    "Active four-module first halo has a nonpositive actual Gauss radius");
            for(auto* fluid:rz_runtime_witness::slots(block)) {
                fluid->stage_repairs.reset(species_count,state::RepairSemantics::RzVolumeAngular);
                for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
                    const auto value=ProblemHelper::detail::InitialRzCellState(
                        grid.GetFacePosL(i),grid.GetFacePosR(i),grid.GetAxialFacePosL(j),
                        grid.GetAxialFacePosR(j),species_count,*eos,config.numerics,
                        [this](const PointCoords& p,PrimitiveData& primitive) {
                            const double dr=(p.r_cy-3.)/.5,dz=p.z_cy/.5;
                            primitive.rho=1.e7;
                            if(dynamic) {
                                const double rr=(p.r_cy-1.7)/.25,zz=p.z_cy/.5;
                                primitive.rho*=1.+.1*std::exp(-rr*rr-zz*zz);
                            }
                            primitive.u=primitive.v=primitive.w=0.;
                            primitive.SetTemperature(1.e9+2.e9*std::exp(-.5*(dr*dr+dz*dz)));
                            primitive.mass_fractions.assign(species_count,0.);
                            primitive.mass_fractions[timmes_aprox13_detail::ic12]=.5;
                            primitive.mass_fractions[timmes_aprox13_detail::io16]=.5;
                        });
                    const int c=grid.GetIndex(i,j,0);fluid->set(c,value.conserved);
                    fluid->set_species_from_buffer(c,value.mass_fractions.data());fluid->enuc_rate[c]=0.;
                }
            }
        } // All real roots use the same eight-point Init/EOS owner.
        start.repairs.reset(species_count,state::RepairSemantics::RzVolumeAngular);
        } else {
            // Genuine fresh path never runs model Init. The selected approved
            // fixture loader owns file/provenance checks and conserved means;
            // actual Runtime BC/Helm below alone grants completed readiness.
            require(dynamic&&control.tree->GetActiveBlocks().empty(),
                "Warm restart requires EMPTY independent dynamic control");
            initialization.load(initialization.payload,control,start,config,species,*eos);
            require(start.checkpoint_provenance_verified&&start.has_timestep_state
                &&!start.resume_after_regrid&&bits(start.time,2.*macro_dt)&&start.step==2,
                "Warm checkpoint did not retain the actual M1 accepted schedule");
            control.flux_register.EnsureSpecies(species_count);burn_advice=start.dt_burn;
        }
        controller=std::make_unique<SimulationController>(config,start);
        bc=std::make_unique<BCHandler>(config,native);bc->bind(*eos,species);
        bc->configure_stage(start.time,boundary::BoundaryPurpose::Hydro);
        runtime=std::make_unique<driver::DriverRuntime>(control,*bc,config,species,*controller);
        runtime->bind_native_rz_eos(*eos);runtime->initialize_topology();
        gravity=std::make_unique<Physical::Gravity::SelfGravity>(config.physics.gravity);
        gravity_stage=std::make_unique<driver::GravityStage>(*runtime,gravity.get(),
            driver::GravityStage::Qualification::NativeRzSelfHydroCandidate);
        require(gravity_stage->supports_host_macro_step_journal(),"Active macro lacks actual gravity journal");
        gravity_stage->set_native_self_flux_observation(&Owner::observe_source,this);
        hydro=std::make_unique<Hydro>(*eos,native);
        plan.flux=dispatch::FluxId::Hllc;plan.reconstruction=dispatch::ReconstructionId::Muscl;
        plan.limiter=dispatch::LimiterId::Mc;plan.time_integrator=dispatch::TimeIntegratorId::Rk2;
        plan.eos=dispatch::EosId::Helmholtz;plan.network=dispatch::NetworkId::Aprox13;
        plan.ode_solver=dispatch::OdeSolverId::Bd;plan.linear_solver=dispatch::LinearSolverId::DenseLu;
        plan.diffusion_integrator=dispatch::DiffusionIntegratorId::Rkl2;
        bind_context_at_current();
        const auto& topology=control.RequireFluxTopologyPlan(species_count,native,-1,true);
        if(!initialization.load)
            require(topology.routes.empty(),"Uniform first activity witness unexpectedly has CF routes");
        else require(control.tree->GetActiveBlocks().size()==5&&!topology.routes.empty(),
            "Fresh warm checkpoint did not reconstruct the actual mixed CF domain");
    }

    /** Bind actual new Runtime references and controller time after topology publication.
     * The old context must be destroyed before a regrid replaces its ledger.
     * No counters, accumulated accounting values or gravity journal are reset.
     */
    void bind_context_at_current() {
        require(!context&&!runtime->active_host_hydro_transaction()&&!gravity->prepared_native_self(),
            "Context rebinding requires a quiescent actual Runtime");
        context.emplace(runtime->stage_context());context->step_start_time=controller->t_current;
        context->step_dt=macro_dt;
        context->configure_boundary_context=[this](double time,boundary::BoundaryPurpose purpose) {
            bc->configure_stage(time,purpose);
            runtime->bind_native_boundary_acceptance(*context,runtime->handles());
        };
        context->physical_boundary_preparation=[this](state::StateSlot slot,double time,boundary::BoundaryPurpose purpose) {
            context->configure_boundary_context(time,purpose);runtime->ensure_fluid_ghosts(slot);
        };
        context->hydro_preparation=gravity_stage.get();
        context->configure_boundary_context(controller->t_current,boundary::BoundaryPurpose::Hydro);
        runtime->bind_boundary_accounting(*context);bind_rkl_observers();
        const auto candidates=driver::calculate_timestep_candidates(*runtime,workspace,*eos,&plan);
        forward_euler=candidates.diffusion_forward_euler;
        require(std::isfinite(forward_euler)&&forward_euler>0.&&std::isfinite(candidates.hydro)
            &&macro_dt<=candidates.hydro,"Frozen active macro exceeds its actual physical stability proposal");
    }
    /** Borrow the actual first pooled Current; the single-root entry retains its scope. */
    amr::Block& current_block() {return control.pool->GetBlock(control.tree->GetActiveBlocks().front());}
    /** Resolve a recorded pooled cell only under its same UID, epoch and logical key. */
    amr::Block& recorded_block(const CellBefore& cell) {
        const auto& active=control.tree->GetActiveBlocks();
        const auto found=std::find(active.begin(),active.end(),cell.block_id);
        require(found!=active.end(),"Diagnostic cell no longer belongs to the actual active domain");
        const auto n=static_cast<std::size_t>(found-active.begin());
        auto& b=control.pool->GetBlock(cell.block_id);
        require(n<runtime->handles().size()&&runtime->handles()[n]==cell.handle
            &&b.level==cell.level&&std::array<std::uint32_t,3>{b.logical_x1,b.logical_x2,b.logical_x3}==cell.logical,
            "Diagnostic cell crossed a topology/storage owner");return b;
    }
    /** Freeze every actual active cell in runtime handle order; no ghost state is copied. */
    std::vector<CellBefore> freeze_active() {
        const auto& active=control.tree->GetActiveBlocks();std::vector<CellBefore> result;
        require(active.size()==runtime->handles().size(),"Active snapshot has incomplete real handles");
        result.reserve(256*active.size());
        for(std::size_t n=0;n<active.size();++n) {
            auto& b=control.pool->GetBlock(active[n]);const auto& g=b.grid;
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                CellBefore cell;cell.index=g.GetIndex(i,j,0);cell.fluid=b.fluid_state.get(cell.index);
                cell.block_id=active[n];cell.handle=runtime->handles()[n];cell.level=b.level;
                cell.logical={b.logical_x1,b.logical_x2,b.logical_x3};cell.enuc=b.fluid_state.enuc_rate[cell.index];
                for(int s=0;s<species_count;++s)cell.fractions[s]=b.fluid_state.X(s,cell.index);
                result.push_back(cell);
            }
        }
        return result;
    }
    /** Sum actual native M,J,E,rhoX across all active patches, in original cell order. */
    Totals totals() {
        Totals result;
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
            const auto view=GridMetrics::make_geometry_view(g,native);
            for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i) {
                const int c=g.GetIndex(i,j,0);const long double V=GridMetrics::CellVolume(view,i,j,0);
                const long double W=GridMetrics::Rz::AngularMomentumMeasure(view,i,j);
                const auto value=b.fluid_state.get(c);result.mass+=V*value.rho;
                result.angular+=W*value.mom_w;result.energy+=V*value.eng;
                for(int s=0;s<species_count;++s)result.species[s]+=V*value.rho*b.fluid_state.X(s,c);
            }
        }
        return result;
    }
    /** Dynamic-only observers are defined in the cohesive AMR witness owner. */
    void observe_dynamic_source(const Physical::Gravity::NativeSelfStageObservation&);
    void complete_dynamic_sources();
    void check_dynamic_journal();

    /** Record NEW rate and rhoX changes immediately after this genuine burn half.
     * Delta E and Q use independent diagnostics. A large thermal energy may
     * mask a small source in subtraction; activity requires composition and
     * the ODE-owned rate, rather than inventing a detectable gravity signal.
     */
    void record_burn(HalfRecord& record,const std::vector<CellBefore>& before,double half) {
        for(const auto& old:before) {
            auto& b=recorded_block(old);const auto& g=b.grid;
            const auto view=GridMetrics::make_geometry_view(g,native);
            const int i=old.index%g.stride_y,j=old.index/g.stride_y;
            const long double V=GridMetrics::CellVolume(view,i,j,0);const auto now=b.fluid_state.get(old.index);
            const double rate=b.fluid_state.enuc_rate[old.index];
            require(std::isfinite(rate)&&std::isfinite(now.eng)&&bits(now.rho,old.fluid.rho)
                &&bits(now.mom_u,old.fluid.mom_u)&&bits(now.mom_v,old.fluid.mom_v)
                &&bits(now.mom_w,old.fluid.mom_w),"Real burn changed native fixed-density/momentum operands");
            const long double delta=V*(static_cast<long double>(now.eng)-old.fluid.eng);
            record.delta_energy+=delta;record.energy_scale+=V*std::abs(old.fluid.eng);
            record.nuclear_integral+=V*static_cast<long double>(now.rho)*rate*half;
            record.maximum_enuc=std::max(record.maximum_enuc,std::abs(rate));
            record.changed_energy+=!bits(now.eng,old.fluid.eng);record.nonzero_enuc+=rate!=0.;
            bool changed=false;
            for(int s=0;s<species_count;++s) {
                const double x=b.fluid_state.X(s,old.index);
                require(std::isfinite(x),"Real burn published nonfinite composition");
                const long double change=V*now.rho*(static_cast<long double>(x)-old.fractions[s]);
                record.species_delta[s]+=change;record.composition_l1+=std::abs(change);
                changed|=!bits(x,old.fractions[s]);
            }
            record.changed_composition+=changed;
        }
        require(record.changed_composition>0&&record.nonzero_enuc>0&&record.maximum_enuc>0.,
            "Actual warm burn half had no new composition/ENUC activity");
        require(std::abs(record.delta_energy-record.nuclear_integral)
            <=integral_budget*std::max(1.L,record.energy_scale),
            "Actual burn half violated the original normalized first-law diagnostic budget");
    }

    /** Probe the same shared flux owner on immutable actual input, without
     * capture/accounting writes. The true RKL stages subsequently run normally.
     */
    double thermal_flux_probe() {
        double maximum=0.;
        for(int block_id:control.tree->GetActiveBlocks()) {
            auto& b=control.pool->GetBlock(block_id);const auto& g=b.grid;const FluidState before=b.fluid_state;
            std::vector<FluidVector> flux(g.GetTotalSize());
            std::vector<double> species_flux(static_cast<std::size_t>(g.GetTotalSize())*species_count);
            for(int axis=0;axis<2;++axis) {
                std::fill(flux.begin(),flux.end(),FluidVector{});
                std::fill(species_flux.begin(),species_flux.end(),0.);
                DiffFlux::compute_fluxes(b.fluid_state,*eos,g,config,flux,species_flux,axis,false,native);
                for(int j=g.Js();j<g.Je()+(axis==1);++j)for(int i=g.Is();i<g.Ie()+(axis==0);++i) {
                    const auto f=flux[g.GetIndex(i,j,0)];
                    require(std::isfinite(f.eng)&&f.rho==0.&&f.mom_u==0.&&f.mom_v==0.&&f.mom_w==0.,
                        "Actual thermal-only flux probe had nonfinite or nonthermal transport");
                    maximum=std::max(maximum,std::abs(f.eng));
                }
            }
            for(const auto field:rz_runtime_witness::fields)
                require(bits(b.fluid_state.*field,before.*field),"Flux probe changed an actual state/diagnostic array");
        }
        require(maximum>0.,"Actual warm half input has no nonzero stellar thermal flux");return maximum;
    }
    /** Keep the genuine Runtime capture/accounting callbacks, adding observations
     * of their actual RKL2 plans, completed stages and finite boundary planes.
     */
    void bind_rkl_observers() {
        const auto begin=context->rkl_flux_capture_begin,accept=context->rkl_flux_capture_accept;
        require(bool(begin)&&bool(accept),"Native Runtime did not bind actual RKL accounting");
        context->rkl_flux_capture_begin=[this,begin](const scheduler::RklStageDescriptor& stage,const scheduler::RklPlan& p) {
            require(active_diffusion>=0&&active_diffusion<2&&runtime->active_host_hydro_transaction()
                &&p.method==scheduler::RklMethod::RKL2&&p.second_order&&p.stages.size()>=2,
                "Actual RKL observer lost its split/method/transaction");
            auto& r=diffusions[active_diffusion];require(stage.stage==++r.rkl_begin,"Actual RKL stages were skipped/repeated");
            r.rkl_stages=static_cast<int>(p.stages.size());begin(stage,p);
        };
        context->rkl_flux_capture_accept=[this,accept](const scheduler::RklStageDescriptor& stage,const scheduler::RklPlan& p) {
            auto& r=diffusions.at(static_cast<std::size_t>(active_diffusion));
            require(stage.stage==++r.rkl_accept&&r.rkl_accept<=r.rkl_begin,"Actual RKL accepted a foreign stage");
            for(int id:control.tree->GetActiveBlocks()) {
                const auto capture=control.pool->GetBlock(id).fluid_state.boundary_flux_capture;
                require(bool(capture),"Actual RKL physical capture owner is missing");
                for(const auto& plane:capture->stage)for(double value:plane)
                    require(std::isfinite(value),"Actual RKL produced a nonfinite boundary flux");
            }
            accept(stage,p); // Original Runtime recurrence; never substitute a receipt.
        };
    }
    /** Copy exact identities only AFTER each genuine source work axis.
     * Actual frame/journal acceptance independently checks momentum and routes.
     */
    static void observe_source(void* payload,const Physical::Gravity::NativeSelfStageObservation& event) {
        auto& o=*static_cast<Owner*>(payload);
        if(o.dynamic){o.observe_dynamic_source(event);return;}
        if(event.kind!=Physical::Gravity::NativeSelfStageObservation::Kind::AxisAfter)return;
        require(event.grid&&event.input&&event.delta&&event.flux&&event.descriptor&&event.source
            &&event.axis>=0&&event.axis<2&&event.block_id==o.control.tree->GetActiveBlocks().front()
            &&o.runtime->active_host_hydro_transaction()&&bits(event.dt,macro_dt)
            &&event.source_generation>0&&event.field_generation>0,
            "Actual four-module source work lost its real stage/field/input owner");
        const auto plan=scheduler::make_hydro_plan(scheduler::HydroMethod::RK2);
        const int stage=event.descriptor->stage;
        require(stage>=1&&stage<=2&&scheduler::same_stage_descriptor(*event.descriptor,plan.stages[stage-1])
            &&bits(event.stage_weight,plan.stages[stage-1].flux_register_weight)
            &&bits(event.source->input_time,o.context->step_start_time+event.descriptor->input_time_fraction*macro_dt)
            &&event.source->inputs.size()==o.runtime->handles().size(),
            "Actual four-module source work changed RK2 weights/time/domain");
        for(const auto& input:event.source->inputs)
            require(input.slot==event.descriptor->input_slot&&input.version.value>0&&input.storage_generation>0,
                "Actual four-module gravity did not consume its published selected slot");
        for(const auto& value:*event.delta)
            require(std::isfinite(value.rho)&&std::isfinite(value.mom_u)&&std::isfinite(value.mom_v)
                &&std::isfinite(value.mom_w)&&std::isfinite(value.eng),"Actual source work produced nonfinite delta");
        std::lock_guard<std::mutex> lock(o.source_mutex);
        auto [it,inserted]=o.sources.try_emplace(stage);
        auto& visit=it->second;
        const auto& timing=o.gravity->timings(Physical::Gravity::GravityFieldScope::NativeRzCandidate);
        const std::array<double,3> times{timing.source_boundary,timing.poisson,timing.force};
        for(double value:times)require(std::isfinite(value)&&value>=0.,"Actual solve phase timing is invalid");
        const auto ring=o.gravity->ring_memo_observations(Physical::Gravity::GravityFieldScope::NativeRzCandidate);
        require(ring.epoch==event.source->topology.value&&ring.source_generation==event.source_generation
            &&ring.field_generation==event.field_generation,"Actual source ring observations changed field identity");
        if(inserted) {visit.descriptor=*event.descriptor;visit.field_generation=event.field_generation;
            visit.source_generation=event.source_generation;visit.input_time=event.source->input_time;
            visit.solver_seconds=times;visit.ring_memo=ring;}
        require(visit.ring_memo==ring,"Same-field ring observations changed between actual consumers");
        for(int k=0;k<3;++k)require(bits(visit.solver_seconds[k],times[k]),
            "Same-field actual solve phase timing changed between observers");
        require(visit.field_generation==event.field_generation&&visit.source_generation==event.source_generation
            &&++visit.axes[event.axis]==1,"Actual source axis was repeated or crossed field generations");
    }

    /** Run the frozen real split. Each callback delegates its sole production
     * operation and records only the input/output evidence around that call.
     */
    void advance() {
        const auto macro_started=std::chrono::steady_clock::now();
        const double entry_time=controller->t_current;const int entry_step=controller->step_count;
        if(dynamic) {
            burns={};diffusions={};sources.clear();hydro_calls=0;active_diffusion=-1;
            context->step_start_time=entry_time;context->step_dt=macro_dt;
            context->configure_boundary_context(entry_time,boundary::BoundaryPurpose::Hydro);
        }
        scheduler::ScopedStageBinding binding(*context,runtime->handles());
        const auto burn=BurnerHandle<HelmEos>::bind<Burn>();
        driver::execute_driver_macro_step(*runtime,*context,hydro.get(),true,
            [&](driver::BurnHalf which,double half,state::CompletionToken token) {
                const int index=which==driver::BurnHalf::First?0:1;auto& record=burns[index];
                require(bits(half,.5*macro_dt)&&++record.calls==1&&runtime->active_host_hydro_transaction(),
                    "Real burn half lost its original interval/transaction");
                const auto before=freeze_active();
                const auto completed=driver::execute_burn_half(*runtime,workspace,*eos,burn,which,half,burn_advice,token);
                require(state::is_complete(completed)&&completed.value==token.value,"Actual warm burn lost completion");
                record_burn(record,before,half);return completed;
            },
            [&](double half) {
                active_diffusion=diffusions[0].calls==0?0:1;auto& record=diffusions[active_diffusion];
                require(bits(half,.5*macro_dt)&&++record.calls==1,"Actual thermal half was skipped/repeated");
                runtime->ensure_fluid_ghosts(state::StateSlot::Current);
                record.maximum_flux_probe=thermal_flux_probe();const auto before=freeze_active();
                driver::advance_diffusion(*runtime,workspace,*context,*eos,&plan,controller->step_count,half,forward_euler);
                for(const auto& old:before) {
                    const auto& fluid=recorded_block(old).fluid_state;
                    if(!bits(fluid.rho[old.index],old.fluid.rho))
                        std::cerr<<std::setprecision(17)<<"THERMAL_MASS_DIAGNOSTIC half="<<active_diffusion+1
                            <<" cell="<<old.index<<" before="<<old.fluid.rho<<" after="<<fluid.rho[old.index]
                            <<" delta="<<fluid.rho[old.index]-old.fluid.rho
                            <<" rkl_stages="<<record.rkl_stages<<'\n';
                    require(bits(fluid.rho[old.index],old.fluid.rho),"Thermal diffusion transported mass");
                    record.changed_energy+=!bits(fluid.eng[old.index],old.fluid.eng);
                    for(int s=0;s<species_count;++s) {
                        // Failure-only evidence from this actual accepted RKL
                        // half; no EOS/flux recomputation, tolerance or state
                        // change is allowed before the original bit assertion.
                        if(!bits(fluid.X(s,old.index),old.fractions[s])) {
                            const auto& grid=control.pool->GetBlock(old.block_id).grid;
                            const double after=fluid.X(s,old.index);
                            std::cerr<<std::setprecision(17)<<"THERMAL_SPECIES_DIAGNOSTIC"
                                <<" blockId="<<old.block_id<<" handleUid="<<old.handle.uid.value
                                <<" epoch="<<old.handle.epoch.value<<" level="<<old.level
                                <<" logical="<<old.logical[0]<<','<<old.logical[1]<<','<<old.logical[2]
                                <<" cell="<<old.index<<" i="<<old.index%grid.stride_y
                                <<" j="<<(old.index%grid.stride_z)/grid.stride_y<<" species="<<s
                                <<" Xbefore="<<old.fractions[s]<<" Xafter="<<after
                                <<" delta="<<after-old.fractions[s]
                                <<" XbeforeBits="<<std::bit_cast<std::uint64_t>(old.fractions[s])
                                <<" XafterBits="<<std::bit_cast<std::uint64_t>(after)
                                <<" rhoBefore="<<old.fluid.rho<<" rhoAfter="<<fluid.rho[old.index]
                                <<" rhoBeforeBits="<<std::bit_cast<std::uint64_t>(old.fluid.rho)
                                <<" rhoAfterBits="<<std::bit_cast<std::uint64_t>(fluid.rho[old.index])
                                <<" rkl_begin="<<record.rkl_begin<<" rkl_accept="<<record.rkl_accept
                                <<" rkl_stages="<<record.rkl_stages<<" half="<<active_diffusion+1
                                <<" half_dt="<<half<<" macro_time="<<controller->t_current
                                <<" macro_step="<<controller->step_count
                                <<" macro_start="<<context->step_start_time<<" macro_dt="<<context->step_dt
                                <<" clock_token="<<context->clock.last_token()
                                <<" clock_version="<<context->clock.last_version()<<'\n';
                        }
                        require(bits(fluid.X(s,old.index),old.fractions[s]),
                            "Thermal-only diffusion changed species");
                    }
                }
                require(record.rkl_stages>=2&&record.rkl_begin==record.rkl_stages
                    &&record.rkl_accept==record.rkl_stages,"Actual RKL2 half did not complete all genuine stages");
                // An extremely small physical dt can make the update unresolvable
                // in E; flux/stage evidence remains primary and this is recorded.
                active_diffusion=-1;
            },
            [&](double full) {require(++hydro_calls==1&&bits(full,macro_dt),"Actual Hydro was skipped/repeated");
                driver::advance_hydro(*runtime,workspace,*context,&plan,full,&SolverRK2::solve<BCHandler>,gravity.get(),hydro.get());},
            [](driver::CpuStage,auto&& call) {call();});
        require(!runtime->active_host_hydro_transaction()&&!gravity->prepared_native_self()
            &&bits(controller->t_current,dynamic?entry_time:start.time)&&controller->step_count==(dynamic?entry_step:start.step),
            "Actual macro left a tentative source/transaction or prematurely advanced time");
        if(dynamic)complete_dynamic_sources();
        else require(sources.size()==2&&sources.at(1).axes==std::array<int,2>{1,1}
            &&sources.at(2).axes==std::array<int,2>{1,1}
            &&sources.at(1).field_generation!=sources.at(2).field_generation,
            "Actual RK2 omitted a solved stage or genuine work-axis consumer");
        for(const auto handle:runtime->handles()) {
            const state::StateKey key{handle,state::StateSlot::Current};const auto stamp=context->ledger.inspect(key);
            require(stamp.interior.pending_transfer==state::PendingTransferPhase::None
                &&stamp.ghost.pending_transfer==state::PendingTransferPhase::None,"Actual endpoint has pending residency");
            context->ledger.require_readable(key,{state::ExecutionSide::Host,stamp.interior.version,true,true});
        }
        for(int id:control.tree->GetActiveBlocks()) {
            const auto& b=control.pool->GetBlock(id);
            RzThermodynamics::validate_completed_patch_eos(b.fluid_state,b.grid,species_count,
                {config.numerics.sml_rho,config.numerics.min_eint,config.numerics.max_eint},*eos);
        }
        require(runtime->repair_budget().semantics==state::RepairSemantics::RzVolumeAngular,
            "Accepted active macro changed native repair measure");
        for(double value:runtime->repair_budget().values)require(value==0.,"Warm activity witness required a repair");
        controller->advance(macro_dt);gravity_stage->flush_committed_diagnostics();
        if(dynamic)check_dynamic_journal();else check_journal();
        macro_wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-macro_started).count();
    }
    /** Read the actual two committed rows; no synthesized successful receipt. */
    void check_journal() {
        std::ifstream file(config.io.out_dir+"/native_rz_candidates.tsv");std::string line;
        require(bool(std::getline(file,line)),"Actual active-macro journal header is missing");int count=0;
        while(std::getline(file,line))if(!line.empty()) {
            std::istringstream row(line);double time=0.,residual=0.,safe=0.;int stage=0,physical=-1;
            std::uint64_t epoch=0,lease=0,source=0;std::size_t cells=0;
            require(bool(row>>time>>stage>>epoch>>lease>>cells>>source>>residual>>safe>>physical)
                &&stage==++count&&epoch>0&&lease>0&&cells==256&&source==sources.at(stage).source_generation
                &&bits(time,sources.at(stage).input_time)&&std::isfinite(residual)&&residual>=0.
                &&std::isfinite(safe)&&safe>0.&&residual<=safe&&physical==0,
                "Actual active-macro committed journal changed candidate/error/identity semantics");
        }
        require(count==2,"Actual active-macro journal omitted/duplicated a genuine stage");
    }
};

/** Single maintained private entry; first activity exit only, no public grant.
 * Boundary M/J/species balance uses original actual observers and V/W totals.
 * E diagnostics intentionally do not label a total gravitational-energy test.
 * Initial u_phi=0 and g_phi=0 make J a zero-angular sanity check only; its
 * existing mass-normalized diagnostic does not qualify nonzero wall torque.
 */
inline void run(const std::string& table) {
    Owner owner(table);const auto before=owner.totals();owner.advance();const auto after=owner.totals();
    const auto& hydro=owner.runtime->hydro_boundary_budget();const auto& diffusion=owner.runtime->diffusion_boundary_budget();
    require(hydro.size()==6+species_count&&diffusion.size()==hydro.size(),"Actual macro boundary receipts are incomplete");
    for(double value:hydro)require(std::isfinite(value),"Actual Hydro boundary receipt is nonfinite");
    for(double value:diffusion)require(std::isfinite(value),"Actual thermal boundary receipt is nonfinite");
    require(std::abs(after.mass-before.mass+hydro[0]+diffusion[0])<=integral_budget*std::max(1.L,std::abs(before.mass))
        &&std::abs(after.angular-before.angular+hydro[3]+diffusion[3])<=integral_budget*std::max(1.L,std::abs(before.mass)),
        "Actual warm macro violated original normalized M/J balance");
    for(int s=0;s<species_count;++s) {
        const long double change=owner.burns[0].species_delta[s]+owner.burns[1].species_delta[s];
        require(std::abs(after.species[s]-before.species[s]-change+hydro[5+s]+diffusion[5+s])
            <=integral_budget*std::max(1.L,std::abs(before.mass)),"Actual active macro lost burn/transport species balance");
    }
    std::cout<<std::setprecision(17);
    for(int h=0;h<2;++h) {
        const auto& b=owner.burns[h];const auto& d=owner.diffusions[h];
        std::cout<<"NATIVE_ACTIVE_HALF half="<<h+1<<" burn_calls="<<b.calls
            <<" changed_composition_cells="<<b.changed_composition<<" fresh_enuc_cells="<<b.nonzero_enuc
            <<" max_abs_enuc="<<b.maximum_enuc<<" Q_actual_half="<<b.nuclear_integral
            <<" delta_E_burn="<<b.delta_energy<<" rhoX_change_L1="<<b.composition_l1
            <<" thermal_probe_max="<<d.maximum_flux_probe<<" diffusion_changed_E_cells="<<d.changed_energy
            <<" rkl2_begin="<<d.rkl_begin<<" rkl2_accept="<<d.rkl_accept<<'\n';
    }
    for(const auto& [stage,visit]:owner.sources)
        std::cout<<"NATIVE_ACTIVE_SOURCE_TIMING stage="<<stage<<" source_boundary_seconds="<<visit.solver_seconds[0]
            <<" poisson_seconds="<<visit.solver_seconds[1]<<" force_seconds="<<visit.solver_seconds[2]<<'\n';
    std::cout<<"PRIVATE_NATIVE_ACTIVE_FOUR_MODULE candidate_only=1 actual_BD_DenseLU=1 actual_Helm_table=1"
        <<" actual_aprox13=1 actual_RKL2=1 actual_RK2_Self=1 macro_dt="<<macro_dt
        <<" macro_wall_seconds="<<owner.macro_wall_seconds
        <<" accepted_time="<<owner.controller->t_current<<" accepted_steps="<<owner.controller->step_count
        <<" source_stages="<<owner.sources.size()<<" CF_routes=0 active_AMR_qualified=0 restart_qualified=0"
        <<" total_energy_qualified=0 physical_qualified=0 gravity_signal_may_be_below_thermal_ULP=1"
        <<" reaction_dt_advice_exercised=0 angular_torque_qualified=0"
        <<" initial_E="<<before.energy<<" final_E="<<after.energy<<'\n';
}
} // namespace native_active_four_module
