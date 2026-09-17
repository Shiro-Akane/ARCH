// Execution-only prototype. Does not modify any shared ODE/network/EOS body.
#include "CuDssSparseWindowSolver.h"
#include "cuda/common/DeviceAllocation.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace arch::cuda::experimental {
namespace {
bool active(const SparseWaveTask& task) { return task.operation != SparseWaveOperation::Idle; }
bool solves(const SparseWaveTask& task) {
    return task.operation == SparseWaveOperation::SolveWithFactors
        || task.operation == SparseWaveOperation::FactorizeAndSolve;
}
bool factors(const SparseWaveTask& task) {
    return task.operation == SparseWaveOperation::Factorize
        || task.operation == SparseWaveOperation::FactorizeAndSolve;
}
void device_buffer(const void* pointer, int device) {
    if (!pointer) throw std::invalid_argument("window requires nonnull device buffers");
    cudaPointerAttributes attributes{};
    check_cuda(cudaPointerGetAttributes(&attributes, pointer), "window buffer attributes");
    if (attributes.type != cudaMemoryTypeDevice || attributes.device != device)
        throw std::invalid_argument("window buffer belongs to a different memory space/device");
}
bool overlap(const void* a, std::size_t an, const void* b, std::size_t bn) {
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    if (an > std::numeric_limits<std::uintptr_t>::max()-x
        || bn > std::numeric_limits<std::uintptr_t>::max()-y)
        throw std::invalid_argument("window pointer extent overflow");
    return x < y+bn && y < x+an;
}
} // namespace

struct CuDssSparseWindowSolver::Impl {
    struct LogicalKey {
        const double* values = nullptr;
        std::uint64_t caller_token = 0, native_token = 0;
    };
    int window = 0, capacity = 0, extent = 0, nonzeros = 0, device = -1;
    const int* offsets = nullptr;
    const int* columns = nullptr;
    std::uint64_t next_token = 0;
    std::vector<LogicalKey> logical;
    std::vector<std::uint64_t> slots;
    std::vector<SparseWaveTask> page;
    SparseWindowStatistics stats;
    std::unique_ptr<CuDssSparseWaveSolver> native;

