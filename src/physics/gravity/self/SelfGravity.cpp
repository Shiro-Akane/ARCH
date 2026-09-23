#include "physics/gravity/GravitySolveTypes.h"
#include "physics/gravity/GravityBoundary.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "numerics/multigrid/HostCompositeMG.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/constant/PhysicalConstants.h"
#include "physics/gravity/self/GravityWorkspace.h"
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <chrono>
namespace Physical::Gravity {
SelfGravity::SelfGravity(GravityConfig config):config_(std::move(config)) {
    if ((config_.boundary!="periodic" && config_.boundary!="isolated") || !std::isfinite(config_.G_const) || config_.G_const<=0.
        || !std::isfinite(config_.relative_tolerance) || config_.relative_tolerance<=0. || config_.relative_tolerance>=1.
        || !std::isfinite(config_.absolute_tolerance) || config_.absolute_tolerance<0. || config_.max_cycles<1)
        throw std::invalid_argument("Invalid self-gravity physical or convergence controls");
}
SelfGravity::~SelfGravity()=default;
SelfGravity::Workspace& SelfGravity::workspace() const {
    if (!work_) throw std::logic_error("Self-gravity mesh is not bound");
    return *work_;
}
void SelfGravity::bind(amr::EllipticMeshBinding binding) const {
    invalidate();
    if (binding.grids.empty() || binding.grids.size()!=binding.handles.size()
        || binding.cells.size()!=binding.storage.size()) throw std::invalid_argument("Invalid gravity mesh binding");
    std::size_t cell=0;
    for (std::size_t b=0;b<binding.grids.size();++b) {
        if (!binding.grids[b] || !amr::is_valid(binding.handles[b])
            || binding.handles[b].epoch!=binding.handles.front().epoch)
            throw std::invalid_argument("Invalid gravity patch binding");
        const auto& grid=*binding.grids[b];
        for (int k=grid.Ks();k<grid.Ke();++k) for (int j=grid.Js();j<grid.Je();++j) for (int i=grid.Is();i<grid.Ie();++i,++cell)
            if (cell>=binding.storage.size() || binding.storage[cell].block!=b
                || binding.storage[cell].offset!=grid.GetIndex(i,j,k))
                throw std::invalid_argument("Gravity cells do not match native active storage order");
    }
    if (cell!=binding.cells.size()) throw std::invalid_argument("Extra cells in gravity mesh binding");
    work_=std::make_unique<Workspace>(std::move(binding),config_.boundary=="periodic"
        ? arch::elliptic::BoundaryKind::Periodic : arch::elliptic::BoundaryKind::Dirichlet,execution_?execution_:(execution_=make_host_gravity_execution()));
}
void SelfGravity::invalidate() const noexcept { if(work_) { work_->ready=false; work_->downloaded=false; work_->validity.invalidate(); } }
arch::state::CompletionToken SelfGravity::prepare(const GravitySolveRequest& request) const {
    invalidate();
    if (!work_) throw std::logic_error("Self-gravity mesh is not bound");
    auto& w=*work_; const auto& op=w.solver.op(); const auto& identity=request.identity;
    if (request.blocks.size()!=w.patches.size() || identity.inputs.size()!=w.patches.size()
        || identity.topology!=w.binding.handles.front().epoch || identity.gravitational_constant!=config_.G_const
        || identity.operator_revision!=1 || identity.boundary_revision!=1 || identity.accuracy_revision!=1)
        throw std::logic_error("Self-gravity solve identity differs from bound mesh/configuration");
    for (std::size_t b=0;b<w.patches.size();++b) {
        const auto& view=request.blocks[b];
        if (view.identity!=identity.inputs[b] || view.identity.block!=w.binding.handles[b]
            || view.density.memory!=(w.solver.execution().device()?arch::grid::FieldMemory::Device:arch::grid::FieldMemory::Host) || !view.density.data
            || view.density.layout!=amr::native_scalar_layout(*w.binding.grids[b])
            || view.density.storage_generation!=view.identity.storage_generation
            || view.density.size!=static_cast<std::size_t>(w.binding.grids[b]->GetTotalSize()))
            throw std::logic_error("Self-gravity density view does not match its dependency");
    }
    using Clock=std::chrono::steady_clock;
    const auto started=Clock::now();
    auto& e=w.solver.execution();std::vector<const double*> pointers;
    for(const auto& view:request.blocks)pointers.push_back(view.density.data);
    e.copy(w.density_pointers.data,pointers.data(),sizeof(double*)*pointers.size(),arch::multigrid::Transfer::Upload);
    w.execution->run(GatherDensity{op.size(),w.cells.data,w.density_pointers.data,w.density.data});
    w.max_density=e.maximum(w.density);
    if(!std::isfinite(w.max_density))throw std::invalid_argument("Self gravity requires finite positive active density");
    w.mean=w.solver.mean(w.density);
    const double factor=-4.*arch::constants::math::pi*config_.G_const;
    e.linear(w.rhs,factor,w.density,0.,{},w.nodes.size?0.:-factor*w.mean);
    if(w.nodes.size){
        for(auto it=w.layers.rbegin();it!=w.layers.rend();++it)
            w.execution->run(UpdateMoments{it->size,it->data,w.nodes.data,w.moments.data,w.density.data,w.volumes.data});
        w.execution->run(EvaluateBoundary{w.points.size,w.points.data,w.nodes.data,w.moments.data,w.nodes.size,config_.G_const,w.boundary_values.data});
        w.solver.boundary_rhs(w.rhs,w.boundary_values);
    }
    w.solver.project(w.rhs);
    e.fence();
    const auto source_ready=Clock::now();
    w.report=w.solver.solve(w.rhs,{config_.relative_tolerance,config_.absolute_tolerance,config_.max_cycles});
    if(w.report.status!=arch::multigrid::SolveStatus::Converged){
        std::ostringstream message;message<<std::setprecision(17)<<"Self-gravity Poisson solve failed: iterations="<<w.report.cycles
            <<" residual="<<w.report.residual<<" target="<<w.report.target<<" rhs="<<w.report.rhs_rms;throw std::runtime_error(message.str());}
    e.fence();
    const auto poisson_ready=Clock::now();
    w.solver.gradient(w.solver.resident_potential(),w.face_gradient,w.boundary_values);
    e.run(arch::multigrid::RowsWork{w.sides.size,w.side_gather.view(),w.face_gradient.data,w.sides.data});
    e.run(arch::multigrid::RowsWork{w.patch_faces.size,w.patch_gather.view(),w.sides.data,w.patch_faces.data});
    w.execution->run(CellAcceleration{op.size(),op.base().dimension,w.cells.data,w.sides.data,w.g.data,w.inverse_dt_squared.data});
    w.max_acceleration_ratio=e.maximum(w.inverse_dt_squared);
    if(!std::isfinite(w.max_acceleration_ratio))throw std::runtime_error("Nonfinite self-gravity force");
    e.fence();
    w.timings={std::chrono::duration<double>(source_ready-started).count(),
        std::chrono::duration<double>(poisson_ready-source_ready).count(),
        std::chrono::duration<double>(Clock::now()-poisson_ready).count()};
    for(std::size_t b=0;b<w.patches.size();++b)w.patches[b].density=pointers[b];
    if (++w.generation==0) throw std::overflow_error("Self-gravity publication counter exhausted");
    w.source=identity;
    arch::state::CompletionToken token{w.generation,arch::state::CompletionState::Complete};
    w.validity.publish({identity,w.generation,token}); w.ready=true; return token;
}
void SelfGravity::set_execution(std::shared_ptr<GravityExecution> execution) const {
    if(!execution||execution_==execution)return;
    invalidate();if(work_)work_->solver.execution().fence();execution_=std::move(execution);
    if(work_){auto binding=std::move(work_->binding);work_.reset();bind(std::move(binding));}
}
GravityPatchView SelfGravity::patch_view(std::size_t block) const {workspace().require();return work_->patches.at(block);}
std::size_t SelfGravity::cell_count() const {return workspace().solver.op().size();}
double SelfGravity::timestep(double cfl) const {
    workspace().require();const auto& w=*work_;
    if(!std::isfinite(cfl)||cfl<=0.||cfl>1.)throw std::invalid_argument("Invalid gravity CFL");
    return cfl/std::sqrt(std::max(4.*arch::constants::math::pi*config_.G_const*w.max_density,w.max_acceleration_ratio));
}
const std::vector<double>& SelfGravity::potential() const {workspace().download();return work_->host_phi;}
const std::array<std::vector<double>,3>& SelfGravity::acceleration() const {workspace().download();return work_->host_g;}
const arch::multigrid::SolveReport& SelfGravity::report() const {workspace().require();return work_->report;}
double SelfGravity::density_mean() const {workspace().require();return work_->mean;}
const SelfGravity::Timings& SelfGravity::timings() const {workspace().require();return work_->timings;}
void SelfGravity::add_sources_on_patch(std::vector<FluidVector>& delta,const FluidState& state,
    const Grid& grid,double dt,void*) const {
    const auto& patch=workspace().patch(grid,state); const int stride[]{1,grid.stride_y,grid.stride_z};
    for (int k=grid.Ks();k<grid.Ke();++k) for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
        const int c=grid.GetIndex(i,j,k); double* momentum[]{&delta[c].mom_u,&delta[c].mom_v,&delta[c].mom_w};
        for(int a=0;a<grid.dim;++a) *momentum[a]+=gravity_momentum(patch.faces[a][c],patch.faces[a][c+stride[a]],state.rho[c],dt);
    }
}
void SelfGravity::add_flux_work_on_patch(std::vector<FluidVector>& delta,const std::vector<FluidVector>& flux,
    const FluidState& state,const Grid& grid,double dt,int axis) const {
    const auto& patch=workspace().patch(grid,state); const int stride=axis==0?1:axis==1?grid.stride_y:grid.stride_z;
    for (int k=grid.Ks();k<grid.Ke();++k) for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
        const int c=grid.GetIndex(i,j,k);
        delta[c].eng+=gravity_flux_work(patch.faces[axis][c],patch.faces[axis][c+stride],flux[c].rho,flux[c+stride].rho,dt);
    }
}
}
