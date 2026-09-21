#include "physics/gravity/GravitySolveTypes.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "numerics/multigrid/HostCompositeMG.h"
#include "driver/stages/GravityStage.h"
#include "driver/runtime/DriverRuntime.h"
#include "amr/AMRControl.h"
#include "physics/gravity/self/SelfGravity.h"
#include <filesystem>
#include <iomanip>
namespace arch::driver {
GravityStage::GravityStage(DriverRuntime& runtime,const Physical::Gravity::IGravityPolicy* policy)
    :runtime_(runtime),gravity_(dynamic_cast<const Physical::Gravity::SelfGravity*>(policy)) {
    if (!gravity_) return;
    const auto& config=runtime.configuration();
    std::filesystem::create_directories(config.io.out_dir);
    diagnostics_.open(config.io.out_dir+"/gravity_solves.tsv");
    if (!diagnostics_) throw std::runtime_error("Cannot open gravity solve diagnostics");
    diagnostics_<<"time\tstage\tepoch\tgeneration\tcells\titerations\trhs_rms\tresidual\ttarget\trho_mean\n"<<std::setprecision(17);
}
state::CompletionToken GravityStage::solve(state::StateSlot slot,const state::StateResidencyLedger& ledger,double time,int stage) {
    gravity_->invalidate();
    const auto& handles=runtime_.handles(); const auto& config=runtime_.configuration();
    if (handles.empty()) throw std::logic_error("Gravity requires active topology");
    if (epoch_!=handles.front().epoch) {
        gravity_->bind(amr::bind_elliptic_mesh(runtime_.control(),config.grid,handles));
        epoch_=handles.front().epoch;
    }
    // A new borrowed storage lease is issued for every solve, even if slots or
    // pool addresses are reused. No publication can survive an expired lease.
    if (++generation_==0) throw std::overflow_error("Gravity density lease exhausted");
    Physical::Gravity::GravitySolveIdentity identity;
    identity.topology=epoch_; identity.input_time=time; identity.gravitational_constant=config.physics.gravity.G_const;
    identity.operator_revision=1;identity.boundary_revision=1;identity.accuracy_revision=1;
    std::vector<Physical::Gravity::GravityDensityView> views;
    const auto& active=runtime_.control().tree->GetActiveBlocks();
    for (std::size_t b=0;b<handles.size();++b) {
        const auto version=ledger.inspect({handles[b],slot}).interior.version;
        ledger.require_readable({handles[b],slot},{state::ExecutionSide::Host,version,true,false});
        const auto& block=runtime_.control().pool->GetBlock(active[b]);
        const auto& fluid=slot==state::StateSlot::Current?block.fluid_state:slot==state::StateSlot::Next?block.state_next:block.state_scratch;
        const auto& grid=block.grid;
        Physical::Gravity::GravityInputIdentity input{handles[b],slot,version,generation_};
        identity.inputs.push_back(input);
        const auto layout=amr::native_scalar_layout(grid);
        views.push_back({input,{fluid.rho.data(),fluid.rho.size(),layout,arch::grid::FieldMemory::Host,generation_}});
    }
    const auto token=gravity_->prepare({identity,views}); const auto& report=gravity_->report();
    diagnostics_<<time<<'\t'<<stage<<'\t'<<epoch_.value<<'\t'<<generation_<<'\t'<<gravity_->potential().size()<<'\t'
        <<report.cycles<<'\t'<<report.rhs_rms<<'\t'<<report.residual<<'\t'<<report.target<<'\t'<<gravity_->density_mean()<<'\n';
    if (!diagnostics_) throw std::runtime_error("Cannot write gravity diagnostics");
    return token;
}
state::CompletionToken GravityStage::prepare(const scheduler::HydroStagePreparationRequest& request) {
    if (!gravity_ || request.side!=state::ExecutionSide::Host) throw std::logic_error("Self-gravity requires the CPU stage route");
    return solve(request.descriptor.input_slot,request.ledger,request.input_time,request.descriptor.stage);
}
void GravityStage::prepare_current(double time) {
    if (gravity_) { auto context=runtime_.stage_context(); solve(state::StateSlot::Current,context.ledger,time,0); }
}
void GravityStage::invalidate() const { if(gravity_) gravity_->invalidate(); }
double GravityStage::timestep() const { return gravity_?gravity_->timestep(runtime_.configuration().numerics.cfl):std::numeric_limits<double>::infinity(); }
std::vector<io::PlotScalarField> GravityStage::plot_fields() const {
    if (!gravity_) return {};
    std::vector<io::PlotScalarField> fields{{"GPOT",gravity_->potential()}};
    const char* names[]{"GACX","GACY","GACZ"};
    for(int a=0;a<runtime_.configuration().grid.dim;++a) fields.push_back({names[a],gravity_->acceleration()[a]});
    return fields;
}
}
