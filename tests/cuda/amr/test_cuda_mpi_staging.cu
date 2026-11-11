/**
 * @file test_cuda_mpi_staging.cu
 * @brief Check production same-level kernels with Host-staged MPI.
 *
 * Only local blocks occupy device storage. A serial Host snapshot/scatter
 * oracle checks fields, corners and untouched slots bitwise. This transport
 * witness does not qualify the application GPU MPI or mixed-level AMR path.
 */
#include "amr/exchange/ExchangePlan.h"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/hydro/boundary/ExchangeKernels.cuh"
#include <mpi.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

amr::SameLevelExchangePlan make_staging_plan(int dimension, int level,
    const std::array<std::int32_t, 3>& extent);

namespace {
using namespace arch::cuda;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void mpi_check(int status) { if (status != MPI_SUCCESS) MPI_Abort(MPI_COMM_WORLD, status); }
void cuda_check(cudaError_t status) { check_cuda(status, "CUDA staging witness"); }
// Declared after allocations: quiesce before borrowed storage is destroyed.
struct Stream {
    cudaStream_t value{};
    cudaEvent_t ready{};
    Stream() {
        cuda_check(cudaStreamCreateWithFlags(&value, cudaStreamNonBlocking));
        cuda_check(cudaEventCreateWithFlags(&ready, cudaEventDisableTiming));
    }
    ~Stream() {
        require_cuda_success_or_terminate(cudaStreamSynchronize(value));
        require_cuda_success_or_terminate(cudaEventDestroy(ready));
        require_cuda_success_or_terminate(cudaStreamDestroy(value));
    }
    void finish() {
        cuda_check(cudaEventRecord(ready, value));
        cuda_check(cudaEventSynchronize(ready));
    }
};
// Ensure queued copies cannot outlive vectors in a nested scope on failure.
struct BorrowedWork {
    cudaStream_t stream;
    bool completed = false;
    ~BorrowedWork() {
        if (!completed) require_cuda_success_or_terminate(cudaStreamSynchronize(stream));
    }
};
int block_index(const amr::ExchangeEndpoint& e) { return e.handle.uid.value - 1; }
int owner(const amr::ExchangeEndpoint& e) { return block_index(e) % 2; }

void rejected_launches()
{
    auto* blocks = reinterpret_cast<const DeviceExchangeBlock*>(std::uintptr_t{1});
    auto* ops = reinterpret_cast<const DeviceExchangeOperation*>(std::uintptr_t{1});
    auto* scratch = reinterpret_cast<double*>(std::uintptr_t{1});
    for (bool gather : {false, true}) {
        auto launch = [&](int count, int fields, std::uint64_t cells) {
            return gather
                ? launch_same_level_exchange_gather(blocks, ops, count, fields, cells, scratch, nullptr)
                : launch_same_level_exchange_scatter(blocks, ops, count, fields, cells, scratch, nullptr);
        };
        require(launch(0, 0, 0) == cudaSuccess, "empty launch rejected");
        require(launch(-1, 6, 1) == cudaErrorInvalidValue, "negative count accepted");
        require(launch(0, 6, 1) == cudaErrorInvalidValue, "uncovered payload accepted");
        require(launch(1, 5, 1) == cudaErrorInvalidValue, "missing fields accepted");
        require(launch(1, 6, 0) == cudaErrorInvalidValue, "empty payload accepted");
        require(launch(1, 6, std::numeric_limits<std::uint64_t>::max())
            == cudaErrorInvalidValue, "payload multiplication overflow accepted");
        require(launch(1, 6, std::uint64_t{1} << 40)
            == cudaErrorInvalidValue, "launch grid overflow accepted");
    }
}

std::uint64_t exercise(int rank, int dimension, int species, int level)
{
    constexpr int slots = 3;
    const int fields = 6 + species, count = 1 << dimension, local_count = count / 2;
    const std::array<std::int32_t, 3> extent{8, dimension >= 2 ? 6 : 1, dimension == 3 ? 4 : 1};
    DeviceGridView grid{};
    grid.dim = dimension; grid.ng = 2;
    grid.is = 2; grid.js = dimension >= 2 ? 2 : 0; grid.ks = dimension == 3 ? 2 : 0;
    grid.ie = grid.is + extent[0] - 1; grid.je = grid.js + extent[1] - 1; grid.ke = grid.ks + extent[2] - 1;
    grid.total_x = extent[0] + 4; grid.total_y = extent[1] + (dimension >= 2 ? 4 : 0);
    grid.total_z = extent[2] + (dimension == 3 ? 4 : 0);
    grid.stride_y = grid.total_x; grid.stride_z = grid.total_x * grid.total_y;
    grid.total_size = grid.stride_z * grid.total_z;
    const int cells = grid.total_size;
    const auto plan = make_staging_plan(dimension, level, extent);
    std::vector<std::uint64_t> expected(std::size_t(count) * slots * fields * cells);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = std::bit_cast<std::uint64_t>(double(i + 1));
        if (i % 31 == 0) expected[i] = UINT64_C(0x8000000000000000);
        if (i % 37 == 0) expected[i] = UINT64_C(0x7ff8000000000000) | i;
    }
    auto offset = [&](int b, int slot, int field, int cell) {
        return ((std::size_t(b) * slots + slot) * fields + field) * cells + cell;
    };
    auto index = [&](const auto& first, const auto& box, std::uint64_t n) {
        const int i = first[0] + n % box[0]; n /= box[0];
        const int j = first[1] + n % box[1], k = first[2] + n / box[1];
        return grid.index(grid.is + i, grid.js + j, grid.ks + k);
    };
    std::vector<std::uint64_t> local(std::size_t(local_count) * slots * fields * cells);
    for (int b = rank; b < count; b += 2)
        std::copy_n(expected.data() + offset(b, 0, 0, 0), slots * fields * cells,
                    local.data() + offset(b / 2, 0, 0, 0));
    DeviceAllocation<double> states; states.allocate(local.size());
    ReusableDeviceAllocation<DeviceExchangeBlock> blocks;
    ReusableDeviceAllocation<DeviceExchangeOperation> send_ops, receive_ops, local_ops;
    ReusableDeviceAllocation<double> send_values, receive_values, local_values;
    Stream stream;
    cuda_check(cudaMemcpyAsync(states.get(), local.data(), local.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream.value));
    stream.finish();
    std::uint64_t traffic = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (int slot = 0; slot < slots; ++slot) {
            std::vector<DeviceExchangeBlock> views(local_count);
            for (int b = 0; b < local_count; ++b) {
                auto* state = states.get() + offset(b, slot, 0, 0);
                views[b] = {{state, state + cells, state + 2 * cells, state + 3 * cells,
                             state + 4 * cells, state + 5 * cells,
                             species ? state + 6 * cells : nullptr, cells, species}, grid};
            }
            BorrowedWork views_work{stream.value};
            blocks.reserve(views.size());
            cuda_check(cudaMemcpyAsync(blocks.get(), views.data(), views.size() * sizeof(views[0]),
                                       cudaMemcpyHostToDevice, stream.value));
            stream.finish();
            views_work.completed = true;
            for (const auto phase : plan.phases) {
                if (phase.count == 0) continue;
                std::vector<DeviceExchangeOperation> sending, receiving, same_rank;
                std::vector<std::uint64_t> expected_wire;
                std::array<int, 2> sends{}, receives{}, send_offsets{}, receive_offsets{};
                std::uint64_t send_cells = 0, receive_cells = 0, local_cells = 0;
                for (int peer = 0; peer < 2; ++peer) {
                    send_offsets[peer] = send_cells * fields; receive_offsets[peer] = receive_cells * fields;
                    for (std::size_t n = phase.first; n < phase.first + phase.count; ++n) {
                        const auto& op = plan.operations[n];
                        const auto size = amr::exchange_detail::checked_product(op.source_box.extent);
                        auto lower = [&](bool gather) {
                            DeviceExchangeOperation out{};
                            out.source_block = gather ? block_index(op.source) / 2 : -1;
                            out.destination_block = gather ? -1 : block_index(op.destination) / 2;
                            out.scratch_first = gather ? send_cells : receive_cells;
                            for (int axis = 0; axis < 3; ++axis) {
                                out.source_first[axis] = op.source_box.first[axis];
                                out.destination_first[axis] = op.destination_box.first[axis];
                                out.extent[axis] = op.source_box.extent[axis];
                            }
                            return out;
                        };
                        if (peer == rank && owner(op.source) == rank && owner(op.destination) == rank) {
                            auto local = lower(true);
                            local.destination_block = block_index(op.destination) / 2;
                            local.scratch_first = local_cells;
                            same_rank.push_back(local); local_cells += size;
                            continue;
                        }
                        if (owner(op.source) == rank && owner(op.destination) == peer) {
                            sending.push_back(lower(true)); send_cells += size; sends[peer] += size * fields;
                            // Existing Host GhostExchange sends cell-major 6+species words.
                            for (std::uint64_t c = 0; c < size; ++c)
                                for (int f = 0; f < fields; ++f)
                                    expected_wire.push_back(expected[offset(block_index(op.source), slot, f,
                                        index(op.source_box.first, op.source_box.extent, c))]);
                        }
                        if (owner(op.destination) == rank && owner(op.source) == peer) {
                            receiving.push_back(lower(false)); receive_cells += size; receives[peer] += size * fields;
                        }
                    }
                }
                std::array<int, 2> actual{};
                mpi_check(MPI_Alltoall(sends.data(), 1, MPI_INT, actual.data(), 1, MPI_INT, MPI_COMM_WORLD));
                require(actual == receives, "wire counts differ from logical plan");
                std::vector<std::uint64_t> outgoing(send_cells * fields), incoming(receive_cells * fields);
                BorrowedWork transfer_work{stream.value};
                send_ops.reserve(sending.size()); receive_ops.reserve(receiving.size());
                local_ops.reserve(same_rank.size()); local_values.reserve(local_cells * fields);
                send_values.reserve(outgoing.size()); receive_values.reserve(incoming.size());
                cuda_check(cudaMemcpyAsync(send_ops.get(), sending.data(), sending.size() * sizeof(sending[0]),
                                           cudaMemcpyHostToDevice, stream.value));
                cuda_check(cudaMemcpyAsync(receive_ops.get(), receiving.data(), receiving.size() * sizeof(receiving[0]),
                                           cudaMemcpyHostToDevice, stream.value));
                cuda_check(launch_same_level_exchange_gather(blocks.get(), send_ops.get(), sending.size(),
                    fields, send_cells, send_values.get(), stream.value));
                cuda_check(cudaMemcpyAsync(outgoing.data(), send_values.get(), outgoing.size() * sizeof(double),
                                           cudaMemcpyDeviceToHost, stream.value));
                if (!same_rank.empty()) {
                    cuda_check(cudaMemcpyAsync(local_ops.get(), same_rank.data(),
                        same_rank.size() * sizeof(same_rank[0]), cudaMemcpyHostToDevice, stream.value));
                    cuda_check(launch_same_level_exchange_phase(blocks.get(), local_ops.get(), same_rank.size(),
                        fields, local_cells, local_values.get(), stream.value));
                }
                stream.finish(); // MPI cannot read pending D2H.
                require(outgoing == expected_wire, "GPU packet differs from existing Host MPI word order");
                mpi_check(MPI_Alltoallv(outgoing.data(), sends.data(), send_offsets.data(), MPI_UINT64_T,
                    incoming.data(), receives.data(), receive_offsets.data(), MPI_UINT64_T, MPI_COMM_WORLD));
                cuda_check(cudaMemcpyAsync(receive_values.get(), incoming.data(), incoming.size() * sizeof(double),
                                           cudaMemcpyHostToDevice, stream.value));
                cuda_check(launch_same_level_exchange_scatter(blocks.get(), receive_ops.get(), receiving.size(),
                    fields, receive_cells, receive_values.get(), stream.value));
                stream.finish(); // Next-axis donors and buffer reuse require completed scatter.
                transfer_work.completed = true;
                traffic += (outgoing.size() + incoming.size()) * sizeof(double);
                const auto snapshot = expected;
                for (std::size_t n = phase.first; n < phase.first + phase.count; ++n) {
                    const auto& op = plan.operations[n];
                    const auto size = amr::exchange_detail::checked_product(op.source_box.extent);
                    for (std::uint64_t c = 0; c < size; ++c)
                        for (int f = 0; f < fields; ++f)
                            expected[offset(block_index(op.destination), slot, f,
                                index(op.destination_box.first, op.destination_box.extent, c))]
                                = snapshot[offset(block_index(op.source), slot, f,
                                    index(op.source_box.first, op.source_box.extent, c))];
                }
                cuda_check(cudaMemcpyAsync(local.data(), states.get(), local.size() * sizeof(double),
                                           cudaMemcpyDeviceToHost, stream.value));
                stream.finish();
                for (int b = rank; b < count; b += 2)
                    require(std::equal(local.data() + offset(b / 2, 0, 0, 0),
                        local.data() + offset(b / 2, 0, 0, 0) + slots * fields * cells,
                        expected.data() + offset(b, 0, 0, 0)), "fields/slots/corners differ bitwise");
            }
        }
    }
    return traffic;
}
} // namespace

