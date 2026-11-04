/**
 * @file test_distributed_exchange.cpp
 * @brief Bitwise complete ghost sweeps across non-root X/Y/Z owners.
 * This tests exchange ordering, not scheduler acceptance, migration or restart.
 */
#include "fixtures/amr/exchange_topology_fixture.h"
#include "fixtures/amr/partitioned_topology_fixture.h"
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
#include <bit>
#include <iostream>
#include <set>

namespace {
using Fixture = amr::test::ExchangeTopologyFixture;
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
std::vector<std::uint64_t> snapshot(const auto& f) {
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
void seed(auto& f, bool constant) {
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
void compare(const auto& actual, const auto& reference) {
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

void execute(auto& f, Slot slot) {
    f.control.ghost_exchange.ExecuteDistributedExchange(f.control.pool, f.control.tree,
                                                        f.config.grid.dim, slot, f.handles);
}
auto active_snapshot(const auto& f) {
    std::vector<std::uint64_t> words;
    for (int id : f.control.tree->GetActiveBlocks()) {
        const auto& b = f.control.pool->GetBlock(id);
        for (const auto slot : slots) {
            const auto& s = b.*slot;
            const auto fields = amr::test::regrid_fields(s);
            for (int k = b.grid.Ks(); k < b.grid.Ke(); ++k)
                for (int j = b.grid.Js(); j < b.grid.Je(); ++j)
                    for (int i = b.grid.Is(); i < b.grid.Ie(); ++i) {
                        const auto cell = b.grid.GetIndex(i, j, k);
                        for (const auto* field : fields)
                            words.push_back(std::bit_cast<std::uint64_t>(field[cell]));
                        for (int sp = 0; sp < s.GetNumSpecies(); ++sp)
                            words.push_back(std::bit_cast<std::uint64_t>(s.X(sp, cell)));
                    }
        }
    }
    return words;
}
void check_case(int dim, int axis, int pattern, bool periodic, bool constant) {
    Fixture reference(dim, 0, 1, periodic, axis, pattern);
    Fixture actual(dim, rank, ranks, periodic, axis, pattern);
    seed(reference, constant);
    seed(actual, constant);
    const auto active = active_snapshot(actual);
    const auto constant_before = constant ? snapshot(actual) : std::vector<std::uint64_t>{};
    const auto count = actual.control.pool->GetNumActiveBlocks();
    if (ranks == 2 && pattern == 1) {
        bool remote_fine = false;
        for (int id : actual.control.tree->GetActiveBlocks()) {
            const auto& b = actual.control.pool->GetBlock(id);
            for (int side = 0; side < 2; ++side) {
                const auto& n = b.face_neighbors[2 * axis + side];
                remote_fine |= b.level > 0 && n.count == 1 && n.level_diff == 0 &&
                               n.blocks[0].owner_rank != rank;
            }
        }
        require(remote_fine, "fixture omitted requested non-root remote axis");
    }
    for (const auto slot : slots) {
        // Direct legacy serial executor is the independent ordering reference.
        reference.control.ghost_exchange.ExecuteExchange(
            reference.control.pool, reference.control.tree, dim, slot, reference.handles);
        execute(actual, slot);
        compare(actual, reference); // all fields, all ghosts, all three slots
        require(active_snapshot(actual) == active, "exchange wrote an active cell");
        if (constant)
            require(snapshot(actual) == constant_before, "constant field changed");
    }
    require(actual.control.pool->GetNumActiveBlocks() == count, "exchange allocated remote fields");
}
void empty_local_level() {
    // Rank zero has no fine leaves or same-level neighbors; both must still
    // enter every collective. Level-local loop counts must not select MPI calls.
    amr::test::PartitionedTopologyFixture reference(2, 0, 1, false), actual(2, rank, ranks, false);
    if (ranks == 2 && rank == 0)
        for (const auto& plan : actual.control.ghost_exchange.BuildSameLevelPlans(
                 actual.control.pool, actual.control.tree, 2, actual.handles))
            require(plan.operations.empty(), "zero-work rank has a same-level operation");
    seed(reference, false);
    seed(actual, false);
    for (const auto slot : slots) {
        reference.control.ghost_exchange.ExecuteExchange(
            reference.control.pool, reference.control.tree, 2, slot, reference.handles);
        execute(actual, slot);
        compare(actual, reference);
    }
}
void copy_bits() {
    Fixture reference(2, 0, 1, true, 1, 1), actual(2, rank, ranks, true, 1, 1);
    // Same-level transport is a copy, including signed zero and NaN payloads.
    const auto bits = [](auto& f) {
        seed(f, false);
        for (int id : f.control.tree->GetActiveBlocks()) {
            auto& b = f.control.pool->GetBlock(id);
            for (const auto slot : slots)
                for (std::size_t cell = 0; cell < (b.*slot).rho.size(); ++cell) {
                    (b.*slot).mom_u[cell] =
                        std::bit_cast<double>(UINT64_C(0x7ff8000000000000) + cell + 1);
                    (b.*slot).mom_v[cell] = cell % 2 ? -0. : 0.;
                }
        }
    };
    bits(reference);
    bits(actual);
    for (const auto slot : slots) {
        reference.control.ghost_exchange.ExecuteExchange(
            reference.control.pool, reference.control.tree, 2, slot, reference.handles);
        execute(actual, slot);
        compare(actual, reference);
    }
}
void failures() {
    if (ranks != 2)
        return;
    for (int fault = 0; fault < 10; ++fault) {
        Fixture f(2, rank, ranks, true, 1, 1);
        seed(f, false);
        auto& first = f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
        const auto saved = first.fluid_state;
        const auto neighbors = std::to_array(first.face_neighbors);
        auto slot = slots[0];
        if (rank == 0) {
            if (fault == 0)
                first.fluid_state.mom_u.pop_back();
            if (fault == 1)
                f.handles.front().epoch.value += 1;
            if (fault == 2)
                slot = slots[1];
            if (fault == 3)
                first.fluid_state.InitSpecies(3);
            int face = -1;
            for (int n = 0; n < 4; ++n)
                if (first.face_neighbors[n].count &&
                    first.face_neighbors[n].blocks[0].owner_rank != rank) {
                    face = n;
                    break;
                }
            require(face >= 0, "fault fixture missing remote neighbor");
            if (fault == 4)
                first.face_neighbors[face].blocks[0].owner_rank = ranks;
            if (fault == 5)
                first.face_neighbors[face].blocks[0].handle.uid.value += 10000;
            if (fault == 6)
                first.face_neighbors[face].count = 0; // reciprocal metadata must reject
            if (fault == 7)
                first.face_neighbors[face].level_diff = 1;
            if (fault == 8)
                slot = nullptr;
        }
        const auto before = snapshot(f);
        bool rejected = false;
        try {
            if (fault == 9 && rank == 0) {
                const auto plan = f.control.ghost_exchange.BuildCoarseFinePlan(
                    f.control.pool, f.control.tree, 2, f.handles);
                f.control.ghost_exchange.ExecuteCoarseFinePlan(plan, f.control.pool, f.control.tree,
                                                               2, slot, f.handles);
            } else
                execute(f, slot);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "single-rank prepare failure did not reject collectively");
        require(before == snapshot(f), "prepare failure wrote a field");
        first.fluid_state = saved;
        std::copy(neighbors.begin(), neighbors.end(), std::begin(first.face_neighbors));
        if (rank == 0 && fault == 1)
            f.handles.front().epoch.value -= 1;
        execute(f, slots[0]);
    }
}
void late_coarse_failure() {
    if (ranks != 2)
        return;
    Fixture f(2, rank, ranks, true, 1, 2);
    seed(f, false);
    if (rank == 0)
        for (int id : f.control.tree->GetActiveBlocks())
            std::fill(f.control.pool->GetBlock(id).fluid_state.rho.begin(),
                      f.control.pool->GetBlock(id).fluid_state.rho.end(), -1.);
    const auto before = active_snapshot(f);
    bool rejected = false;
    try {
        execute(f, slots[0]);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "coarse/fine numerical failure did not propagate collectively");
    require(before == active_snapshot(f), "late failure wrote active cells");
    // Earlier face sweeps may have changed ghosts; no completed-domain token
    // may be published. Reseeding and a full exchange must recover.
    seed(f, false);
    execute(f, slots[0]);
}
} // namespace
int main(int argc, char** argv) {
#if ARCH_MPI_ENABLED
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED)
        MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
#else
    (void)argc;
    (void)argv;
#endif
    try {
        require(ranks == 1 || ranks == 2, "one or two ranks required");
        for (int dim = 1; dim <= 3; ++dim)
            for (int axis = 0; axis < dim; ++axis)
                for (int pattern = 0; pattern < 3; ++pattern)
                    for (bool periodic : {false, true})
                        for (bool constant : {false, true})
                            check_case(dim, axis, pattern, periodic, constant);
        empty_local_level();
        copy_bits();
        failures();
        late_coarse_failure();
        if (rank == 0)
            std::cout
                << "complete exchange passed: 72 configurations, X/Y/Z non-root owners, "
                   "three slots, bitwise all fields/species/ghosts, constants, active cells "
                   "unchanged, "
                   "zero-work ranks, prepare failures, late numerical rejection and recovery\n";
    } catch (const std::exception& error) {
        std::cerr << "rank " << rank << ": " << error.what() << '\n';
#if ARCH_MPI_ENABLED
        MPI_Abort(MPI_COMM_WORLD, 1);
#endif
        return 1;
    }
#if ARCH_MPI_ENABLED
    MPI_Finalize();
#endif
}
