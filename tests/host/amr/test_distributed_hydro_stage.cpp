/**
 * @file test_distributed_hydro_stage.cpp
 * @brief Original Host Hydro lanes on a prescribed mixed-level MPI mesh.
 * Includes the original Runtime macro recovery; application AMR remains closed.
 */
#include "fixtures/amr/partitioned_topology_fixture.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "physics/eos/IdealGas.h"
#include "driver/stages/DriverMacroStep.h"
#include <cstdlib>
#include <new>
#include <bit>
#include <iostream>
#include <numbers>
#ifdef _OPENMP
#include <omp.h>
#endif
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
namespace allocation_fault { inline thread_local int fail_after=0; }
#if ARCH_MPI_ENABLED
namespace ownership_wire_fault { inline int mode=0; }
extern "C" int MPI_Send(const void* buffer,int count,MPI_Datatype type,int dest,int tag,MPI_Comm comm) {
    if(tag!=49||ownership_wire_fault::mode==0)
        return PMPI_Send(buffer,count,type,dest,tag,comm);
    if(ownership_wire_fault::mode==4)
        return PMPI_Send(buffer,count-1,type,dest,tag,comm);
    auto* words=const_cast<std::uint64_t*>(static_cast<const std::uint64_t*>(buffer));
    const int index=ownership_wire_fault::mode==1?0:ownership_wire_fault::mode==2?1:ownership_wire_fault::mode==5?14:21;
    const auto original=words[index];words[index]^=1;
    const int result=PMPI_Send(buffer,count,type,dest,tag,comm);
    words[index]=original;return result;
}
#endif

