// Actual Runtime transaction, native J/W state and ledger; no timestep.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/GravityStage.h"
#include "physics/boundary/UserBoundary.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/gravity/GravityExecution.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
static void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
template<class F> static void rejects(F f,const char* m){
    bool failed=false;try{f();}catch(const std::exception&){failed=true;}require(failed,m);
}
struct Totals {long double mass=0,energy=0,j=0,absj=0,x0=0,x1=0;};
// Independent native integrals from stored face coordinates; not the transfer
// implementation's weights. Scalars only, no alternate evolved state.
static Totals totals(const amr::AMRControl& control){
    constexpr long double pi=3.141592653589793238462643383279502884L;
    Totals t;
    for(int id:control.tree->GetActiveBlocks()){
        const auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
        for(int j=g.Js();j<g.Je();++j)for(int i=g.Is();i<g.Ie();++i){
            const int c=g.GetIndex(i,j,0);
            const long double lo=g.GetFacePosL(i),hi=g.GetFacePosR(i),dz=g.dx2;
            const long double v=pi*(hi*hi-lo*lo)*dz,w=2.L*pi/3.L*(hi*hi*hi-lo*lo*lo)*dz;
            t.mass+=b.fluid_state.rho[c]*v;t.energy+=b.fluid_state.eng[c]*v;
            const long double angular=b.fluid_state.mom_w[c]*w;
            t.j+=angular;t.absj+=std::abs(angular);
            t.x0+=b.fluid_state.rho[c]*b.fluid_state.X(0,c)*v;
            t.x1+=b.fluid_state.rho[c]*b.fluid_state.X(1,c)*v;
        }
    }return t;
}
static void conservation(const Totals& before,const Totals& after,const char* label){
    const auto relative=[](long double a,long double b){return std::abs(a-b)/std::abs(a);};
    require(before.mass>0&&before.energy>0&&before.x0>0&&before.x1>0,"invalid conservation reference");
    // Reuse existing 1e-12 short-transfer budgets; zero angular normalization
    // has its own exact-zero contract, never a tiny replacement denominator.
    const long double jerror=before.absj==0?std::abs(after.j):std::abs(after.j-before.j)/before.absj;
    if(before.absj==0)require(after.j==0&&after.absj==0,"zero rotation generated angular momentum");
    require(jerror<=1.e-12L,"Runtime RZ angular transfer budget exceeded");
    require(relative(before.mass,after.mass)<=1.e-12L,"Runtime RZ mass transfer budget exceeded");
    require(relative(before.energy,after.energy)<=1.e-12L,"Runtime RZ energy transfer budget exceeded");
    require(relative(before.x0,after.x0)<=1.e-12L&&relative(before.x1,after.x1)<=1.e-12L,
        "Runtime RZ species transfer budget exceeded");
    std::cout<<std::setprecision(20)<<"ACTUAL_RZ_REGRID_CONSERVATION phase="<<label
        <<" mass="<<relative(before.mass,after.mass)<<" energy="<<relative(before.energy,after.energy)
        <<" angular="<<jerror<<" x0="<<relative(before.x0,after.x0)
        <<" x1="<<relative(before.x1,after.x1)<<" zero_rotation="<<(before.absj==0)<<std::endl;
}

/** Borrowed addresses of the seven actual source-owned Host arrays. */
using SourceAddresses=std::array<const double*,7>;
static SourceAddresses source_addresses(const FluidState& s){
    return {s.rho.data(),s.mom_u.data(),s.mom_v.data(),s.mom_w.data(),
        s.eng.data(),s.enuc_rate.data(),s.mass_fractions.data()};
}

/** Independent quartic physical integrals; common 2*pi*dz cancels in U.
 * M=int rho |r|dr, J=int rho r^3dr, E=e0*M+|J|/2 for Omega=1.
 * Negative logical ghosts have odd m_phi and even rho/E; padding is not read.
 */
