// Original Runtime host finalizer and rollback; explicitly injected failure.
// No replacement transaction, scientific dynamics or production RZ grant.
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "driver/stages/GravityStage.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/gravity/GravityExecution.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
static void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
static bool bits(const std::vector<double>& a,const std::vector<double>& b){
    return a.size()==b.size()&&(a.empty()||std::memcmp(a.data(),b.data(),a.size()*sizeof(double))==0);
}
static bool same_state(const FluidState& a,const FluidState& b){
    return a.block_total_size_==b.block_total_size_&&a.n_species_==b.n_species_
        &&bits(a.rho,b.rho)&&bits(a.mom_u,b.mom_u)&&bits(a.mom_v,b.mom_v)
        &&bits(a.mom_w,b.mom_w)&&bits(a.eng,b.eng)&&bits(a.enuc_rate,b.enuc_rate)
        &&bits(a.mass_fractions,b.mass_fractions);
}

/** Borrowed addresses of the seven actual source-owned Host arrays. */
using SourceAddresses=std::array<const double*,7>;
static SourceAddresses source_addresses(const FluidState& s){
    return {s.rho.data(),s.mom_u.data(),s.mom_v.data(),s.mom_w.data(),
        s.eng.data(),s.enuc_rate.data(),s.mass_fractions.data()};
}

