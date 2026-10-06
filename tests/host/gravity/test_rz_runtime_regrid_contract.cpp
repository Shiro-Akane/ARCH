// Actual Runtime transaction, native J/W state and ledger; no timestep.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/GravityStage.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/gravity/GravityExecution.h"
#include <array>
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
        if(omega!=0.) {
            // Frozen W-parent counterexample, scaled radial cell coordinates
            // leave the exact W 1:7 / V 1:3 ratios unchanged.
            for(int id:control.tree->GetActiveBlocks()){
                auto& b=control.pool->GetBlock(id);
                for(int c=0;c<b.grid.GetTotalSize();++c){
                    b.fluid_state.set(c,{1.,0.,0.,0.,100.});
                    b.fluid_state.X(0,c)=.6;b.fluid_state.X(1,c)=.4;
                }
            }
            int injected=0;
            for(int id:control.tree->GetActiveBlocks()){
                auto& b=control.pool->GetBlock(id);const auto& g=b.grid;
                if(g.x1_min!=0.||g.x2_min!=-.5)continue;
                for(int j=g.Js();j<g.Js()+2;++j)for(int i=g.Is();i<g.Is()+2;++i){
                    const bool high=i==g.Is()+1;
                    b.fluid_state.set(g.GetIndex(i,j,0),
                        {1.,0.,0.,high?-16.:1.,high?2049./16.:9./16.});++injected;
                }
            }
            require(injected==4,"frozen native parent children not injected");
            arch::scheduler::publish_completed_interior(context,runtime.handles(),arch::state::StateSlot::Current);
            runtime.ensure_fluid_ghosts();
            const auto veto_input=totals(control);const auto veto_handles=runtime.handles();
            bool refused=false;
            try{(void)runtime.regrid_native_rz_candidate(0,0.);}
            catch(const std::runtime_error& error){
                refused=std::string(error.what()).find("inadmissible coarse-cell fluid state")!=std::string::npos;
                require(refused,"Runtime parent rejection had unrelated cause");
            }
            if(refused)require(runtime.handles()==veto_handles,"failed Runtime transaction changed identities");
            else {
                require(runtime.handles().size()==5,"frozen veto did not retain only the inadmissible family");
                for(int id:control.tree->GetActiveBlocks()){
                    const auto& block=control.pool->GetBlock(id);
                    if(block.grid.x1_min==0.&&block.grid.x2_min==-.5)
                        {
                        require(block.level==1,"inadmissible frozen parent was published");
                        const auto& g=block.grid;
                        for(int j=g.Js();j<g.Js()+2;++j)for(int i=g.Is();i<g.Is()+2;++i){
                            const int c=g.GetIndex(i,j,0);const bool high=i==g.Is()+1;
                            require(block.fluid_state.mom_w[c]==(high?-16.:1.)
                                &&block.fluid_state.eng[c]==(high?2049./16.:9./16.),
                                "frozen veto repaired or changed original child");
                        }
                    }
                }
            }
            conservation(veto_input,totals(control),"frozen-parent-veto");
            if(field_after_regrid){
                require(runtime.handles().size()==5,"actual mixed field witness missing");
                verify_field("mixed");
                stage->invalidate();
                rejects([&]{gravity.native_rz_potential();},"old mixed field survived topology invalidation");
            }
            std::cout<<"ACTUAL_RZ_RUNTIME_PARENT_VETO_PASS transaction_rejected="<<refused
                <<" active_blocks="<<runtime.handles().size()<<" children=4 parent_m=-111/8 parent_eint=-9/128 time=0 steps=0"<<std::endl;
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
    }return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