static FluidVector quartic_native_cell(const Grid& grid,int i){
    const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
    require((lo>=0||hi<=0)&&hi>lo,"quartic reference cell straddles the axis");
    const auto power=[](long double x,int n){long double v=1.;for(int k=0;k<n;++k)v*=x;return v;};
    const auto integral=[&](int n){return (power(hi,n+1)-power(lo,n+1))/(n+1);};
    const long double sign=hi<=0?-1.L:1.L;
    const long double volume=sign*integral(1),mass=sign*(32.L*integral(1)-integral(5));
    const long double angular_measure=integral(2),angular=32.L*integral(3)-integral(7);
    constexpr long double internal=1.L/33554432.L;
    return {double(mass/volume),0.,0.,double(angular/angular_measure),
        double((internal*mass+.5L*std::abs(angular))/volume)};
}
/** Compare genuine completed logical cells with independent analytic means.
 * Reuse the original 1e-12 physical budget; do not compare storage padding.
 */
static void quartic_completed_reference(const amr::AMRControl& control){
    for(int id:control.tree->GetActiveBlocks()){
        const auto& block=control.pool->GetBlock(id);const auto& g=block.grid;
        require(g.Ie()-g.Is()==16&&g.GetTotalX()==24&&g.GetTotalY()==24,
            "quartic reference requires actual 16-active/24-logical patches");
        require(g.dx1==1./32.,"quartic source is not the real n32 fine mesh");
        for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i){
            const auto expected=quartic_native_cell(g,i);const int cell=g.GetIndex(i,j,0);
            const auto actual=block.fluid_state.get(cell);
            const auto close=[](double value,double expected){
                return std::isfinite(value)&&std::abs((static_cast<long double>(value)-expected)/expected)<=1.e-12L;};
            require(close(actual.rho,expected.rho)&&close(actual.eng,expected.eng)
                &&close(actual.mom_w,expected.mom_w),"actual quartic boundary/ghost changed analytic M/J/E");
            require(actual.mom_u==0.&&actual.mom_v==0.
                &&block.fluid_state.X(0,cell)==.6&&block.fluid_state.X(1,cell)==.4,
                "actual quartic boundary/ghost changed velocity or composition");
        }
    }
}
/** Real four-fine Runtime source, actual analytic user BC, conservative parent
 * attempt and typed early thermal veto; no evolved step or public RZ grant.
 * The exact independent Fraction reference is rho=32-r^4, Omega=1,e0=2^-25.
 * All fine logical states are admissible; several coarse interiors are not.
 */
