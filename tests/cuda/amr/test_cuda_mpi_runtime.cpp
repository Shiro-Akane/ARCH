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
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "driver/schedule/DriverControl.h"
#include "physics/boundary/PhysicalBoundaryHandler.h"
#include "driver/stages/DriverMacroStep.h"
#include "physics/eos/IdealGas.h"
#include "numerics/integrator/TimeIntegratorHelper.h"

#include <cuda_runtime_api.h>
#include <mpi.h>
#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <cstdint>
#include <cmath>
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
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
bool mixed = false, flux_mode = false, macro_mode = false, ownership_mode = false;
dispatch::TimeIntegratorId macro_method=dispatch::TimeIntegratorId::Euler;
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
    c.grid.nblockx1=mixed?2:3;c.grid.nblockx2=dim>=2?(mixed?1:2):0;c.grid.nblockx3=dim==3?2:0;
    c.grid.x1_min=0.;c.grid.x1_max=3.;c.grid.x2_min=0.;c.grid.x2_max=2.;c.grid.x3_min=0.;c.grid.x3_max=2.;
    c.grid.amr_max_blocks=12;c.amr.lrefinemin=0;c.amr.lrefinemax=mixed?1:0;
    const auto bc=periodic?"periodic":"outflow";
    c.grid.x1l_boundary_type=c.grid.x1r_boundary_type=bc;
    c.grid.x2l_boundary_type=c.grid.x2r_boundary_type=bc;
    c.grid.x3l_boundary_type=c.grid.x3r_boundary_type=bc;
    c.io.tmax=1.;c.numerics.dt_min=1.e-20;c.numerics.dt_max=0.;c.numerics.tstep_change_factor=2.;
    c.physics.burn.use_burn=false;c.physics.diffusion.use_diffusion=false;
    return c;
}
cuda::CudaLaunchConfig launch(const SimConfig& c) {
    using namespace dispatch;
    return cuda::make_cuda_launch_config({FluxId::Hllc,(flux_mode||macro_mode||ownership_mode)?ReconstructionId::Pcm:ReconstructionId::Ppm,LimiterId::MinMod,
        macro_method,EosId::Ideal,NetworkId::None,OdeSolverId::None,
        LinearSolverId::None,DiffusionIntegratorId::None},c);
}
void seed(amr::Block& b,int slot,int round);
// Build the five-leaf case through the already accepted Host regrid transaction.
// The fixed-root constructor and application preflight keep their existing gates.
std::vector<amr::BlockHandle> initialize_tree(
    amr::AMRControl& control,const SimConfig& config,int n,bool distributed) {
    auto root=config;root.amr.lrefinemax=0;
    control.tree->InitRootGrid(root,n);
    if(!mixed)return {};
    const int owner=distributed?rank:0, count=distributed?2:1;
    topology::TopologyIdentityRegistry registry({2,{2,1,1},1},owner,count);
    const auto observations=[&](std::span<const int> ids) {
        std::vector<topology::TopologyObservation> local;
        for(int id:ids) {
            const auto& b=control.pool->GetBlock(id);
            local.push_back({id,{2,b.level,b.logical_x1,b.logical_x2,b.logical_x3},owner});
        }
        return distributed?topology::collect_topology_observations(local,owner,count):local;
    };
    auto initial=registry.stage_adoption(observations(control.tree->GetActiveBlocks()));
    const auto old_handles=initial.reconciliation().handles_in_observation_order;
    registry.commit(std::move(initial));
    for(int id:control.tree->GetActiveBlocks())for(int slot=0;slot<3;++slot)
        seed(control.pool->GetBlock(id),slot,0);
    auto prepared=control.tree->PrepareRegrid(config,{},{},[&] {
        for(int id:control.tree->GetActiveBlocks())
            control.pool->GetBlock(id).refine_flag=control.pool->GetBlock(id).logical_x1==1?1:0;
    });
    auto candidate=registry.stage_reconciliation(observations(prepared.proposed_active_blocks()));
    const auto next=candidate.reconciliation().handles_in_observation_order;
    if(prepared.topology_changed()) {
        prepared.BuildMigrationPlans(old_handles,next,{1,registry.epoch(),candidate.reconciliation().epoch});
        prepared.ExecuteMigration();prepared.ActivateForFinalization();
        prepared.PublishNoexcept();prepared.ReleaseRetired();
    } else prepared.PublishNoChangeNoexcept();
    registry.commit(std::move(candidate));
    control.tree->UpdateNeighbors(config,registry.blocks());
    control.BindActiveHandles(next);
    require(control.pool->GetNumActiveBlocks()==(distributed?(rank?4:1):5),
        "five-leaf fixture allocated nonlocal storage");
    return next;
}
bool field_equal(double actual,double expected,bool interpolated) {
    if (!interpolated) return same(actual,expected);
    // The unchanged native CUDA AMR test uses this Host/Device arithmetic gate.
    return std::isfinite(actual)&&std::isfinite(expected)
        &&std::abs(actual-expected)<=2.0e-13*std::max({1.0,std::abs(actual),std::abs(expected)});
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
        if (mixed) {
            // The topology transaction exercises physical admissibility too.
            // Same-level copy sentinels above intentionally need no EOS validity.
            s.rho[i]=2.+round/8.+slot/16.+b.level/32.+b.logical_x1/64.
                +b.logical_x2/128.+i/65536.;
            s.mom_u[i]=s.rho[i]/4.;s.mom_v[i]=-s.rho[i]/8.;s.mom_w[i]=s.rho[i]/16.;
            s.eng[i]=8.+s.rho[i]/8.+slot/4.;
            s.enuc_rate[i]=-0.25+slot/16.+i/1048576.;
            double rest=0.;
            for(int k=1;k<s.GetNumSpecies();++k) {
                s.X(k,i)=0.5/s.GetNumSpecies()+(k%3)*(i%7)/1048576.;
                rest+=s.X(k,i);
            }
            s.X(0,i)=1.-rest;
        }
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
    std::array<bool,3> refreshed{};
    const SpeciesManager& populate(int n) {
        for(int i=0;i<n;++i)species.add_species("s"+std::to_string(i),1.,1.,1.4,1.);
        return species;
    }
    Reference(int dim,int n,bool periodic)
        :config(config_for(dim,periodic)),eos(1.4,populate(n)),control(12,dim),bc(config) {
        bc.bind(eos,species);handles=initialize_tree(control,config,n,false);
        if(!mixed)for(std::size_t i=0;i<control.tree->GetActiveBlocks().size();++i)
            handles.push_back({{i+1},{1}});
        control.BindActiveHandles(handles);
    }
    void reset(int round) {
        refreshed.fill(false);
        for(int id:control.tree->GetActiveBlocks())
            for(int s=0;s<3;++s)seed(control.pool->GetBlock(id),s,round);
    }
    void ensure_fluid_ghosts(Slot slot) {
        TimeIntegration::synchronize_domain_boundary(control,bc,members[static_cast<std::size_t>(slot)],handles);
        refreshed[static_cast<std::size_t>(slot)]=true;
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
        initialize_tree(control,config,n,distributed);
        for(int id:control.tree->GetActiveBlocks())for(int s=0;s<3;++s)seed(control.pool->GetBlock(id),s,0);
        return control;
    }
    Run(int dim,int n,bool periodic,bool distributed)
        :config(config_for(dim,periodic)),eos(1.4,populate(n)),control(ownership_mode&&distributed?6:12,dim),
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
            if(!mixed)runtime.prepare_device_flux_plans();
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
            const auto id=oracle.control.tree->FindBlock(b.level,b.logical_x1,b.logical_x2,b.logical_x3);
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
                        if(!field_equal(got[field],want[field],mixed&&r==1&&oracle.refreshed[s])) {
                            std::cerr<<"rank="<<rank<<" dim="<<config.grid.dim<<" slot="<<s
                                <<" block="<<b.logical_x1<<","<<b.logical_x2<<","<<b.logical_x3
                                <<" cell="<<cell<<" field="<<field
                                <<" got_bits="<<std::bit_cast<std::uint64_t>(got[field])
                                <<" expected_bits="<<std::bit_cast<std::uint64_t>(want[field])<<std::endl;
                            throw std::runtime_error("Runtime CUDA MPI field/ghost/untouched-slot bit mismatch");
                        }
                    }
                    for(int k=0;k<species.count();++k) {
                        const double fraction=result.composition[j*species.count()+k];
                        require(field_equal(fraction,expected.X(k,cell),mixed&&r==1&&oracle.refreshed[s]),
                            "Runtime CUDA MPI species mismatch");
                        if(mixed)require(std::isfinite(fraction)&&fraction>=0.&&fraction<=1.,
                            "mixed-level exchange produced invalid composition");
                    }
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
    if(mixed) {
        // A valid native plan is now supported; stale identity is still rejected.
        rejected([&] {
            auto stale=actual.control.RequireFluxTopologyPlan(species);
            ++stale.epoch.value;
            actual.runtime.backend()->prepare_amr_flux_plan(stale,
                actual.control.RequireRefluxTopologyPlan(species));
        });
        rejected([&]{actual.runtime.backend()->clear_amr_flux_register(
            {1000,state::CompletionState::Complete});});
    }
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
    storage.gather_coarse_fine=[&](const auto& cells,auto packet){
        runtime.backend()->gather_coarse_fine_packet(accesses,cells,packet);
    };
    int phase=0;
    storage.scatter=[&](auto slices,auto packet){
        runtime.backend()->scatter_exchange_packet(accesses,slices,packet);
        if(++phase==(mixed?3:1)&&rank==1)throw std::runtime_error("injected completed-scatter rejection");
    };
    rejected([&]{actual.control.ghost_exchange.ExecuteDistributedExchange(actual.control.pool,
        actual.control.tree,dim,&amr::Block::fluid_state,runtime.handles(),&storage);});
    for(auto h:runtime.handles())
        require(context.ledger.inspect({h,Slot::Current}).ghost.residency==state::StateResidency::Invalid,
            "failed distributed exchange exposed ghost completion");
    actual.refresh();actual.compare(oracle);
    if (mixed) {
        // Fail the actual source-owner gather with invalid Device density.
        // Host mirrors stay poisoned: using them would fail every normal case too.
        actual.reset(3);
        if (rank==0) {
            auto& b=actual.control.pool->GetBlock(actual.control.tree->GetActiveBlocks().front());
            seed(b,0,3);
            std::fill(b.fluid_state.rho.begin(),b.fluid_state.rho.end(),-1.);
            runtime.backend()->enqueue_upload_slot(runtime.backend_access(0,Slot::Current),
                Region::Interior,view(b.fluid_state));
            runtime.backend()->quiesce();
            for(auto* f:fields(b.fluid_state))std::fill(f->begin(),f->end(),-123456789.);
        }
        rejected([&]{actual.refresh();});
        for(auto h:runtime.handles())
            require(context.ledger.inspect({h,Slot::Current}).ghost.residency==state::StateResidency::Invalid,
                "failed source gather published ghosts");
        if(rank==0) {
            const auto& b=actual.control.pool->GetBlock(actual.control.tree->GetActiveBlocks().front());
            const int cell=b.grid.GetIndex(b.grid.Is(),b.grid.Js(),b.grid.Ks());
            const auto sample=runtime.backend()->read_boundary_cells(
                runtime.backend_access(0,Slot::Current),{&cell,1},Region::Interior);
            require(same(sample.conserved.front().rho,-1.),"failed source gather changed interior");
        }
        actual.reset(4);oracle.reset(4);
        for(auto slot:slots) {
            oracle.ensure_fluid_ghosts(slot);actual.refresh(slot);actual.compare(oracle);
        }
    }
    runtime.materialize_current_for_host();
    for(int id:actual.control.tree->GetActiveBlocks()) {
        auto& block=actual.control.pool->GetBlock(id);
        const auto ref=oracle.control.tree->FindBlock(block.level,block.logical_x1,block.logical_x2,block.logical_x3);
        const auto got=fields(block.fluid_state);
        const auto expected=fields(oracle.control.pool->GetBlock(ref).fluid_state);
        // The region transfer contract copies logical cells, never allocation padding.
        // Check every physical/ghost value and require poisoned Host padding to survive.
        std::vector<bool> logical(block.grid.GetTotalSize(),false),ghost(logical.size(),false);
        for(int k=0;k<block.grid.GetTotalZ();++k)
            for(int j=0;j<block.grid.GetTotalY();++j)
                for(int x=0;x<block.grid.GetTotalX();++x) {
                    const auto index=block.grid.GetIndex(x,j,k);
                    logical[index]=true;
                    ghost[index]=x<block.grid.Is()||x>=block.grid.Ie()
                        ||j<block.grid.Js()||j>=block.grid.Je()||k<block.grid.Ks()||k>=block.grid.Ke();
                }
        for(std::size_t field=0;field<got.size();++field)
            for(std::size_t cell=0;cell<got[field]->size();++cell) {
                const double want=logical[cell%logical.size()]?(*expected[field])[cell]:-123456789.;
                if(!field_equal((*got[field])[cell],want,mixed&&ghost[cell%logical.size()])) {
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
std::vector<double> snapshot(Run& run,Slot slot,Region region) {
    std::vector<double> result;
    const auto& active=run.control.tree->GetActiveBlocks();
    for(std::size_t b=0;b<active.size();++b) {
        const auto& grid=run.control.pool->GetBlock(active[b]).grid;
        std::vector<int> cells;
        for(int k=0;k<grid.GetTotalZ();++k)for(int j=0;j<grid.GetTotalY();++j)
            for(int i=0;i<grid.GetTotalX();++i) {
                const bool interior=i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je()
                    &&k>=grid.Ks()&&k<grid.Ke();
                if(interior==(region==Region::Interior))cells.push_back(grid.GetIndex(i,j,k));
            }
        const auto values=run.runtime.backend()->read_boundary_cells(
            run.runtime.backend_access(b,slot),cells,region);
        for(const auto& u:values.conserved)
            result.insert(result.end(),{u.rho,u.mom_u,u.mom_v,u.mom_w,u.eng});
        result.insert(result.end(),values.enuc.begin(),values.enuc.end());
        result.insert(result.end(),values.composition.begin(),values.composition.end());
    }
    return result;
}
void unchanged(const std::vector<double>& actual,const std::vector<double>& expected) {
    require(actual.size()==expected.size(),"snapshot extent changed");
    for(std::size_t i=0;i<actual.size();++i)
        require(same(actual[i],expected[i]),"flux operation changed an unrelated state region");
}
// Native flux-stage consumer. Full Runtime macro recovery remains separately gated.
void run_flux(int n, bool periodic) {
    Run actual(2,n,periodic,true);
    Reference oracle(2,n,periodic);
    actual.reset(6);oracle.reset(6);
    oracle.ensure_fluid_ghosts(Slot::Current);actual.refresh();
    auto& control=actual.control;
    auto& runtime=actual.runtime;
    auto* device=runtime.backend();
    control.flux_register.EnsureSpecies(n);
    const auto& topology=control.RequireFluxTopologyPlan(n);
    const auto& reflux=control.RequireRefluxTopologyPlan(n);
    device->prepare_amr_flux_plan(topology,reflux);
    const auto& active=control.tree->GetActiveBlocks();
    std::vector<backend::BackendStateAccess> accesses;
    for(std::size_t i=0;i<active.size();++i)accesses.push_back(runtime.backend_access(i,Slot::Current));
    control.flux_register.Clear();
    amr::FluxRegister expected;expected.Resize(12,2);expected.EnsureSpecies(n);
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> host_solver(actual.eos);
    const auto descriptor=scheduler::make_hydro_plan(scheduler::HydroMethod::Euler).stages.front();
    constexpr double dt=1.e-6;
    const std::array weights{0.0,0.5,1.0/6.0,2.0/3.0};
    rejected([&]{(void)device->download_amr_flux_stage(topology,{99,state::CompletionState::Complete});});
    rejected([&]{device->upload_amr_flux_register(topology,control.flux_register,1);});
    const auto accepted=snapshot(actual,Slot::Current,Region::Interior);
    rejected([&]{
        auto invalid=accesses;
        if(rank==1)invalid.pop_back();
        std::exception_ptr failure;
        try {device->execute_hydro_stage_batch(invalid,descriptor,dt,
            {99,state::CompletionState::Complete});}
        catch(...) {failure=std::current_exception();}
        control.CompleteDistributedHydroPhase(failure);
    });
    unchanged(snapshot(actual,Slot::Current,Region::Interior),accepted);
    for(std::size_t stage=0;stage<weights.size();++stage) {
        std::vector<std::vector<double>> host_values;
        {
            amr::FluxRegister::HostStageCapture capture(control.flux_register,topology,1.0);
            for(int id:active) {
                const auto& block=control.pool->GetBlock(id);
                const int reference=oracle.control.tree->FindBlock(
                    block.level,block.logical_x1,block.logical_x2,block.logical_x3);
                const auto& source=oracle.control.pool->GetBlock(reference);
                std::vector<FluidVector> delta(block.grid.GetTotalSize());
                std::vector<double> species_delta(n*block.grid.GetTotalSize());
                host_solver.evaluate_patch(&control,id,source.fluid_state,block.grid,dt,
                    delta,species_delta,nullptr,actual.config.numerics,1.0);
            }
            const auto values=capture.complete_values();
            host_values.assign(values.begin(),values.end());
        }
        const state::CompletionToken token{100+stage,state::CompletionState::Complete};
        amr::GhostExchange::CompleteDistributedPhase(nullptr);
        std::exception_ptr failure;
        try {device->execute_hydro_stage_batch(accesses,descriptor,dt,token);}
        catch(...) {failure=std::current_exception();}
        control.CompleteDistributedHydroPhase(failure);
        rejected([&]{(void)device->download_amr_flux_stage(
            topology,{1000+stage,state::CompletionState::Complete});});
        auto values=device->download_amr_flux_stage(topology,token);
        require(values.size()==host_values.size(),"captured CUDA flux route count");
        for(std::size_t route=0;route<values.size();++route) {
            require(values[route].size()==host_values[route].size(),"captured CUDA flux operation count");
            for(std::size_t i=0;i<values[route].size();++i)
                require(field_equal(values[route][i],host_values[route][i],true),
                    "captured CUDA face flux differs from original Host solver");
        }
        ++checks;
        rejected([&]{(void)device->download_amr_flux_stage(topology,token);});
        auto broken=values;
        if(rank==1)broken.front().pop_back();
        rejected([&]{control.flux_register.ApplyDistributedRegistrationStage(
            *control.pool,*control.tree,runtime.handles(),topology,broken,stage,weights[stage]);});
        broken=values;
        if(rank==1)broken.front().front()=std::numeric_limits<double>::quiet_NaN();
        rejected([&]{control.flux_register.ApplyDistributedRegistrationStage(
            *control.pool,*control.tree,runtime.handles(),topology,broken,stage,weights[stage]);});
        if(stage==0) {
            auto wrong_owner=topology;
            if(rank==1)wrong_owner.owners.at(wrong_owner.routes.front().source)=0;
            rejected([&]{control.flux_register.ApplyDistributedRegistrationStage(
                *control.pool,*control.tree,runtime.handles(),wrong_owner,values,stage,weights[stage]);});
        }
        control.flux_register.ApplyDistributedRegistrationStage(
            *control.pool,*control.tree,runtime.handles(),topology,values,stage,weights[stage]);
        expected.ApplyDistributedRegistrationStage(
            *control.pool,*control.tree,runtime.handles(),topology,host_values,stage,weights[stage]);
        if(stage==0) {
            device->upload_amr_flux_register(topology,control.flux_register,1);
            rejected([&]{device->upload_amr_flux_register(topology,control.flux_register,2);});
            rejected([&]{device->execute_amr_reflux(
                Slot::Current,dt,{899,state::CompletionState::Complete});});
            device->upload_amr_flux_register(topology,control.flux_register,1);
        } else rejected([&]{device->execute_amr_reflux(
            Slot::Current,dt,{900+stage,state::CompletionState::Complete});});
        rejected([&]{control.flux_register.ApplyDistributedRegistrationStage(
            *control.pool,*control.tree,runtime.handles(),topology,values,stage,weights[stage]);});
        for(int id:active)for(int face=0;face<4;++face) {
            require(control.flux_register.HasData(id,face)==expected.HasData(id,face),
                "coarse-owner accepted face coverage mismatch");
            if(!expected.HasData(id,face))continue;
            for(int cell=0;cell<amr::flux_plan_detail::face_cell_count(2,
                    static_cast<amr::AmrAxis>(face/2));++cell) {
                const auto a=control.flux_register.GetSummedFlux(id,face,cell);
                const auto b=expected.GetSummedFlux(id,face,cell);
                for(auto member:{&FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,
                                 &FluidVector::mom_w,&FluidVector::eng})
                    require(field_equal(a.*member,b.*member,true),"coarse-owner fluid register mismatch");
                for(int species=0;species<n;++species)
                    require(field_equal(control.flux_register.GetSummedSpeciesFlux(id,face,cell,species),
                        expected.GetSummedSpeciesFlux(id,face,cell,species),true),
                        "coarse-owner species register mismatch");
            }
        }
        ++checks;
    }
    unchanged(snapshot(actual,Slot::Current,Region::Interior),accepted);
    const auto current_ghost=snapshot(actual,Slot::Current,Region::Ghost);
    const std::array other_slots{snapshot(actual,Slot::Next,Region::Interior),
        snapshot(actual,Slot::Next,Region::Ghost),snapshot(actual,Slot::Scratch,Region::Interior),
        snapshot(actual,Slot::Scratch,Region::Ghost)};
    int callbacks=0;
    std::function<void(const amr::RefluxPlan&)> mode;
    if(rank==1)mode=[&](const auto&){++callbacks;};
    rejected([&]{control.flux_register.ExecuteDistributedReflux(control.pool,*control.tree,
        runtime.handles(),dt,&amr::Block::fluid_state,weights.size(),mode);});
    require(callbacks==0,"Host/Device reflux mismatch reached storage callback");
    rejected([&]{control.flux_register.ExecuteDistributedReflux(control.pool,*control.tree,
        runtime.handles(),dt,&amr::Block::fluid_state,weights.size(),[&](const auto&){
            device->upload_amr_flux_register(topology,control.flux_register,weights.size());
            if(rank==1)throw std::runtime_error("injected completed flux-upload rejection");
        });});
    require(!control.flux_register.DistributedRefluxComplete(),"failed upload published reflux completion");
    unchanged(snapshot(actual,Slot::Current,Region::Interior),accepted);
    // Host state remains poisoned through capture/registration. Only now restore
    // the independent Host oracle inputs; no input fields are uploaded again.
    for(int id:active) {
        auto& b=control.pool->GetBlock(id);
        const int ref=oracle.control.tree->FindBlock(b.level,b.logical_x1,b.logical_x2,b.logical_x3);
        b.fluid_state=oracle.control.pool->GetBlock(ref).fluid_state;
    }
    expected.ExecuteDistributedReflux(control.pool,*control.tree,runtime.handles(),
        dt,&amr::Block::fluid_state,weights.size());
    control.flux_register.ExecuteDistributedReflux(control.pool,*control.tree,
        runtime.handles(),dt,&amr::Block::fluid_state,weights.size(),[&](const auto&){
            device->upload_amr_flux_register(topology,control.flux_register,weights.size());
            device->execute_amr_reflux(Slot::Current,dt,{200,state::CompletionState::Complete});
        });
    require(control.flux_register.DistributedRefluxComplete(),"GPU reflux was not collectively completed");
    for(std::size_t index=0;index<active.size();++index) {
        const auto& b=control.pool->GetBlock(active[index]);
        std::vector<int> cells;
        for(int j=b.grid.Js();j<b.grid.Je();++j)for(int i=b.grid.Is();i<b.grid.Ie();++i)
            cells.push_back(b.grid.GetIndex(i,j,b.grid.Ks()));
        const auto got=device->read_boundary_cells(runtime.backend_access(index,Slot::Current),cells,Region::Interior);
        for(std::size_t i=0;i<cells.size();++i) {
            const auto want=b.fluid_state.get(cells[i]);
            for(auto member:{&FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,
                             &FluidVector::mom_w,&FluidVector::eng})
                require(field_equal(got.conserved[i].*member,want.*member,true),
                    "GPU reflux fields differ from original Host reflux");
            for(int species=0;species<n;++species)
                require(field_equal(got.composition[i*n+species],b.fluid_state.X(species,cells[i]),true),
                    "GPU reflux species differ from original Host reflux");
            require(same(got.enuc[i],b.fluid_state.enuc_rate[cells[i]]),"GPU reflux changed ENUC");
        }
    }
    unchanged(snapshot(actual,Slot::Current,Region::Ghost),current_ghost);
    unchanged(snapshot(actual,Slot::Next,Region::Interior),other_slots[0]);
    unchanged(snapshot(actual,Slot::Next,Region::Ghost),other_slots[1]);
    unchanged(snapshot(actual,Slot::Scratch,Region::Interior),other_slots[2]);
    unchanged(snapshot(actual,Slot::Scratch,Region::Ghost),other_slots[3]);
    ++checks;
    rejected([&]{control.flux_register.ExecuteDistributedReflux(control.pool,*control.tree,
        runtime.handles(),dt,&amr::Block::fluid_state,weights.size(),[&](const auto&){++callbacks;});});
    require(callbacks==0,"repeated reflux reached storage callback");
    rejected([&]{device->execute_amr_reflux(Slot::Current,dt,{201,state::CompletionState::Complete});});
    require(control.pool->GetNumActiveBlocks()==(rank?4:1),"flux transport allocated remote fluid blocks");
}


// Full macro witness: the production Runtime/scheduler owns every GPU stage,
// while the reference calls the original serial CPU integrator independently.
struct HostMacro {
    Reference& reference;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> solver;
    state::StateResidencyLedger ledger;
    scheduler::MonotonicSchedulerClock clock{2,1};
    scheduler::StageExecutionContext context{Side::Host,ledger,clock};
    explicit HostMacro(Reference& r):reference(r),solver(r.eos),ledger(r.handles.front().epoch) {
        for(auto h:r.handles)ledger.register_block(h,{1},{1,state::CompletionState::Complete});
        context.configure_boundary_context=[&](double time,boundary::BoundaryPurpose purpose) {
            reference.bc.configure_stage(time,purpose);
        };
    }
    void ghosts() {
        reference.bc.configure_stage(context.step_start_time,boundary::BoundaryPurpose::Hydro);
        const auto version=ledger.inspect({reference.handles.front(),Slot::Current}).interior.version;
        scheduler::detail::execute_completed_boundary(context,reference.handles,Slot::Current,version,
            [&](Slot,state::StateVersion,state::CompletionToken token) {
                reference.ensure_fluid_ghosts(Slot::Current);return token;
            });
    }
    void step(double dt) {
        scheduler::ScopedStageBinding binding(context,reference.handles);
        context.step_dt=dt;ghosts();
        if(macro_method==dispatch::TimeIntegratorId::Euler)
            SolverEuler::solve(reference.control,dt,reference.bc,nullptr,&solver,reference.config.numerics);
        else if(macro_method==dispatch::TimeIntegratorId::Rk2)
            SolverRK2::solve(reference.control,dt,reference.bc,nullptr,&solver,reference.config.numerics);
        else SolverRK3::solve(reference.control,dt,reference.bc,nullptr,&solver,reference.config.numerics);
        context.step_start_time+=dt;ghosts();
    }
};
struct DeviceMacro {
    Run& run;
    driver::DriverStageWorkspace workspace;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> solver;
    double burn_advice=123.;
    explicit DeviceMacro(Run& r):run(r),solver(r.eos) {}
    double step(int fault=0) {
        auto& runtime=run.runtime;
        dispatch::ResolvedExecutionPlan plan;
        plan.time_integrator=macro_method;
        const double cap=rank==1&&fault==10?2.e-6:1.e-6;
        const auto dt=driver::execute_driver_macro_attempts(runtime,run.controller,burn_advice,cap,
            &solver,run.eos,[&](double actual_dt,std::uint64_t attempt) {
                require(attempt==1,"Device macro changed retry policy");
                auto context=runtime.stage_context();
                const auto kernels=runtime.backend()->counters().kernel_count;
                scheduler::complete_hydro_local_phase(context,[&] {
                    context.step_start_time=run.controller.t_current;context.step_dt=actual_dt;
                    context.configure_boundary_context=[&](double time,boundary::BoundaryPurpose purpose) {
                        run.bc.configure_stage(time,purpose);
                    };
                    if(fault==14)context.hydro_flux_capture_accept=[&](const scheduler::StageDescriptor&) {
                        require(runtime.backend()->counters().kernel_count>kernels,"fault did not follow GPU writes");
                        if(rank==1)throw std::runtime_error("injected rejection after GPU stage writes");
                    };
                    run.bc.configure_stage(run.controller.t_current,boundary::BoundaryPurpose::Hydro);
                });
                scheduler::ScopedStageBinding binding(context,runtime.handles());
                driver::execute_driver_macro_step(runtime,context,&solver,false,
                    [](driver::BurnHalf,double,state::CompletionToken token){return token;},
                    [](double){throw std::runtime_error("unexpected diffusion");},
                    [&](double value) {
                        driver::advance_hydro(runtime,workspace,context,rank==1&&fault==9?nullptr:&plan,
                            value,nullptr,nullptr,&solver);
                    },[](driver::CpuStage,auto&& work){work();});
                if(fault==12)runtime.begin_distributed_hydro_attempt(&solver,run.controller);
                if(rank!=1)return;
                if(fault==1) {
                    run.controller.repairs.values[0]=7.;
                    throw std::runtime_error("injected rejection after reflux and rotation");
                }
                if(fault==2||fault==3) {
                    const auto& b=run.control.pool->GetBlock(run.control.tree->GetActiveBlocks().front());
                    auto damaged=b.fluid_state;
                    const auto access=runtime.backend_access(0,Slot::Current);
                    runtime.backend()->enqueue_materialize_host_current(access,Region::Interior,view(damaged));
                    runtime.backend()->quiesce();
                    const int cell=b.grid.GetIndex(b.grid.Is()+1,b.grid.Js()+1,b.grid.Ks());
                    if(fault==2)damaged.rho[cell]=-1.;else damaged.X(0,cell)=-.5;
                    runtime.backend()->enqueue_upload_slot(access,Region::Interior,view(damaged));
                    runtime.backend()->quiesce();
                }
                if(fault==4)run.control.flux_register.Clear();
                if(fault==5){run.controller.t_current+=actual_dt;++run.controller.step_count;}
                if(fault==6)runtime.materialize_current_for_host();
                if(fault==7)(void)runtime.perform_regrid(run.controller.step_count,run.controller.t_current);
                if(fault==8) {
                    (void)context.clock.next_publication();
                    throw std::runtime_error("injected rejection after issued identity");
                }
                if(fault==13)(void)runtime.prepare_backend_bindings();
                if(fault==15)(void)static_cast<cuda::CudaBackend*>(runtime.backend())->begin_store_transaction({},{});
                // Local backend replacement rejects before its original plan can retire.
                if(fault==11)runtime.backend()->prepare_amr_flux_plan(
                    run.control.RequireFluxTopologyPlan(run.species.count()),
                    run.control.RequireRefluxTopologyPlan(run.species.count()));
            });
        run.controller.advance(dt);return dt;
    }
};
std::vector<long double> macro_compare(Run& actual,Reference& reference) {
    std::vector<long double> local(5+actual.species.count()),total(local.size());
    for(std::size_t index=0;index<actual.runtime.handles().size();++index) {
        const auto& b=actual.control.pool->GetBlock(actual.control.tree->GetActiveBlocks()[index]);
        auto values=b.fluid_state;
        const auto access=actual.runtime.backend_access(index,Slot::Current);
        actual.runtime.backend()->enqueue_materialize_host_current(access,Region::Interior,view(values));
        actual.runtime.backend()->enqueue_materialize_host_current(access,Region::Ghost,view(values));
        actual.runtime.backend()->quiesce();
        const auto id=reference.control.tree->FindBlock(b.level,b.logical_x1,b.logical_x2,b.logical_x3);
        auto& expected=reference.control.pool->GetBlock(id).fluid_state;
        const auto got=fields(values),want=fields(expected);
        for(int k=0;k<b.grid.GetTotalZ();++k)for(int j=0;j<b.grid.GetTotalY();++j)
            for(int i=0;i<b.grid.GetTotalX();++i) {
                const int cell=b.grid.GetIndex(i,j,k);
                for(std::size_t field=0;field<6;++field)
                    require(field_equal((*got[field])[cell],(*want[field])[cell],true),
                        "macro Current differs from original serial CPU");
                for(int n=0;n<actual.species.count();++n)
                    require(field_equal(values.X(n,cell),expected.X(n,cell),true),"macro species differs from CPU");
                if(i>=b.grid.Is()&&i<b.grid.Ie()&&j>=b.grid.Js()&&j<b.grid.Je()&&k>=b.grid.Ks()&&k<b.grid.Ke()) {
                    const long double volume=GridMetrics::CellVolume(b.grid,i,j,k);
                    for(int field=0;field<5;++field)local[field]+=volume*(*got[field])[cell];
                    for(int n=0;n<actual.species.count();++n)local[5+n]+=volume*values.rho[cell]*values.X(n,cell);
                }
            }
    }
    MPI_Allreduce(local.data(),total.data(),total.size(),MPI_LONG_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
    ++checks;return total;
}
void run_macro(int n,bool periodic) {
    Run actual(2,n,periodic,true);Reference reference(2,n,periodic);
    actual.reset(0);reference.reset(0);actual.refresh();reference.ensure_fluid_ghosts(Slot::Current);
    actual.runtime.prepare_device_flux_plans();
    DeviceMacro device(actual);HostMacro host(reference);
    const auto initial=macro_compare(actual,reference);
    const auto accepted_step=[&] {
        const double dt=device.step();host.step(dt);
        const auto totals=macro_compare(actual,reference);
        if(periodic)for(std::size_t field=0;field<totals.size();++field)
            require(std::abs(totals[field]-initial[field])<=2.e-11L+2.e-12L*std::abs(initial[field]),
                "GPU macro periodic conservation failed");
    };
    accepted_step();
    for(int fault:{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}) {
        auto context=actual.runtime.stage_context();
        const auto ledger=context.ledger.snapshot_metadata(Side::Device);
        const auto flux=actual.control.flux_register.snapshot_host();
        const auto bc=actual.bc.snapshot_stage_context();
        const auto old_controller=actual.controller;
        const auto old_advice=device.burn_advice;
        const auto old_handles=actual.runtime.handles();
        std::vector<backend::BackendStateAccess> accesses;
        for(std::size_t i=0;i<old_handles.size();++i)accesses.push_back(actual.runtime.backend_access(i,Slot::Current));
        const auto old_token=context.clock.last_token(),old_version=context.clock.last_version();
        std::array<std::vector<double>,6> before;
        for(int slot=0;slot<3;++slot)for(int region=0;region<2;++region)
            before[2*slot+region]=snapshot(actual,slots[slot],region?Region::Ghost:Region::Interior);
        std::vector<std::array<FluidState,3>> mirrors;
        for(int id:actual.control.tree->GetActiveBlocks()) {
            auto& b=actual.control.pool->GetBlock(id);
            mirrors.push_back({b.fluid_state,b.state_next,b.state_scratch});
        }
        if(!rank)std::cout<<"macro fault method="<<static_cast<int>(macro_method)<<" case="<<fault<<std::endl;
        bool expected_rejection=true;
        rejected([&] {
            try {device.step(fault);}
            catch(const std::exception& error) {
                std::cout<<"macro rejected rank="<<rank<<" case="<<fault<<" reason="<<error.what()<<std::endl;
                if(rank==1) {
                    constexpr std::array<const char*,16> reasons{"","after reflux and rotation","final EOS rejected",
                        "final EOS rejected","Current/reflux incomplete","changed physical time",
                        "Cannot expose","Cannot regrid","after issued identity","requires its selected plan",
                        "interval/identity mismatch","excludes flux-plan replacement","attempt already active",
                        "Cannot prepare backend bindings","after GPU stage writes","excludes topology mutation"};
                    expected_rejection=std::string(error.what()).find(reasons[fault])!=std::string::npos;
                }
                throw;
            }
        });
        require(expected_rejection,"GPU macro rejected for the wrong reason");
        for(int slot=0;slot<3;++slot)for(int region=0;region<2;++region)
            unchanged(snapshot(actual,slots[slot],region?Region::Ghost:Region::Interior),before[2*slot+region]);
        for(std::size_t b=0;b<mirrors.size();++b)for(int slot=0;slot<3;++slot) {
            auto live=fields(actual.control.pool->GetBlock(actual.control.tree->GetActiveBlocks()[b]).*members[slot]);
            auto saved=fields(mirrors[b][slot]);
            for(int field=0;field<7;++field)unchanged(*live[field],*saved[field]);
        }
        require(context.ledger.metadata_snapshot_matches(ledger),"Device macro rollback changed ledger");
        require(actual.control.flux_register.host_snapshot_matches(flux),"Device macro rollback changed flux receipt");
        require(actual.bc.stage_context_matches(bc),"Device macro rollback changed boundary context");
        require(actual.runtime.handles()==old_handles,"Device macro rollback changed topology");
        for(std::size_t i=0;i<accesses.size();++i) {
            const auto live=actual.runtime.backend_access(i,Slot::Current);
            require(live.block==accesses[i].block&&live.storage==accesses[i].storage
                &&actual.runtime.backend()->contains(accesses[i]),"Device macro rollback changed storage lease");
        }
        require(same(actual.controller.t_current,old_controller.t_current)
            &&same(actual.controller.dt_old,old_controller.dt_old)
            &&actual.controller.step_count==old_controller.step_count
            &&same(device.burn_advice,old_advice),"Device macro rollback changed clock/advice");
        unchanged(actual.controller.repairs.values,old_controller.repairs.values);
        require(context.clock.last_token()>=old_token&&context.clock.last_version()>=old_version,
            "Device macro reused scheduler identity");
        require(!actual.runtime.backend()->hydro_attempt_layout_matches(),"Device macro retained recovery storage");
        accepted_step(); // Same Runtime, original CPU oracle, no reseeding or reset.
    }
    if(!rank)std::cout<<"macro PASS method="<<static_cast<int>(macro_method)
        <<" species="<<n<<" periodic="<<periodic<<" accepted_steps=16 rollback_cases=15"<<std::endl;
}

// Ownership snapshots include all three resident slots and allocation padding.
// The independent serial CPU oracle continues to check accepted Hydro evolution.
using OwnershipImage=std::vector<std::uint64_t>;
OwnershipImage ownership_image(Run& run,std::size_t index) {
    const auto& b=run.control.pool->GetBlock(run.control.tree->GetActiveBlocks()[index]);
    std::array copies{b.fluid_state,b.state_next,b.state_scratch};
    std::array views{view(copies[0]),view(copies[1]),view(copies[2])};
    run.runtime.backend()->read_ownership_slots(run.runtime.backend_access(index,Slot::Current),views);
    OwnershipImage result;
    for(auto& copy:copies)for(auto* field:fields(copy))
        for(double value:*field)result.push_back(std::bit_cast<std::uint64_t>(value));
    return result;
}
amr::AmrEndpoint ownership_endpoint(Run& run,int owner) {
    amr::AmrEndpoint endpoint{{2,1,2,0,0},{}};
    if(rank==owner) {
        const auto& ids=run.control.tree->GetActiveBlocks();
        for(std::size_t i=0;i<ids.size();++i) {
            const auto& b=run.control.pool->GetBlock(ids[i]);
            if(b.level==1&&b.logical_x1==2&&b.logical_x2==0)endpoint.handle=run.runtime.handles()[i];
        }
    }
    std::array<std::uint64_t,2> wire{endpoint.handle.uid.value,endpoint.handle.epoch.value};
    MPI_Bcast(wire.data(),2,MPI_UINT64_T,owner,MPI_COMM_WORLD);
    endpoint.handle={{wire[0]},{wire[1]}};return endpoint;
}
void run_ownership(int n,bool periodic) {
    Run actual(2,n,periodic,true);Reference reference(2,n,periodic);
    actual.reset(0);reference.reset(0);actual.refresh();reference.ensure_fluid_ghosts(Slot::Current);
    actual.runtime.prepare_device_flux_plans();
    DeviceMacro device(actual);HostMacro host(reference);
    const auto initial=macro_compare(actual,reference);
    const auto step=[&] {
        const double dt=device.step();host.step(dt);
        const auto totals=macro_compare(actual,reference);
        if(periodic)for(std::size_t f=0;f<totals.size();++f)
            require(std::abs(totals[f]-initial[f])<=2.e-11L+2.e-12L*std::abs(initial[f]),
                "ownership periodic conservation failed");
    };
    step();
    using Phase=driver::DriverRuntime::LeafTransferPhase;
    for(auto phase:{Phase::Reserved,Phase::FieldReceived,Phase::TopologyActivated,
            Phase::DevicePrepared,Phase::ReadyToPublish}) {
        const auto endpoint=ownership_endpoint(actual,1);
        const auto old_handles=actual.runtime.handles();
        const auto ids=actual.control.tree->GetActiveBlocks();
        auto context=actual.runtime.stage_context();
        const auto ledger=context.ledger.snapshot_metadata(Side::Device);
        const auto flux=actual.control.flux_register.snapshot_host();
        const auto controller=actual.controller;
        std::vector<OwnershipImage> before;
        std::vector<backend::BackendStateAccess> old_accesses;
        std::vector<std::array<FluidState,3>> mirrors;
        for(std::size_t i=0;i<ids.size();++i) {
            before.push_back(ownership_image(actual,i));
            old_accesses.push_back(actual.runtime.backend_access(i,Slot::Current));
            auto& b=actual.control.pool->GetBlock(ids[i]);
            mirrors.push_back({b.fluid_state,b.state_next,b.state_scratch});
        }
        rejected([&] {
            actual.runtime.migrate_leaf(endpoint,0,[&](Phase got) {
                if(got==phase && rank==(phase==Phase::FieldReceived?0:1))
                    throw std::runtime_error("injected ownership preparation failure");
            });
        });
        require(actual.runtime.handles()==old_handles&&actual.control.tree->GetActiveBlocks()==ids,
            "ownership rejection changed topology");
        require(context.ledger.metadata_snapshot_matches(ledger),"ownership rejection changed residency");
        require(actual.control.flux_register.host_snapshot_matches(flux),"ownership rejection changed flux");
        require(actual.controller.t_current==controller.t_current&&actual.controller.step_count==controller.step_count,
            "ownership rejection changed physical time");
        for(std::size_t i=0;i<ids.size();++i) {
            require(ownership_image(actual,i)==before[i],"ownership rollback changed resident slot bits");
            require(actual.runtime.backend()->contains(old_accesses[i]),"ownership rollback invalidated old lease");
            auto& b=actual.control.pool->GetBlock(ids[i]);
            for(int slot=0;slot<3;++slot) {
                auto a=fields(b.*members[slot]),e=fields(mirrors[i][slot]);
                for(std::size_t f=0;f<7;++f) {
                    require(a[f]->size()==e[f]->size(),"ownership rollback changed Host shape");
                    for(std::size_t j=0;j<a[f]->size();++j)
                        require(same((*a[f])[j],(*e[f])[j]),"ownership rollback changed Host mirror");
                }
            }
        }
        const auto store=static_cast<cuda::CudaBackend*>(actual.runtime.backend())->store_snapshot();
        require(store.active_blocks==ids.size()&&store.staged_blocks==0&&store.retirement_batches==0
            &&actual.control.pool->GetNumReservedBlocks()==0,"ownership rejection retained candidate resources");
        // Same Runtime: retry out and back, then advance without reseeding.
        for(int owner:{1,0}) {
            const auto source=ownership_endpoint(actual,owner);
            OwnershipImage packet;
            std::vector<OwnershipImage> survivors;
            for(std::size_t i=0;i<actual.runtime.handles().size();++i) {
                survivors.push_back(ownership_image(actual,i));
                if(actual.runtime.handles()[i]==source.handle)packet=survivors.back();
            }
            int count=packet.size();MPI_Bcast(&count,1,MPI_INT,owner,MPI_COMM_WORLD);
            packet.resize(count);MPI_Bcast(packet.data(),count,MPI_UINT64_T,owner,MPI_COMM_WORLD);
            const auto handles=actual.runtime.handles();
            std::vector<backend::BackendStateAccess> stale;
            for(std::size_t i=0;i<handles.size();++i)stale.push_back(actual.runtime.backend_access(i,Slot::Current));
            actual.runtime.migrate_leaf(source,1-owner);
            for(const auto access:stale)require(!actual.runtime.backend()->contains(access),
                "ownership publication retained stale Device lease");
            for(std::size_t i=0;i<actual.runtime.handles().size();++i) {
                const auto handle=actual.runtime.handles()[i];
                const auto value=ownership_image(actual,i);
                if(handle.uid==source.handle.uid)require(value==packet,"ownership received slots differ bitwise");
                else {
                    const auto it=std::find_if(handles.begin(),handles.end(),[&](auto h){return h.uid==handle.uid;});
                    require(it!=handles.end()&&value==survivors[it-handles.begin()],
                        "ownership survivor slots differ bitwise");
                }
            }
            const auto accepted=static_cast<cuda::CudaBackend*>(actual.runtime.backend())->store_snapshot();
            require(accepted.staged_blocks==0&&accepted.retirement_batches==0
                &&accepted.active_blocks==actual.runtime.handles().size(),"ownership retained old Device storage");
            macro_compare(actual,reference);
        }
        step();
    }
    if(!rank)std::cout<<"ownership PASS method="<<static_cast<int>(macro_method)<<" species="<<n
        <<" periodic="<<periodic<<" accepted_transfers=10 rejected_phases=5 accepted_steps=6"<<std::endl;
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
        flux_mode=argc==2&&std::string(argv[1])=="--flux";
        macro_mode=argc==2&&std::string(argv[1])=="--macro";
        ownership_mode=argc==2&&std::string(argv[1])=="--ownership";
        mixed=ownership_mode||macro_mode||flux_mode||(argc==2&&std::string(argv[1])=="--mixed");
        if(macro_mode||ownership_mode)for(auto method:{dispatch::TimeIntegratorId::Euler,dispatch::TimeIntegratorId::Rk2,
                dispatch::TimeIntegratorId::Rk3}) {
            macro_method=method;
            for(int species:{1,4,37})for(bool periodic:{false,true}) {
                if(ownership_mode)run_ownership(species,periodic);else run_macro(species,periodic);
            }
        }
        else if(flux_mode)for(int species:{1,4,37})for(bool periodic:{false,true})run_flux(species,periodic);
        else if(mixed)for(int species:{1,4,37})for(bool periodic:{false,true})run(2,species,periodic);
        else for(int dim:{1,2,3})for(int species:{1,4,37})for(bool periodic:{false,true})run(dim,species,periodic);
        std::array<std::uint64_t,3> traffic{boundary_h2d,boundary_d2h,observed_device_used};
        std::array<std::uint64_t,6> all{};
        MPI_Gather(traffic.data(),3,MPI_UINT64_T,all.data(),3,MPI_UINT64_T,0,MPI_COMM_WORLD);
        if(!rank&&!flux_mode&&!macro_mode)for(int r=0;r<2;++r)std::cout<<"boundary traffic rank="<<r
            <<" h2d_including_metadata="<<all[3*r]<<" d2h="<<all[3*r+1]
            <<" observed_device_used_including_test_and_contexts="<<all[3*r+2]<<std::endl;
        if(!rank) {
            if(ownership_mode)std::cout<<"PASS CUDA MPI ownership layouts=18 comparisons="<<checks;
            else if(macro_mode)std::cout<<"PASS CUDA MPI macro layouts=18 comparisons="<<checks;
            else if(flux_mode)std::cout<<"PASS CUDA MPI flux layouts=6 weighted_stages=24 flux_reflux_checks="<<checks;
            else std::cout<<"PASS CUDA MPI Runtime mixed="<<mixed<<" layouts="<<(mixed?6:18)
                <<" full_state_checks="<<checks;
            std::cout<<" collective_rejections="<<rejections<<std::endl;
        }
        MPI_Finalize();return 0;
    } catch(const std::exception& e) {
        std::cerr<<"rank="<<rank<<" "<<e.what()<<std::endl;MPI_Abort(MPI_COMM_WORLD,1);return 1;
    }
}