int main(int argc, char** argv)
{
    int provided = 0, rank = 0, size = 0;
    mpi_check(MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided));
    mpi_check(MPI_Comm_rank(MPI_COMM_WORLD, &rank)); mpi_check(MPI_Comm_size(MPI_COMM_WORLD, &size));
    try {
        require(provided >= MPI_THREAD_FUNNELED && size == 2, "requires two FUNNELED MPI ranks");
        int devices = 0, all_present = 0;
        const int present = cudaGetDeviceCount(&devices) == cudaSuccess && devices > 0;
        mpi_check(MPI_Allreduce(&present, &all_present, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD));
        if (!all_present) { MPI_Finalize(); return 77; }
        cuda_check(cudaSetDevice(0)); // Also supports two ranks on one GPU.
        rejected_launches();
        std::uint64_t traffic = 0;
        for (int dimension : {1, 2, 3})
            for (int species : {0, 4, 37})
                for (int level : {0, 2}) traffic += exercise(rank, dimension, species, level);
        if (rank == 0) std::cout << "CUDA Host-staged MPI: 18 layouts, 162 slot exchanges, "
            << "324 phase checks per rank; bitwise fields/corners/untouched slots passed; "
            << traffic << " staged bytes per rank\n";
        mpi_check(MPI_Finalize()); return 0;
    } catch (const std::exception& error) {
        std::cerr << "rank " << rank << ": " << error.what() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 1); return 1;
    }
}
