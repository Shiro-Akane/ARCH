/** Real MPI transport through the production pool/tree/ghost owner. */
#include "amr/AMRControl.h"
#include "driver/DriverUtils.h"
#include <mpi.h>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void test_exchange(int rank, int size, bool periodic)
{
    SimConfig config;
    config.grid.dim = 1;
    config.grid.geometry = "cartesian";
    config.grid.nblockx1 = 2 * size + 1;
    config.grid.nblockx2 = 0;
    config.grid.nblockx3 = 0;
    config.grid.x1_min = 0.;
    config.grid.x1_max = 1.;
    config.grid.x1l_boundary_type = config.grid.x1r_boundary_type =
        periodic ? "periodic" : "outflow";
    config.amr.lrefinemin = 0;
    config.amr.lrefinemax = 0;
    const int begin = config.grid.nblockx1 * rank / size,
              end = config.grid.nblockx1 * (rank + 1) / size;
    amr::AMRControl control(end - begin, 1);
    control.tree->SetRootPartition(rank, size);
    control.tree->InitRootGrid(config, 2);
    require(control.pool->GetNumActiveBlocks() == end - begin, "nonlocal field storage allocated");
    require(control.tree->GetRootGrid().nblockx1 == config.grid.nblockx1, "global domain changed");
    std::vector<amr::BlockHandle> handles;
    for (int id : control.tree->GetActiveBlocks())
        handles.push_back({{control.pool->GetBlock(id).logical_x1 + 1}, {1}});
    control.BindActiveHandles(handles);
    const std::array<FluidState amr::Block::*, 3> slots{
        &amr::Block::fluid_state, &amr::Block::state_next, &amr::Block::state_scratch};
    int slot = 0;
    for (auto member : slots) {
        const auto expected = [&](int field, int cell) {
            return double(slot * 1000000 + field * 10000 + cell);
        };
        for (int id : control.tree->GetActiveBlocks()) {
            auto &b = control.pool->GetBlock(id);
            auto &s = b.*member;
            std::array<double *, 8> fields{s.rho.data(),
                                           s.mom_u.data(),
                                           s.mom_v.data(),
                                           s.mom_w.data(),
                                           s.eng.data(),
                                           s.enuc_rate.data(),
                                           s.mass_fractions.data(),
                                           s.mass_fractions.data() + b.grid.GetTotalSize()};
            for (int field = 0; field < 8; ++field) {
                std::fill_n(fields[field], b.grid.GetTotalSize(), -777.);
                for (int i = 0; i < amr::BLOCK_NX; ++i)
                    fields[field][b.grid.Is() + i] =
                        expected(field, b.logical_x1 * amr::BLOCK_NX + i);
            }
        }
        control.ghost_exchange.ExecuteExchange(control.pool, control.tree, 1, member, handles);
        const int cells = config.grid.nblockx1 * amr::BLOCK_NX;
        for (int id : control.tree->GetActiveBlocks()) {
            auto &b = control.pool->GetBlock(id);
            auto &s = b.*member;
            std::array<double *, 8> fields{s.rho.data(),
                                           s.mom_u.data(),
                                           s.mom_v.data(),
                                           s.mom_w.data(),
                                           s.eng.data(),
                                           s.enuc_rate.data(),
                                           s.mass_fractions.data(),
                                           s.mass_fractions.data() + b.grid.GetTotalSize()};
            for (int i = -amr::MAX_NG; i < amr::BLOCK_NX + amr::MAX_NG; ++i) {
                int global = static_cast<int>(b.logical_x1) * amr::BLOCK_NX + i;
                bool outside = global < 0 || global >= cells;
                if (periodic)
                    global = (global % cells + cells) % cells;
                for (int field = 0; field < 8; ++field)
                    require(
                        fields[field][b.grid.Is() + i] ==
                            (outside && !periodic ? -777. : expected(field, global)),
                        "owned interior/ghost/species value differs from global serial indexing");
            }
        }
        ++slot;
    }
    require(control.ghost_exchange.PlanCacheBuilds() == 1,
            "topology cache rebuilt for a state-slot change");
    require(control.ghost_exchange.HostPlanCacheBuilds() == 1,
            "host lowering rebuilt for a state-slot change");
}
void test_minimum(int rank, int size)
{
    const auto key = DriverReduction::make_block_reduction_key(
        0, rank, size - rank, 0, 0, DriverReduction::BlockReductionComponent::Hydro);
    std::vector<arch::reduction::ReductionCandidate> candidates{{double(10 - rank), key, true}};
    require(DriverReduction::reduce_block_minimum(1e99, candidates, size) == 11. - size,
            "global CFL minimum differs");
    candidates[0].value = rank == size - 1 ? -0. : +0.;
    const double zero = DriverReduction::reduce_block_minimum(1e99, candidates, size);
    require(std::signbit(zero), "equal-value tie did not retain the lowest logical key");
    auto nan = candidates[0];
    nan.key.logical_i += 100;
    nan.value = std::numeric_limits<double>::quiet_NaN();
    candidates.push_back(nan);
    require(std::signbit(DriverReduction::reduce_block_minimum(1e99, candidates, size)),
            "NaN policy changed");
    if (size > 1) {
        candidates.resize(1);
        candidates[0].value = rank == 0 ? std::numeric_limits<double>::quiet_NaN() : double(rank);
        require(DriverReduction::reduce_block_minimum(1e99, candidates, size) == 1.,
                "NaN-only local partition was treated as a globally empty reduction");
    }
    candidates.clear();
    bool rejected = false;
    try {
        (void)DriverReduction::reduce_block_minimum(1e99, candidates, size);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "globally empty reduction was accepted");
}
int main(int argc, char **argv)
{
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    try {
        require(provided >= MPI_THREAD_FUNNELED, "insufficient thread support");
        test_exchange(rank, size, false);
        test_exchange(rank, size, true);
        test_minimum(rank, size);
        if (rank == 0)
            std::cout << "MPI fixed-grid transport/reduction passed: ranks=" << size << '\n';
    } catch (const std::exception &e) {
        std::cerr << "rank=" << rank << ": " << e.what() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
    MPI_Finalize();
    return 0;
}