void* operator new(std::size_t bytes) {
    if(allocation_fault::fail_after>0&&--allocation_fault::fail_after==0) {
        throw std::bad_alloc();
    }
    if(void* p=std::malloc(bytes?bytes:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
namespace {
using Fixture = amr::test::PartitionedTopologyFixture;
using namespace arch;
int rank = 0, ranks = 1;
void require(bool okay, const char* text) {
    if (!okay)
        throw std::runtime_error(text);
}
bool bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
void seed(Fixture& f, bool wave) {
    f.config.numerics.sml_rho = f.config.numerics.min_eint = 1.e-20;
    f.config.numerics.max_eint = 1.e99;
    f.control.flux_register.EnsureSpecies(2);
    for (int id : f.control.tree->GetActiveBlocks()) {
        auto& b = f.control.pool->GetBlock(id);
        for (int j = 0; j < b.grid.GetTotalY(); ++j)
            for (int i = 0; i < b.grid.GetTotalX(); ++i) {
                const int c = b.grid.GetIndex(i, j, 0);
                const double x = b.grid.GetCellCenterX(i), y = b.grid.GetCellCenterY(j);
                const double rho = wave ? 1. + .05 * std::sin(2. * std::numbers::pi * (x + y)) : 1.;
                b.fluid_state.set(c, {rho, rho * .125, -rho * .0625, 0.,
                                      2.5 + .5 * rho * (.125 * .125 + .0625 * .0625)});
                b.fluid_state.X(0, c) =
                    wave ? .25 + .01 * std::sin(2. * std::numbers::pi * x) : .25;
                b.fluid_state.X(1, c) = 1. - b.fluid_state.X(0, c);
                b.fluid_state.enuc_rate[c] = -.125;
            }
        b.state_next = b.state_scratch = b.fluid_state;
    }
}
std::array<long double, 7> totals(const Fixture& f) {
    std::array<long double, 7> result{};
    for (int id : f.control.tree->GetActiveBlocks()) {
        const auto& b = f.control.pool->GetBlock(id);
        for (int j = b.grid.Js(); j < b.grid.Je(); ++j)
            for (int i = b.grid.Is(); i < b.grid.Ie(); ++i) {
                const int c = b.grid.GetIndex(i, j, 0);
                const long double v = GridMetrics::CellVolume(b.grid, i, j, 0);
                const auto u = b.fluid_state.get(c);
                const std::array<double, 7> q{u.rho,
                                              u.mom_u,
                                              u.mom_v,
                                              u.mom_w,
                                              u.eng,
                                              u.rho * b.fluid_state.X(0, c),
                                              u.rho * b.fluid_state.X(1, c)};
                for (int n = 0; n < 7; ++n)
                    result[n] += v * q[n];
            }
    }
    return result;
}
void compare(const Fixture& actual, const Fixture& reference) {
    for (int id : actual.control.tree->GetActiveBlocks()) {
        const auto& b = actual.control.pool->GetBlock(id);
        const int rid =
            reference.control.tree->FindBlock(b.level, b.logical_x1, b.logical_x2, b.logical_x3);
        require(rid >= 0, "missing serial leaf");
        const auto& expected = reference.control.pool->GetBlock(rid);
        for (auto member :
             {&amr::Block::fluid_state, &amr::Block::state_next, &amr::Block::state_scratch}) {
            const auto& a = b.*member;
            const auto& e = expected.*member;
            const auto af = amr::test::regrid_fields(a), ef = amr::test::regrid_fields(e);
            for (std::size_t field = 0; field < af.size(); ++field)
                for (std::size_t cell = 0; cell < a.rho.size(); ++cell)
                    if (!bits(af[field][cell], ef[field][cell])) {
                        std::cerr << "mismatch rank=" << rank << " level=" << b.level
                                  << " block=" << b.logical_x1 << "," << b.logical_x2
                                  << " field=" << field << " cell=" << cell << "\n";
                        throw std::runtime_error("Hydro fields are not bitwise equal");
                    }
            for (std::size_t cell = 0; cell < a.mass_fractions.size(); ++cell)
                require(bits(a.mass_fractions[cell], e.mass_fractions[cell]),
                        "Hydro species mismatch");
        }
        TimeIntegration::validate_stage_state(b.fluid_state, b.grid, actual.config.numerics);
        require(actual.control.pool->GetNumActiveBlocks() ==
                    static_cast<int>(actual.handles.size()),
                "remote fluid storage allocated");
    }
}
struct Run {
    Fixture& f;
    SpeciesManager species;
    IdealGas eos;
    BCHandler bc;
    Numerics::HydroSolverImpl<IdealGas, FluxHLLC<PCMReconstruction>> solver;
    state::StateResidencyLedger ledger;
    scheduler::MonotonicSchedulerClock clock{2, 1};
    scheduler::StageExecutionContext context{state::ExecutionSide::Host, ledger, clock};
    explicit Run(Fixture& fixture)
        : f(fixture), eos(make_species()), bc(f.config), solver(eos),
          ledger(f.handles.front().epoch) {
        bc.bind(eos, species);
        for (auto handle : f.handles)
            ledger.register_block(handle, {1}, {1, state::CompletionState::Complete});
        context.step_dt = 1.e-4;
        context.configure_boundary_context = [&](double time, boundary::BoundaryPurpose purpose) {
            bc.configure_stage(time, purpose);
        };
        if (f.control.tree->PartitionSize() > 1)
            context.hydro_rank_completion = [&](std::exception_ptr failure) {
                f.control.CompleteDistributedHydroPhase(failure);
            };
    }
    const SpeciesManager& make_species() {
        species.add_species("X", 1., 1., 1.4, 1.);
        species.add_species("Y", 2., 1., 1.4, 1.);
        return species;
    }
    void ghosts() {
        bc.configure_stage(context.step_start_time, boundary::BoundaryPurpose::Hydro);
        const auto version =
            ledger.inspect({f.handles.front(), state::StateSlot::Current}).interior.version;
        scheduler::detail::execute_completed_boundary(
            context, f.handles, state::StateSlot::Current, version,
            [&](state::StateSlot, state::StateVersion, state::CompletionToken token) {
                TimeIntegration::synchronize_domain_boundary(f.control, bc,
                                                             &amr::Block::fluid_state, f.handles);
                return token;
            });
    }
    void step(int method, const Numerics::IHydroSolver* hydro = nullptr) {
        if (!hydro)
            hydro = &solver;
        if (method == 0)
            SolverEuler::solve(f.control, context.step_dt, bc, nullptr, hydro, f.config.numerics);
        else if (method == 1)
            SolverRK2::solve(f.control, context.step_dt, bc, nullptr, hydro, f.config.numerics);
        else
            SolverRK3::solve(f.control, context.step_dt, bc, nullptr, hydro, f.config.numerics);
        context.step_start_time += context.step_dt;
    }
};
struct FaultSolver final : Numerics::IHydroSolver {
    const Numerics::IHydroSolver& original;
    int fault, victim;
    mutable int calls = 0;
    FaultSolver(const Numerics::IHydroSolver& solver, int mode, int id)
        : original(solver), fault(mode), victim(id) {}
    Numerics::HostHydroStorageContract host_storage_contract() const noexcept override {
        return original.host_storage_contract();
    }
    void evaluate_patch(amr::AMRControl* control, int id, const FluidState& state, const Grid& grid,
                        double dt, std::vector<FluidVector>& du, std::vector<double>& dx,
                        const Physical::Gravity::IGravityPolicy* gravity,
                        const NumericsConfig& config, double weight, void* stream,
                        const boundary::HostHydroBoundaryAuthority* wall) const override {
        if (rank == 1 && id == victim) {
            ++calls;
            if (fault == 1 || (fault == 4 && calls == 2))
                throw std::runtime_error("injected local solver rejection");
            if (fault == 2)
                return;
        }
        original.evaluate_patch(control, id, state, grid, dt, du, dx, gravity, config, weight,
                                stream, wall);
        if (rank == 1 && id == victim && fault == 3)
            original.evaluate_patch(control, id, state, grid, dt, du, dx, gravity, config, weight,
                                    stream, wall);
    }
    void update_patch(const FluidState& old, const FluidState& in, FluidState& out,
                      const std::vector<FluidVector>& du, const std::vector<double>& dx,
                      const Grid& grid, double a, double b, const NumericsConfig& config,
                      void* stream) const override {
        original.update_patch(old, in, out, du, dx, grid, a, b, config, stream);
    }
};
// The bitwise oracle is the original single-thread serial execution.
// Legacy immediate OMP flux registration can interleave atomic additions;
// it is not a deterministic ordering oracle for the collected MPI stage.
struct SerialReferenceThreads {
#ifdef _OPENMP
    int previous = omp_get_max_threads();
    SerialReferenceThreads() { omp_set_num_threads(1); }
    ~SerialReferenceThreads() { omp_set_num_threads(previous); }
#endif
};
void successful_cases() {
    for (int method = 0; method < 3; ++method)
        for (bool wave : {false, true}) {
            Fixture actual(2, rank, ranks, true), reference(2, 0, 1, true);
            seed(actual, wave);
            seed(reference, wave);
            Run a(actual), b(reference);
            const auto initial = totals(reference);
            for (int step = 0; step < 3; ++step) {
                {
                    SerialReferenceThreads serial_reference;
                    scheduler::ScopedStageBinding binding(b.context, reference.handles);
                    b.ghosts();
                    b.step(method);
                    b.ghosts();
                }
                {
                    scheduler::ScopedStageBinding binding(a.context, actual.handles);
                    a.ghosts();
                    a.step(method);
                    a.ghosts();
                }
                compare(actual, reference);
                if (!wave) {
                    for (int id : actual.control.tree->GetActiveBlocks()) {
                        const auto& block = actual.control.pool->GetBlock(id);
                        for (int j = block.grid.Js(); j < block.grid.Je(); ++j)
                            for (int i = block.grid.Is(); i < block.grid.Ie(); ++i) {
                                const int cell = block.grid.GetIndex(i, j, 0);
                                const auto u = block.fluid_state.get(cell);
                                require(bits(u.rho, 1.) && bits(u.mom_u, .125) &&
                                            bits(u.mom_v, -.0625) && bits(u.mom_w, 0.) &&
                                            bits(u.eng, 2.5 + .5 * (.125 * .125 + .0625 * .0625)) &&
                                            bits(block.fluid_state.X(0, cell), .25) &&
                                            bits(block.fluid_state.X(1, cell), .75) &&
                                            bits(block.fluid_state.enuc_rate[cell], -.125),
                                        "constant state changed");
                            }
                    }
                }

                auto total = totals(actual);
#if ARCH_MPI_ENABLED
                if (ranks > 1) {
                    auto local = total;
                    MPI_Allreduce(local.data(), total.data(), 7, MPI_LONG_DOUBLE, MPI_SUM,
                                  MPI_COMM_WORLD);
                }
#endif
                for (int field = 0; field < 7; ++field)
                    require(std::abs(total[field] - initial[field]) <=
                                2.e-11L + 2.e-12L * std::abs(initial[field]),
                            "periodic Hydro conservation failed");
            }
        }
}
struct EndpointEos {
    const IdealGas& original;
    bool reject;
    double get_temperature(double rho,double e,const double* x) const {
        return reject?-1.:original.get_temperature(rho,e,x);
    }
    double get_pressure(const FluidVector& u,const double* x) const {return original.get_pressure(u,x);}
    double get_sound_speed(const FluidVector& u,double p,const double* x) const {
        return original.get_sound_speed(u,p,x);
    }
};
struct RuntimeRun {
    Fixture& f;
    SpeciesManager species;
    IdealGas eos;
    BCHandler bc;
    SimulationController controller;
    driver::DriverRuntime runtime;
    driver::DriverStageWorkspace workspace;
    Numerics::HydroSolverImpl<IdealGas,FluxHLLC<PCMReconstruction>> solver;
    double burn_advice=123.;
    const SpeciesManager& make_species() {
        species.add_species("X",1.,1.,1.4,1.);
        species.add_species("Y",2.,1.,1.4,1.);
        return species;
    }
    static SimConfig& settings(Fixture& f) {
        f.config.io.tmax=1.;f.config.numerics.dt_min=1.e-20;
        f.config.numerics.dt_max=0.;f.config.numerics.tstep_change_factor=2.;
        return f.config;
    }
    explicit RuntimeRun(Fixture& fixture)
        :f(fixture),eos(make_species()),bc(f.config),controller(settings(f),RunState{}),
         runtime(f.control,bc,f.config,species,controller),solver(eos) {
        bc.bind(eos,species);runtime.initialize_topology();
        f.handles=runtime.handles();
    }
    double step(int method,int fault=0) {
        SimulationController foreign_controller=controller;
        auto& proposal_controller=rank==1&&fault==22?foreign_controller:controller;
        dispatch::ResolvedExecutionPlan plan;
        plan.time_integrator=method==0?dispatch::TimeIntegratorId::Euler:
            method==1?dispatch::TimeIntegratorId::Rk2:dispatch::TimeIntegratorId::Rk3;
        const driver::IntegratorSolve integrator=method==0?&SolverEuler::solve<BCHandler>:
            method==1?&SolverRK2::solve<BCHandler>:&SolverRK3::solve<BCHandler>;
        FaultSolver faulty(solver,fault==1?1:4,f.control.tree->GetActiveBlocks().front());
        const Numerics::IHydroSolver* selected=(fault==1||fault==2)?static_cast<const Numerics::IHydroSolver*>(&faulty):&solver;
        const double cap=rank==1&&fault==7?2.e-4:rank==1&&fault==8?-1.:1.e-4;
        if(rank==1&&fault==14)f.config.physics.burn.use_burn=true;
        if(rank==1&&fault>=15&&fault<=17)
            allocation_fault::fail_after=fault==15?1:fault==16?32:80;
        const double dt=driver::execute_driver_macro_attempts(runtime,proposal_controller,burn_advice,
            cap,selected,EndpointEos{eos,rank==1&&fault==12},
            [&](double actual_dt,std::uint64_t attempt) {
                require(attempt==1,"macro inserted retry policy");
                auto context=runtime.stage_context();
                scheduler::complete_hydro_local_phase(context,[&] {
                    context.step_start_time=controller.t_current;context.step_dt=actual_dt;
                    context.configure_boundary_context=[&](double time,boundary::BoundaryPurpose purpose) {
                        bc.configure_stage(time,purpose);
                    };
                    bc.configure_stage(controller.t_current,boundary::BoundaryPurpose::Hydro);
                    if(rank==1&&fault==21)
                        context.hydro_acceptance=[](const scheduler::StageDescriptor&){};
                    if(rank==1&&fault==13)throw std::runtime_error("injected macro preparation failure");
                });
                scheduler::ScopedStageBinding binding(context,runtime.handles());
                driver::execute_driver_macro_step(runtime,context,selected,false,
                    [](driver::BurnHalf,double,state::CompletionToken token){return token;},
                    [](double){throw std::runtime_error("unexpected diffusion");},
                    [&](double value) {
                        if(rank==1&&fault==20)allocation_fault::fail_after=1;
                        driver::advance_hydro(runtime,workspace,context,
                            rank==1&&fault==19?nullptr:&plan,value,integrator,nullptr,selected);
                    },
                    [](driver::CpuStage,auto&& work){work();});
                if(rank!=1)return;
                // Each fault occurs after actual RK rotation, reflux and repair promotion.
                if(fault==3) {
                    controller.repairs.reset(2);controller.repairs.values[0]=7.;
                    throw std::runtime_error("injected final macro rejection");
                }
                auto& b=f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
                if(fault==4)b.fluid_state.rho[b.grid.GetIndex(b.grid.Is(),b.grid.Js(),0)]=-1.;
                if(fault==5)f.control.flux_register.Clear();
                if(fault==6)b.fluid_state.X(0,b.grid.GetIndex(b.grid.Is(),b.grid.Js(),0))=-.5;
                if(fault==9) {
                    controller.t_current+=actual_dt;
                    controller.step_count++;
                }
                if(fault==10)(void)runtime.perform_regrid(controller.step_count,controller.t_current);
                if(fault==11)runtime.materialize_current_for_host();
                if(fault==18) {
                    (void)context.clock.next_publication();
                    throw std::runtime_error("injected rejection after consuming a scheduler identity");
                }
            });
        if(!runtime.distributed_hydro_topology()) {
            bc.configure_stage(controller.t_current+dt,boundary::BoundaryPurpose::Hydro);
            runtime.ensure_fluid_ghosts();
        }
        controller.advance(dt);
        return dt;
    }
};
struct MacroFieldWitness {
    using Field=std::vector<double> FluidState::*;
    static constexpr std::array<Field,7> fields{&FluidState::rho,&FluidState::mom_u,
        &FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,&FluidState::enuc_rate,&FluidState::mass_fractions};
    int id;
    std::array<FluidState,3> saved;
    std::array<std::array<const double*,7>,3> addresses{};
    static std::array<FluidState*,3> slots(amr::Block& b) {return {&b.fluid_state,&b.state_next,&b.state_scratch};}
    MacroFieldWitness(amr::Block& b,int id):id(id),saved{b.fluid_state,b.state_next,b.state_scratch} {
        const auto a=slots(b);
        for(int s=0;s<3;++s)for(int f=0;f<7;++f)addresses[s][f]=(a[s]->*fields[f]).data();
    }
    static bool vector_bits(const std::vector<double>& a,const std::vector<double>& b) {
        return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),bits);
    }
    static bool repair_equal(const state::RepairBudget& a,const state::RepairBudget& b) {
        return vector_bits(a.values,b.values)&&a.block_uid==b.block_uid&&a.stage==b.stage
            &&bits(a.time,b.time)&&bits(a.position[0],b.position[0])
            &&bits(a.position[1],b.position[1])&&bits(a.position[2],b.position[2]);
    }
    void check(amr::AMRControl& control) const {
        const auto a=slots(control.pool->GetBlock(id));
        for(int s=0;s<3;++s) {
            for(int f=0;f<7;++f)
                require((a[s]->*fields[f]).data()==addresses[s][f]
                    &&vector_bits(a[s]->*fields[f],saved[s].*fields[f]),"macro failed to restore field/address");
            require(repair_equal(a[s]->stage_repairs,saved[s].stage_repairs),"macro stage receipt changed");
            require(a[s]->diffusion_boundary==saved[s].diffusion_boundary
                &&a[s]->boundary_flux_capture==saved[s].boundary_flux_capture,"macro slot alias changed");
        }
    }
};
struct LeafTopologyWitness {
    int id, index;
    std::array<amr::Block::FaceNeighbors,6> faces;
    std::array<bool,6> coarse_fine;
    explicit LeafTopologyWitness(const amr::Block& b):id(b.id),index(b.active_index) {
        std::copy_n(b.face_neighbors,6,faces.begin());
        std::copy_n(b.grid.amr_coarse_fine_face,6,coarse_fine.begin());
    }
    void check(const amr::Block& b) const {
        require(b.id==id&&b.active_index==index,"ownership rollback lost native index");
        for(int face=0;face<6;++face) {
            const auto& a=b.face_neighbors[face];const auto& old=faces[face];
            require(a.count==old.count&&a.level_diff==old.level_diff
                &&b.grid.amr_coarse_fine_face[face]==coarse_fine[face],"ownership rollback lost face metadata");
            for(int n=0;n<4;++n)require(a.blocks[n]==old.blocks[n],"ownership rollback lost global adjacency");
        }
    }
};
amr::AmrEndpoint leaf_endpoint(const RuntimeRun& run,int owner) {
    amr::AmrEndpoint source{{2,1,2,0,0},{}};
    if(rank==owner) {
        const auto& ids=run.f.control.tree->GetActiveBlocks();
        for(std::size_t i=0;i<ids.size();++i) {
            const auto& b=run.f.control.pool->GetBlock(ids[i]);
            if(b.level==1&&b.logical_x1==2&&b.logical_x2==0)
                source.handle=run.runtime.handles()[i];
        }
    }
#if ARCH_MPI_ENABLED
    std::array<std::uint64_t,2> wire{source.handle.uid.value,source.handle.epoch.value};
    MPI_Bcast(wire.data(),2,MPI_UINT64_T,owner,MPI_COMM_WORLD);
    source.handle={{wire[0]},{wire[1]}};
#endif
    return source;
}
void serial_reference_step(Run& run,int method) {
    SerialReferenceThreads serial;
    scheduler::ScopedStageBinding binding(run.context,run.f.handles);
    run.ghosts();run.step(method);run.ghosts();
}
void compare_transferred(const Fixture& actual,const Fixture& reference) { compare(actual,reference); }
void runtime_leaf_transfer_cases() {
    using Phase=driver::DriverRuntime::LeafTransferPhase;
    if(ranks==1) {
        Fixture actual(2,0,1,true);seed(actual,true);RuntimeRun run(actual);
        bool rejected=false;
        try {run.runtime.migrate_host_leaf({{2,1,2,0,0},run.runtime.handles().front()},1);}
        catch(const std::exception&) {rejected=true;}
        require(rejected,"serial runtime accepted a distributed ownership transfer");
        return;
    }
    // Receipt metadata is transported, not reset or silently regenerated.
    {
        Fixture actual(2,rank,ranks,true),reference(2,0,1,true);
        seed(actual,true);seed(reference,true);RuntimeRun a(actual);Run b(reference);
        {SerialReferenceThreads serial;scheduler::ScopedStageBinding binding(b.context,reference.handles);b.ghosts();}
        const auto mark=[](Fixture& f) {
            for(int id:f.control.tree->GetActiveBlocks()) {
                auto& block=f.control.pool->GetBlock(id);int slot=0;
                for(auto member:MacroFieldWitness::slots(block)) {
                    auto& r=member->stage_repairs;r.reset(2);
                    r.block_uid=700+block.logical_x1;r.stage=++slot;
                    r.time=-0.;r.position[0]=.125;r.position[1]=.25;r.position[2]=-.5;
                    for(std::size_t i=0;i<r.values.size();++i)r.values[i]=slot*32+i+.125;
                    // Payload slots are intentionally distinct even before a numerical step.
                    for(double& value:member->enuc_rate)value=slot==2?-0.:-.125*slot;
                }
            }
        };
        mark(actual);mark(reference);
        for(int destination:{0,1}) {
            a.runtime.migrate_host_leaf(leaf_endpoint(a,1-destination),destination);
            actual.handles=a.runtime.handles();compare(actual,reference);
            for(int id:actual.control.tree->GetActiveBlocks()) {
                const auto& block=actual.control.pool->GetBlock(id);
                const auto& expected=reference.control.pool->GetBlock(reference.control.tree->FindBlock(
                    block.level,block.logical_x1,block.logical_x2,block.logical_x3));
                for(auto member:{&amr::Block::fluid_state,&amr::Block::state_next,&amr::Block::state_scratch})
                    require(MacroFieldWitness::repair_equal((block.*member).stage_repairs,(expected.*member).stage_repairs),
                        "ownership round trip changed native repair metadata");
            }
        }
    }
    // Euler/RK2/RK3: migrate an evolved fine leaf out and back, then continue
    // the original macro driver and compare to an independent serial runtime.
    for(int method=0;method<3;++method) {
        Fixture actual(2,rank,ranks,true),reference(2,0,1,true);
        seed(actual,true);seed(reference,true);RuntimeRun a(actual);Run b(reference);
        {SerialReferenceThreads serial;scheduler::ScopedStageBinding binding(b.context,reference.handles);b.ghosts();}
        a.step(method);
        serial_reference_step(b,method);
        compare(actual,reference);
        auto endpoint=leaf_endpoint(a,1);
        const auto uid=endpoint.handle.uid;
        for(int destination:{0,1}) {
            const auto old_epoch=endpoint.handle.epoch;
            const auto old_handles=a.runtime.handles();
            const auto before_ids=actual.control.tree->GetActiveBlocks();
            std::vector<MacroFieldWitness> kept;
            for(int id:before_ids) {
                const auto& block=actual.control.pool->GetBlock(id);
                if(!(block.level==1&&block.logical_x1==2&&block.logical_x2==0))
                    kept.emplace_back(actual.control.pool->GetBlock(id),id);
            }
            const auto before_context=a.runtime.stage_context();
            const auto previous_version=before_context.clock.last_version();
            const auto previous_token=before_context.clock.last_token();
            a.runtime.migrate_host_leaf(endpoint,destination,[&](Phase phase) {
                if(phase==Phase::Reserved||phase==Phase::ReadyToPublish)
                    require(actual.control.pool->GetNumActiveBlocks()==static_cast<int>(
                        before_ids.size()+(rank==destination?1:0)),
                        "ownership transfer did not retain sender/receiver temporary peak");
            });
            for(const auto& witness:kept)witness.check(actual.control);
            actual.handles=a.runtime.handles();
            endpoint=leaf_endpoint(a,destination);
            require(endpoint.handle.uid==uid&&endpoint.handle.epoch.value==old_epoch.value+1,
                "ownership move replaced UID or failed to advance epoch");
            require(actual.handles.size()==static_cast<std::size_t>(
                rank==0?(destination==0?2:1):(destination==0?3:4)),
                "ownership move has the wrong local leaf count");
            require(actual.control.pool->GetNumReservedBlocks()==0,"ownership move leaked reservation");
            auto context=a.runtime.stage_context();
            require(context.clock.last_version()==previous_version+1
                &&context.clock.last_token()==previous_token+1,
                "ownership publication did not use exactly one scheduler witness");
            for(auto handle:a.runtime.handles()) {
                const auto current=context.ledger.inspect({handle,state::StateSlot::Current});
                require(current.interior.version.value==previous_version+1
                    &&current.interior.completion.value==previous_token+1,
                    "ownership ledger derived its witness from a block identity");
            }
            bool stale=false;
            try {(void)context.ledger.inspect({old_handles.front(),state::StateSlot::Current});}
            catch(const std::exception&) {stale=true;}
            require(stale,"ownership publication kept old epoch readable");
            compare_transferred(actual,reference);
            a.step(method);
            serial_reference_step(b,method);
            compare(actual,reference);
            require(bits(a.controller.t_current,b.context.step_start_time),"ownership changed accepted time");
        }
    }
    // Every rejection must restore the same original arrays, indices, neighbors,
    // handles and ledger owner; then the same runtime must complete a real retry.
    for(int fault=1;fault<=21;++fault) {
        Fixture actual(2,rank,ranks,true),reference(2,0,1,true);
        seed(actual,true);seed(reference,true);RuntimeRun a(actual);Run b(reference);
        {SerialReferenceThreads serial;scheduler::ScopedStageBinding binding(b.context,reference.handles);b.ghosts();}
        const auto ids=actual.control.tree->GetActiveBlocks();
        const auto handles=a.runtime.handles();
        auto context=a.runtime.stage_context();auto* ledger=&context.ledger;
        std::vector<MacroFieldWitness> fields;
        std::vector<LeafTopologyWitness> topology;
        for(int id:ids) {
            fields.emplace_back(actual.control.pool->GetBlock(id),id);
            topology.emplace_back(actual.control.pool->GetBlock(id));
        }
        const int available=actual.control.pool->GetNumAvailableBlocks();
        auto endpoint=leaf_endpoint(a,1),request=endpoint;
        if(rank==0&&fault==1)++request.handle.uid.value;
        if(rank==1&&fault==2)++request.handle.epoch.value;
        if(rank==0&&fault==3)++request.logical.logical_x2;
        std::optional<amr::MemoryPool::Reservation> occupied;
        if(rank==0&&fault==4)occupied.emplace(actual.control.pool->ReserveBlocks(available));
        int source_id=rank==1?actual.control.tree->FindBlock(1,2,0,0):-1;
        double last=0.;
        if(rank==1&&fault==14) {
            auto& rho=actual.control.pool->GetBlock(source_id).fluid_state.rho;
            last=rho.back();rho.pop_back();
        }
        if(rank==1&&fault==16)actual.control.pool->GetBlock(source_id).refine_flag=1;
#if ARCH_MPI_ENABLED
        if(rank==1&&fault>=17)ownership_wire_fault::mode=fault-16;
#endif
        int received=0;bool rejected=false;
        try {
            a.runtime.migrate_host_leaf(request,0,[&](Phase phase) {
                if(rank==0&&phase==Phase::FieldReceived)++received;
                const bool fail=(fault==5&&rank==0&&phase==Phase::Reserved)
                    ||(fault==6&&rank==0&&phase==Phase::FieldReceived&&received==1)
                    ||(fault==7&&rank==0&&phase==Phase::FieldReceived&&received==10)
                    ||(fault==8&&rank==0&&phase==Phase::FieldReceived&&received==21)
                    ||(fault==9&&rank==1&&phase==Phase::TopologyActivated)
                    ||(fault==10&&rank==0&&phase==Phase::TopologyActivated)
                    ||(fault==11&&rank==1&&phase==Phase::ReadyToPublish)
                    ||(fault==12&&rank==0&&phase==Phase::ReadyToPublish);
                if(fail)throw std::runtime_error("injected ownership rejection");
                if(rank==0&&((fault==13&&phase==Phase::ReadyToPublish)
                    ||(fault==15&&phase==Phase::Reserved)))allocation_fault::fail_after=1;
            });
        } catch(const std::exception&) {rejected=true;}
        allocation_fault::fail_after=0;
#if ARCH_MPI_ENABLED
        ownership_wire_fault::mode=0;
#endif
        int all=rejected?1:0;
#if ARCH_MPI_ENABLED
        MPI_Allreduce(MPI_IN_PLACE,&all,1,MPI_INT,MPI_MIN,MPI_COMM_WORLD);
#endif
        require(all==1,"ownership rejection did not reach every rank");
        occupied.reset();
        if(rank==1&&fault==14)actual.control.pool->GetBlock(source_id).fluid_state.rho.push_back(last);
        if(rank==1&&fault==16)actual.control.pool->GetBlock(source_id).refine_flag=0;
        require(actual.control.tree->GetActiveBlocks()==ids&&a.runtime.handles()==handles,
            "failed ownership move changed topology publication");
        require(&a.runtime.stage_context().ledger==ledger,"failed ownership move replaced ledger");
        require(actual.control.pool->GetNumAvailableBlocks()==available
            &&actual.control.pool->GetNumReservedBlocks()==0,"failed ownership move leaked capacity");
        for(const auto& witness:fields)witness.check(actual.control);
        for(const auto& witness:topology)witness.check(actual.control.pool->GetBlock(witness.id));
        compare_transferred(actual,reference);
        a.runtime.migrate_host_leaf(endpoint,0);actual.handles=a.runtime.handles();
        compare_transferred(actual,reference);
        a.runtime.migrate_host_leaf(leaf_endpoint(a,0),1);actual.handles=a.runtime.handles();
        compare_transferred(actual,reference);
        a.step(2);serial_reference_step(b,2);compare(actual,reference);
    }
    if(rank==0)std::cout<<"Ownership: 3 evolved round trips; 21 collective rejections with same-runtime retries\n";
}

// C3 first increment: real density indicators and original transfer operators.
// Threshold pairs below are fixed experiment inputs, not acceptance tolerances.
void seed_regrid_wave(Fixture& f,int budget) {
    seed(f,false);
    f.config.grid.amr_max_blocks=budget;
    f.config.amr.refine_threshold=.02;
    f.config.amr.derefine_threshold=.001;
    for(int id:f.control.tree->GetActiveBlocks()) {
        auto& block=f.control.pool->GetBlock(id);
        for(int j=0;j<block.grid.GetTotalY();++j)for(int i=0;i<block.grid.GetTotalX();++i) {
            const int c=block.grid.GetIndex(i,j,0);
            const double x=block.grid.GetCellCenterX(i);
            const double rho=1.+(std::abs(x-.75)<.1
                ? .08*(1.+std::cos(2.*std::numbers::pi*(x-.75)/.2)):0.);
            block.fluid_state.set(c,{rho,rho*.125,-rho*.0625,0.,
                2.5+.5*rho*(.125*.125+.0625*.0625)});
        }
        block.state_next=block.state_scratch=block.fluid_state;
    }
}
void request_coarsening(Fixture& f) {
    f.config.amr.refine_threshold=.9;
    f.config.amr.derefine_threshold=.8;
}
// Independent serial owner: original tree, registry and numerical operators.
// A serial DriverRuntime inside a live two-rank WORLD would misidentify its owner.
void serial_reference_regrid(Fixture& f,std::unique_ptr<Run>& run,double time) {
    SerialReferenceThreads serial;
    run.reset();
    auto prepared=f.control.tree->PrepareRegrid(f.config);
    std::vector<topology::TopologyObservation> observations;
    for(int id:prepared.proposed_active_blocks()) {
        const auto& b=f.control.pool->GetBlock(id);
        observations.push_back({id,{2,b.level,b.logical_x1,b.logical_x2,b.logical_x3},0});
    }
    auto candidate=f.registry.stage_reconciliation(observations);
    auto next=candidate.reconciliation().handles_in_observation_order;
    require(prepared.topology_changed(),"serial indicator did not request the expected regrid");
    prepared.BuildMigrationPlans(f.handles,next,
        {f.registry.epoch().value,f.registry.epoch(),candidate.reconciliation().epoch});
    prepared.ExecuteMigration();
    prepared.ActivateForFinalization(candidate.blocks());
    prepared.PublishNoexcept();
    f.registry.commit(std::move(candidate));
    f.handles=std::move(next);
    f.control.BindActiveHandles(f.handles);
    prepared.ReleaseRetiredNoexcept();
    run=std::make_unique<Run>(f);
    run->context.step_start_time=time;
    scheduler::ScopedStageBinding binding(run->context,f.handles);
    run->ghosts();
}
bool runtime_regrid(RuntimeRun& run,
    const std::function<void(driver::DriverRuntime::RegridPhase)>& observer={}) {
    const bool changed=run.runtime.perform_regrid(
        run.controller.step_count,run.controller.t_current,false,observer);
    run.f.handles=run.runtime.handles();
    return changed;
}
struct RegridWitness {
    std::vector<MacroFieldWitness> fields;
    std::vector<LeafTopologyWitness> topology;
    std::vector<int> active,flags;
    std::vector<amr::BlockHandle> handles;
    const state::StateResidencyLedger* ledger;
    int available;
    explicit RegridWitness(RuntimeRun& run)
        :active(run.f.control.tree->GetActiveBlocks()),handles(run.runtime.handles()),
         ledger(&run.runtime.stage_context().ledger),
         available(run.f.control.pool->GetNumAvailableBlocks()) {
        for(int id:active) {
            const auto& b=run.f.control.pool->GetBlock(id);
            fields.emplace_back(run.f.control.pool->GetBlock(id),id);
            topology.emplace_back(b);flags.push_back(b.refine_flag);
        }
    }
    void check(RuntimeRun& run) const {
        require(run.f.control.tree->GetActiveBlocks()==active
            &&run.runtime.handles()==handles,"regrid rollback changed active identities");
        require(&run.runtime.stage_context().ledger==ledger,"regrid rollback replaced ledger");
        require(run.f.control.pool->GetNumAvailableBlocks()==available
            &&run.f.control.pool->GetNumReservedBlocks()==0,"regrid rollback leaked original pool capacity");
        for(std::size_t i=0;i<active.size();++i) {
            fields[i].check(run.f.control);
            const auto& b=run.f.control.pool->GetBlock(active[i]);
            topology[i].check(b);
            require(b.refine_flag==flags[i],"regrid rollback changed original indicator decision");
        }
    }
};
void require_conservation(const Fixture& f,const std::array<long double,7>& initial) {
    auto sum=totals(f);
#if ARCH_MPI_ENABLED
    if(ranks>1) {auto local=sum;MPI_Allreduce(local.data(),sum.data(),7,MPI_LONG_DOUBLE,MPI_SUM,MPI_COMM_WORLD);}
#endif
    for(int field=0;field<7;++field)
        require(std::abs(sum[field]-initial[field])<=2.e-11L+2.e-12L*std::abs(initial[field]),
            "dynamic regrid conservation failed");
}
void runtime_regrid_cases() {
    using Phase=driver::DriverRuntime::RegridPhase;
    if(ranks==1)return; // Original serial regrid has its own transaction tests.
    for(int method=0;method<3;++method) {
        Fixture actual(2,rank,ranks,true,false,rank==0?3:5),reference(2,0,1,true,false);
        seed_regrid_wave(actual,8);seed_regrid_wave(reference,8);
        RuntimeRun a(actual);auto b=std::make_unique<Run>(reference);
        const auto initial=totals(reference);
        a.step(method);serial_reference_step(*b,method);compare(actual,reference);
        const auto old=actual.handles;
        require(runtime_regrid(a),"an unchanged rank missed the global refine publication");
        serial_reference_regrid(reference,b,a.controller.t_current);
        require(actual.handles.size()==(rank==0?1:4),"wrong first-level local leaf count");
        require(actual.handles.front().epoch.value>old.front().epoch.value,"regrid did not advance epoch");
        if(rank==0)require(actual.handles.front().uid==old.front().uid,"unchanged root lost UID");
        else for(auto h:actual.handles)require(h.uid!=old.front().uid,"retired root UID reused");
        compare(actual,reference);require_conservation(actual,initial);
        a.step(method);serial_reference_step(*b,method);compare(actual,reference);
        // C2 interoperation: split a family, reject regrid, then return that leaf.
        a.runtime.migrate_host_leaf(leaf_endpoint(a,1),0);actual.handles=a.runtime.handles();
        RegridWitness split(a);
        bool rejected=false;
        try {runtime_regrid(a);}catch(const std::logic_error& error) {
            rejected=std::string(error.what()).find("colocated sibling")!=std::string::npos;
        }
        require(rejected,"split sibling ownership was not rejected");split.check(a);
        a.runtime.migrate_host_leaf(leaf_endpoint(a,0),1);actual.handles=a.runtime.handles();
        compare(actual,reference);
        request_coarsening(actual);request_coarsening(reference);
        require(runtime_regrid(a),"distributed sibling coarsening did not publish");
        serial_reference_regrid(reference,b,a.controller.t_current);
        require(actual.handles.size()==1,"coarsening did not restore two global roots");
        if(rank==1)require(actual.handles.front().uid!=old.front().uid,"coarsening reused a retired UID");
        compare(actual,reference);require_conservation(actual,initial);
        a.step(method);serial_reference_step(*b,method);compare(actual,reference);
        require_conservation(actual,initial);
        RegridWitness nochange(a);
        require(!runtime_regrid(a),"unchanged global topology reported a regrid");
        nochange.check(a);
    }
    // Both changing and unchanged ranks may fail at every publication phase.
    // Both refine and coarsen must restore leases, flags, indices and epoch.
    constexpr std::array<Phase,6> phases{Phase::Selected,Phase::Constructed,
        Phase::Migrated,Phase::TopologyActivated,Phase::GhostsSynchronized,Phase::ReadyToPublish};
    int failures=0;
    for(bool coarsen:{false,true})for(int victim:{0,1})for(auto phase:phases) {
        Fixture actual(2,rank,ranks,true,false,rank==0?1:5),reference(2,0,1,true,false);
        seed_regrid_wave(actual,6);seed_regrid_wave(reference,6);
        RuntimeRun a(actual);auto b=std::make_unique<Run>(reference);
        if(coarsen) {
            require(runtime_regrid(a),"precondition refine failed");
            serial_reference_regrid(reference,b,a.controller.t_current);
            request_coarsening(actual);request_coarsening(reference);
        }
        RegridWitness before(a);
        const auto c=a.runtime.stage_context();
        const auto ledger=c.ledger.snapshot_metadata(state::ExecutionSide::Host);
        const auto bc=a.bc.snapshot_stage_context();
        bool rejected=false,observed=false;
        try {runtime_regrid(a,[&](Phase current) {
            if(rank==victim&&current==phase) {observed=true;throw std::runtime_error("injected regrid phase failure");}
        });}catch(const std::exception&) {rejected=true;}
        require(rejected&&(rank!=victim||observed),"single-rank regrid failure did not reach all ranks");
        before.check(a);
        require(c.ledger.metadata_snapshot_matches(ledger),"regrid rollback changed accepted ledger metadata");
        require(a.bc.stage_context_matches(bc),"regrid rollback changed boundary context");
        require(runtime_regrid(a),"same-runtime regrid retry did not publish");
        serial_reference_regrid(reference,b,a.controller.t_current);
        compare(actual,reference);
        a.step(2);serial_reference_step(*b,2);compare(actual,reference);
        ++failures;
    }
    // Real allocation failures before metadata transport, staged binding and commit.
    for(auto phase:{Phase::Constructed,Phase::TopologyActivated,Phase::ReadyToPublish}) {
        Fixture actual(2,rank,ranks,true,false,rank==0?1:5);
        seed_regrid_wave(actual,6);RuntimeRun a(actual);RegridWitness before(a);
        bool rejected=false;
        try {runtime_regrid(a,[&](Phase current) {
            if(rank==1&&current==phase)allocation_fault::fail_after=1;
        });}catch(const std::exception& error) {
            rejected=true;
            if(rank==1)require(std::string(error.what()).find("bad_alloc")!=std::string::npos,
                "allocation failure was rejected for another reason");
        }
        allocation_fault::fail_after=0;
        require(rejected,"regrid accepted a local allocation failure");before.check(a);
        require(runtime_regrid(a),"allocation rejection prevented same-runtime retry");
        ++failures;
    }
    // All allocations inside the metadata collector, including those between
    // count and payload collectives, must fail collectively and permit reuse.
    for(int ordinal=1;ordinal<=8;++ordinal) {
        std::vector<topology::TopologyObservation> local{
            {rank+7,{2,0,static_cast<std::uint32_t>(rank),0,0},rank}};
        bool rejected=false;
        if(rank==1)allocation_fault::fail_after=ordinal;
        try {(void)topology::collect_topology_observations(local,rank,ranks);}
        catch(const std::exception& error) {
            rejected=true;
            if(rank==1)require(std::string(error.what()).find("bad_alloc")!=std::string::npos,
                "metadata allocation failed for another reason");
        }
        allocation_fault::fail_after=0;
        require(rejected,"metadata allocation failure did not complete collectively");
        const auto collected=topology::collect_topology_observations(local,rank,ranks);
        require(collected.size()==2,"metadata collector could not retry");
        for(int peer=0;peer<ranks;++peer)
            require(collected[peer].owner_rank==peer
                &&collected[peer].pool_index==(peer==rank?rank+7:-1),
                "metadata collector transported a remote native pool index");
    }
    for(int fault=1;fault<=9;++fault) {
        Fixture actual(2,rank,ranks,true,false,rank==0?3:5);
        seed_regrid_wave(actual,8);RuntimeRun a(actual);RegridWitness before(a);
        auto c=a.runtime.stage_context();
        auto ledger=c.ledger.snapshot_metadata(state::ExecutionSide::Host);
        const auto config=actual.config;
        std::optional<amr::MemoryPool::Reservation> borrowed;
        if(rank==1) {
            if(fault==3)actual.config.amr.refine_threshold=.03;
            if(fault==4)actual.config.grid.amr_max_blocks=9;
            if(fault==5)actual.config.grid.x1l_boundary_type="outflow";
            if(fault==6)actual.config.amr.lrefinemax=2;
            if(fault==7) {
                c.ledger.freeze_metadata_snapshot(ledger);
                c.ledger.publish_interior({actual.handles.front(),state::StateSlot::Current},
                    state::ExecutionSide::Host,{2},{3,state::CompletionState::Complete});
            }
            if(fault==8)borrowed.emplace(actual.control.pool->ReserveBlocks(1));
            if(fault==9)actual.config.physics.burn.use_burn=true;
        }
        bool rejected=false;
        try {(void)a.runtime.perform_regrid(a.controller.step_count+(rank==1&&fault==1?1:0),
            a.controller.t_current+(rank==1&&fault==2?1.e-4:0.));}
        catch(const std::exception&) {rejected=true;}
        require(rejected,"regrid accepted inconsistent controls, stale state or borrowed storage");
        borrowed.reset();actual.config=config;
        if(rank==1&&fault==7) {
            c.ledger.restore_metadata_snapshot_noexcept(ledger);
            c.ledger.release_metadata_snapshot(ledger);
        } else require(c.ledger.metadata_snapshot_matches(ledger),"rejected controls changed accepted metadata");
        before.check(a);
        require(runtime_regrid(a),"restored regrid controls could not retry");
        ++failures;
    }
    // Global temporary peak is six slots, despite only five final leaves.
    // Local quota exhaustion is distinct from that global budget.
    for(int mode=0;mode<3;++mode) {
        const int capacity=mode==0?(rank==0?1:5):mode==1?(rank==0?1:4):3;
        const int budget=mode==1?5:6;
        Fixture actual(2,rank,ranks,true,false,capacity);
        seed_regrid_wave(actual,budget);RuntimeRun a(actual);RegridWitness before(a);
        int peak=0;bool rejected=false;
        try {
            require(runtime_regrid(a,[&](Phase phase) {
                if(phase==Phase::Constructed)peak=actual.control.pool->GetNumActiveBlocks();
            }),"capacity case did not request refinement");
        }catch(const std::exception& error) {
            rejected=true;
            if(mode==1)require(std::string(error.what()).find("global max_blocks")!=std::string::npos,
                "global capacity failed for another reason");
        }
        require(rejected==(mode!=0),"temporary peak/local quota guard failed");
        if(rejected) {
            before.check(a);
            request_coarsening(actual); // Restore a feasible no-change request in this same owner.
            require(!runtime_regrid(a),"capacity rejection prevented a subsequent no-change transaction");
            ++failures;
        } else {
            require(peak==(rank==0?1:5),"minimum six-slot pool peak was not observed");
            require(actual.control.pool->GetNumActiveBlocks()==(rank==0?1:4),"retired root slot leaked");
            request_coarsening(actual);require(runtime_regrid(a),"minimum pool could not coarsen");
            require(actual.control.pool->GetNumActiveBlocks()==1,"retired sibling slots leaked");
        }
    }
    if(rank==0)std::cout<<"Regrid: 3 evolved 2/5/2-leaf cycles; "<<failures
        <<" phase/allocation/control/capacity rejections with same-runtime retries; 3 split-family rejections; 8 metadata allocation retries; six-slot peak\n";
}

void runtime_macro_cases() {
    for(int method=0;method<3;++method)for(bool wave:{false,true}) {
        Fixture actual(2,rank,ranks,true),reference(2,0,1,true);
        seed(actual,wave);seed(reference,wave);
        RuntimeRun a(actual);Run b(reference);
        const auto initial=totals(reference);
        for(int step=0;step<3;++step) {
            {
                SerialReferenceThreads serial;
                scheduler::ScopedStageBinding binding(b.context,reference.handles);
                b.ghosts();b.step(method);b.ghosts();
            }
            a.step(method);
            compare(actual,reference);
            require(a.controller.step_count==step+1&&bits(a.controller.t_current,b.context.step_start_time),
                "macro accepted time/step mismatch");
            auto c=a.runtime.stage_context();
            const auto version=c.ledger.inspect({a.runtime.handles().front(),state::StateSlot::Current}).interior.version;
            for(auto h:a.runtime.handles())
                c.ledger.require_readable({h,state::StateSlot::Current},{state::ExecutionSide::Host,version,true,true});
            auto sum=totals(actual);
#if ARCH_MPI_ENABLED
            if(ranks>1) {auto local=sum;MPI_Allreduce(local.data(),sum.data(),7,MPI_LONG_DOUBLE,MPI_SUM,MPI_COMM_WORLD);}
#endif
            for(int f=0;f<7;++f)require(std::abs(sum[f]-initial[f])<=2.e-11L+2.e-12L*std::abs(initial[f]),
                "macro conservation failed");
        }
    }
    if(ranks==1)return;
    for(int fault=1;fault<=22;++fault) {
        Fixture actual(2,rank,ranks,true),reference(2,0,1,true);
        seed(actual,true);seed(reference,true);
        RuntimeRun a(actual);Run b(reference);
        std::vector<MacroFieldWitness> fields;
        for(int id:actual.control.tree->GetActiveBlocks())fields.emplace_back(actual.control.pool->GetBlock(id),id);
        auto c=a.runtime.stage_context();
        const auto ledger=c.ledger.snapshot_metadata(state::ExecutionSide::Host);
        const auto flux=actual.control.flux_register.snapshot_host();
        const auto bc=a.bc.snapshot_stage_context();
        const auto ctrl=a.controller;
        const auto clock=c.clock.last_token();
        const double advice=a.burn_advice;
        bool rejected=false;
        try {a.step(2,fault);}catch(const std::exception& error) {
            rejected=true;
            std::cout<<"macro rejection rank="<<rank<<" fault="<<fault<<" reason="<<error.what()<<std::endl;
            if(rank==1) {
                constexpr std::array<const char*,22> reasons{
                    "injected local solver rejection","injected local solver rejection",
                    "injected final macro rejection","final EOS rejected","Current/reflux incomplete",
                    "final EOS rejected","interval/identity mismatch","dt too small",
                    "changed physical time","Cannot regrid","Cannot expose","final EOS rejected",
                    "injected macro preparation failure","requires static Cartesian",
                    "std::bad_alloc","std::bad_alloc","std::bad_alloc","consuming a scheduler identity",
                    "requires its selected plan","std::bad_alloc","acceptance already has another owner",
                    "controller owner mismatch"};
                require(std::string(error.what()).find(reasons[fault-1])!=std::string::npos,
                    "macro rejected for the wrong reason");
            }
        }
        allocation_fault::fail_after=0;
        actual.config.physics.burn.use_burn=false;
        require(rejected,"macro accepted injected failure");
        for(const auto& saved:fields)saved.check(actual.control);
        require(c.ledger.metadata_snapshot_matches(ledger),"macro ledger not restored");
        require(actual.control.flux_register.host_snapshot_matches(flux),"macro flux not restored");
        require(a.bc.stage_context_matches(bc),"macro boundary clock not restored");
        require(bits(a.controller.t_current,ctrl.t_current)&&a.controller.step_count==ctrl.step_count
            &&bits(a.controller.dt_old,ctrl.dt_old)&&bits(a.burn_advice,advice)
            &&a.controller.plt_file_index==ctrl.plt_file_index&&a.controller.chk_file_index==ctrl.chk_file_index
            &&bits(a.controller.next_plt_time,ctrl.next_plt_time)&&bits(a.controller.next_chk_time,ctrl.next_chk_time)
            &&a.controller.suppress_step_io_once==ctrl.suppress_step_io_once
            &&MacroFieldWitness::repair_equal(a.controller.repairs,ctrl.repairs),"macro controller/receipt not restored");
        require(c.clock.last_token()>=clock,"macro reused scheduler identity");
        {
            SerialReferenceThreads serial;
            scheduler::ScopedStageBinding binding(b.context,reference.handles);
            b.ghosts();b.step(2);b.ghosts();
        }
        a.step(2);
        compare(actual,reference);
        require(a.controller.step_count==1&&bits(a.controller.t_current,1.e-4),"retry did not accept once");
    }
    if(rank==0)std::cout<<"Runtime macros: six three-step cases; 22 collective rejections with restoration and retry passed\n";
}
void rejected_cases() {
    if (ranks == 1)
        return;
    for (int fault = 1; fault <= 10; ++fault) {
        Fixture actual(2, rank, ranks, true);
        seed(actual, true);
        Run run(actual);
        scheduler::ScopedStageBinding binding(run.context, actual.handles);
        run.ghosts();
        const auto original = totals(actual);
        FaultSolver solver(run.solver, fault, actual.control.tree->GetActiveBlocks().front());
        if (fault == 5 && rank == 1)
            run.ledger.publish_interior({actual.handles.front(), state::StateSlot::Current},
                                        state::ExecutionSide::Host, {2},
                                        {3, state::CompletionState::Complete});
        if (fault == 6)
            run.context.configure_boundary_context = [&](double, boundary::BoundaryPurpose) {
                if (rank == 1)
                    throw std::runtime_error("injected boundary preparation failure");
            };
        if (fault == 7 && rank == 1)
            actual.control.pool->GetBlock(actual.control.tree->GetActiveBlocks().front())
                .grid.geometry = "bad";
        if (fault == 8)
            run.context.hydro_acceptance = [&](const scheduler::StageDescriptor&) {
                if (rank == 1)
                    throw std::runtime_error("injected acceptance failure");
            };
        if (fault == 9 && rank == 1)
            run.clock =
                scheduler::MonotonicSchedulerClock{std::numeric_limits<std::uint64_t>::max(), 1};
        if (fault == 10)
            run.context.configure_boundary_context = [&](double time,
                                                         boundary::BoundaryPurpose purpose) {
                if (rank == 1 && time > 0.)
                    throw std::runtime_error("injected output boundary failure");
                run.bc.configure_stage(time, purpose);
            };
        bool rejected = false;
        try {
            run.step(2, &solver);
        } catch (const std::exception&) {
            rejected = true;
        }
        int all = rejected ? 1 : 0;
#if ARCH_MPI_ENABLED
        MPI_Allreduce(MPI_IN_PLACE, &all, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
#endif
        require(all == 1, "single-rank failure did not reject every rank");
        if (fault != 7)
            require(totals(actual) == original, "failed pre-rotation stage changed Current");
        if (fault == 8 || fault == 9)
            for (auto handle : actual.handles)
                require(
                    run.ledger.inspect({handle, state::StateSlot::Scratch}).interior.residency ==
                        state::StateResidency::Invalid,
                    "failed stage published an output");
        if (fault == 10)
            for (auto handle : actual.handles)
                require(run.ledger.inspect({handle, state::StateSlot::Scratch}).ghost.residency ==
                            state::StateResidency::Invalid,
                        "failed boundary published ghosts");

        if (fault <= 3)
            for (int id : actual.control.tree->GetActiveBlocks())
                for (int face = 0; face < 4; ++face)
                    require(!actual.control.flux_register.HasData(id, face),
                            "failed first stage published flux");
        if (fault <= 3) {
            run.step(2);
            require(run.context.step_start_time == run.context.step_dt,
                    "recovery did not complete one step");
        }
    }
}
void receipt_and_boundary_rejections() {
    if (ranks == 1)
        return;
    Fixture actual(2, rank, ranks, true);
    seed(actual, true);
    struct Receipt {
        int reads = 0, commits = 0;
        double registered_energy(const amr::AmrFluxTopologyPlan&,
                                 const amr::AmrFluxRegistrationRoute&, std::size_t, int,
                                 double energy, double, double) {
            ++reads;
            return energy;
        }
        void commit_registration(const amr::AmrFluxRegistrationRoute&) { ++commits; }
    } receipt;
    bool rejected = false;
    try {
        actual.control.ExecuteDistributedHydroStage(0, 1., [&] {
            const auto& topology = actual.control.RequireFluxTopologyPlan(2);
            for (const auto& route : topology.routes) {
                const auto& b = actual.control.pool->GetBlock(route.key.source_block);
                std::vector<FluidVector> flux(b.grid.GetTotalSize(),
                                              FluidVector{.125, 0., 0., 0., .25});
                std::vector<double> species(2 * b.grid.GetTotalSize(), .0625);
                amr::RegisterCoarseFineFluxes(actual.control, topology, b.id, b.grid,
                                              amr::axis_value(route.key.axis), flux, species, 2, 1.,
                                              rank == 1 ? &receipt : nullptr);
            }
        });
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && receipt.reads == 0 && receipt.commits == 0,
            "borrowed EnergyReceipt escaped stage boundary");
    for (int id : actual.control.tree->GetActiveBlocks())
        for (int face = 0; face < 4; ++face)
            require(!actual.control.flux_register.HasData(id, face),
                    "rejected receipt published register");
    Run run(actual);
    scheduler::ScopedStageBinding binding(run.context, actual.handles);
    struct FailingBoundary {
        BCHandler& actual;
        void apply(FluidState& state, const Grid& grid) {
            if (rank == 1)
                throw std::runtime_error("injected physical boundary failure");
            actual.apply(state, grid);
        }
    } boundary{run.bc};
    rejected = false;
    try {
        TimeIntegration::synchronize_domain_boundary(actual.control, boundary,
                                                     &amr::Block::fluid_state, actual.handles);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "physical boundary failure was not collective");
    run.ghosts();
    run.step(0); // Original owner is usable after failed captures.
}

} // namespace
int main(int argc, char** argv) {
#if ARCH_MPI_ENABLED
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
#endif
    try {
        require(argc == 2, "test requires an explicit expected thread count");
        const int expected_threads = std::stoi(argv[1]);
        int observed_threads = 1;
#ifdef _OPENMP
#pragma omp parallel
        {
#pragma omp single
            observed_threads = omp_get_num_threads();
        }
#endif
        require(expected_threads >= 1 && observed_threads == expected_threads,
                "actual OpenMP team does not match the requested test lane");
#if ARCH_MPI_ENABLED
        require(provided >= MPI_THREAD_FUNNELED, "MPI did not provide FUNNELED");
#endif
        require(ranks == 1 || ranks == 2, "test requires one or two ranks");
        successful_cases();
        runtime_macro_cases();
        runtime_leaf_transfer_cases();
        runtime_regrid_cases();
        rejected_cases();
        receipt_and_boundary_rejections();
        if (rank == 0)
            std::cout << "Hydro: 6 method/field cases x 3 steps; collective rejection cases="
                      << (ranks > 1 ? 12 : 0) << "; actual threads=" << observed_threads << "; reference threads=1\n";
    } catch (const std::exception& error) {
        std::cerr << "rank " << rank << ": " << error.what() << "\n";
#if ARCH_MPI_ENABLED
        MPI_Abort(MPI_COMM_WORLD, 1);
#endif
        return 1;
    }
#if ARCH_MPI_ENABLED
    MPI_Finalize();
#endif
}
