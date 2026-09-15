// Real CUDA/cuDSS contract test, NOT a replacement for nuclear/ODE validation.
#include "CuDssSparseWaveSolver.h"
#include "cuda/common/DeviceAllocation.h"
#include "numerics/linalg/CsrMatrixView.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace arch::cuda;
using namespace arch::cuda::experimental;
using Op = SparseWaveOperation;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejected(F&& f, const char* message, const char* expected_reason = nullptr) {
    bool failed = false;
    try { f(); }
    catch (const std::bad_alloc&) { throw; } // Resource exhaustion is not a passing negative contract.
    catch (const std::exception& error) {
        failed = expected_reason == nullptr || std::string(error.what()).find(expected_reason) != std::string::npos;
    }
    require(failed, message);
}
struct Stream {
    cudaStream_t value{};
    int device = 0;
    Stream() {
        check_cuda(cudaGetDevice(&device), "test device");
        check_cuda(cudaStreamCreate(&value), "test stream");
    }
    ~Stream() {
        set_device_and_quiesce_or_terminate(device, value);
        require_cuda_success_or_terminate(cudaStreamDestroy(value));
    }
};
// Declared after each Host result/device metadata owner, so exceptional
// unwinding cannot free a transfer buffer before its completion witness.
struct TransferCompletion {
    cudaStream_t stream;
    int device = 0;
    bool finished = false;
    explicit TransferCompletion(cudaStream_t value) : stream(value) {
        check_cuda(cudaGetDevice(&device), "test transfer device");
    }
    ~TransferCompletion() {
        if (!finished) set_device_and_quiesce_or_terminate(device, stream);
    }
    void finish() {
        check_cuda(cudaStreamSynchronize(stream), "test transfer completion");
        finished = true;
    }
};
struct System {
    std::vector<double> matrix, b, exact, x;
    DeviceAllocation<double> a_device, b_device, x_device;
    System(int n, int nnz) : matrix(nnz), b(n), exact(n), x(n, -987.25) {
        a_device.allocate(nnz); b_device.allocate(n); x_device.allocate(n);
    }
};
void upload(const std::vector<double>& values, double* to, cudaStream_t stream) {
    TransferCompletion completion(stream);
    check_cuda(cudaMemcpyAsync(to, values.data(), values.size() * sizeof(double),
        cudaMemcpyHostToDevice, stream), "test vector upload");
    // Test-owned Host input may otherwise unwind before the provider is reached.
    // This is correctness-only setup, never a performance sample.
    completion.finish();
}
std::vector<double> download(const double* from, std::size_t count, cudaStream_t stream) {
    std::vector<double> result(count);
    TransferCompletion completion(stream);
    check_cuda(cudaMemcpyAsync(result.data(), from, count * sizeof(double),
        cudaMemcpyDeviceToHost, stream), "test vector download");
    completion.finish();
    return result;
}
template<int N> void verify(System& s, const std::vector<int>& rows,
                            const std::vector<int>& columns, cudaStream_t stream) {
    s.x = download(s.x_device.get(), N, stream);
    double maximum = 0;
    for (int i = 0; i < N; ++i) {
        require(std::isfinite(s.x[i]), "nonfinite native-batch solution");
        maximum = std::max(maximum, std::abs(s.x[i] - s.exact[i]) / std::max(1.0, std::abs(s.exact[i])));
    }
    require(maximum < 1e-12, "independent manufactured solution mismatch");
    CsrMatrixView<N> matrix{rows.data(), columns.data(), s.matrix.data(), static_cast<int>(columns.size()), true};
    require(matrix.solution_accurate(s.b.data(), s.x.data()), "original-system residual rejected batch solution");
    require(download(s.a_device.get(), s.matrix.size(), stream) == s.matrix, "borrowed matrix was modified");
    require(download(s.b_device.get(), s.b.size(), stream) == s.b, "borrowed RHS was modified");
}
template<int N> void run(int capacity) {
    Stream stream;
    std::vector<int> rows{0}, columns;
    for (int i = 0; i < N; ++i) {
        if (i) columns.push_back(i - 1);
        columns.push_back(i);
        if (i + 1 < N) columns.push_back(i + 1);
        rows.push_back(static_cast<int>(columns.size()));
    }
    DeviceAllocation<int> device_rows, device_columns;
    device_rows.allocate(rows.size()); device_columns.allocate(columns.size());
    TransferCompletion metadata_completion(stream.value);
    check_cuda(cudaMemcpyAsync(device_rows.get(), rows.data(), rows.size() * sizeof(int),
        cudaMemcpyHostToDevice, stream.value), "test rows upload");
    check_cuda(cudaMemcpyAsync(device_columns.get(), columns.data(), columns.size() * sizeof(int),
        cudaMemcpyHostToDevice, stream.value), "test columns upload");
    metadata_completion.finish();
    std::vector<std::unique_ptr<System>> systems;
    std::vector<SparseWaveTask> tasks(capacity);
    auto fill = [&](int lane, int round) {
        auto& s = *systems[lane];
        std::fill(s.b.begin(), s.b.end(), 0.0);
        for (int i = 0; i < N; ++i) s.exact[i] = .25 + i / 32.0 + lane / 64.0 + round / 8.0;
        for (int i = 0; i < N; ++i) {
            const double scale = std::ldexp(1.0, (i % 3 - 1) * 32);
            for (int k = rows[i]; k < rows[i + 1]; ++k) {
                s.matrix[k] = scale * (columns[k] == i ? 4.0 + round / 8.0 + lane / 64.0 : -.5);
                s.b[i] += s.matrix[k] * s.exact[columns[k]];
            }
        }
        upload(s.matrix, s.a_device.get(), stream.value);
        upload(s.b, s.b_device.get(), stream.value);
        upload(s.x, s.x_device.get(), stream.value);
        tasks[lane] = {Op::FactorizeAndSolve, s.a_device.get(), s.b_device.get(), s.x_device.get(),
            static_cast<std::uint64_t>(1 + round)};
    };
    for (int i = 0; i < capacity; ++i) {
        systems.push_back(std::make_unique<System>(N, static_cast<int>(columns.size())));
        fill(i, 0);
    }
    // Declare the provider AFTER all borrowed numeric owners. Its destructor
    // fences queued work before any System/metadata storage is released.
    CuDssSparseWaveSolver provider(capacity, N, static_cast<int>(columns.size()),
        device_rows.get(), device_columns.get(), stream.value);
    // Empty/invalid requests cannot enqueue work or publish optimistic success.
    auto idle = tasks;
    for (auto& t : idle) t = {};
    auto before = provider.statistics();
    provider.execute(idle).require_success();
    require(provider.statistics().native_factor_calls == before.native_factor_calls
        && provider.statistics().native_solve_calls == before.native_solve_calls, "idle wave ran dummy work");
    auto malformed = tasks;
    malformed[0].operation = Op::SolveWithFactors;
    rejected([&] { provider.execute(malformed); }, "cold factor reuse accepted");
    malformed = tasks; malformed[0].values = systems[0]->matrix.data();
    rejected([&] { provider.execute(malformed); }, "host numeric input accepted");
    malformed = tasks; malformed[0].solution = const_cast<double*>(malformed[0].rhs);
    rejected([&] { provider.execute(malformed); }, "RHS/output alias accepted");
    malformed = tasks; malformed[0].solution = reinterpret_cast<double*>(device_rows.get());
    rejected([&] { provider.execute(malformed); }, "output/CSR metadata alias accepted",
        "output overlaps immutable CSR metadata");
    if (capacity > 1) {
        malformed = tasks; malformed[0].solution = malformed[1].solution;
        rejected([&] { provider.execute(malformed); }, "cross-lane output alias accepted");
    }
    rejected([&] { provider.execute(std::span<const SparseWaveTask>(tasks.data(), capacity - 1)); },
        "wrong wave length accepted");
    // Initially unused slots must be private identity placeholders, not reads
    // from uninitialized per-lane matrices. Later activation must replace them.
    auto first_lane = tasks;
    for (int i = 1; i < capacity; ++i) first_lane[i] = {};
    provider.execute(first_lane).require_success();
    verify<N>(*systems[0], rows, columns, stream.value);
    systems[0]->x.assign(N, -987.25);
    upload(systems[0]->x, systems[0]->x_device.get(), stream.value);
    // One native batch supports mixed factor-only and factor+solve requests.
    for (int i = 0; i < capacity; i += 2) tasks[i].operation = Op::Factorize;
    provider.execute(tasks).require_success();
    for (int i = 0; i < capacity; ++i) {
        if (i % 2) verify<N>(*systems[i], rows, columns, stream.value);
        else require(download(systems[i]->x_device.get(), N, stream.value)
            == std::vector<double>(N, -987.25), "factor-only wave wrote output");
        tasks[i].operation = Op::SolveWithFactors;
    }
    before = provider.statistics();
    provider.execute(tasks).require_success();
    require(provider.statistics().native_factor_calls == before.native_factor_calls,
        "pure reuse wave refactorized matrices");
    for (auto& s : systems) verify<N>(*s, rows, columns, stream.value);
    // Changing one lane refactorizes the whole bounded cohort, counted honestly.
    fill(0, 1);
    before = provider.statistics();
    provider.execute(tasks).require_success();
    require(provider.statistics().native_factor_calls == before.native_factor_calls + 1
        && provider.statistics().native_factor_systems == before.native_factor_systems + capacity,
        "whole-cohort factor work omitted from statistics");
    for (auto& s : systems) verify<N>(*s, rows, columns, stream.value);
    for (auto& t : tasks) t.operation = Op::SolveWithFactors;
    malformed = tasks; ++malformed[0].token;
    rejected([&] { provider.execute(malformed); }, "stale token accepted");
    malformed = tasks; malformed[0].token = 0;
    rejected([&] { provider.execute(malformed); }, "zero token accepted");
    // No stale output publication when only one lane remains active.
    auto tail = tasks;
    for (int i = 1; i < capacity; ++i) tail[i] = {};
    provider.execute(tail).require_success();
    for (auto& s : systems) verify<N>(*s, rows, columns, stream.value);
    before = provider.statistics();
    provider.invalidate();
    provider.execute(tasks).require_success();
    require(provider.statistics().analyses == before.analyses + 1,
        "external residual rejection did not recreate native analysis");
    for (auto& s : systems) verify<N>(*s, rows, columns, stream.value);
    // A native/arithmetic failure is fatal, not an ODE stiffness success.
    auto broken = systems[0]->matrix;
    broken[0] = std::numeric_limits<double>::quiet_NaN();
    upload(broken, systems[0]->a_device.get(), stream.value);
    malformed = tasks; malformed[0].operation = Op::FactorizeAndSolve; malformed[0].token = 77;
    rejected([&] { provider.execute(malformed).require_success(); }, "nonfinite factor accepted");
    upload(systems[0]->matrix, systems[0]->a_device.get(), stream.value);
    for (auto& t : tasks) { t.operation = Op::FactorizeAndSolve; t.token += 100; }
    provider.execute(tasks).require_success();
    for (auto& s : systems) verify<N>(*s, rows, columns, stream.value);
    // A singular inconsistent system may be rejected by native INFO or by the
    // unchanged original residual. Neither route is an accepted ODE response.
    auto singular = systems[0]->matrix;
    std::fill(singular.begin() + rows[0], singular.begin() + rows[1], 0.0);
    auto singular_rhs = systems[0]->b;
    singular_rhs[0] = 1.0;
    upload(singular, systems[0]->a_device.get(), stream.value);
    upload(singular_rhs, systems[0]->b_device.get(), stream.value);
    malformed = tasks; malformed[0].operation = Op::FactorizeAndSolve; malformed[0].token += 1000;
    bool singular_rejected = false;
    try {
        const auto result = provider.execute(malformed);
        if (!result.success()) singular_rejected = true;
        else {
            const auto x = download(systems[0]->x_device.get(), N, stream.value);
            CsrMatrixView<N> a{rows.data(), columns.data(), singular.data(), static_cast<int>(columns.size()), true};
            singular_rejected = !a.solution_accurate(singular_rhs.data(), x.data());
        }
    } catch (const std::runtime_error&) { singular_rejected = true; }
    require(singular_rejected, "singular inconsistent system was accepted");
    provider.invalidate();
    upload(systems[0]->matrix, systems[0]->a_device.get(), stream.value);
    upload(systems[0]->b, systems[0]->b_device.get(), stream.value);
    for (auto& t : tasks) { t.operation = Op::FactorizeAndSolve; t.token += 2000; }
    provider.execute(tasks).require_success();
    for (auto& s : systems) verify<N>(*s, rows, columns, stream.value);
    const auto stats = provider.statistics();
    require(stats.estimated_peak_device_bytes > 0 && stats.estimated_peak_device_bytes <= 256ull*1024*1024,
        "batch factor/private estimate missing or over budget");
    std::cout << "SPARSE_WAVE_CONTRACT_PASS extent=" << N << " capacity=" << capacity
        << " analyses=" << stats.analyses << " factor_calls=" << stats.native_factor_calls
        << " factor_systems=" << stats.native_factor_systems << " solve_calls=" << stats.native_solve_calls
        << " solve_systems=" << stats.native_solve_systems << " syncs=" << stats.synchronizations
        << " estimated_peak_bytes=" << stats.estimated_peak_device_bytes << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: test_sparse_wave <151|201> <1..32>");
        const int n = std::stoi(argv[1]), capacity = std::stoi(argv[2]);
        require(n == 151 || n == 201, "unsupported manufactured extent");
        require(capacity >= 1 && capacity <= 32, "capacity outside 1..32");
        rejected([&] { CuDssSparseWaveSolver bad(0, 151, 451, nullptr, nullptr, nullptr); },
            "zero capacity accepted", "invalid bounded batch dimensions");
        rejected([&] { CuDssSparseWaveSolver bad(32, INT_MAX, INT_MAX, nullptr, nullptr, nullptr); },
            "aggregate overflow accepted", "invalid bounded batch dimensions");
        rejected([&] { CuDssSparseWaveSolver bad(1, 100000000, 100000000, nullptr, nullptr, nullptr); },
            "private budget overflow accepted", "private dimensions exceed candidate budget");
        if (n == 151) run<151>(capacity); else run<201>(capacity);
    } catch (const std::exception& e) {
        std::cerr << "SPARSE_WAVE_CONTRACT_FAIL: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
