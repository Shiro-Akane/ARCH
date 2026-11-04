/**
 * @file test_distributed_hydro_stage.cpp
 * @brief Original Host Hydro lanes on a prescribed mixed-level MPI mesh.
 * No application AMR, migration, source journal or whole-step rollback claim.
 */
#include "fixtures/amr/partitioned_topology_fixture.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/integrator/HydroSolverImpl.h"
#include "numerics/integrator/TimeIntegratorEuler.h"
#include "numerics/integrator/TimeIntegratorRK2.h"
#include "numerics/integrator/TimeIntegratorRK3.h"
#include "physics/eos/IdealGas.h"
#include <bit>
#include <iostream>
#include <numbers>
#ifdef _OPENMP
#include <omp.h>
#endif
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
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
