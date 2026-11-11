/**
 * @file test_coarse_fine_exchange.cpp
 * @brief Real owner-side coarse/fine payloads, bitwise reference and atomic failure.
 * These operator fixtures do not claim a distributed AMR time step or reflux.
 */
#include "fixtures/amr/partitioned_topology_fixture.h"
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
#include <bit>
#include <iostream>
#include <set>

namespace {
using Fixture = amr::test::PartitionedTopologyFixture;
using Slot = FluidState amr::Block::*;
constexpr std::array<Slot, 3> slots{&amr::Block::fluid_state, &amr::Block::state_next,
                                    &amr::Block::state_scratch};
int rank = 0, ranks = 1;
void require(bool good, const char* message) {
    if (!good)
        throw std::runtime_error(message);
}
bool same(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
std::vector<std::uint64_t> snapshot(const Fixture& f) {
    std::vector<std::uint64_t> words;
    for (int id : f.control.tree->GetActiveBlocks())
        for (const auto slot : slots) {
            const auto& s = f.control.pool->GetBlock(id).*slot;
            for (const auto* v :
                 {&s.rho, &s.mom_u, &s.mom_v, &s.mom_w, &s.eng, &s.enuc_rate, &s.mass_fractions}) {
                words.push_back(v->size());
                for (double value : *v)
                    words.push_back(std::bit_cast<std::uint64_t>(value));
            }
        }
    return words;
}
void seed(Fixture& f, bool constant) {
    for (int id : f.control.tree->GetActiveBlocks()) {
        auto& b = f.control.pool->GetBlock(id);
        amr::test::seed_regrid_parent(b);
        b.state_next = b.state_scratch = b.fluid_state;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            auto& s = b.*slots[index];
            for (std::size_t cell = 0; cell < s.rho.size(); ++cell) {
                if (constant) {
                    s.rho[cell] = 2.;
                    s.mom_u[cell] = .25;
                    s.mom_v[cell] = -.5;
                    s.mom_w[cell] = .125;
                    s.eng[cell] = 4.;
                    s.enuc_rate[cell] = -.25;
                    s.X(0, cell) = .25;
                    s.X(1, cell) = .75;
                } else {
                    // Distinct endpoints and time slots catch wrong routing even when
                    // a remote face has the same cell count as another face.
                    s.rho[cell] +=
                        index + b.logical_x1 / 16. + b.logical_x2 / 32. + b.logical_x3 / 64.;
                    s.eng[cell] += 2. * index + b.level;
                    s.enuc_rate[cell] -= index / 2.;
                }
            }
        }
    }
}
auto plan(Fixture& f, int dim) {
    return f.control.ghost_exchange.BuildCoarseFinePlan(f.control.pool, f.control.tree, dim,
                                                        f.handles);
}
void execute(Fixture& f, const amr::CoarseFineTransferPlan& p, int dim, Slot slot) {
    f.control.ghost_exchange.ExecuteCoarseFinePlan(p, f.control.pool, f.control.tree, dim, slot,
                                                   f.handles);
}
void compare(const Fixture& actual, const Fixture& reference) {
    for (int id : actual.control.tree->GetActiveBlocks()) {
        const auto& b = actual.control.pool->GetBlock(id);
        const int expected_id =
            reference.control.tree->FindBlock(b.level, b.logical_x1, b.logical_x2, b.logical_x3);
        require(expected_id >= 0, "reference leaf missing");
        const auto& r = reference.control.pool->GetBlock(expected_id);
        for (const auto slot : slots) {
            const auto& a = b.*slot;
            const auto& e = r.*slot;
            const auto af = amr::test::regrid_fields(a), ef = amr::test::regrid_fields(e);
            require(a.rho.size() == e.rho.size(), "field extent differs");
            for (std::size_t field = 0; field < af.size(); ++field)
                for (std::size_t cell = 0; cell < a.rho.size(); ++cell)
                    require(same(af[field][cell], ef[field][cell]), "field bitwise mismatch");
            require(a.mass_fractions.size() == e.mass_fractions.size(), "species extent differs");
            for (std::size_t cell = 0; cell < a.mass_fractions.size(); ++cell)
                require(same(a.mass_fractions[cell], e.mass_fractions[cell]),
                        "species bitwise mismatch");
        }
    }
}
void check_case(int dim, bool periodic, bool constant, bool refine = true) {
    Fixture reference(dim, 0, 1, periodic, refine), actual(dim, rank, ranks, periodic, refine);
    seed(reference, constant);
    seed(actual, constant);
    const auto expected = plan(reference, dim), current = plan(actual, dim);
    require(current.operations.empty() == !refine, "fixture did not exercise expected work");
    const auto cells = amr::compile_coarse_fine_cell_plan(current, 2);
    std::set<std::pair<int, int>> targets;
    for (const auto& t : cells.transfers) {
        const auto& k = t.destination.logical;
        const int id =
            actual.control.tree->FindBlock(k.level, k.logical_x1, k.logical_x2, k.logical_x3);
        const auto& g = actual.control.pool->GetBlock(id).grid;
        const int cell = amr::coarse_fine_host::cell_index(g, t.destination_cell);
        targets.emplace(id, cell);
        const auto& c = t.destination_cell;
        require(c[0] < 0 || c[0] >= amr::BLOCK_NX ||
                    (dim >= 2 && (c[1] < 0 || c[1] >= amr::BLOCK_NY)) ||
                    (dim == 3 && (c[2] < 0 || c[2] >= amr::BLOCK_NZ)),
                "destination is active");
    }
    std::vector<std::vector<FluidState>> before;
    for (int id : actual.control.tree->GetActiveBlocks()) {
        const auto& b = actual.control.pool->GetBlock(id);
        before.push_back({b.fluid_state, b.state_next, b.state_scratch});
    }
    const auto count = actual.control.pool->GetNumActiveBlocks();
    for (const auto slot : slots) {
        execute(reference, expected, dim, slot);
        execute(actual, current, dim, slot);
        compare(actual, reference);
    }
    require(actual.control.pool->GetNumActiveBlocks() == count, "exchange allocated remote blocks");
    std::size_t block_index = 0;
    for (int id : actual.control.tree->GetActiveBlocks()) {
        const auto& b = actual.control.pool->GetBlock(id);
        for (std::size_t slot = 0; slot < slots.size(); ++slot) {
            const auto& s = b.*slots[slot];
            const auto& old = before[block_index][slot];
            const auto fields = amr::test::regrid_fields(s), saved = amr::test::regrid_fields(old);
            const std::array<double, 6> constants{2., .25, -.5, .125, 4., -.25};
            for (std::size_t cell = 0; cell < s.rho.size(); ++cell) {
                const bool target = targets.contains({id, static_cast<int>(cell)});
                for (std::size_t field = 0; field < fields.size(); ++field) {
                    if (!target)
                        require(same(fields[field][cell], saved[field][cell]),
                                "write outside plan");
                    else if (constant)
                        require(same(fields[field][cell], constants[field]),
                                "constant not preserved");
                }
                for (int sp = 0; sp < 2; ++sp)
                    if (!target)
                        require(same(s.X(sp, cell), old.X(sp, cell)), "species write outside plan");
                    else if (constant)
                        require(same(s.X(sp, cell), sp == 0 ? .25 : .75),
                                "constant rho X not preserved");
            }
        }
        ++block_index;
    }
}
void duplicate_logical_blocks() {
    Fixture f(2, 0, 1, false);
    seed(f, false);
    auto empty = plan(f, 2);
    empty.operations.clear();
    amr::finalize_amr_plan(empty);
    const auto& active = f.control.tree->GetActiveBlocks();
    const auto& first = f.control.pool->GetBlock(active[0]);
    auto& second = f.control.pool->GetBlock(active[1]);
    second.level = first.level;
    second.logical_x1 = first.logical_x1;
    second.logical_x2 = first.logical_x2;
    second.logical_x3 = first.logical_x3;
    const auto before = snapshot(f);
    bool rejected = false;
    try {
        execute(f, empty, 2, slots[0]);
    } catch (const std::invalid_argument& error) {
        rejected = std::string(error.what()).find("duplicate logical block") != std::string::npos;
    }
    require(rejected, "distinct handles hid a duplicate logical block");
    require(before == snapshot(f), "duplicate logical view wrote a field");
}
void failures() {
    for (int fault = 0; fault < (ranks == 2 ? 7 : 4); ++fault) {
        Fixture f(2, rank, ranks, false);
        seed(f, false);
        auto p = plan(f, 2);
        auto slot = slots[0];
        auto& b = f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
        const auto saved = b.fluid_state;
        const auto neighbors = std::to_array(b.face_neighbors);
        if (rank == 0) {
            if (fault == 0)
                p.operations.front().source.handle.epoch.value += 1;
            if (fault == 1)
                b.fluid_state.mom_u.pop_back();
            if (fault == 2)
                std::fill(b.fluid_state.rho.begin(), b.fluid_state.rho.end(), -1.);
            if (fault == 3)
                f.handles.front().epoch.value += 1;
            if (fault == 4)
                slot = slots[1];
            if (fault == 5)
                b.fluid_state.InitSpecies(3);
            if (fault == 6)
                for (auto& face : b.face_neighbors)
                    for (int i = 0; i < face.count; ++i)
                        face.blocks[i].owner_rank = ranks;
        }
        const auto before = snapshot(f);
        bool rejected = false;
        try {
            execute(f, p, 2, slot);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "one-rank failure did not reject on every rank");
        require(before == snapshot(f), "failed exchange partially wrote a field");
        // A collective preparation failure leaves the communicator usable.
        b.fluid_state = saved;
        std::copy(neighbors.begin(), neighbors.end(), std::begin(b.face_neighbors));
        if (rank == 0 && fault == 3)
            f.handles.front().epoch.value -= 1;
        execute(f, plan(f, 2), 2, slots[0]);
    }
}
} // namespace
int main(int argc, char** argv) {
#if ARCH_MPI_ENABLED
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
#else
    (void)argc;
    (void)argv;
#endif
    try {
        require(ranks == 1 || ranks == 2, "one or two ranks required");
        for (int dim = 1; dim <= 3; ++dim)
            for (bool periodic : {false, true}) {
                for (bool constant : {false, true})
                    check_case(dim, periodic, constant);
                check_case(dim, periodic, false, false);
            }
        duplicate_logical_blocks();
        failures();
        if (rank == 0)
            std::cout << "coarse/fine exchange passed: 1D/2D/3D, both boundaries, "
                         "three slots, bitwise fields/species, constants, unchanged active cells, "
                         "zero-work collectives, atomic failures and recovery\n";
    } catch (const std::exception& e) {
        std::cerr << "rank " << rank << ": " << e.what() << '\n';
#if ARCH_MPI_ENABLED
        MPI_Abort(MPI_COMM_WORLD, 1);
#endif
        return 1;
    }
#if ARCH_MPI_ENABLED
    MPI_Finalize();
#endif
}
