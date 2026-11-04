/**
 * @file test_flux_reflux_exchange.cpp
 * @brief Real two-rank stage flux registration and native reflux correction.
 * This is an operator/stage-boundary fixture, not a distributed solver timestep.
 */
#include "fixtures/amr/partitioned_topology_fixture.h"
#include "amr/flux/AMRFluxRegistering.h"
#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif
#include <bit>
#include <iostream>
#include <limits>

namespace {
using Fixture = amr::test::PartitionedTopologyFixture;
using Slot = FluidState amr::Block::*;
constexpr std::array<Slot, 3> slots{&amr::Block::fluid_state, &amr::Block::state_next,
                                    &amr::Block::state_scratch};
int rank = 0, ranks = 1;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool same(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
std::array<double, 5> fluid(const FluidVector& v) {
    return {v.rho, v.mom_u, v.mom_v, v.mom_w, v.eng};
}
void seed(Fixture& f) {
    f.control.flux_register.EnsureSpecies(2);
    for (int id : f.control.tree->GetActiveBlocks()) {
        auto& b = f.control.pool->GetBlock(id);
        b.state_next = b.state_scratch = b.fluid_state;
        for (std::size_t n = 0; n < slots.size(); ++n) {
            auto& s = b.*slots[n];
            for (std::size_t i = 0; i < s.rho.size(); ++i) {
                s.rho[i] = 2. + n;
                s.mom_u[i] = .25;
                s.mom_v[i] = -.125;
                s.mom_w[i] = .0625;
                s.eng[i] = 4. + n;
                s.enuc_rate[i] = -.25;
                s.X(0, i) = .25;
                s.X(1, i) = .75;
            }
        }
    }
}
auto fields(const Fixture& f) {
    std::vector<std::uint64_t> out;
    for (int id : f.control.tree->GetActiveBlocks())
        for (auto slot : slots) {
            const auto& s = f.control.pool->GetBlock(id).*slot;
            for (const auto* v :
                 {&s.rho, &s.mom_u, &s.mom_v, &s.mom_w, &s.eng, &s.enuc_rate, &s.mass_fractions}) {
                out.push_back(v->size());
                for (double value : *v)
                    out.push_back(std::bit_cast<std::uint64_t>(value));
            }
        }
    return out;
}
struct FaceValues {
    std::vector<FluidVector> fluid;
    std::vector<double> species;
};
FaceValues fluxes(const amr::Block& block, int mode) {
    FaceValues f;
    const auto cells = block.grid.GetTotalSize();
    f.fluid.resize(cells);
    f.species.resize(2 * cells);
    for (int i = 0; i < cells; ++i) {
        double density = (mode == 0 || block.level == 0) ? .125 : .25;
        if (mode == 2)
            density += (i % 23) / 4096. + block.logical_x1 / 512. + block.logical_x2 / 1024. +
                       block.logical_x3 / 2048.;
        f.fluid[i] = {density, density * .5, -density * .25, density * .125, density * 2.};
        f.species[i] = density * .25;
        f.species[cells + i] = density * .75;
    }
    return f;
}
std::vector<std::vector<double>> gather(Fixture& f, const amr::AmrFluxTopologyPlan& plan, int mode,
                                        double weight) {
    std::vector<std::vector<double>> values;
    for (const auto& route : plan.routes) {
        const auto& b = f.control.pool->GetBlock(route.key.source_block);
        const auto data = fluxes(b, mode);
        values.push_back(amr::GatherCoarseFineFluxValues(plan, route, b.grid, data.fluid,
                                                         data.species, 2, weight));
    }
    return values;
}
void serial_stage(Fixture& f, const amr::AmrFluxTopologyPlan& plan, int mode, double weight) {
    for (const auto& route : plan.routes) {
        const auto& b = f.control.pool->GetBlock(route.key.source_block);
        const auto data = fluxes(b, mode);
        amr::RegisterCoarseFineFluxes(f.control, plan, b.id, b.grid,
                                      amr::axis_value(route.key.axis), data.fluid, data.species, 2,
                                      weight);
    }
}
void stage(Fixture& f, const amr::AmrFluxTopologyPlan& plan,
           const std::vector<std::vector<double>>& values, std::uint64_t index, double weight) {
    f.control.flux_register.ApplyDistributedRegistrationStage(
        *f.control.pool, *f.control.tree, f.handles, plan, values, index, weight);
}
void compare_register(const Fixture& actual, const Fixture& reference, int dim) {
    for (int id : actual.control.tree->GetActiveBlocks()) {
        const auto& b = actual.control.pool->GetBlock(id);
        const int rid =
            reference.control.tree->FindBlock(b.level, b.logical_x1, b.logical_x2, b.logical_x3);
        require(rid >= 0, "missing reference leaf");
        for (int face = 0; face < 2 * dim; ++face) {
            const auto& a = actual.control.flux_register;
            const auto& e = reference.control.flux_register;
            require(a.HasData(id, face) == e.HasData(rid, face), "activity witness mismatch");
            const int count =
                amr::flux_plan_detail::face_cell_count(dim, static_cast<amr::AmrAxis>(face / 2));
            for (int cell = 0; cell < count; ++cell) {
                auto av = fluid(a.GetSummedFlux(id, face, cell)),
                     ev = fluid(e.GetSummedFlux(rid, face, cell));
                for (std::size_t field = 0; field < av.size(); ++field)
                    require(same(av[field], ev[field]), "registered flux not bitwise equal");
                for (int sp = 0; sp < 2; ++sp)
                    require(same(a.GetSummedSpeciesFlux(id, face, cell, sp),
                                 e.GetSummedSpeciesFlux(rid, face, cell, sp)),
                            "registered rho X mismatch");
            }
        }
    }
}
void compare_fields(const Fixture& actual, const Fixture& reference) {
    for (int id : actual.control.tree->GetActiveBlocks()) {
        const auto& b = actual.control.pool->GetBlock(id);
        const int rid =
            reference.control.tree->FindBlock(b.level, b.logical_x1, b.logical_x2, b.logical_x3);
        const auto& ref = reference.control.pool->GetBlock(rid);
        for (auto slot : slots) {
            const auto& a = b.*slot;
            const auto& e = ref.*slot;
            const auto af = amr::test::regrid_fields(a), ef = amr::test::regrid_fields(e);
            for (std::size_t field = 0; field < af.size(); ++field)
                for (std::size_t cell = 0; cell < a.rho.size(); ++cell)
                    require(same(af[field][cell], ef[field][cell]), "reflux native field mismatch");
            for (std::size_t cell = 0; cell < a.mass_fractions.size(); ++cell)
                require(same(a.mass_fractions[cell], e.mass_fractions[cell]),
                        "reflux species mismatch");
        }
    }
}
std::array<long double, 7> integrals(const Fixture& f, Slot slot) {
    std::array<long double, 7> total{};
    for (int id : f.control.tree->GetActiveBlocks()) {
        const auto& b = f.control.pool->GetBlock(id);
        const auto& g = b.grid;
        const auto& s = b.*slot;
        const auto native = amr::test::regrid_fields(s);
        for (int k = g.Ks(); k < g.Ke(); ++k)
            for (int j = g.Js(); j < g.Je(); ++j)
                for (int i = g.Is(); i < g.Ie(); ++i) {
                    const auto cell = g.GetIndex(i, j, k);
                    const long double volume =
                        GridMetrics::CellVolume(GridMetrics::make_geometry_view(g), i, j, k);
                    for (int field = 0; field < 5; ++field)
                        total[field] += native[field][cell] * volume;
                    for (int sp = 0; sp < 2; ++sp)
                        total[5 + sp] +=
                            static_cast<long double>(s.rho[cell]) * s.X(sp, cell) * volume;
                }
    }
    return total;
}
void check_case(int dim, bool periodic, int mode, Slot slot, bool refine = true) {
    Fixture reference(dim, 0, 1, periodic, refine), actual(dim, rank, ranks, periodic, refine);
    seed(reference);
    seed(actual);
    const auto& expected = reference.control.RequireFluxTopologyPlan(2);
    const auto& current = actual.control.RequireFluxTopologyPlan(2);
    const auto before = integrals(actual, slot);
    const auto before_fields = fields(actual);
    const auto active = actual.control.pool->GetNumActiveBlocks();
    if (!refine) {
        const auto empty = actual.control.flux_register.snapshot_host();
        actual.control.ApplyReflux(.01, slot);
        require(fields(actual) == before_fields &&
                    actual.control.flux_register.host_snapshot_matches(empty),
                "fixed-grid legacy reflux must remain an empty local operation");
    }
    const std::array<double, 4> weights{0., .5, -.25, .75};
    for (std::size_t n = 0; n < weights.size(); ++n) {
        serial_stage(reference, expected, mode, weights[n]);
        if (ranks == 1)
            serial_stage(actual, current, mode, weights[n]);
        else
            stage(actual, current, gather(actual, current, mode, weights[n]), n, weights[n]);
        compare_register(actual, reference, dim);
    }
    constexpr double dt = 1. / 128.;
    reference.control.ApplyReflux(dt, slot);
    if (ranks > 1)
        actual.control.ApplyDistributedReflux(dt, slot, weights.size());
    else
        actual.control.ApplyReflux(dt, slot);
    compare_fields(actual, reference);
    require(actual.control.pool->GetNumActiveBlocks() == active,
            "remote flux allocated pool storage");
    if (mode == 0 || !refine)
        require(fields(actual) == before_fields, "balanced/empty flux changed a field");
    if (mode != 2) {
        const auto after = integrals(actual, slot);
        std::array<long double, 7> difference{};
        for (std::size_t n = 0; n < difference.size(); ++n)
            difference[n] = after[n] - before[n];
#if ARCH_MPI_ENABLED
        if (ranks > 1) {
            auto local = difference;
            MPI_Allreduce(local.data(), difference.data(), 7, MPI_LONG_DOUBLE, MPI_SUM,
                          MPI_COMM_WORLD);
        }
#endif
        // Independently known interface budget: area=1, fine/coarse density
        // flux jump=1/8, signed stage weights sum to 1. Periodic faces cancel.
        const long double mass = (mode == 1 && !periodic && refine) ? -dt * .125L : 0.L;
        const std::array<long double, 7> expected_delta{
            mass, mass * .5L, -mass * .25L, mass * .125L, mass * 2.L, mass * .25L, mass * .75L};
        for (std::size_t n = 0; n < difference.size(); ++n)
            if (std::abs(difference[n] - expected_delta[n]) > 2.e-12L) {
                std::cerr << "budget dim=" << dim << " periodic=" << periodic << " mode=" << mode
                          << " field=" << n << " actual=" << static_cast<double>(difference[n])
                          << " expected=" << static_cast<double>(expected_delta[n])
                          << " before=" << static_cast<double>(before[n])
                          << " after=" << static_cast<double>(after[n]) << '\n';
                throw std::runtime_error("independent interface integral budget failed");
            }
    }
}
template <class Function> void rejects_without_write(Fixture& f, Function&& function) {
    const auto saved = f.control.flux_register.snapshot_host();
    const auto before = fields(f);
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "collective error not rejected on every rank");
    require(f.control.flux_register.host_snapshot_matches(saved),
            "failed phase changed register/sequence");
    require(fields(f) == before, "failed phase changed a fluid field");
}
void count_boundaries() {
#if ARCH_MPI_ENABLED
    constexpr int limit = std::numeric_limits<int>::max();
    require(amr::flux_mpi_detail::packet_count(0, 0) == 2, "empty packet extent");
    require(amr::flux_mpi_detail::packet_count(limit - 2, 0) == limit, "packet limit rejected");
    require(amr::flux_mpi_detail::packet_count(0, limit - 2) == limit, "aggregate limit rejected");
    for (auto [values, previous] : std::array<std::pair<std::size_t, int>, 5>{
             {{0, -1},
              {0, limit - 1},
              {1, limit - 2},
              {static_cast<std::size_t>(limit), 0},
              {std::numeric_limits<std::size_t>::max(), 0}}}) {
        bool rejected = false;
        try {
            (void)amr::flux_mpi_detail::packet_count(values, previous);
        } catch (const std::overflow_error&) {
            rejected = true;
        }
        require(rejected, "packet extent overflow accepted");
    }
    std::vector<int> offsets;
    require(amr::flux_mpi_detail::displacements({limit - 2, 2}, offsets) == limit &&
                offsets == std::vector<int>{0, limit - 2},
            "displacement boundary changed");
    for (const auto& counts : {std::vector<int>{limit, 1}, std::vector<int>{-1}}) {
        bool rejected = false;
        try {
            (void)amr::flux_mpi_detail::displacements(counts, offsets);
        } catch (const std::overflow_error&) {
            rejected = true;
        }
        require(rejected, "displacement overflow accepted");
    }
#endif
}
void failures() {
    if (ranks != 2)
        return;
    for (int fault = 0; fault < 7; ++fault) {
        Fixture f(2, rank, ranks, true);
        seed(f);
        auto plan = f.control.RequireFluxTopologyPlan(2);
        auto values = gather(f, plan, 2, .5);
        double weight = .5;
        std::uint64_t index = 0;
        if (rank == 1) {
            if (fault == 0)
                values.pop_back();
            if (fault == 1)
                values.front().pop_back();
            if (fault == 2)
                values.front().front() = std::numeric_limits<double>::quiet_NaN();
            if (fault == 3)
                weight = -.5;
            if (fault == 4)
                index = 1;
            if (fault == 5)
                f.handles.front().epoch.value += 1;
        }
        auto& first_block = f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
        const auto saved_face = first_block.face_neighbors[0];
        if (fault == 6) {
            // Both sides now offer valid local recipes, but rank 0 omits one
            // periodic receive face. Counts must reject the unmatched sender.
            if (rank == 0)
                first_block.face_neighbors[0] = {};
            plan = amr::build_amr_flux_topology_plan(
                *f.control.pool, f.control.tree->GetActiveBlocks(), f.handles, 2, 2,
                &f.control.tree->GetRootGrid(), rank, ranks);
            values = gather(f, plan, 2, .5);
        }
        rejects_without_write(f, [&] { stage(f, plan, values, index, weight); });
        if (fault == 6) {
            first_block.face_neighbors[0] = saved_face;
            plan = f.control.RequireFluxTopologyPlan(2);
        }
        if (rank == 1 && fault == 5)
            f.handles.front().epoch.value -= 1;
        values = gather(f, plan, 2, .5);
        stage(f, plan, values, 0, .5);
        rejects_without_write(f, [&] { stage(f, plan, values, 0, .5); });
        rejects_without_write(f, [&] { f.control.flux_register.EnsureSpecies(3); });
        rejects_without_write(f, [&] { f.control.ApplyReflux(.01, slots[0]); });
        rejects_without_write(f, [&] {
            const auto local = amr::build_amr_reflux_topology_plan(*f.control.pool, plan);
            f.control.flux_register.ExecuteRefluxPlan(
                local, f.control.pool, f.control.tree->GetActiveBlocks(), f.handles, slots[0], .01);
        });
        rejects_without_write(f, [&] { f.control.ApplyDistributedReflux(.01, slots[0], 2); });
        auto& b = f.control.pool->GetBlock(f.control.tree->GetActiveBlocks().front());
        const auto old = b.state_next;
        if (rank == 1)
            b.state_next.mom_u.pop_back();
        rejects_without_write(f, [&] { f.control.ApplyDistributedReflux(.01, slots[1], 1); });
        b.state_next = old;
        rejects_without_write(f, [&] { f.control.ApplyDistributedReflux(.01, slots[rank], 1); });
        f.control.ApplyDistributedReflux(.01, slots[0], 1);
        rejects_without_write(f, [&] { f.control.ApplyDistributedReflux(.01, slots[0], 1); });
        rejects_without_write(f, [&] { stage(f, plan, values, 1, .5); });
        f.control.flux_register.Clear();
        const auto backup = f.control.flux_register.snapshot_host();
        f.control.flux_register.freeze_host_snapshot(backup);
        stage(f, plan, values, 0, .5);
        f.control.flux_register.restore_host_snapshot_noexcept(backup);
        require(f.control.flux_register.host_snapshot_matches(backup),
                "rollback lost stage sequence");
        f.control.flux_register.release_host_snapshot(backup);
        stage(f, plan, values, 0, .5);
        f.control.ApplyDistributedReflux(.01, slots[2], 1);
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
        require(ranks == 1 || ranks == 2, "requires one or two ranks");
        for (int dim = 1; dim <= 3; ++dim)
            for (bool periodic : {false, true}) {
                for (int mode = 0; mode < 3; ++mode)
                    for (auto slot : slots)
                        check_case(dim, periodic, mode, slot);
                check_case(dim, periodic, 0, slots[0], false);
            }
        count_boundaries();
        failures();
        if (rank == 0)
            std::cout
                << "flux reflux exchange passed: bitwise registers/all slots, signed/zero stages, "
                   "independent integral budgets, empty work, one-rank failures, replay rejection "
                   "and rollback\n";
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