/** Check the real Stage gathers the original in-place-restored source pointers. */
class FreshPointers final:public Physical::Gravity::GravityExecution {
    amr::AMRControl& control_;
    std::shared_ptr<Physical::Gravity::GravityExecution> host_=
        Physical::Gravity::make_host_gravity_execution();
public:
    int gathers=0;
    explicit FreshPointers(amr::AMRControl& c):control_(c){}
    std::shared_ptr<arch::multigrid::CompositeExecution> numeric() const override{return host_->numeric();}
    void run(const Physical::Gravity::GravityWork& work) override {
        if(const auto* gather=std::get_if<Physical::Gravity::GatherDensity>(&work)){
            const auto& active=control_.tree->GetActiveBlocks();
            for(std::size_t b=0;b<active.size();++b)
                require(gather->patches[b]==control_.pool->GetBlock(active[b]).fluid_state.rho.data(),
                    "post-rollback Stage reused expired source pointer");
            ++gathers;
        }
        host_->run(work);
    }
};
int main(int argc,char** argv){
 try{
    require(argc==2&&!std::filesystem::exists(argv[1]),"new persistent directory required");
    SimConfig config;config.grid.geometry="cylindrical";config.grid.dim=2;
    config.grid.nblockx1=2;config.grid.nblockx2=1;config.grid.nblockx3=0;
    config.grid.x1_min=0.;config.grid.x1_max=1.;config.grid.x2_min=-.5;config.grid.x2_max=.5;
    config.grid.amr_max_blocks=16;config.amr.lrefinemax=1;config.amr.lrefinemin=0;
    config.amr.refine_on_rho=true;config.amr.refine_threshold=.001;config.amr.derefine_threshold=.0005;
    config.io.out_dir=argv[1];config.amr.regrid_interval=1;
    config.physics.gravity.boundary="isolated";
    config.physics.gravity.relative_tolerance=1.e-10;
    config.physics.gravity.absolute_tolerance=0.;config.physics.gravity.max_cycles=200;
    SpeciesManager species;species.add_species("a",1.,1.,1.4,1.);species.add_species("b",2.,1.,1.4,1.);
    IdealGas eos(1.4,species);
    amr::AMRControl control(16,2);
    control.tree->InitRootGrid(config,2,GridMetrics::GeometrySemantics::AxisymmetricRz);
    for(int id:control.tree->GetActiveBlocks()){
        auto& b=control.pool->GetBlock(id);b.fluid_state.InitSpecies(2);
        for(int j=0;j<b.grid.GetTotalY();++j)for(int i=0;i<b.grid.GetTotalX();++i){
            const int c=b.grid.GetIndex(i,j,0);
            const double rho=1.+.1*std::cos(2.*arch::constants::math::pi*b.grid.GetPhysicalCoords(i,j,0).x);
            const double radius=GridMetrics::Rz::AngularReconstructionCoordinate(
                b.grid.GetFacePosL(i),b.grid.GetFacePosR(i));
            b.fluid_state.set(c,{rho,0.,0.,rho*radius,100.*rho});
            b.fluid_state.X(0,c)=.6;b.fluid_state.X(1,c)=.4;
        }
    }
    RunState start{};SimulationController counters(config,start);
    BCHandler boundary(config,GridMetrics::GeometrySemantics::AxisymmetricRz);
    arch::driver::DriverRuntime runtime(control,boundary,config,species,counters);
    runtime.bind_native_rz_eos(eos);
    runtime.initialize_topology();
    const auto roots=control.tree->GetActiveBlocks();const auto handles=runtime.handles();
    const int pool_before=control.pool->GetNumActiveBlocks();
    std::vector<FluidState> expected;
    std::vector<arch::state::StateVersion> versions;
    for(std::size_t i=0;i<roots.size();++i){
        expected.push_back(control.pool->GetBlock(roots[i]).fluid_state);
        versions.push_back(runtime.stage_context().ledger.inspect(
            {handles[i],arch::state::StateSlot::Current}).interior.version);
    }
    std::vector<SourceAddresses> original_addresses;
    for(int id:roots)original_addresses.push_back(source_addresses(control.pool->GetBlock(id).fluid_state));
    const auto original_boundary=boundary.snapshot_stage_context();
    int callbacks=0,peak_pool=pool_before;
    for(int attempt=0;attempt<3;++attempt){
        bool exact_failure=false;
        try {
            runtime.regrid_native_rz_candidate(0,0.,[&]{
                ++callbacks;
                require(control.tree->GetActiveBlocks().size()==8,"fault did not run after staged topology activation");
                require(runtime.handles()==handles,"fallible finalizer already published Runtime handles");
                peak_pool=std::max(peak_pool,control.pool->GetNumActiveBlocks());
                // Deliberately corrupt every retained old source field after
                // real staged BC/ghost work. Original backup/abort must restore.
                for(int id:roots){
                    auto& s=control.pool->GetBlock(id).fluid_state;
                    s.set(0,{-777.,-778.,-779.,-780.,-781.});
                    s.enuc_rate[0]=-782.;s.mass_fractions[0]=-783.;
                }
                boundary.configure_stage(3.+attempt,arch::boundary::BoundaryPurpose::Hydro);
                throw std::runtime_error("INTERNAL_RZ_FINALIZER_FAULT");
            });
        }catch(const std::runtime_error& error){
            exact_failure=std::string_view(error.what())=="INTERNAL_RZ_FINALIZER_FAULT";
            if(!exact_failure)throw;
        }
        require(exact_failure,"injected finalizer failure was swallowed");
        require(control.tree->GetActiveBlocks()==roots&&runtime.handles()==handles,"failed finalizer published topology");
        require(control.pool->GetNumActiveBlocks()==pool_before,"failed finalizer leaked staged blocks");
        require(runtime.regrid_records().empty(),"failed finalizer recorded a successful transaction");
        require(boundary.stage_context_matches(original_boundary),"failed finalizer did not restore exact BC snapshot");
        auto context=runtime.stage_context();
        for(std::size_t i=0;i<roots.size();++i){
            const auto& source=control.pool->GetBlock(roots[i]).fluid_state;
            require(same_state(source,expected[i]),"failed finalizer did not restore original source bits");
            require(source_addresses(source)==original_addresses[i],
                "failed finalizer changed one of seven borrowed source array addresses");
            const auto version=context.ledger.inspect({handles[i],arch::state::StateSlot::Current}).interior.version;
            require(version==versions[i],"failed finalizer published new interior version");
            context.ledger.require_readable({handles[i],arch::state::StateSlot::Current},
                {arch::state::ExecutionSide::Host,version,true,true});
        }
    }
    require(callbacks==3,"finalizer callback count");
    // Reject a real staged native thermodynamic closure after completed BC and
    // exchange. Unlike the callback throws above, this hook returns normally:
    // the mandatory shared post-ghost gate must fatally reject the unpublished candidate.
    // The deliberately negative energy is an engineering counterexample, not
    // a physical floor repair or an evolved solution.
    int gate_fault_callbacks=0;bool closure_rejected=false;
    try {
        runtime.regrid_native_rz_candidate(0,0.,[&]{
            ++gate_fault_callbacks;
            require(control.tree->GetActiveBlocks().size()==8,
                "closure counterexample did not run in staged topology");
            require(runtime.handles()==handles,
                "closure counterexample already published Runtime handles");
            auto& late=control.pool->GetBlock(control.tree->GetActiveBlocks().back());
            late.fluid_state.eng[late.grid.GetIndex(late.grid.Is(),late.grid.Js(),0)]=-1.;
        });
    }catch(const std::runtime_error& error){
        closure_rejected=std::string_view(error.what()).find("RZ native closure/EOS rejected")
            !=std::string_view::npos;
        if(!closure_rejected)throw;
    }
    require(gate_fault_callbacks==1&&closure_rejected,
        "actual post-ghost native closure failure was not propagated");
    require(control.tree->GetActiveBlocks()==roots&&runtime.handles()==handles,
        "rejected native closure published staged topology");
    require(control.pool->GetNumActiveBlocks()==pool_before&&runtime.regrid_records().empty(),
        "rejected native closure leaked staged storage or successful record");
    require(boundary.stage_context_matches(original_boundary),"fatal late closure rejection did not restore BC snapshot");
    require(runtime.native_coarsening_veto_records().empty(),"late injected closure failure became a parent veto");
    auto restored=runtime.stage_context();
    for(std::size_t i=0;i<roots.size();++i){
        require(source_addresses(control.pool->GetBlock(roots[i]).fluid_state)==original_addresses[i],
            "late native rejection changed one of seven borrowed source array addresses");
        require(same_state(control.pool->GetBlock(roots[i]).fluid_state,expected[i]),
            "fatal native closure rejection did not restore original source bits");
        require(restored.ledger.inspect({handles[i],arch::state::StateSlot::Current})
            .interior.version==versions[i],"fatal native closure rejection published new interior version");
        restored.ledger.require_readable({handles[i],arch::state::StateSlot::Current},
            {arch::state::ExecutionSide::Host,versions[i],true,true});
    }
    std::cout<<"ACTUAL_RZ_POSTGHOST_GATE_ROLLBACK actual_closure_rejection=1"
        <<" returned_hook=1 unpublished_candidate=1 source_bits_versions_preserved=1\n";
    // Real native field recovery checks fresh restored pointers, all Current
    // versions and the same original complete residual budget before retry.
    Physical::Gravity::SelfGravity gravity(config.physics.gravity);
    auto capture=std::make_shared<FreshPointers>(control);gravity.set_execution(capture);
    using Stage=arch::driver::GravityStage;
    Stage stage(runtime,&gravity,Stage::Qualification::NativeRzCandidate);
    stage.prepare_current(0.,true);
    const auto& assessment=gravity.native_rz_assessment();
    require(capture->gathers==1&&assessment.source.inputs.size()==handles.size(),
        "post-rollback field omitted actual source blocks");
    for(std::size_t i=0;i<handles.size();++i)
        require(assessment.source.inputs[i].block==handles[i]
            &&assessment.source.inputs[i].version==versions[i]
            &&assessment.source.inputs[i].slot==arch::state::StateSlot::Current,
            "post-rollback field lost original current identity");
    require(gravity.native_rz_potential().size()==512,"post-rollback field extent");
    std::cout.precision(17);
    std::cout<<"ACTUAL_RZ_ROLLBACK_FIELD_PASS fresh_pointers=1 current_ledger=1 cells=512"
        <<" total="<<assessment.conditional.total_residual_upper
        <<" safe="<<assessment.conditional.tolerance_safe<<" time=0 steps=0"<<std::endl;
    stage.invalidate();
    bool field_retired=false;try{gravity.native_rz_potential();}
    catch(const std::logic_error&){field_retired=true;}
    require(field_retired,"pre-transaction invalidation retained old field");
    // A fresh real request must succeed without stale staged namespace/resource.
    require(runtime.regrid_native_rz_candidate(0,0.),"valid retry after rollback failed");
    require(runtime.handles().size()==8&&runtime.handles().front().epoch!=handles.front().epoch,
        "valid retry did not publish refined topology");
    require(control.pool->GetNumActiveBlocks()==8,"valid retry pool count");
    require(runtime.regrid_records().size()==1,"valid retry missing successful record");
    auto final=runtime.stage_context();
    bool retired=false;try{final.ledger.inspect({handles.back(),arch::state::StateSlot::Current});}
    catch(const std::logic_error&){retired=true;}
    require(retired,"successful retry retained old source handle");
    for(auto handle:runtime.handles()){
        const auto version=final.ledger.inspect({handle,arch::state::StateSlot::Current}).interior.version;
        final.ledger.require_readable({handle,arch::state::StateSlot::Current},
            {arch::state::ExecutionSide::Host,version,true,true});
    }
    bool gated=false;try{runtime.perform_regrid(0,0.);}catch(const std::logic_error&){gated=true;}
    require(gated,"fault verification lifted production RZ gate");
    std::cout<<"ACTUAL_RZ_RUNTIME_ROLLBACK_PASS attempts=3 source_blocks=2"
        <<" staged_blocks=8 pool_before="<<pool_before<<" peak_pool="<<peak_pool
        <<" source_all_seven_addresses_preserved=1 final_pool=8"
        <<" all_source_arrays_bitwise=1 topology_preserved=1 original_versions=1"
        <<" old_ghost_readable=1 no_staged_leak=1 retry=1 retired_handle=1"
        <<" production_gate_held=1 time=0 steps=0"<<std::endl;
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