static void cold_quartic_parent_veto(const char* output){
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    SimConfig config;config.grid.geometry="cylindrical";config.grid.dim=2;
    config.grid.nblockx1=1;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=-.5;config.grid.x2_max=.5;
    config.grid.amr_max_blocks=16;config.amr.lrefinemin=0;config.amr.lrefinemax=1;
    // An empty physical indicator selection requests derefinement through the
    // real maximum=0 policy; no source flag or equation is manually replaced.
    config.amr.refine_on_rho=false;config.amr.refine_threshold=.001;config.amr.derefine_threshold=.0005;
    config.amr.regrid_interval=1;config.io.out_dir=output;
    config.grid.x1r_boundary_type="user";
    config.grid.x2l_boundary_type="user";config.grid.x2r_boundary_type="user";
    SpeciesManager species;species.add_species("a",1.,1.,1.4,1.);species.add_species("b",2.,1.,1.4,1.);
    IdealGas eos(1.4,species);
    arch::boundary::ResolvedUserBoundaries callbacks;
    callbacks.identity="quartic-native-coarsening-reference";
    callbacks.physical=[](const arch::boundary::PhysicalBoundaryContext& context){
        const double r=context.ghost_point.x;
        require(std::isfinite(r)&&r>=0.,"quartic physical callback received invalid radius");
        PrimitiveData point;point.rho=32.-r*r*r*r;point.w=r;
        point.SetTemperature(1./33554432.); // actual species Cv=1, hence e0=2^-25
        point.mass_fractions={.6,.4};
        arch::boundary::PhysicalBoundaryData data;data.hydro=std::move(point);return data;
    };
    arch::boundary::ScopedUserBoundarySelection selected(std::move(callbacks),config,species);
    amr::AMRControl control(16,2);
    // Real saved-leaf topology API creates four level-one 16x16 patches. No
    // invalid cold coarse root is initialized, accepted or used as a donor.
    control.tree->LoadLeafGrid(config,2,{1,1,1,1},{0,1,0,1},{0,0,1,1},{0,0,0,0},rz);
    for(int id:control.tree->GetActiveBlocks()){
        auto& block=control.pool->GetBlock(id);const auto& g=block.grid;
        for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i){
            const int cell=g.GetIndex(i,j,0);block.fluid_state.set(cell,quartic_native_cell(g,i));
            block.fluid_state.X(0,cell)=.6;block.fluid_state.X(1,cell)=.4;
        }
    }
    RunState start{};SimulationController counters(config,start);
    BCHandler boundary(config,rz);boundary.bind(eos,species);
    arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.bind_native_rz_eos(eos);runtime.initialize_topology();
    quartic_completed_reference(control);
    const auto source_ids=control.tree->GetActiveBlocks();const auto handles=runtime.handles();
    require(handles.size()==4&&control.pool->GetNumActiveBlocks()==4,"quartic source topology is not four fine patches");
    const auto before=totals(control);constexpr long double pi=3.141592653589793238462643383279502884L;
    const auto relative=[](long double value,long double expected){return std::abs((value-expected)/expected);};
    require(relative(before.mass,95.L*pi/3.L)<=1.e-12L
        &&relative(before.j,63.L*pi/4.L)<=1.e-12L
        &&relative(before.energy,(95.L/3.L/33554432.L+63.L/8.L)*pi)<=1.e-12L,
        "quartic source M/J/E do not match independent antiderivatives");
    std::vector<FluidState> original;std::vector<SourceAddresses> addresses;
    std::vector<arch::state::StateVersion> versions;
    for(std::size_t n=0;n<source_ids.size();++n){
        const auto& state=control.pool->GetBlock(source_ids[n]).fluid_state;
        original.push_back(state);addresses.push_back(source_addresses(state));
        versions.push_back(runtime.stage_context().ledger.inspect({handles[n],arch::state::StateSlot::Current}).interior.version);
    }
    const auto boundary_frame=boundary.snapshot_stage_context();
    require(!runtime.regrid_native_rz_candidate(0,0.),"inadmissible quartic coarse parent was published");
    const auto& records=runtime.native_coarsening_veto_records();
    require(records.size()==1,"real quartic restriction did not yield one exact parent veto");
    const auto& record=records.front();
    require(record.parent==amr::LogicalBlockKey{2,0,0,0,0}
        &&record.kind==arch::driver::NativeCoarseningVetoKind::EffectiveThermal
        &&record.scope.from_epoch==handles.front().epoch
        &&record.scope.to_epoch!=record.scope.from_epoch&&record.scope.transaction_id!=0
        &&record.target.epoch==record.scope.to_epoch&&amr::is_valid(record.target)
        &&record.version==arch::state::StateVersion{},"quartic veto lost authentic early parent/scope/source identity");
    require(record.diagnostic.has_value(),"quartic veto omitted actual thermal diagnostic");
    const auto& d=*record.diagnostic;
    require(d.phase==RzThermodynamics::AcceptancePhase::effective_thermal&&d.inertia_mapping_valid
        &&d.status==arch::state::Status::unresolved_energy
        &&d.i>=5&&d.i<19&&d.j>=4&&d.j<20&&d.node==-1
        &&d.index==d.j*amr::PAD_NX+d.i,"quartic veto was not an actual migrated active-interior thermal failure");
    require(runtime.handles()==handles&&control.tree->GetActiveBlocks()==source_ids
        &&control.pool->GetNumActiveBlocks()==4&&boundary.stage_context_matches(boundary_frame),
        "quartic veto changed source topology/pool or BC frame");
    auto restored=runtime.stage_context();
    for(std::size_t n=0;n<source_ids.size();++n){
        const auto& state=control.pool->GetBlock(source_ids[n]).fluid_state;
        require(source_addresses(state)==addresses[n],"quartic veto invalidated a borrowed source array address");
        const auto equal=[](const auto& a,const auto& b){return a.size()==b.size()&&(a.empty()||std::memcmp(a.data(),b.data(),a.size()*sizeof(double))==0);};
        const auto& old=original[n];
        require(state.block_total_size_==old.block_total_size_&&state.n_species_==old.n_species_
            &&equal(state.rho,old.rho)&&equal(state.mom_u,old.mom_u)&&equal(state.mom_v,old.mom_v)
            &&equal(state.mom_w,old.mom_w)&&equal(state.eng,old.eng)&&equal(state.enuc_rate,old.enuc_rate)
            &&equal(state.mass_fractions,old.mass_fractions),"quartic veto changed an accepted source array bit");
        require(restored.ledger.inspect({handles[n],arch::state::StateSlot::Current}).interior.version==versions[n],
            "quartic veto changed an accepted source version");
        restored.ledger.require_readable({handles[n],arch::state::StateSlot::Current},
            {arch::state::ExecutionSide::Host,versions[n],true,true});
    }
    conservation(before,totals(control),"quartic-native-parent-veto");
    std::cout<<"ACTUAL_RZ_RUNTIME_PARENT_VETO_PASS analytic_quartic=1 source_fine_eos=1"
        <<" exact_parent=1 early_thermal=1 active_blocks=4 source_arrays_addresses_bits=1"
        <<" source_versions_bc_frame=1 physical_e0=2^-25 time=0 steps=0\n";
}

