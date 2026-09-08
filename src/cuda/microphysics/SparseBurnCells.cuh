/**
 * @brief Device packing and commit around the shared sparse ODE executor.
 * DriverBurn remains the sole source of activation/composition/energy physics.
 */
#pragma once

#include "SparseOdeBatch.cuh"
#include "cuda/common/CudaCommon.cuh"
#include "driver/DriverBurnPolicy.h"

namespace arch::cuda {
struct SparseBurnCellRecord
{
    FluidVector fluid{};
    DriverBurn::BurnCellPreparation prepared{};
    DriverBurn::BurnCellDisposition disposition = DriverBurn::BurnCellDisposition::BurnDisabled;
};

namespace sparse_burn_detail {
template <class Network, template <class, class, class> class Solver, class Eos>
__global__ void prepare_cells(
    SparseOdeBatchView<Network, Solver> batch, SparseBurnCellRecord* records,
    DeviceStateView state, DeviceGridView grid, int first, int count,
    double burn_dt, Eos eos, BurnConfigView config)
{
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane >= count) return;
    int* const eos_status = batch.eos_statuses == nullptr ? nullptr : batch.eos_statuses + lane;
    if (eos_status != nullptr) *eos_status = 0;
    const auto checked_eos = bind_device_eos_status(eos, eos_status);
    const int cell = grid.active_cell(first + lane);
    auto& record = records[lane];
    record.fluid = state.load(cell);
    record.prepared = {};
    record.disposition = DriverBurn::BurnCellDisposition::BurnDisabled;
    double* packed = batch.states + static_cast<std::size_t>(lane) * Network::ODE_NEQ;
    for (int species = 0; species < Network::NUM_SPECIES; ++species)
        packed[species] = state.species(species, cell);
    // Reused lanes must not retain a preceding cell's signed loss integral.
    for (int index = Network::NUM_SPECIES; index < Network::ODE_NEQ; ++index)
        packed[index] = 0.0;
    batch.densities[lane] = record.fluid.rho;
    batch.intervals[lane] = 0.0; // Inactive/invalid cells make no ODE progress.
    if (!config.use_burn) return;
    record.disposition = DriverBurn::check_burn_density(record.fluid, config);
    if (record.disposition == DriverBurn::BurnCellDisposition::BelowDensity) return;
    record.prepared = DriverBurn::prepare_burn_cell(
        record.fluid, packed, Network::NUM_SPECIES, checked_eos, config);
    record.disposition = record.prepared.disposition;
    if (eos_status != nullptr && *eos_status != 0)
        record.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
    if (record.disposition == DriverBurn::BurnCellDisposition::Ready)
        batch.intervals[lane] = burn_dt;
}

template <class Network, template <class, class, class> class Solver, class Eos>
__global__ void commit_cells(
    SparseOdeBatchView<Network, Solver> batch, SparseBurnCellRecord* records,
    DeviceStateView state, DeviceGridView grid, int first, int count,
    double burn_dt, Eos eos, BurnConfigView config,
    reduction::ReductionCandidate* candidates, int* statuses)
{
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane >= count) return;
    const int linear = first + lane;
    const int cell = grid.active_cell(linear);
    auto& record = records[lane];
    int* const eos_status = batch.eos_statuses == nullptr ? nullptr : batch.eos_statuses + lane;
    const auto checked_eos = bind_device_eos_status(eos, eos_status);
    const double* packed = batch.states + static_cast<std::size_t>(lane) * Network::ODE_NEQ;
    double limiter = DriverBurn::INACTIVE_LIMITER_CANDIDATE;
    if (eos_status != nullptr && *eos_status != 0)
        record.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
    if (record.disposition == DriverBurn::BurnCellDisposition::Ready) {
        if (!batch.contexts[lane].report.success()) {
            record.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
        } else {
            const auto handoff = DriverBurn::compute_burn_energy_handoff(
                record.fluid, packed, Network::NUM_SPECIES,
                record.prepared.internal_energy, record.prepared.kinetic_energy,
                burn_dt, checked_eos, config, batch.contexts[lane].report.energy_change);
            if (!handoff.valid || (eos_status != nullptr && *eos_status != 0)) {
                batch.contexts[lane].report.status = BurnOdeStatus::EosFailure;
                batch.contexts[lane].phase = SparseOdePolicy<Network, Solver>::Phase::Complete;
                record.disposition = DriverBurn::BurnCellDisposition::SolverFailed;
            } else {
                DriverBurn::commit_burn_energy(record.fluid, handoff);
                state.store(cell, record.fluid);
                for (int species = 0; species < Network::NUM_SPECIES; ++species)
                    state.set_species(species, cell, packed[species]);
                state.enuc_rate[cell] = handoff.enuc_rate;
                limiter = handoff.limiter_candidate;
            }
        }
    }
    const int nx = grid.ie - grid.is, ny = grid.je - grid.js;
    amr::CellLogicalKey key{};
    key.logical_i = grid.is + linear % nx;
    key.logical_j = grid.js + (linear / nx) % ny;
    key.logical_k = grid.ks + linear / (nx * ny);
    key.component = DriverBurn::BURN_LIMITER_COMPONENT;
    candidates[linear] = {limiter, key, true};
    statuses[linear] = static_cast<int>(record.disposition);
}
} // namespace sparse_burn_detail

/** Execute one block using the same bounded pool for successive cell chunks.
 * The caller retains the existing block-generation and reducer authority. Only
 * the backend-specific gather/scatter and host-API scheduling differ here.
 */
template <class Network, template <class, class, class> class Solver, class Eos>
void execute_sparse_burn_cells(
    SparseOdeBatchExecutor<Network, Solver>& executor, SparseBurnCellRecord* records,
    DeviceStateView state, DeviceGridView grid, double burn_dt, Eos eos,
    BurnConfigView config, reduction::ReductionCandidate* candidates, int* statuses)
{
    if (state.n_species != Network::NUM_SPECIES || records == nullptr
        || candidates == nullptr || statuses == nullptr)
        throw std::invalid_argument("Sparse burn cell route has invalid buffers or species extent");
    const auto batch = executor.view();
    if constexpr (requires(Eos view) { view.device_error_status; }) {
        if (batch.eos_statuses == nullptr)
            throw std::invalid_argument("Sparse table EOS requires a persistent device failure latch");
    }
    const auto stream = executor.stream();
    const int total = grid.active_cell_count();
    for (int first = 0; first < total;) {
        const int count = std::min(batch.capacity, total - first);
        const int threads = executor.launch_threads();
        const int blocks = (count + threads - 1) / threads;
        sparse_burn_detail::prepare_cells<Network, Solver>
            <<<blocks, threads, 0, stream>>>(batch, records, state, grid, first, count, burn_dt, eos, config);
        sparse_burn_detail::checked(cudaGetLastError(), "Sparse burn prepare launch");
        executor.execute(count, eos, config);
        sparse_burn_detail::commit_cells<Network, Solver>
            <<<blocks, threads, 0, stream>>>(batch, records, state, grid, first, count,
                burn_dt, eos, config, candidates, statuses);
        sparse_burn_detail::checked(cudaGetLastError(), "Sparse burn commit launch");
        first += count; // The tail advances to total without exceeding INT_MAX.
    }
}
} // namespace arch::cuda
