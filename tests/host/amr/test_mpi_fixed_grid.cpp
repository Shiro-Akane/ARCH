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
void test_exchange(int rank, int size, int dim, int periodic_mask)
{
    SimConfig config;
    config.grid.dim = dim;
    config.grid.geometry = "cartesian";
    config.grid.nblockx1 = 2 * size + 1;
    config.grid.nblockx2 = dim >= 2 ? 3 : 0;
    config.grid.nblockx3 = dim == 3 ? 2 : 0;
    config.grid.x1_min = config.grid.x2_min = config.grid.x3_min = 0.;
    config.grid.x1_max = config.grid.x2_max = config.grid.x3_max = 1.;
    config.grid.x1l_boundary_type = config.grid.x1r_boundary_type =
        periodic_mask & 1 ? "periodic" : "outflow";
    config.grid.x2l_boundary_type = config.grid.x2r_boundary_type =
        periodic_mask & 2 ? "periodic" : "outflow";
    config.grid.x3l_boundary_type = config.grid.x3r_boundary_type =
        periodic_mask & 4 ? "periodic" : "outflow";
    config.amr.lrefinemin = config.amr.lrefinemax = 0;
    const int begin = config.grid.nblockx1 * rank / size,
              end = config.grid.nblockx1 * (rank + 1) / size;
    const int rows = std::max(1, config.grid.nblockx2);
    const int planes = std::max(1, config.grid.nblockx3);
    const std::array<int, 3> extent{amr::BLOCK_NX,
        dim >= 2 ? amr::BLOCK_NY : 1, dim == 3 ? amr::BLOCK_NZ : 1};
    const std::array<int, 3> cells{
        config.grid.nblockx1 * extent[0], rows * extent[1], planes * extent[2]};
    amr::AMRControl control((end - begin) * rows * planes, dim);
    control.tree->SetRootPartition(rank, size);
    control.tree->InitRootGrid(config, 2);
    require(control.pool->GetNumActiveBlocks() == (end - begin) * rows * planes,
            "nonlocal field storage allocated");
    require(control.tree->GetRootGrid().nblockx1 == config.grid.nblockx1, "global domain changed");
    std::vector<amr::BlockHandle> handles;
    for (int id : control.tree->GetActiveBlocks()) {
        const auto &b = control.pool->GetBlock(id);
        const auto key = 1 + b.logical_x1
            + config.grid.nblockx1 * (b.logical_x2 + rows * b.logical_x3);
        handles.push_back({{key}, {1}});
    }
    control.BindActiveHandles(handles);
    const std::array<FluidState amr::Block::*, 3> slots{
        &amr::Block::fluid_state, &amr::Block::state_next, &amr::Block::state_scratch};
    int slot = 0;
    const auto field_pointers = [](FluidState &s, int total) {
        return std::array<double *, 8>{s.rho.data(), s.mom_u.data(), s.mom_v.data(),
            s.mom_w.data(), s.eng.data(), s.enuc_rate.data(),
            s.mass_fractions.data(), s.mass_fractions.data() + total};
    };
    for (auto member : slots) {
        const auto expected = [&](int field, const std::array<int, 3> &global) {
            return slot * 1e12 + field * 1e10
                + global[0] + cells[0] * (global[1] + cells[1] * global[2]);
        };
        for (int id : control.tree->GetActiveBlocks()) {
            auto &b = control.pool->GetBlock(id);
            const auto fields = field_pointers(b.*member, b.grid.GetTotalSize());
            for (auto *field : fields)
                std::fill_n(field, b.grid.GetTotalSize(), -777.);
            for (int k = 0; k < extent[2]; ++k)
                for (int j = 0; j < extent[1]; ++j)
                    for (int i = 0; i < extent[0]; ++i) {
                        const std::array<int, 3> global{
                            static_cast<int>(b.logical_x1) * extent[0] + i,
                            static_cast<int>(b.logical_x2) * extent[1] + j,
                            static_cast<int>(b.logical_x3) * extent[2] + k};
                        const int index = b.grid.Is() + i + (b.grid.Js() + j) * b.grid.stride_y
                            + (b.grid.Ks() + k) * b.grid.stride_z;
                        for (int field = 0; field < 8; ++field)
                            fields[field][index] = expected(field, global);
                    }
        }
        control.ghost_exchange.ExecuteExchange(control.pool, control.tree, dim, member, handles);
        for (int id : control.tree->GetActiveBlocks()) {
            auto &b = control.pool->GetBlock(id);
            const auto fields = field_pointers(b.*member, b.grid.GetTotalSize());
            const int gy = dim >= 2 ? amr::MAX_NG : 0, gz = dim == 3 ? amr::MAX_NG : 0;
            for (int k = -gz; k < extent[2] + gz; ++k)
                for (int j = -gy; j < extent[1] + gy; ++j)
                    for (int i = -amr::MAX_NG; i < extent[0] + amr::MAX_NG; ++i) {
                        std::array<int, 3> global{
                            static_cast<int>(b.logical_x1) * extent[0] + i,
                            static_cast<int>(b.logical_x2) * extent[1] + j,
                            static_cast<int>(b.logical_x3) * extent[2] + k};
                        bool physical = false;
                        for (int axis = 0; axis < dim; ++axis) {
                            if (periodic_mask & (1 << axis))
                                global[axis] = (global[axis] % cells[axis] + cells[axis]) % cells[axis];
                            else
                                physical |= global[axis] < 0 || global[axis] >= cells[axis];
                        }
                        const int index = b.grid.Is() + i + (b.grid.Js() + j) * b.grid.stride_y
                            + (b.grid.Ks() + k) * b.grid.stride_z;
                        for (int field = 0; field < 8; ++field)
                            require(fields[field][index] == (physical ? -777. : expected(field, global)),
                                    "interior/face/edge/corner/species differs from global indexing");
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
        for (int dim = 1; dim <= 3; ++dim) {
            test_exchange(rank, size, dim, 0);
            test_exchange(rank, size, dim, (1 << dim) - 1);
            if (dim >= 2)
                test_exchange(rank, size, dim, 1);
        }
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
