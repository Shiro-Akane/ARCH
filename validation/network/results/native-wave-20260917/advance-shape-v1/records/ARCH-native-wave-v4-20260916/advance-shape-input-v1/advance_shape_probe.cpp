// Isolated diagnostic only: relaunch exactly the same compiled scalar kernel.
// Only six frozen IdealGas audit150/200 advance_ode entry points are eligible.
// No matrix, ODE, precision, library configuration, buffer, stream or fence changes.
// Both A/B sides must load this same probe; none of these are formal speed samples.
#include <cuda_runtime_api.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace {
[[noreturn]] void reject(const char* reason) {
    std::fprintf(stderr, "ADVANCE_SHAPE_PROBE_REJECTED %s\n", reason);
    std::fflush(stderr);
    // A generated kernel stub can discard cudaLaunchKernel's return value.
    // Returning a made-up CUDA error would not set the runtime last-error latch.
    // Reject the diagnostic explicitly, without generating a large core dump.
    std::_Exit(78);
}
constexpr const char* allowed[] = {
    "_ZN4arch4cuda18sparse_burn_detail11advance_odeI18NetCustom_audit15012Solver_BE_NR12IdealGasViewEEvNS0_18SparseOdeBatchViewIT_T0_EEiT1_14BurnConfigViewb",
    "_ZN4arch4cuda18sparse_burn_detail11advance_odeI18NetCustom_audit1509Solver_BD12IdealGasViewEEvNS0_18SparseOdeBatchViewIT_T0_EEiT1_14BurnConfigViewb",
    "_ZN4arch4cuda18sparse_burn_detail11advance_odeI18NetCustom_audit15011Solver_ROS412IdealGasViewEEvNS0_18SparseOdeBatchViewIT_T0_EEiT1_14BurnConfigViewb",
    "_ZN4arch4cuda18sparse_burn_detail11advance_odeI18NetCustom_audit20012Solver_BE_NR12IdealGasViewEEvNS0_18SparseOdeBatchViewIT_T0_EEiT1_14BurnConfigViewb",
    "_ZN4arch4cuda18sparse_burn_detail11advance_odeI18NetCustom_audit2009Solver_BD12IdealGasViewEEvNS0_18SparseOdeBatchViewIT_T0_EEiT1_14BurnConfigViewb",
    "_ZN4arch4cuda18sparse_burn_detail11advance_odeI18NetCustom_audit20011Solver_ROS412IdealGasViewEEvNS0_18SparseOdeBatchViewIT_T0_EEiT1_14BurnConfigViewb"
};
struct Counts {
    unsigned long long calls = 0, errors = 0;
    unsigned max_cells = 0, max_blocks = 0;
};
struct Registry {
    std::mutex mutex;
    std::map<const void*, int> functions;
    std::array<Counts, 6> counts{};
};
// The destructor reports only Host counters; no CUDA work at process shutdown.
Registry& registry() { static auto* value = new Registry; return *value; }
thread_local bool inside = false;
template<class F> F resolve(const char* symbol) {
    auto function = reinterpret_cast<F>(dlsym(RTLD_NEXT, symbol));
    if (!function) {
        std::fprintf(stderr, "ADVANCE_SHAPE_PROBE_UNAVAILABLE symbol=%s\n", symbol);
        reject("required runtime symbol unavailable");
    }
    return function;
}
unsigned threads() {
    static const unsigned value = [] {
        const char* setting = std::getenv("ARCH_SPARSE_ADVANCE_THREADS");
        if (setting) for (unsigned candidate : {1u, 2u, 4u, 8u, 16u, 32u})
            if (std::to_string(candidate) == setting) return candidate;
        reject("explicit threads in 1/2/4/8/16/32 required");
    }();
    return value;
}
int identify(const void* function) {
    auto& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    const auto found = r.functions.find(function);
    if (found != r.functions.end()) return found->second;
    using Query = cudaError_t (*)(const char**, const void*);
    static auto query = resolve<Query>("cudaFuncGetName");
    const char* name = nullptr;
    if (query(&name, function) != cudaSuccess || name == nullptr || r.functions.size() >= 512) {
        reject("kernel identity or bounded registry unavailable");
    }
    int index = -1;
    for (int i = 0; i < 6; ++i) if (std::strcmp(name, allowed[i]) == 0) index = i;
    r.functions.emplace(function, index);
    return index;
}
__attribute__((destructor)) void report() {
    auto& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    unsigned long long total = 0;
    for (std::size_t i = 0; i < r.counts.size(); ++i) {
        const auto& c = r.counts[i];
        total += c.calls;
        if (c.calls) std::fprintf(stderr,
            "ADVANCE_SHAPE_DIAGNOSTIC index=%zu calls=%llu errors=%llu max_cells=%u max_blocks=%u threads=%u kernel=%s\n",
            i, c.calls, c.errors, c.max_cells, c.max_blocks, threads(), allowed[i]);
    }
    std::fprintf(stderr, "ADVANCE_SHAPE_DIAGNOSTIC_END matched_launches=%llu scope=not_formal_performance\n", total);
}
} // namespace

extern "C" cudaError_t cudaLaunchKernel(const void* function, dim3 grid, dim3 block,
                                        void** arguments, size_t shared, cudaStream_t stream) {
    static auto original = resolve<decltype(&cudaLaunchKernel)>("cudaLaunchKernel");
    if (inside) return original(function, grid, block, arguments, shared, stream);
    inside = true;
    const int index = identify(function);
    inside = false;
    if (index < 0) return original(function, grid, block, arguments, shared, stream);
    int count = 0;
    if (arguments != nullptr && arguments[1] != nullptr)
        std::memcpy(&count, arguments[1], sizeof(count));
    if (count < 1 || count > 32 || grid.x != 1 || grid.y != 1 || grid.z != 1
            || block.x != 32 || block.y != 1 || block.z != 1 || shared != 0) {
        reject("unexpected frozen launch/count ABI");
    }
    const unsigned selected = threads();
    grid.x = (static_cast<unsigned>(count) + selected - 1) / selected;
    block.x = selected;
    const auto result = original(function, grid, block, arguments, shared, stream);
    auto& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    auto& c = r.counts[index];
    ++c.calls;
    c.errors += result != cudaSuccess;
    c.max_cells = std::max(c.max_cells, static_cast<unsigned>(count));
    c.max_blocks = std::max(c.max_blocks, grid.x);
    return result;
}
