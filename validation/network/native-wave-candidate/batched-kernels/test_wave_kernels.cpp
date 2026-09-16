// Device-to-device implementation equivalence, not a nuclear physics oracle.
#include "SparseWaveKernels.h"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/microphysics/SparseEquilibration.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace arch::cuda;
using namespace arch::cuda::experimental;
namespace {
struct Stream {
    cudaStream_t value{};
    int device{};
    Stream() {
        check_cuda(cudaGetDevice(&device), "test device");
        check_cuda(cudaStreamCreate(&value), "test stream");
    }
    ~Stream() {
        set_device_and_quiesce_or_terminate(device, value);
        require_cuda_success_or_terminate(cudaStreamDestroy(value));
    }
};
struct Completion {
    Stream& stream;
    ~Completion() { set_device_and_quiesce_or_terminate(stream.device, stream.value); }
};
template<class T> struct Buffer {
    DeviceAllocation<T> device;
    explicit Buffer(const std::vector<T>& host) {
        device.allocate(host.size());
        check_cuda(cudaMemcpy(device.get(), host.data(), host.size() * sizeof(T), cudaMemcpyHostToDevice), "fixture upload");
    }
    T* get() const { return device.get(); }
    std::vector<T> read() const {
        std::vector<T> result(device.size());
        check_cuda(cudaMemcpy(result.data(), get(), result.size() * sizeof(T), cudaMemcpyDeviceToHost), "fixture read");
        return result;
    }
};
template<class T> void equal(const Buffer<T>& old, const Buffer<T>& next, const std::string& label) {
    const auto a = old.read(), b = next.read();
    if (a.size() != b.size() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) != 0)
        throw std::runtime_error("non-bitwise-equivalent kernel buffers: " + label);
}
struct Work {
    Buffer<double> rows, columns, matrix, rhs, solution, residual, correction, physical;
    Buffer<int> statuses;
    Work(int count, int n, int nnz, const std::vector<double>& solutions)
        : rows(std::vector<double>(count * n, -17.25)),
          columns(std::vector<double>(count * n, -19.5)),
          matrix(std::vector<double>(count * nnz, -23.75)),
          rhs(std::vector<double>(count * n, -29.125)), solution(solutions),
          residual(std::vector<double>(count * n, -31.5)),
          correction(std::vector<double>(count * n, -37.25)),
          physical(std::vector<double>(count * n, -41.5)),
          statuses(std::vector<int>(2 * count, 7)) {}
};
void equal(const Work& old, const Work& next, const char* phase, Stream& stream) {
    check_cuda(cudaStreamSynchronize(stream.value), "test completed phase");
    const std::string label(phase);
    equal(old.rows, next.rows, label + "/row-divisor");
    equal(old.columns, next.columns, label + "/column-divisor");
    equal(old.matrix, next.matrix, label + "/matrix");
    equal(old.rhs, next.rhs, label + "/rhs");
    equal(old.solution, next.solution, label + "/scaled-solution");
    equal(old.residual, next.residual, label + "/residual");
    equal(old.correction, next.correction, label + "/correction");
    equal(old.physical, next.physical, label + "/physical-output");
    equal(old.statuses, next.statuses, label + "/statuses");
}

