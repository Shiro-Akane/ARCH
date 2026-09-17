// Manufacturing/ownership contracts only, NOT nuclear, ODE or performance proof.
// The build extracts this support unchanged from the pinned original GPU test.
#include "shared_sparse_wave_test_support.h"
#include "CuDssSparseWindowSolver.h"

namespace {
template<int N> void run_window(int window, int capacity) {
    Stream stream;
    std::vector<int> rows{0}, columns;
    for (int i = 0; i < N; ++i) {
        if (i) columns.push_back(i-1);
        columns.push_back(i);
        if (i+1 < N) columns.push_back(i+1);
        rows.push_back(static_cast<int>(columns.size()));
    }
    DeviceAllocation<int> device_rows, device_columns;
    device_rows.allocate(rows.size()); device_columns.allocate(columns.size());
    TransferCompletion metadata_completion(stream.value);
    check_cuda(cudaMemcpyAsync(device_rows.get(), rows.data(), rows.size()*sizeof(int),
                              cudaMemcpyHostToDevice, stream.value), "window test rows");
    check_cuda(cudaMemcpyAsync(device_columns.get(), columns.data(), columns.size()*sizeof(int),
                              cudaMemcpyHostToDevice, stream.value), "window test columns");
    metadata_completion.finish();
    std::vector<std::unique_ptr<System>> systems;
    std::vector<SparseWaveTask> tasks(window);
    for (int lane = 0; lane < window; ++lane)
        systems.push_back(std::make_unique<System>(N, static_cast<int>(columns.size())));
    auto fill = [&](int lane, int round, std::uint64_t token) {
        auto& s = *systems[lane];
        std::fill(s.b.begin(), s.b.end(), 0.0);
        s.x.assign(N, -987.25);
        for (int i = 0; i < N; ++i) s.exact[i] = .25 + i/32.0 + lane/64.0 + round/8.0;
        for (int i = 0; i < N; ++i) {
            const double scale = std::ldexp(1.0, (i%3-1)*32);
            for (int k = rows[i]; k < rows[i+1]; ++k) {
                s.matrix[k] = scale*(columns[k] == i ? 4.0 + round/8.0 + lane/64.0 : -.5);
                s.b[i] += s.matrix[k]*s.exact[columns[k]];
            }
        }
        upload(s.matrix, s.a_device.get(), stream.value);
        upload(s.b, s.b_device.get(), stream.value);
        upload(s.x, s.x_device.get(), stream.value);
        tasks[lane] = {Op::FactorizeAndSolve, s.a_device.get(), s.b_device.get(), s.x_device.get(), token};
    };
    for (int lane = 0; lane < window; ++lane) fill(lane, 0, 11);
    // All borrowed owners precede the provider; its destructor is the fence.
    CuDssSparseWindowSolver provider(window, capacity, N, static_cast<int>(columns.size()),
                                    device_rows.get(), device_columns.get(), stream.value);
    auto verify_all = [&] {
        for (auto& s : systems) verify<N>(*s, rows, columns, stream.value);
    };
    auto no_work_rejection = [&](const std::vector<SparseWaveTask>& malformed, const char* reason) {
        const auto before = provider.statistics();
        const auto logical = provider.logical_statistics();
        std::vector<std::vector<double>> outputs;
        for (auto& s : systems) outputs.push_back(download(s->x_device.get(), N, stream.value));
        rejected([&] { provider.execute(malformed); }, "invalid window was accepted", reason);
        const auto after = provider.statistics();
        require(after.analyses == before.analyses && after.native_factor_calls == before.native_factor_calls
            && after.native_solve_calls == before.native_solve_calls && after.kernels == before.kernels
            && after.synchronizations == before.synchronizations,
            "window preflight rejection occurred after native numeric work");
        require(provider.logical_statistics().executions == logical.executions
            && provider.logical_statistics().native_pages == logical.native_pages,
            "window preflight rejection published logical work");
        for (int i = 0; i < window; ++i)
            require(download(systems[i]->x_device.get(), N, stream.value) == outputs[i],
                    "window preflight rejection modified an earlier page output");
    };
    auto idle = std::vector<SparseWaveTask>(window);
    provider.execute(idle).require_success();
    require(provider.statistics().native_factor_calls == 0 && provider.statistics().native_solve_calls == 0
        && provider.logical_statistics().executions == 0, "idle window performed work");
    auto malformed = tasks;
    malformed.back().operation = Op::SolveWithFactors;
    no_work_rejection(malformed, "absent, stale, or relocated logical token");
    malformed = tasks; malformed.back().token = 0;
    no_work_rejection(malformed, "token zero is reserved");
    malformed = tasks; malformed.back().operation = static_cast<Op>(99);
    no_work_rejection(malformed, "unknown operation");
    malformed = tasks; malformed.back().values = systems.back()->matrix.data();
    no_work_rejection(malformed, "different memory space/device");
    malformed = tasks; malformed.back().values = nullptr;
    no_work_rejection(malformed, "nonnull device buffers");
    malformed = tasks; malformed[0].solution = reinterpret_cast<double*>(device_rows.get());
    no_work_rejection(malformed, "output overlaps immutable CSR metadata");
    malformed = tasks; malformed[0].solution = const_cast<double*>(malformed.back().rhs);
    no_work_rejection(malformed, "output overlaps borrowed input/another output");
    malformed = tasks; malformed[0].solution = const_cast<double*>(malformed.back().values);
    no_work_rejection(malformed, "output overlaps borrowed input/another output");
    if (window > 1) {
        malformed = tasks; malformed[0].solution = malformed.back().solution;
        no_work_rejection(malformed, "output overlaps borrowed input/another output");
    }
    malformed = tasks; malformed.pop_back();
    no_work_rejection(malformed, "wrong fixed capacity");

    // Empty earlier pages must be skipped, and inactive outputs remain untouched.
    auto last_only = idle;
    last_only.back() = tasks.back();
    provider.execute(last_only).require_success();
    require(provider.logical_statistics().native_pages == 1, "empty pages ran dummy native work");
    verify<N>(*systems.back(), rows, columns, stream.value);
    for (int i = 0; i+1 < window; ++i)
        require(download(systems[i]->x_device.get(), N, stream.value) == std::vector<double>(N, -987.25),
                "inactive page output was changed");

    // Every lane deliberately has the same caller token but different values.
    provider.execute(tasks).require_success();
    verify_all();
    for (auto& task : tasks) task.operation = Op::SolveWithFactors;
    const int pages = (window+capacity-1)/capacity;
    for (int repetition = 0; repetition < 2; ++repetition) {
        const auto before = provider.statistics();
        const auto logical = provider.logical_statistics();
        provider.execute(tasks).require_success();
        verify_all();
        const auto after = provider.statistics();
        const auto expected_calls = window == capacity ? 0 : pages;
        require(after.native_factor_calls-before.native_factor_calls == static_cast<std::uint64_t>(expected_calls)
            && after.native_factor_systems-before.native_factor_systems == static_cast<std::uint64_t>(expected_calls*capacity),
            "eviction restore omitted whole-cohort factor cost or refactored resident factors");
        require(provider.logical_statistics().native_pages-logical.native_pages == static_cast<std::uint64_t>(pages),
                "native page count differs from complete window coverage");
        const auto restored = provider.logical_statistics().evicted_factor_restores-logical.evicted_factor_restores;
        require(window == capacity ? restored == 0 : restored > 0,
                "evicted logical factors were not restored or resident reuse was lost");
    }
    // Latest tail remains resident. Do not disturb it when preceding pages are idle.
    last_only = idle; last_only.back() = tasks.back();
    auto before = provider.statistics();
    provider.execute(last_only).require_success();
    require(provider.statistics().native_factor_calls == before.native_factor_calls,
            "resident tail-only solve refactorized");
    verify_all();
    malformed = tasks; ++malformed.back().token;
    no_work_rejection(malformed, "absent, stale, or relocated logical token");
    if (window > 1) {
        malformed = tasks; malformed.back().values = tasks.front().values;
        no_work_rejection(malformed, "absent, stale, or relocated logical token");
        malformed = idle; malformed[0] = tasks[0];
        malformed[0].solution = const_cast<double*>(tasks.back().values);
        no_work_rejection(malformed, "output overlaps inactive retained matrix");
    }

    // Changed coefficients require a fresh token; output of Factorize-only and
    // Idle requests must remain sentinel even when another page solves.
    for (int i = 0; i < window; ++i) {
        systems[i]->x.assign(N, -987.25);
        upload(systems[i]->x, systems[i]->x_device.get(), stream.value);
        if (i%3 == 0) fill(i, 1, 12);
    }
    auto mixed = tasks;
    for (int i = 0; i < window; ++i) {
        if (i%3 == 0) mixed[i].operation = Op::Factorize;
        if (i%3 == 1) mixed[i] = {};
    }
    provider.execute(mixed).require_success();
    for (int i = 0; i < window; ++i) {
        if (i%3 == 2) verify<N>(*systems[i], rows, columns, stream.value);
        else require(download(systems[i]->x_device.get(), N, stream.value) == std::vector<double>(N, -987.25),
                     "factor-only or idle logical lane wrote output");
        tasks[i].operation = Op::SolveWithFactors;
    }
    provider.execute(tasks).require_success();
    verify_all();
    before = provider.statistics();
    provider.invalidate();
    provider.execute(tasks).require_success();
    require(provider.statistics().analyses == before.analyses+1,
            "external rejection did not recreate the single native provider");
    verify_all();

    // Fail the last page after earlier pages can succeed. Proposed identities
    // from that partial execution must NOT be available for subsequent reuse.
    auto broken = systems.back()->matrix;
    broken[0] = std::numeric_limits<double>::quiet_NaN();
    upload(broken, systems.back()->a_device.get(), stream.value);
    malformed = tasks;
    for (auto& task : malformed) { task.operation = Op::FactorizeAndSolve; task.token = 777; }
    require(numeric_rejection(provider.execute(malformed)), "nonfinite last page was accepted");
    upload(systems.back()->matrix, systems.back()->a_device.get(), stream.value);
    for (auto& task : malformed) task.operation = Op::SolveWithFactors;
    no_work_rejection(malformed, "absent, stale, or relocated logical token");
    provider.execute(tasks).require_success();
    verify_all();

    const auto native = provider.statistics();
    const auto logical = provider.logical_statistics();
    require(native.estimated_peak_device_bytes > 0 && native.estimated_peak_device_bytes <= 256ull*1024*1024,
            "window changed or exceeded the native estimate budget");
    require(native.native_factor_systems == native.native_factor_calls*capacity
        && native.native_solve_systems == native.native_solve_calls*capacity,
        "window native statistics excluded padded/extra physical work");
    require(logical.invalidated_factor_restores > 0,
            "external invalidation restores were omitted or mislabeled as eviction");
    std::cout << "SPARSE_WINDOW_CONTRACT_PASS extent=" << N << " window=" << window << " capacity=" << capacity
        << " pages=" << logical.native_pages
        << " restored=" << logical.evicted_factor_restores+logical.invalidated_factor_restores
        << " evicted=" << logical.evicted_factor_restores << " invalidated=" << logical.invalidated_factor_restores
        << " factor_calls=" << native.native_factor_calls << " factor_systems=" << native.native_factor_systems
        << " solve_calls=" << native.native_solve_calls << " solve_systems=" << native.native_solve_systems
        << " estimated_peak_bytes=" << native.estimated_peak_device_bytes << '\n';
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4, "usage: test_sparse_window <151|201> <1..128> <1..32>");
        const int n = std::stoi(argv[1]), window = std::stoi(argv[2]), capacity = std::stoi(argv[3]);
        require(n == 151 || n == 201, "unsupported manufactured extent");
        require(window >= 1 && window <= 128 && capacity >= 1 && capacity <= 32 && capacity <= window,
                "unsupported window/cohort bounds");
        verify_negative_gate();
        for (auto dims : {std::pair{0, 1}, {129, 32}, {32, 0}, {64, 33}, {1, 2}})
            rejected([&] { CuDssSparseWindowSolver bad(dims.first, dims.second, 151, 451, nullptr, nullptr, nullptr); },
                     "invalid owner bounds accepted", "outside the bounded prototype");
        if (n == 151) run_window<151>(window, capacity); else run_window<201>(window, capacity);
    } catch (const std::exception& error) {
        std::cerr << "SPARSE_WINDOW_CONTRACT_FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