    std::size_t vector_bytes() const { return static_cast<std::size_t>(extent)*sizeof(double); }
    std::size_t matrix_bytes() const { return static_cast<std::size_t>(nonzeros)*sizeof(double); }
    void invalidate() noexcept {
        if (native) native->invalidate();
        std::fill(slots.begin(), slots.end(), 0);
        ++stats.invalidations;
        // Preserve established logical identity. Failed, newly proposed identity
        // is never committed by execute(), so it cannot justify later reuse.
    }
    std::vector<LogicalKey> preflight(std::span<const SparseWaveTask> tasks,
                                      std::uint64_t& proposed_next) const {
        if (tasks.size() != static_cast<std::size_t>(window))
            throw std::invalid_argument("window task vector has the wrong fixed capacity");
        int current = -1;
        check_cuda(cudaGetDevice(&current), "window selected device");
        if (current != device) throw std::invalid_argument("window device changed");
        auto proposed = logical;
        proposed_next = next_token;
        for (int i = 0; i < window; ++i) {
            const auto& task = tasks[i];
            if (active(task) && !solves(task) && !factors(task))
                throw std::invalid_argument("window unknown operation");
            if (!active(task)) continue;
            if (!task.token) throw std::invalid_argument("window token zero is reserved");
            device_buffer(task.values, device);
            const auto& old = logical[i];
            const bool same = old.caller_token == task.token && old.values == task.values && old.native_token != 0;
            if (task.operation == SparseWaveOperation::SolveWithFactors && !same)
                throw std::logic_error("window absent, stale, or relocated logical token");
            if (!same) {
                if (proposed_next == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("window native token space exhausted");
                proposed[i] = {task.values, task.token, ++proposed_next};
            }
            if (solves(task)) {
                device_buffer(task.rhs, device);
                device_buffer(task.solution, device);
            }
        }
        // Per-cohort validation alone cannot catch a write corrupting a later
        // cohort's input. Check all active borrowed inputs and outputs up front.
        for (int i = 0; i < window; ++i) {
            if (!solves(tasks[i])) continue;
            if (overlap(tasks[i].solution, vector_bytes(), offsets,
                        (static_cast<std::size_t>(extent)+1)*sizeof(int))
                || overlap(tasks[i].solution, vector_bytes(), columns,
                           static_cast<std::size_t>(nonzeros)*sizeof(int)))
                throw std::invalid_argument("window output overlaps immutable CSR metadata");
            for (int j = 0; j < window; ++j) {
                if (!active(tasks[j])) {
                    // Idle is not permission to corrupt a matrix whose logical
                    // identity is retained for a later reuse request.
                    if (logical[j].values && overlap(tasks[i].solution, vector_bytes(), logical[j].values, matrix_bytes()))
                        throw std::invalid_argument("window output overlaps inactive retained matrix");
                    continue;
                }
                if (overlap(tasks[i].solution, vector_bytes(), tasks[j].values, matrix_bytes())
                    || (solves(tasks[j]) && overlap(tasks[i].solution, vector_bytes(), tasks[j].rhs, vector_bytes()))
                    || (i != j && solves(tasks[j]) && overlap(tasks[i].solution, vector_bytes(), tasks[j].solution, vector_bytes())))
                    throw std::invalid_argument("window output overlaps borrowed input/another output");
            }
        }
        return proposed;
    }
};

CuDssSparseWindowSolver::CuDssSparseWindowSolver(int window, int capacity,
        int extent, int nonzeros, const int* offsets, const int* columns, cudaStream_t stream)
    : impl_(std::make_unique<Impl>()) {
    if (window < 1 || window > 128 || capacity < 1 || capacity > 32 || capacity > window
        || extent <= 0 || nonzeros < extent)
        throw std::invalid_argument("window/cohort extent is outside the bounded prototype");
    auto& p = *impl_;
    p.window = window; p.capacity = capacity; p.extent = extent; p.nonzeros = nonzeros;
    p.offsets = offsets; p.columns = columns;
    check_cuda(cudaGetDevice(&p.device), "window owner device");
    p.logical.resize(window);
    p.slots.resize(capacity);
    p.page.resize(capacity);
    p.native = std::make_unique<CuDssSparseWaveSolver>(capacity, extent, nonzeros, offsets, columns, stream);
}

CuDssSparseWindowSolver::~CuDssSparseWindowSolver() = default;
void CuDssSparseWindowSolver::invalidate() noexcept { impl_->invalidate(); }
SparseWaveStatistics CuDssSparseWindowSolver::statistics() const { return impl_->native->statistics(); }
SparseWindowStatistics CuDssSparseWindowSolver::logical_statistics() const { return impl_->stats; }

CuDssResult CuDssSparseWindowSolver::execute(std::span<const SparseWaveTask> tasks) {
    auto& p = *impl_;
    std::uint64_t proposed_next = 0;
    auto proposed = p.preflight(tasks, proposed_next); // No numeric work or key publication before this succeeds.
    if (std::none_of(tasks.begin(), tasks.end(), active)) return {};
    // Never recycle a token exposed to a partial native execution, even on failure.
    p.next_token = proposed_next;
    ++p.stats.executions;
    for (const auto& task : tasks) {
        p.stats.requested_factors += factors(task);
        p.stats.requested_solves += solves(task);
    }
    try {
        for (int first = 0; first < p.window; first += p.capacity) {
            std::fill(p.page.begin(), p.page.end(), SparseWaveTask{});
            bool any = false;
            const int count = std::min(p.capacity, p.window-first);
            for (int slot = 0; slot < count; ++slot) {
                const int lane = first+slot;
                if (!active(tasks[lane])) continue;
                any = true;
                p.page[slot] = tasks[lane];
                p.page[slot].token = proposed[lane].native_token;
                if (tasks[lane].operation == SparseWaveOperation::SolveWithFactors
                    && p.slots[slot] != proposed[lane].native_token) {
                    p.page[slot].operation = SparseWaveOperation::FactorizeAndSolve;
                    ++p.stats.evicted_factor_restores;
                }
            }
            if (!any) continue;
            ++p.stats.native_pages;
            const auto result = p.native->execute(p.page);
            if (!result.success()) { p.invalidate(); return result; }
            for (int slot = 0; slot < count; ++slot)
                if (active(p.page[slot])) p.slots[slot] = p.page[slot].token;
        }
        p.logical = std::move(proposed);
    } catch (...) {
        p.invalidate();
        throw;
    }
    return {};
}
} // namespace arch::cuda::experimental