void run(int n, int count, int scenario, bool all_active) {
    Stream stream;
    std::vector<int> offsets{0}, columns, column_offsets(n + 1, 0), column_slots;
    for (int row = 0; row < n; ++row) {
        for (int column = std::max(0, row - 1); column <= std::min(n - 1, row + 1); ++column) {
            columns.push_back(column);
            ++column_offsets[column + 1];
        }
        offsets.push_back(static_cast<int>(columns.size()));
    }
    for (int i = 1; i <= n; ++i) column_offsets[i] += column_offsets[i - 1];
    column_slots.resize(columns.size());
    auto next = column_offsets;
    for (std::size_t slot = 0; slot < columns.size(); ++slot)
        column_slots[next[columns[slot]]++] = static_cast<int>(slot);
    const int nnz = static_cast<int>(columns.size());
    std::vector<double> matrices(count * nnz), rhs(count * n), solutions(count * n);
    for (int lane = 0; lane < count; ++lane) {
        for (int row = 0; row < n; ++row) {
            const double scale = scenario == 1 ? (row % 2 ? 1e-80 : 1e80) : 1.0;
            for (int slot = offsets[row]; slot < offsets[row + 1]; ++slot)
                matrices[lane * nnz + slot] = scale * (columns[slot] == row ? 4.0 + lane * .125 : -.25);
            rhs[lane * n + row] = scale * (1.0 + .03125 * (row % 7) + lane * .0625);
            solutions[lane * n + row] = .75 + (row % 11) * .015625 + lane * .125;
        }
    }
    if (scenario == 2) matrices[0] = std::numeric_limits<double>::quiet_NaN();
    if (scenario == 3) rhs[0] = std::numeric_limits<double>::infinity();
    Buffer<int> dr(offsets), dc(columns), dco(column_offsets), dcs(column_slots);
    Buffer<double> da(matrices), db(rhs);
    Work old(count, n, nnz, solutions), batched(count, n, nnz, solutions);
    Completion completion{stream}; // Must quiesce before any buffers are released.
    WaveKernelBatch b{};
    b.capacity = count; b.extent = n; b.nonzeros = nnz;
    b.offsets = dr.get(); b.columns = dc.get(); b.column_offsets = dco.get(); b.column_slots = dcs.get();
    b.row_divisors = batched.rows.get(); b.column_divisors = batched.columns.get();
    b.scaled_values = batched.matrix.get(); b.scaled_rhs = batched.rhs.get();
    b.scaled_solution = batched.solution.get(); b.residual = batched.residual.get();
    b.correction = batched.correction.get(); b.statuses = batched.statuses.get();
    for (int lane = 0; lane < count; ++lane)
        b.lanes[lane] = {da.get() + lane * nnz, db.get() + lane * n,
                        batched.physical.get() + lane * n, (all_active || lane % 3 != 1) ? 1u : 0u};
    for (int lane = 0; lane < count; ++lane) if (b.lanes[lane].selected)
        check_cuda(equilibrate_sparse_rows(n, dr.get(), da.get() + lane * nnz,
            old.rows.get() + lane * n, old.matrix.get() + lane * nnz,
            old.statuses.get() + 2 * lane, stream.value), "reference rows");
    check_cuda(wave_normalize_rows(b, stream.value), "batched rows");
    equal(old, batched, "rows", stream);
    for (int lane = 0; lane < count; ++lane) if (b.lanes[lane].selected)
        check_cuda(equilibrate_sparse_columns(n, dco.get(), dcs.get(), old.matrix.get() + lane * nnz,
            old.columns.get() + lane * n, old.statuses.get() + 2 * lane, stream.value), "reference columns");
    check_cuda(wave_normalize_columns(b, stream.value), "batched columns");
    equal(old, batched, "columns", stream);
    for (bool correction : {false, true}) {
        for (int lane = 0; lane < count; ++lane) {
            if (!b.lanes[lane].selected) {
                check_cuda(cudaMemsetAsync(old.rhs.get() + lane * n, 0, n * sizeof(double), stream.value), "reference inactive RHS");
                continue;
            }
            check_cuda(equilibrate_sparse_vector(n,
                correction ? old.residual.get() + lane * n : db.get() + lane * n,
                old.rows.get() + lane * n, old.rhs.get() + lane * n,
                old.statuses.get() + 2 * lane, true, stream.value), "reference RHS");
        }
        check_cuda(wave_normalize_rhs(b, correction, stream.value), "batched RHS");
        equal(old, batched, "RHS", stream);
        for (int lane = 0; lane < count; ++lane) if (b.lanes[lane].selected)
            check_cuda(equilibrate_sparse_vector(n, old.solution.get() + lane * n,
                old.columns.get() + lane * n,
                correction ? old.correction.get() + lane * n : old.physical.get() + lane * n,
                old.statuses.get() + 2 * lane, false, stream.value), "reference solution");
        check_cuda(wave_denormalize_solution(b, correction, stream.value), "batched solution");
        equal(old, batched, "solution", stream);
        if (correction) {
            for (int lane = 0; lane < count; ++lane) if (b.lanes[lane].selected) {
                check_cuda(accumulate_sparse_correction(n, old.correction.get() + lane * n,
                    old.physical.get() + lane * n, old.statuses.get() + 2 * lane, stream.value), "reference correction");
                check_cuda(accumulate_sparse_correction(n, batched.correction.get() + lane * n,
                    batched.physical.get() + lane * n, batched.statuses.get() + 2 * lane, stream.value), "unchanged correction");
            }
        }
        for (int lane = 0; lane < count; ++lane) if (b.lanes[lane].selected)
            check_cuda(original_sparse_residual(n, dr.get(), dc.get(), da.get() + lane * nnz,
                db.get() + lane * n, old.physical.get() + lane * n, old.residual.get() + lane * n,
                old.statuses.get() + 2 * lane + 1, stream.value), "reference residual");
        check_cuda(wave_original_residual(b, stream.value), "batched residual");
        equal(old, batched, "residual", stream);
    }
    const auto aa = da.read(), bb = db.read();
    if (std::memcmp(aa.data(), matrices.data(), matrices.size() * sizeof(double)) != 0
            || std::memcmp(bb.data(), rhs.data(), rhs.size() * sizeof(double)) != 0)
        throw std::runtime_error("immutable original matrix/RHS was modified");
    std::cout << "WAVE_KERNEL_BITWISE_PARITY_PASS extent=" << n << " capacity=" << count
              << " scenario=" << scenario << " all_active=" << all_active << '\n';
}
} // namespace

int main() {
    try {
        int cases = 0;
        for (int n : {1, 151, 201, 513}) for (int capacity : {1, 2, 8, 32})
            for (int scenario : {0, 1, 2, 3}) for (bool all : {false, true}) {
                run(n, capacity, scenario, all);
                ++cases;
            }
        std::cout << "WAVE_KERNEL_MATRIX_PASS cases=" << cases
                  << " scope=execution-equivalence-not-nuclear-or-performance\n";
    } catch (const std::exception& error) {
        std::cerr << "WAVE_KERNEL_MATRIX_FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}
