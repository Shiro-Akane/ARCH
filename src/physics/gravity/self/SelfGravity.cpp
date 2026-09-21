#include "physics/gravity/GravitySolveTypes.h"
#include "amr/elliptic/EllipticMeshAdapter.h"
#include "numerics/multigrid/HostCompositeMG.h"
#include "physics/gravity/self/SelfGravity.h"
#include "physics/constant/PhysicalConstants.h"
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <sstream>
#include <iomanip>
namespace Physical::Gravity {
struct SelfGravity::Workspace {
    amr::EllipticMeshBinding binding;
    arch::multigrid::HostCompositeMG solver;
    GravityFieldValidity validity;
    GravitySolveIdentity source;
    std::uint64_t generation=0;
    bool ready=false;
    struct Patch { const double* density=nullptr; std::array<std::vector<double>,3> faces; };
    std::vector<Patch> patches;
    std::unordered_map<const Grid*,std::size_t> lookup;
    std::vector<double> phi;
    std::array<std::vector<double>,3> g;
    arch::multigrid::SolveReport report;
    double mean=0., max_density=0.;
    explicit Workspace(amr::EllipticMeshBinding value)
        : binding(std::move(value)),solver(binding.base,binding.cells) {
        patches.resize(binding.grids.size());
        for (std::size_t b=0;b<patches.size();++b) lookup.emplace(binding.grids[b],b);
    }
    void require() const {
        if (!ready || !validity.matches(source,generation)) throw std::logic_error("Self-gravity field is not published for this input");
    }
    const Patch& patch(const Grid& grid,const FluidState& state) const {
        require(); auto it=lookup.find(&grid);
        if (it==lookup.end() || patches[it->second].density!=state.rho.data())
            throw std::logic_error("Self-gravity patch uses a different density allocation/slot");
        return patches[it->second];
    }
};
SelfGravity::SelfGravity(GravityConfig config):config_(std::move(config)) {
    if (config_.boundary!="periodic" || !std::isfinite(config_.G_const) || config_.G_const<=0.
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
    work_=std::make_unique<Workspace>(std::move(binding));
}
void SelfGravity::invalidate() const noexcept { if(work_) { work_->ready=false; work_->validity.invalidate(); } }
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
            || view.density.memory!=arch::grid::FieldMemory::Host || !view.density.data
            || view.density.layout!=amr::native_scalar_layout(*w.binding.grids[b])
            || view.density.storage_generation!=view.identity.storage_generation
            || view.density.size!=static_cast<std::size_t>(w.binding.grids[b]->GetTotalSize()))
            throw std::logic_error("Self-gravity density view does not match its dependency");
    }
    std::vector<double> density(op.size()),rhs(op.size()); w.max_density=0.;
    for (int c=0;c<op.size();++c) {
        const auto binding=w.binding.storage[c]; const double rho=request.blocks[binding.block].density.data[binding.offset];
        if (!(rho>0.) || !std::isfinite(rho)) throw std::invalid_argument("Self gravity requires finite positive active density");
        density[c]=rho; w.max_density=std::max(w.max_density,rho);
    }
    w.mean=op.mean(density);
    const double factor=-4.*arch::constants::math::pi*config_.G_const; // A=-Laplacian
    for (int c=0;c<op.size();++c) rhs[c]=factor*(density[c]-w.mean);
    op.project(rhs); // Remove cancellation roundoff after subtracting the physical background.
    auto solution=w.solver.solve(rhs,{config_.relative_tolerance,config_.absolute_tolerance,config_.max_cycles});
    w.report=solution.report;
    if (w.report.status!=arch::multigrid::SolveStatus::Converged) {
        std::ostringstream message;
        message<<std::setprecision(17)<<"Self-gravity Poisson solve failed: iterations="<<w.report.cycles
            <<" residual="<<w.report.residual<<" target="<<w.report.target<<" rhs="<<w.report.rhs_rms;
        throw std::runtime_error(message.str());
    }
    w.phi=std::move(solution.potential);
    for (auto& axis:w.g) axis.assign(op.size(),0.);
    // Accumulate low/high face values separately. Adjacent local cells share a
    // face address, so assigning through per-cell sides avoids counting it twice.
    std::vector<std::array<double,6>> side(op.size());
    for (const auto& face:op.faces()) {
        const double gravity=-op.face_gradient(w.phi,face);
        if (!std::isfinite(gravity)) throw std::runtime_error("Nonfinite self-gravity force");
        for (int c:{face.left,face.right}) {
            const double weight=face.area*op.width(c,face.axis)/op.volumes()[c];
            side[c][2*face.axis+(c==face.left ? 1 : 0)]+=weight*gravity;
        }
    }
    for (std::size_t b=0;b<w.patches.size();++b) {
        auto& patch=w.patches[b]; patch.density=request.blocks[b].density.data;
        for (auto& axis:patch.faces) axis.assign(w.binding.grids[b]->GetTotalSize(),0.);
    }
    for (int c=0;c<op.size();++c) {
        const auto binding=w.binding.storage[c]; const auto& grid=*w.binding.grids[binding.block];
        const int stride[]{1,grid.stride_y,grid.stride_z};
        for (int a=0;a<grid.dim;++a) {
            w.g[a][c]=0.5*(side[c][2*a]+side[c][2*a+1]);
            auto& faces=w.patches[binding.block].faces[a];
            faces[binding.offset]=side[c][2*a]; faces[binding.offset+stride[a]]=side[c][2*a+1];
        }
    }
    if (++w.generation==0) throw std::overflow_error("Self-gravity publication counter exhausted");
    w.source=identity;
    arch::state::CompletionToken token{w.generation,arch::state::CompletionState::Complete};
    w.validity.publish({identity,w.generation,token}); w.ready=true; return token;
}
double SelfGravity::timestep(double cfl) const {
    workspace().require(); const auto& w=*work_; const auto& op=w.solver.op();
    if (!std::isfinite(cfl) || cfl<=0. || cfl>1.) throw std::invalid_argument("Invalid gravity CFL");
    double dt=cfl/std::sqrt(4.*arch::constants::math::pi*config_.G_const*w.max_density);
    for (int c=0;c<op.size();++c) for (int a=0;a<op.base().dimension;++a)
        if (w.g[a][c]!=0.) dt=std::min(dt,cfl*std::sqrt(op.width(c,a)/std::abs(w.g[a][c])));
    return dt;
}
const std::vector<double>& SelfGravity::potential() const { workspace().require(); return work_->phi; }
const std::array<std::vector<double>,3>& SelfGravity::acceleration() const { workspace().require(); return work_->g; }
const arch::multigrid::SolveReport& SelfGravity::report() const { workspace().require(); return work_->report; }
double SelfGravity::density_mean() const { workspace().require(); return work_->mean; }
void SelfGravity::add_sources_on_patch(std::vector<FluidVector>& delta,const FluidState& state,
    const Grid& grid,double dt,void*) const {
    const auto& patch=workspace().patch(grid,state); const int stride[]{1,grid.stride_y,grid.stride_z};
    for (int k=grid.Ks();k<grid.Ke();++k) for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
        const int c=grid.GetIndex(i,j,k); double* momentum[]{&delta[c].mom_u,&delta[c].mom_v,&delta[c].mom_w};
        for(int a=0;a<grid.dim;++a) *momentum[a]+=dt*state.rho[c]*0.5*(patch.faces[a][c]+patch.faces[a][c+stride[a]]);
    }
}
void SelfGravity::add_flux_work_on_patch(std::vector<FluidVector>& delta,const std::vector<FluidVector>& flux,
    const FluidState& state,const Grid& grid,double dt,int axis) const {
    const auto& patch=workspace().patch(grid,state); const int stride=axis==0?1:axis==1?grid.stride_y:grid.stride_z;
    for (int k=grid.Ks();k<grid.Ke();++k) for(int j=grid.Js();j<grid.Je();++j) for(int i=grid.Is();i<grid.Ie();++i) {
        const int c=grid.GetIndex(i,j,k);
        delta[c].eng+=0.5*dt*(flux[c].rho*patch.faces[axis][c]+flux[c+stride].rho*patch.faces[axis][c+stride]);
    }
}
}