int main(int argc,char** argv){
 try{
    require(argc==2||(argc==3&&std::string_view(argv[2])=="--field-after-regrid"),
        "new persistent directory and optional field-after-regrid required");
    const bool field_after_regrid=argc==3;
    require(!std::filesystem::exists(argv[1]),"new directory required");
    for(double omega:{0.,1.}){
        if(field_after_regrid&&omega==0.)continue;
        SimConfig config;config.grid.geometry="cylindrical";config.grid.dim=2;
        config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
        config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=-.5;config.grid.x2_max=.5;
        config.grid.amr_max_blocks=16;config.amr.lrefinemax=1;config.amr.lrefinemin=0;
        config.amr.refine_on_rho=true;config.amr.refine_threshold=.001;config.amr.derefine_threshold=.0005;
        config.amr.regrid_interval=1;config.io.out_dir=argv[1];
        if(field_after_regrid){
            config.physics.gravity.boundary="isolated";
            config.physics.gravity.relative_tolerance=1.e-10;
            config.physics.gravity.absolute_tolerance=0.;config.physics.gravity.max_cycles=200;
        }
        SpeciesManager species;species.add_species("a",1.,1.,1.4,1.);species.add_species("b",2.,1.,1.4,1.);
        IdealGas eos(1.4,species);
        amr::AMRControl control(16,2);
        control.tree->InitRootGrid(config,2,GridMetrics::GeometrySemantics::AxisymmetricRz);
        for(int id:control.tree->GetActiveBlocks()){
            auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
            b.fluid_state.InitSpecies(2);
            for(int j=0;j<g.GetTotalY();++j)for(int i=0;i<g.GetTotalX();++i){
                const int c=g.GetIndex(i,j,0);
                const double rho=1.+.1*std::cos(2.*arch::constants::math::pi*g.GetPhysicalCoords(i,j,0).x);
                const double radius=GridMetrics::Rz::AngularReconstructionCoordinate(g.GetFacePosL(i),g.GetFacePosR(i));
                b.fluid_state.set(c,{rho,0.,0.,omega*rho*radius,100.*rho});
                b.fluid_state.X(0,c)=.6;b.fluid_state.X(1,c)=.4;
            }
        }
        RunState start{};SimulationController counters(config,start);
        BCHandler boundary(config,GridMetrics::GeometrySemantics::AxisymmetricRz);
        arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
        runtime.bind_native_rz_eos(eos);
        runtime.initialize_topology();
        Physical::Gravity::SelfGravity gravity(config.physics.gravity);
        using Stage=arch::driver::GravityStage;
        std::unique_ptr<Stage> stage;
        if(field_after_regrid)stage=std::make_unique<Stage>(runtime,&gravity,Stage::Qualification::NativeRzCandidate);
        const auto verify_field=[&](const char* phase){
            stage->prepare_current(0.,true);
            const auto& assessment=gravity.native_rz_assessment();
            auto live=runtime.stage_context();
            require(assessment.source.topology==runtime.handles().front().epoch
                &&assessment.source.inputs.size()==runtime.handles().size(),
                "rebound native field retained old topology/dependencies");
            for(std::size_t n=0;n<runtime.handles().size();++n){
                const auto& input=assessment.source.inputs[n];
                require(input.block==runtime.handles()[n]&&input.slot==arch::state::StateSlot::Current
                    &&input.version==live.ledger.inspect({runtime.handles()[n],input.slot}).interior.version,
                    "rebound field did not consume actual current lease");
            }
            require(gravity.native_rz_potential().size()==256*runtime.handles().size(),
                "rebound native field extent mismatch");
            for(const auto& component:gravity.native_rz_acceleration())
                for(double value:component)require(std::isfinite(value),"rebound native force nonfinite");
            rejects([&]{stage->plot_fields();},"rebound candidate acquired physical output grant");
            rejects([&]{gravity.patch_view(runtime.handles().size()-1);},"rebound candidate acquired Hydro grant");
            std::cout<<std::setprecision(17)<<"ACTUAL_RZ_REGRID_FIELD_PASS phase="<<phase
                <<" blocks="<<runtime.handles().size()<<" cells="<<gravity.native_rz_potential().size()
                <<" epoch="<<assessment.source.topology.value
                <<" total="<<assessment.conditional.total_residual_upper
                <<" safe="<<assessment.conditional.tolerance_safe<<" time=0 steps=0"<<std::endl;
        };
        rejects([&]{runtime.perform_regrid(0,0.);},"production RZ regrid gate lifted");
        const auto root_handles=runtime.handles();const auto root=totals(control);
        require(runtime.regrid_native_rz_candidate(0,0.),"Runtime RZ refinement missing");
        require(runtime.handles().size()>root_handles.size(),"Runtime RZ leaf count did not increase");
        require(runtime.handles().front().epoch!=root_handles.front().epoch,"refine epoch unchanged");
        conservation(root,totals(control),"refine");
        auto context=runtime.stage_context();
        rejects([&]{context.ledger.inspect({root_handles.back(),arch::state::StateSlot::Current});},
            "refined ledger retained old root handle");
        for(auto handle:runtime.handles()){
            const auto v=context.ledger.inspect({handle,arch::state::StateSlot::Current}).interior.version;
            context.ledger.require_readable({handle,arch::state::StateSlot::Current},
                {arch::state::ExecutionSide::Host,v,true,true});
        }
        const auto refined_handles=runtime.handles();
        if(field_after_regrid){
            verify_field("refined");stage->invalidate();
            rejects([&]{gravity.native_rz_potential();},"old refined field survived invalidation");
        }
        // An explicitly new engineering input requests ordinary coarsening.
        // Compare this input's conserved integrals before/after, not against
        // the prior nonuniform physical state (no evolution claim).
        for(int id:control.tree->GetActiveBlocks()){
            auto& b=control.pool->GetBlock(id);
            for(int c=0;c<b.grid.GetTotalSize();++c){
                b.fluid_state.set(c,{1.,0.,0.,omega,100.});
                b.fluid_state.X(0,c)=.6;b.fluid_state.X(1,c)=.4;
            }
        }
        auto coarsen_context=runtime.stage_context();
        arch::scheduler::publish_completed_interior(coarsen_context,runtime.handles(),arch::state::StateSlot::Current);
        runtime.ensure_fluid_ghosts();
        const auto coarsen_input=totals(control);
        require(runtime.regrid_native_rz_candidate(0,0.),"Runtime RZ coarsening missing");
        require(runtime.handles().size()==root_handles.size(),"Runtime RZ coarsening root count");
        conservation(coarsen_input,totals(control),"coarsen");
        if(field_after_regrid)verify_field("coarse");
        auto coarse=runtime.stage_context();
        rejects([&]{coarse.ledger.inspect({refined_handles.back(),arch::state::StateSlot::Current});},
            "coarsened ledger retained refined handle");
        const auto stable=runtime.handles();const auto stable_totals=totals(control);
        require(!runtime.regrid_native_rz_candidate(0,0.),"constant Runtime RZ no-change changed topology");
        require(runtime.handles()==stable,"no-change replaced identity");
        conservation(stable_totals,totals(control),"no-change");
        rejects([&]{runtime.perform_regrid(0,0.);},"candidate transaction enabled production RZ");
        require(runtime.regrid_records().size()>=3,"Runtime regrid records missing");
        std::cout<<"ACTUAL_RZ_RUNTIME_REGRID_PASS omega="<<omega<<" root_blocks=2 refined_blocks="
            <<refined_handles.size()<<" coarse_blocks=2 actual_ledger=1 old_handle_rejected=1"
            <<" no_change_identity=1 production_gate_held=1 time=0 steps=0"<<std::endl;
    }
    cold_quartic_parent_veto(argv[1]);
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
