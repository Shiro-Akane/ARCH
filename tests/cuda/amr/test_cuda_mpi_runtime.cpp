/**
 * @file test_cuda_mpi_runtime.cpp
 * @brief Real Runtime -> GhostExchange -> CudaBackend Host-staging witness.
 * Compare all logical cells/fields/slots with the serial Host boundary authority.
 * Poison Host mirrors, use unequal per-rank block counts, reject one-sided
 * failures, and check cache coordination and completion before publication.
 */
#include "amr/AMRControl.h"
#include "cuda/runtime/CudaBackend.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "physics/eos/IdealGas.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

#include <cuda_runtime_api.h>
#include <mpi.h>
#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace arch;
using Slot = state::StateSlot;
using Region = state::StateRegion;
using Side = state::ExecutionSide;
constexpr std::array slots{Slot::Current, Slot::Next, Slot::Scratch};
constexpr std::array members{&amr::Block::fluid_state, &amr::Block::state_next, &amr::Block::state_scratch};
int rank = 0, checks = 0, rejections = 0;
std::uint64_t boundary_h2d = 0, boundary_d2h = 0, observed_device_used = 0;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool same(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

backend::HostStateTransferView view(FluidState& s) {
    return {s.rho.data(),s.mom_u.data(),s.mom_v.data(),s.mom_w.data(),s.eng.data(),s.enuc_rate.data(),
        s.mass_fractions.empty()?nullptr:s.mass_fractions.data(),s.rho.size(),
        static_cast<std::size_t>(s.GetNumSpecies()),s.rho.size()};
}
std::array<std::vector<double>*,7> fields(FluidState& s) {
    return {&s.rho,&s.mom_u,&s.mom_v,&s.mom_w,&s.eng,&s.enuc_rate,&s.mass_fractions};
}
SimConfig config_for(int dim, bool periodic) {
    SimConfig c{};
    c.grid.dim=dim;c.grid.geometry="cartesian";
    c.grid.nblockx1=3;c.grid.nblockx2=dim>=2?2:0;c.grid.nblockx3=dim==3?2:0;
    c.grid.x1_min=0.;c.grid.x1_max=3.;c.grid.x2_min=0.;c.grid.x2_max=2.;c.grid.x3_min=0.;c.grid.x3_max=2.;
    c.grid.amr_max_blocks=12;c.amr.lrefinemin=c.amr.lrefinemax=0;
    const auto bc=periodic?"periodic":"outflow";
    c.grid.x1l_boundary_type=c.grid.x1r_boundary_type=bc;
    c.grid.x2l_boundary_type=c.grid.x2r_boundary_type=bc;
    c.grid.x3l_boundary_type=c.grid.x3r_boundary_type=bc;
    c.physics.burn.use_burn=false;c.physics.diffusion.use_diffusion=false;
    return c;
}
cuda::CudaLaunchConfig launch(const SimConfig& c) {
    using namespace dispatch;
    return cuda::make_cuda_launch_config({FluxId::Hllc,ReconstructionId::Ppm,LimiterId::MinMod,
        TimeIntegratorId::Euler,EosId::Ideal,NetworkId::None,OdeSolverId::None,
        LinearSolverId::None,DiffusionIntegratorId::None},c);
}
void seed(amr::Block& b, int slot, int round) {
    auto& s=b.*members[slot];
    const double base=100000.*round+10000.*slot+1000.*b.logical_x1+100.*b.logical_x2+10.*b.logical_x3;
    for(std::size_t i=0;i<s.rho.size();++i) {
        s.rho[i]=1.+base+i/1024.;s.mom_u[i]=2.+base+i/512.;
        s.mom_v[i]=3.+base+i/256.;s.mom_w[i]=4.+base+i/128.;
        s.eng[i]=5.+base+i/64.;
        s.enuc_rate[i]=i%7==0?-0.:std::bit_cast<double>(std::uint64_t{0x7ff8000000000123});
        for(int k=0;k<s.GetNumSpecies();++k)s.X(k,i)=(k+1)/128.+i/1048576.;
    }
}
// The independent serial oracle uses the original Host boundary recipe.
// A serial DriverRuntime is intentionally not admitted into a two-rank WORLD.
struct Reference {
    SimConfig config;
    SpeciesManager species;
    IdealGas eos;
    amr::AMRControl control;
    BCHandler bc;
    std::vector<amr::BlockHandle> handles;
    const SpeciesManager& populate(int n) {
        for(int i=0;i<n;++i)species.add_species("s"+std::to_string(i),1.,1.,1.4,1.);
        return species;
    }
    Reference(int dim,int n,bool periodic)
        :config(config_for(dim,periodic)),eos(1.4,populate(n)),control(12,dim),bc(config) {
        bc.bind(eos,species);control.tree->InitRootGrid(config,n);
        for(std::size_t i=0;i<control.tree->GetActiveBlocks().size();++i)
            handles.push_back({{i+1},{1}});
        control.BindActiveHandles(handles);
    }
    void reset(int round) {
        for(int id:control.tree->GetActiveBlocks())
            for(int s=0;s<3;++s)seed(control.pool->GetBlock(id),s,round);
    }
    void ensure_fluid_ghosts(Slot slot) {
        TimeIntegration::synchronize_domain_boundary(control,bc,members[static_cast<std::size_t>(slot)],handles);
    }
};
struct Run {
    SimConfig config;
    SpeciesManager species;
    IdealGas eos;
    amr::AMRControl control;
    BCHandler bc;
    SimulationController controller;
    driver::DriverRuntime runtime;
    const SpeciesManager& populate(int n) {
        for(int i=0;i<n;++i)species.add_species("s"+std::to_string(i),1.,1.,1.4,1.);
        return species;
    }
    amr::AMRControl& initialize_control(bool distributed,int n) {
        if(distributed)control.tree->SetRootPartition(rank,2);
        control.tree->InitRootGrid(config,n);
        for(int id:control.tree->GetActiveBlocks())for(int s=0;s<3;++s)seed(control.pool->GetBlock(id),s,0);
        return control;
    }
    Run(int dim,int n,bool periodic,bool distributed)
        :config(config_for(dim,periodic)),eos(1.4,populate(n)),control(12,dim),
         bc(config),controller(config,RunState{}),
         runtime(initialize_control(distributed,n),bc,config,species,controller) {
        bc.bind(eos,species);
        runtime.initialize_topology();
        runtime.ensure_fluid_ghosts();
        if(distributed) {
            const auto topology=runtime.prepare_backend_bindings();
            std::vector<cuda::CudaBlockBinding> bindings;
            for(const auto& b:topology)bindings.push_back({b.block,b.handle,b.storage,b.physical_boundary,b.hydro_boundary});
            runtime.install_backend(cuda::make_cuda_backend(bindings,0,launch(config),species,eos));
            runtime.upload_initial_state();
        }
    }
    void reset(int round) {
        const auto& active=control.tree->GetActiveBlocks();
        for(std::size_t i=0;i<active.size();++i)for(int s=0;s<3;++s) {
            auto& b=control.pool->GetBlock(active[i]);seed(b,s,round);
            if(auto* device=runtime.backend()) {
                const auto access=runtime.backend_access(i,slots[s]);
                device->enqueue_upload_slot(access,Region::Interior,view(b.*members[s]));
                device->enqueue_upload_slot(access,Region::Ghost,view(b.*members[s]));
            }
        }
        if(auto* device=runtime.backend())device->quiesce();
        auto context=runtime.stage_context();
        for(auto slot:slots)scheduler::publish_completed_interior(context,runtime.handles(),slot);
        if(runtime.backend())for(int id:active)for(auto member:members)
            for(auto* field:fields(control.pool->GetBlock(id).*member))
                std::fill(field->begin(),field->end(),-123456789.);
    }
    void refresh(Slot slot=Slot::Current) {
        const auto before=runtime.backend()->counters();
        runtime.ensure_fluid_ghosts(slot);
        const auto after=runtime.backend()->counters();
        boundary_h2d+=after.bytes_h2d-before.bytes_h2d;
        boundary_d2h+=after.bytes_d2h-before.bytes_d2h;
        std::size_t available=0,total=0;
        require(cudaMemGetInfo(&available,&total)==cudaSuccess,"device memory observation failed");
        observed_device_used=std::max(observed_device_used,static_cast<std::uint64_t>(total-available));
    }
    void compare(Reference& oracle) {
        const auto& active=control.tree->GetActiveBlocks();
        for(std::size_t i=0;i<active.size();++i) {
            auto& b=control.pool->GetBlock(active[i]);
            const auto id=oracle.control.tree->FindBlock(0,b.logical_x1,b.logical_x2,b.logical_x3);
            require(id>=0,"serial oracle block missing");
            std::array<std::vector<int>,2> regions;
            for(int k=0;k<b.grid.GetTotalZ();++k)
                for(int j=0;j<b.grid.GetTotalY();++j)
                    for(int x=0;x<b.grid.GetTotalX();++x) {
                        const bool interior=x>=b.grid.Is()&&x<b.grid.Ie()
                            &&j>=b.grid.Js()&&j<b.grid.Je()&&k>=b.grid.Ks()&&k<b.grid.Ke();
                        regions[interior?0:1].push_back(b.grid.GetIndex(x,j,k));
                    }
            for(int s=0;s<3;++s)for(int r=0;r<2;++r) {
                const auto& cells=regions[r];
                // Independent snapshot kernel with its original region lease.
                const auto result=runtime.backend()->read_boundary_cells(
                    runtime.backend_access(i,slots[s]),cells,r==0?Region::Interior:Region::Ghost);
                auto& expected=oracle.control.pool->GetBlock(id).*members[s];
                for(std::size_t j=0;j<cells.size();++j) {
                    const auto cell=cells[j];const auto u=result.conserved[j];
                    const std::array got{u.rho,u.mom_u,u.mom_v,u.mom_w,u.eng,result.enuc[j]};
                    const std::array want{expected.rho[cell],expected.mom_u[cell],expected.mom_v[cell],
                        expected.mom_w[cell],expected.eng[cell],expected.enuc_rate[cell]};
                    for(std::size_t field=0;field<got.size();++field) {
                        if(!same(got[field],want[field])) {
                            std::cerr<<"rank="<<rank<<" dim="<<config.grid.dim<<" slot="<<s
                                <<" block="<<b.logical_x1<<","<<b.logical_x2<<","<<b.logical_x3
                                <<" cell="<<cell<<" field="<<field
                                <<" got_bits="<<std::bit_cast<std::uint64_t>(got[field])
                                <<" expected_bits="<<std::bit_cast<std::uint64_t>(want[field])<<std::endl;
                            throw std::runtime_error("Runtime CUDA MPI field/ghost/untouched-slot bit mismatch");
                        }
                    }
                    for(int k=0;k<species.count();++k)
                        require(same(result.composition[j*species.count()+k],expected.X(k,cell)),
                            "Runtime CUDA MPI species bit mismatch");
                }
            }
        }
        require(control.pool->GetNumActiveBlocks()==static_cast<int>(active.size()),"remote field allocation");
        const auto store=static_cast<cuda::CudaBackend*>(runtime.backend())->store_snapshot();
        require(store.active_blocks==active.size()&&store.staged_blocks==0&&store.retirement_batches==0,
            "unexpected remote or retained Device block allocation");
        ++checks;
    }
};
template<class Action> void rejected(Action&& action) {
    bool caught=false;try{action();}catch(const std::exception&){caught=true;}
    int local=caught?1:0,total=0;MPI_Allreduce(&local,&total,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
    require(total==2,"one-sided boundary rejection did not reach both ranks");++rejections;
}
void run(int dim,int species,bool periodic) {
    Run actual(dim,species,periodic,true);
    Reference oracle(dim,species,periodic);
    for(int repeat=1;repeat<=2;++repeat) {
        actual.reset(repeat);oracle.reset(repeat);
        for(auto slot:slots) {
            oracle.ensure_fluid_ghosts(slot);
            actual.refresh(slot);
            auto context=actual.runtime.stage_context();
            for(auto handle:actual.runtime.handles()) {
                const auto c=context.ledger.inspect({handle,slot});
                context.ledger.require_readable({handle,slot},{Side::Device,c.interior.version,true,true});
                require(c.ghost.residency==state::StateResidency::DeviceValid,"ghost published on wrong side");
            }
            actual.compare(oracle);
            const auto before=actual.runtime.backend()->counters();
            actual.refresh(slot);
            const auto after=actual.runtime.backend()->counters();
            require(before.kernel_count==after.kernel_count&&before.bytes_h2d==after.bytes_h2d
                &&before.bytes_d2h==after.bytes_d2h,"cache hit performed field traffic");
            // Only rank one loses its ghost visibility. Both ranks must refresh.
            if(rank==1)for(auto h:actual.runtime.handles())context.ledger.invalidate_ghost({h,slot},Side::Device);
            actual.refresh(slot);
            actual.compare(oracle);
        }
    }
    auto& runtime=actual.runtime;auto context=runtime.stage_context();
    const auto before=runtime.backend()->counters();
    rejected([&]{runtime.ensure_fluid_ghosts(rank?Slot::Next:Slot::Current);});
    const auto version=context.ledger.inspect({runtime.handles().front(),Slot::Current}).interior.version;
    const auto token=context.clock.next_completion();
    rejected([&]{runtime.execute_device_boundary(Slot::Current,{version.value+(rank?1:0)},token);});
    const auto after=runtime.backend()->counters();
    require(before.kernel_count==after.kernel_count&&before.bytes_h2d==after.bytes_h2d
        &&before.bytes_d2h==after.bytes_d2h,"rejected identity changed fields");
    actual.refresh();actual.compare(oracle);

    // Fault an actual transport scatter after a completed CUDA write. The
    // same callback contract must drain and report failure before another axis.
    std::vector<backend::BackendStateAccess> accesses;
    for(std::size_t i=0;i<runtime.handles().size();++i)accesses.push_back(runtime.backend_access(i,Slot::Current));
    if(dim==2&&species==4&&periodic) {
        std::vector<std::uint64_t> packet(6+species);
        const amr::ExchangePacketSlice active{runtime.handles().front(),{{0,0,0},{1,1,1}},0};
        const auto before_invalid=runtime.backend()->counters();
        auto stale=accesses;++stale.front().storage.value;
        rejected([&]{runtime.backend()->gather_exchange_packet(stale,{&active,1},packet);});
        auto outside=active;outside.box.first[0]=-amr::MAX_NG-1;
        rejected([&]{runtime.backend()->gather_exchange_packet(accesses,{&outside,1},packet);});
        auto misaligned=active;misaligned.word_offset=1;
        rejected([&]{runtime.backend()->gather_exchange_packet(accesses,{&misaligned,1},packet);});
        rejected([&]{runtime.backend()->scatter_exchange_packet(accesses,{&active,1},packet);});
        auto ghost=active;ghost.box.first[0]=-1;
        std::array duplicate{ghost,ghost};duplicate[1].word_offset=packet.size();
        packet.resize(2*packet.size());
        rejected([&]{runtime.backend()->scatter_exchange_packet(accesses,duplicate,packet);});
        runtime.backend()->gather_exchange_packet(accesses,{},{});
        runtime.backend()->scatter_exchange_packet(accesses,{},{});
        const auto after_invalid=runtime.backend()->counters();
        require(before_invalid.kernel_count==after_invalid.kernel_count
            &&before_invalid.bytes_h2d==after_invalid.bytes_h2d
            &&before_invalid.bytes_d2h==after_invalid.bytes_d2h,"invalid packet reached device writes");
    }
    amr::GhostExchange::DistributedStorage storage;
    storage.source_version=version.value;
    storage.begin=[&]{for(auto h:runtime.handles())context.ledger.invalidate_ghost({h,Slot::Current},Side::Device);};
    storage.gather=[&](auto slices,auto packet){runtime.backend()->gather_exchange_packet(accesses,slices,packet);};
    int phase=0;
    storage.scatter=[&](auto slices,auto packet){
        runtime.backend()->scatter_exchange_packet(accesses,slices,packet);
        if(++phase==1&&rank==1)throw std::runtime_error("injected completed-scatter rejection");
    };
    rejected([&]{actual.control.ghost_exchange.ExecuteDistributedExchange(actual.control.pool,
        actual.control.tree,dim,&amr::Block::fluid_state,runtime.handles(),&storage);});
    for(auto h:runtime.handles())
        require(context.ledger.inspect({h,Slot::Current}).ghost.residency==state::StateResidency::Invalid,
            "failed distributed exchange exposed ghost completion");
    actual.refresh();actual.compare(oracle);
    runtime.materialize_current_for_host();
    for(int id:actual.control.tree->GetActiveBlocks()) {
        auto& block=actual.control.pool->GetBlock(id);
        const auto ref=oracle.control.tree->FindBlock(0,block.logical_x1,block.logical_x2,block.logical_x3);
        const auto got=fields(block.fluid_state);
        const auto expected=fields(oracle.control.pool->GetBlock(ref).fluid_state);
        // The region transfer contract copies logical cells, never allocation padding.
        // Check every physical/ghost value and require poisoned Host padding to survive.
        std::vector<bool> logical(block.grid.GetTotalSize(),false);
        for(int k=0;k<block.grid.GetTotalZ();++k)
            for(int j=0;j<block.grid.GetTotalY();++j)
                for(int x=0;x<block.grid.GetTotalX();++x)
                    logical[block.grid.GetIndex(x,j,k)]=true;
        for(std::size_t field=0;field<got.size();++field)
            for(std::size_t cell=0;cell<got[field]->size();++cell) {
                const double want=logical[cell%logical.size()]?(*expected[field])[cell]:-123456789.;
                if(!same((*got[field])[cell],want)) {
                    std::cerr<<"rank="<<rank<<" dim="<<dim<<" field="<<field<<" offset="<<cell
                        <<" logical="<<logical[cell%logical.size()]<<std::endl;
                    throw std::runtime_error("Runtime materialization field/padding mismatch");
                }
            }
    }
    for(auto h:runtime.handles()) {
        const auto c=context.ledger.inspect({h,Slot::Current});
        require(c.interior.residency==state::StateResidency::Synchronized
            &&c.ghost.residency==state::StateResidency::Synchronized,"materialization did not synchronize ledger");
    }
    const auto materialized=runtime.backend()->counters();
    runtime.materialize_current_for_host();
    const auto cached=runtime.backend()->counters();
    require(materialized.bytes_h2d==cached.bytes_h2d&&materialized.bytes_d2h==cached.bytes_d2h
        &&materialized.kernel_count==cached.kernel_count,"materialization cache lost synchronized state");
    if(rank==0)std::cout<<"runtime layout PASS dim="<<dim<<" species="<<species<<" periodic="<<periodic<<std::endl;
}
}
int main(int argc,char** argv) {
    int provided=0;MPI_Init_thread(&argc,&argv,MPI_THREAD_FUNNELED,&provided);
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);int size=0;MPI_Comm_size(MPI_COMM_WORLD,&size);
    try {
        require(size==2&&provided>=MPI_THREAD_FUNNELED,"test requires two FUNNELED ranks");
        int devices=0;const int local=cudaGetDeviceCount(&devices)==cudaSuccess&&devices>0?1:0;
        int ready=0;MPI_Allreduce(&local,&ready,1,MPI_INT,MPI_MIN,MPI_COMM_WORLD);
        if(!ready){if(!rank)std::cout<<"SKIP no CUDA device\n";MPI_Finalize();return 77;}
        for(int dim:{1,2,3})for(int species:{1,4,37})for(bool periodic:{false,true})run(dim,species,periodic);
        std::array<std::uint64_t,3> traffic{boundary_h2d,boundary_d2h,observed_device_used};
        std::array<std::uint64_t,6> all{};
        MPI_Gather(traffic.data(),3,MPI_UINT64_T,all.data(),3,MPI_UINT64_T,0,MPI_COMM_WORLD);
        if(!rank)for(int r=0;r<2;++r)std::cout<<"boundary traffic rank="<<r
            <<" h2d_including_metadata="<<all[3*r]<<" d2h="<<all[3*r+1]
            <<" observed_device_used_including_test_and_contexts="<<all[3*r+2]<<std::endl;
        if(!rank)std::cout<<"PASS CUDA MPI Runtime layouts=18 full_state_checks="<<checks
            <<" collective_rejections="<<rejections<<std::endl;
        MPI_Finalize();return 0;
    } catch(const std::exception& e) {
        std::cerr<<"rank="<<rank<<" "<<e.what()<<std::endl;MPI_Abort(MPI_COMM_WORLD,1);return 1;
    }
}
