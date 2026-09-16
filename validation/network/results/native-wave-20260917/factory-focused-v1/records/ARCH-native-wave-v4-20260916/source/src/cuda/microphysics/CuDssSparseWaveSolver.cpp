// Isolated native-batch candidate; no network/EOS/ODE formula or production registration.
#include "CuDssSparseWaveSolver.h"
#include "cuda/microphysics/SparseEquilibration.h"
#include "cuda/common/DeviceAllocation.h"
#include <cudss.h>
#include <algorithm>
#include <array>
#include <climits>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if CUDSS_VERSION_MAJOR != 0 || CUDSS_VERSION_MINOR != 8
#error "Experimental ARCH wave provider requires the reviewed cuDSS 0.8 API"
#endif
#ifndef ARCH_CUDSS_IR_STEPS
#error "Compile with the frozen production ARCH_CUDSS_IR_STEPS value"
#endif

namespace arch::cuda::experimental {
namespace {
void native(cudssStatus_t result, const char* what) {
    if (result != CUDSS_STATUS_SUCCESS)
        throw std::runtime_error(std::string("cuDSS wave ") + what + ": " + std::to_string(result));
}
bool active(const SparseWaveTask& t) { return t.operation != SparseWaveOperation::Idle; }
bool solves(const SparseWaveTask& t) {
    return t.operation == SparseWaveOperation::SolveWithFactors
        || t.operation == SparseWaveOperation::FactorizeAndSolve;
}
bool factors(const SparseWaveTask& t) {
    return t.operation == SparseWaveOperation::Factorize
        || t.operation == SparseWaveOperation::FactorizeAndSolve;
}
void device_buffer(const void* pointer, int device) {
    if (!pointer) throw std::invalid_argument("cuDSS wave needs nonnull device buffers");
    cudaPointerAttributes attributes{};
    check_cuda(cudaPointerGetAttributes(&attributes, pointer), "wave buffer attributes");
    if (attributes.type != cudaMemoryTypeDevice || attributes.device != device)
        throw std::invalid_argument("cuDSS wave buffer must belong to the selected device");
}
bool overlap(const void* a, std::size_t a_bytes, const void* b, std::size_t b_bytes) {
    const auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
    if (a_bytes > std::numeric_limits<std::uintptr_t>::max() - x
        || b_bytes > std::numeric_limits<std::uintptr_t>::max() - y)
        throw std::invalid_argument("cuDSS wave pointer extent overflow");
    return x < y + b_bytes && y < x + a_bytes;
}
} // namespace

struct CuDssSparseWaveSolver::Impl {
    static constexpr std::uint64_t estimated_budget = 256ull * 1024 * 1024;
    int capacity = 0, extent = 0, nonzeros = 0, device = 0;
    bool device_known = false, analyzed = false, factored = false, reset_required = false;
    const int* offsets = nullptr;
    const int* columns = nullptr;
    cudaStream_t stream = nullptr;
    cudssHandle_t handle = nullptr;
    cudssConfig_t config = nullptr;
    cudssData_t data = nullptr;
    cudssMatrix_t matrix = nullptr, rhs = nullptr, solution = nullptr;
    DeviceAllocation<double> scaled_values, row_divisors, column_divisors, scaled_rhs,
        scaled_solution, residual, correction, identity;
    DeviceAllocation<int> column_offsets, column_slots, statuses;
    DeviceAllocation<const void*> row_pointers, column_pointers, value_pointers,
        rhs_pointers, solution_pointers;
    std::vector<int> host_rows, host_columns, host_column_offsets, host_column_slots;
    std::vector<int> dimensions, nonzero_counts, ones;
    std::vector<double> host_identity;
    std::vector<const void*> pointer_staging;
    int* completed = nullptr; // One pinned status batch, not one D2H per lane.
    std::vector<const double*> known_values;
    std::vector<std::uint64_t> known_tokens;
    std::vector<bool> ready, selected, prepare, refine;
    SparseWaveStatistics stats;

    ~Impl() {
        // All constructor upload sources are members and remain alive until
        // this completion witness, including on an exceptional construction.
        if (device_known) set_device_and_quiesce_or_terminate(device, stream);
        if (data) cudssDataDestroy(handle, data);
        if (matrix) cudssMatrixDestroy(matrix);
        if (rhs) cudssMatrixDestroy(rhs);
        if (solution) cudssMatrixDestroy(solution);
        if (config) cudssConfigDestroy(config);
        if (handle) cudssDestroy(handle);
        if (completed) require_cuda_success_or_terminate(cudaFreeHost(completed));
    }
    std::size_t n(int lane) const { return static_cast<std::size_t>(lane) * extent; }
    std::size_t nz(int lane) const { return static_cast<std::size_t>(lane) * nonzeros; }
    int* invalid(int lane) const { return statuses.get() + 2 * lane; }
    std::size_t vector_bytes() const { return static_cast<std::size_t>(extent) * sizeof(double); }
    std::size_t matrix_bytes() const { return static_cast<std::size_t>(nonzeros) * sizeof(double); }
    std::uint64_t private_bytes() const {
        return (scaled_values.size() + row_divisors.size() + column_divisors.size()
            + scaled_rhs.size() + scaled_solution.size() + residual.size() + correction.size()
            + identity.size()) * sizeof(double)
            + (column_offsets.size() + column_slots.size() + statuses.size()) * sizeof(int)
            + (row_pointers.size() + column_pointers.size() + value_pointers.size()
                + rhs_pointers.size() + solution_pointers.size()) * sizeof(void*);
    }
    void upload(void* to, const void* from, std::size_t bytes) {
        check_cuda(cudaMemcpyAsync(to, from, bytes, cudaMemcpyHostToDevice, stream), "wave metadata upload");
        stats.bytes_h2d += bytes;
    }
    void fence() {
        check_cuda(cudaStreamSynchronize(stream), "wave completion");
        ++stats.synchronizations;
    }
    void initialize_private_matrices() {
        // No physical matrix is changed: only never-used/invalidated private
        // slots get an invertible placeholder. Active slots are overwritten
        // by shared equilibration from their original device matrices below.
        for (int lane = 0; lane < capacity; ++lane)
            check_cuda(cudaMemcpyAsync(scaled_values.get() + nz(lane), identity.get(),
                matrix_bytes(), cudaMemcpyDeviceToDevice, stream), "wave idle identity");
        check_cuda(cudaMemsetAsync(scaled_rhs.get(), 0, scaled_rhs.size() * sizeof(double), stream),
            "wave initial RHS");
        check_cuda(cudaMemsetAsync(statuses.get(), 0, statuses.size() * sizeof(int), stream),
            "wave initial statuses");
        std::fill(ready.begin(), ready.end(), false);
    }
    CuDssResult complete_native() {
        CuDssResult result;
        const auto bytes = statuses.size() * sizeof(int);
        result.cuda_status = cudaMemcpyAsync(completed, statuses.get(), bytes,
            cudaMemcpyDeviceToHost, stream);
        if (result.cuda_status != cudaSuccess) return result;
        stats.bytes_d2h += bytes;
        result.cuda_status = cudaStreamSynchronize(stream);
        ++stats.synchronizations;
        if (result.cuda_status != cudaSuccess) return result;
        std::size_t written = 0;
        result.library_status = cudssDataGet(handle, data, CUDSS_DATA_INFO,
            &result.device_info, sizeof(int), &written);
        if (result.library_status == CUDSS_STATUS_SUCCESS && written != sizeof(int))
            result.library_status = CUDSS_STATUS_INTERNAL_ERROR;
        for (int lane = 0; lane < capacity; ++lane)
            if (completed[2 * lane] != 0) result.cuda_status = cudaErrorInvalidValue;
        return result;
    }
    CuDssResult launch_native(int phase) {
        CuDssResult r;
        r.library_status = cudssExecute(handle, phase, config, data, matrix, solution, rhs);
        if (phase == CUDSS_PHASE_FACTORIZATION) {
            ++stats.native_factor_calls;
            stats.native_factor_systems += capacity;
        } else if (phase == CUDSS_PHASE_SOLVE) {
            ++stats.native_solve_calls;
            stats.native_solve_systems += capacity;
        }
        return r;
    }
    CuDssResult solve_wave(std::span<const SparseWaveTask> tasks,
                          const std::vector<bool>& selected, bool correction_solve) {
        for (int lane = 0; lane < capacity; ++lane) {
            if (!selected[lane]) {
                check_cuda(cudaMemsetAsync(scaled_rhs.get() + n(lane), 0, vector_bytes(), stream),
                    "wave inactive solve RHS");
                continue;
            }
            check_cuda(equilibrate_sparse_vector(extent,
                correction_solve ? residual.get() + n(lane) : tasks[lane].rhs,
                row_divisors.get() + n(lane), scaled_rhs.get() + n(lane),
                invalid(lane), true, stream), "wave normalize RHS");
            ++stats.kernels;
        }
        auto result = launch_native(CUDSS_PHASE_SOLVE);
        if (!result.success()) return result;
        for (int lane = 0; lane < capacity; ++lane) {
            if (!selected[lane]) continue;
            check_cuda(equilibrate_sparse_vector(extent, scaled_solution.get() + n(lane),
                column_divisors.get() + n(lane),
                correction_solve ? correction.get() + n(lane) : tasks[lane].solution,
                invalid(lane), false, stream), "wave denormalize solution");
            ++stats.kernels;
            if (correction_solve) {
                check_cuda(accumulate_sparse_correction(extent, correction.get() + n(lane),
                    tasks[lane].solution, invalid(lane), stream), "wave accumulate correction");
                ++stats.kernels;
            }
            check_cuda(original_sparse_residual(extent, offsets, columns, tasks[lane].values,
                tasks[lane].rhs, tasks[lane].solution, residual.get() + n(lane),
                invalid(lane) + 1, stream), "wave original residual");
            ++stats.kernels;
        }
        return complete_native();
    }
};

CuDssSparseWaveSolver::CuDssSparseWaveSolver(int capacity, int extent, int nonzeros,
    const int* offsets, const int* columns, cudaStream_t stream) : impl_(std::make_unique<Impl>()) {
    if (capacity < 1 || capacity > 32 || extent <= 0 || nonzeros < extent
        || extent > INT_MAX / capacity || nonzeros > INT_MAX / capacity)
        throw std::invalid_argument("cuDSS wave has invalid bounded batch dimensions");
    const auto c = static_cast<std::uint64_t>(capacity);
    const auto n = static_cast<std::uint64_t>(extent);
    const auto nz = static_cast<std::uint64_t>(nonzeros);
    const auto private_estimate = ((c + 1) * nz + 6 * c * n) * sizeof(double)
        + (n + 1 + nz + 2 * c) * sizeof(int) + 5 * c * sizeof(void*);
    if (private_estimate > Impl::estimated_budget)
        throw std::invalid_argument("cuDSS wave private dimensions exceed candidate budget");
    auto& p = *impl_;
    p.capacity = capacity; p.extent = extent; p.nonzeros = nonzeros;
    p.offsets = offsets; p.columns = columns; p.stream = stream;
    check_cuda(cudaGetDevice(&p.device), "wave current device");
    p.device_known = true;
    device_buffer(offsets, p.device); device_buffer(columns, p.device);
    p.host_rows.resize(static_cast<std::size_t>(extent) + 1);
    p.host_columns.resize(nonzeros);
    check_cuda(cudaMemcpyAsync(p.host_rows.data(), offsets, p.host_rows.size() * sizeof(int),
        cudaMemcpyDeviceToHost, stream), "wave row metadata");
    check_cuda(cudaMemcpyAsync(p.host_columns.data(), columns, p.host_columns.size() * sizeof(int),
        cudaMemcpyDeviceToHost, stream), "wave column metadata");
    p.stats.bytes_d2h += (p.host_rows.size() + p.host_columns.size()) * sizeof(int);
    p.fence();
    if (p.host_rows.front() != 0 || p.host_rows.back() != nonzeros)
        throw std::invalid_argument("cuDSS wave CSR offset endpoints disagree");
    p.host_identity.resize(nonzeros, 0.0);
    for (int row = 0; row < extent; ++row) {
        if (p.host_rows[row] < 0 || p.host_rows[row + 1] < p.host_rows[row]
            || p.host_rows[row + 1] > nonzeros)
            throw std::invalid_argument("cuDSS wave invalid row offsets");
        bool diagonal = false;
        int previous = -1;
        for (int k = p.host_rows[row]; k < p.host_rows[row + 1]; ++k) {
            const int column = p.host_columns[k];
            if (column <= previous || column >= extent)
                throw std::invalid_argument("cuDSS wave columns must be sorted, unique, in range");
            if (column == row) { diagonal = true; p.host_identity[k] = 1.0; }
            previous = column;
        }
        if (!diagonal) throw std::invalid_argument("cuDSS wave pattern needs every diagonal");
    }
    int major = -1, minor = -1;
    native(cudssGetProperty(MAJOR_VERSION, &major), "major version");
    native(cudssGetProperty(MINOR_VERSION, &minor), "minor version");
    if (major != CUDSS_VERSION_MAJOR || minor != CUDSS_VERSION_MINOR)
        throw std::runtime_error("cuDSS wave runtime/header version mismatch");
    p.host_column_offsets.resize(p.host_rows.size(), 0);
    p.host_column_slots.resize(nonzeros);
    for (int c : p.host_columns) ++p.host_column_offsets[c + 1];
    for (int c = 0; c < extent; ++c) p.host_column_offsets[c + 1] += p.host_column_offsets[c];
    auto next = p.host_column_offsets;
    for (int k = 0; k < nonzeros; ++k) p.host_column_slots[next[p.host_columns[k]]++] = k;
    const auto count = static_cast<std::size_t>(capacity);
    p.scaled_values.allocate(count * nonzeros);
    for (auto* buffer : {&p.row_divisors, &p.column_divisors, &p.scaled_rhs,
            &p.scaled_solution, &p.residual, &p.correction}) buffer->allocate(count * extent);
    p.identity.allocate(nonzeros);
    p.column_offsets.allocate(p.host_column_offsets.size());
    p.column_slots.allocate(nonzeros); p.statuses.allocate(2 * count);
    for (auto* buffer : {&p.row_pointers, &p.column_pointers, &p.value_pointers,
            &p.rhs_pointers, &p.solution_pointers}) buffer->allocate(count);
    if (p.private_bytes() > Impl::estimated_budget)
        throw std::runtime_error("cuDSS wave private storage exceeds candidate budget");
    check_cuda(cudaMallocHost(reinterpret_cast<void**>(&p.completed), 2 * count * sizeof(int)),
        "wave pinned completion batch");
    p.ready.resize(count, false); p.known_tokens.resize(count, 0); p.known_values.resize(count, nullptr);
    p.selected.resize(count); p.prepare.resize(count); p.refine.resize(count);
    p.upload(p.identity.get(), p.host_identity.data(), p.matrix_bytes());
    p.upload(p.column_offsets.get(), p.host_column_offsets.data(), p.host_column_offsets.size() * sizeof(int));
    p.upload(p.column_slots.get(), p.host_column_slots.data(), p.host_column_slots.size() * sizeof(int));
    // Keep all five host pointer arrays alive through the fence (do not reuse
    // one async upload source vector between copies).
    p.pointer_staging.resize(5 * count);
    for (int lane = 0; lane < capacity; ++lane) {
        p.pointer_staging[lane] = offsets;
        p.pointer_staging[count + lane] = columns;
        p.pointer_staging[2 * count + lane] = p.scaled_values.get() + p.nz(lane);
        p.pointer_staging[3 * count + lane] = p.scaled_rhs.get() + p.n(lane);
        p.pointer_staging[4 * count + lane] = p.scaled_solution.get() + p.n(lane);
    }
    int array = 0;
    for (auto* buffer : {&p.row_pointers, &p.column_pointers, &p.value_pointers,
            &p.rhs_pointers, &p.solution_pointers})
        p.upload(buffer->get(), p.pointer_staging.data() + count * array++, count * sizeof(void*));
    p.initialize_private_matrices();
    p.fence();
    native(cudssCreate(&p.handle), "handle");
    native(cudssSetStream(p.handle, stream), "stream");
    native(cudssConfigCreate(&p.config), "config");
    const cudssReorderingAlg_t ordering = CUDSS_REORDERING_ALG_BTF_COLAMD;
    const int disabled = 0, ir = ARCH_CUDSS_IR_STEPS;
    const double tolerance = 0.0;
    native(cudssConfigSet(p.config, CUDSS_CONFIG_REORDERING_ALG, &ordering, sizeof(ordering)), "BTF COLAMD");
    native(cudssConfigSet(p.config, CUDSS_CONFIG_HYBRID_EXECUTE_MODE, &disabled, sizeof(disabled)), "GPU numeric only");
    native(cudssConfigSet(p.config, CUDSS_CONFIG_HYBRID_MEMORY_MODE, &disabled, sizeof(disabled)), "no host factor spill");
    native(cudssConfigSet(p.config, CUDSS_CONFIG_IR_N_STEPS, &ir, sizeof(ir)), "original IR count");
    native(cudssConfigSet(p.config, CUDSS_CONFIG_IR_TOL, &tolerance, sizeof(tolerance)), "original IR tolerance");
    native(cudssDataCreate(p.handle, &p.data), "data");
    p.dimensions.resize(count, extent); p.nonzero_counts.resize(count, nonzeros); p.ones.resize(count, 1);
    native(cudssMatrixCreateBatchCsr(&p.matrix, capacity, p.dimensions.data(), p.dimensions.data(),
        p.nonzero_counts.data(), p.row_pointers.get(), nullptr, p.column_pointers.get(), p.value_pointers.get(),
        CUDSS_R_32I, CUDSS_R_32I, CUDSS_R_64F, CUDSS_MTYPE_GENERAL, CUDSS_MVIEW_FULL, CUDSS_BASE_ZERO), "batch CSR");
    native(cudssMatrixCreateBatchDn(&p.rhs, capacity, p.dimensions.data(), p.ones.data(), p.dimensions.data(),
        p.rhs_pointers.get(), CUDSS_R_32I, CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR), "batch RHS");
    native(cudssMatrixCreateBatchDn(&p.solution, capacity, p.dimensions.data(), p.ones.data(), p.dimensions.data(),
        p.solution_pointers.get(), CUDSS_R_32I, CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR), "batch solution");
}

CuDssSparseWaveSolver::~CuDssSparseWaveSolver() = default;
void CuDssSparseWaveSolver::invalidate() noexcept {
    impl_->factored = false; impl_->reset_required = true;
    std::fill(impl_->ready.begin(), impl_->ready.end(), false);
}
SparseWaveStatistics CuDssSparseWaveSolver::statistics() const { return impl_->stats; }

CuDssResult CuDssSparseWaveSolver::execute(std::span<const SparseWaveTask> tasks) {
    auto& p = *impl_;
    if (tasks.size() != static_cast<std::size_t>(p.capacity))
        throw std::invalid_argument("cuDSS wave task vector must match fixed capacity");
    int selected_device = -1;
    check_cuda(cudaGetDevice(&selected_device), "wave selected device");
    if (selected_device != p.device) throw std::invalid_argument("cuDSS wave device changed");
    bool any = false, need_factor = !p.factored;
    auto& selected = p.selected;
    auto& prepare = p.prepare;
    std::fill(selected.begin(), selected.end(), false);
    std::fill(prepare.begin(), prepare.end(), false);
    // Reject the entire invalid request before issuing any new numeric work.
    for (int lane = 0; lane < p.capacity; ++lane) {
        const auto& t = tasks[lane];
        if (t.operation != SparseWaveOperation::Idle && !factors(t) && !solves(t))
            throw std::invalid_argument("cuDSS wave unknown operation");
        if (!active(t)) continue;
        any = true;
        if (t.token == 0) throw std::invalid_argument("cuDSS wave token zero is reserved");
        device_buffer(t.values, p.device);
        const bool same = p.known_tokens[lane] == t.token && p.known_values[lane] == t.values;
        if (t.operation == SparseWaveOperation::SolveWithFactors && !same)
            throw std::logic_error("cuDSS wave absent, stale, or relocated factor token");
        prepare[lane] = !same || !p.ready[lane];
        need_factor = need_factor || prepare[lane];
        selected[lane] = solves(t);
        if (solves(t)) { device_buffer(t.rhs, p.device); device_buffer(t.solution, p.device); }
    }
    if (!any) return {}; // Empty wave does not analyze/factor/solve dummy systems.
    for (int lane = 0; lane < p.capacity; ++lane) {
        if (!selected[lane]) continue;
        if (overlap(tasks[lane].solution, p.vector_bytes(), p.offsets,
                    (static_cast<std::size_t>(p.extent) + 1) * sizeof(int))
            || overlap(tasks[lane].solution, p.vector_bytes(), p.columns,
                       static_cast<std::size_t>(p.nonzeros) * sizeof(int)))
            throw std::invalid_argument("cuDSS wave output overlaps immutable CSR metadata");
        for (int other = 0; other < p.capacity; ++other) {
            if (!active(tasks[other])) continue;
            if (overlap(tasks[lane].solution, p.vector_bytes(), tasks[other].values, p.matrix_bytes())
                || (selected[other] && overlap(tasks[lane].solution, p.vector_bytes(), tasks[other].rhs, p.vector_bytes()))
                || (other != lane && selected[other] && overlap(tasks[lane].solution, p.vector_bytes(),
                    tasks[other].solution, p.vector_bytes())))
                throw std::invalid_argument("cuDSS wave outputs overlap borrowed inputs/other outputs");
        }
    }
    struct FailureGuard {
        CuDssSparseWaveSolver& owner;
        bool complete = false;
        ~FailureGuard() { if (!complete) owner.invalidate(); }
    } guard{*this};
    if (p.reset_required) {
        p.fence();
        native(cudssDataDestroy(p.handle, p.data), "discard invalid batch factors");
        p.data = nullptr;
        native(cudssDataCreate(p.handle, &p.data), "recreate batch factors");
        p.initialize_private_matrices();
        p.analyzed = false; p.factored = false; need_factor = true;
        p.reset_required = false;
    }
    for (int lane = 0; lane < p.capacity; ++lane) {
        const auto& t = tasks[lane];
        if (!active(t)) continue;
        p.stats.requested_factors += factors(t);
        p.stats.requested_solves += solves(t);
        if (!prepare[lane] && p.ready[lane]) continue;
        check_cuda(equilibrate_sparse_rows(p.extent, p.offsets, t.values,
            p.row_divisors.get() + p.n(lane), p.scaled_values.get() + p.nz(lane), p.invalid(lane), p.stream),
            "wave normalize matrix rows");
        check_cuda(equilibrate_sparse_columns(p.extent, p.column_offsets.get(), p.column_slots.get(),
            p.scaled_values.get() + p.nz(lane), p.column_divisors.get() + p.n(lane), p.invalid(lane), p.stream),
            "wave normalize matrix columns");
        p.stats.kernels += 2;
    }
    if (!p.analyzed) {
        auto result = p.launch_native(CUDSS_PHASE_ANALYSIS);
        if (!result.success()) return result;
        result = p.complete_native();
        if (!result.success()) return result;
        ++p.stats.analyses;
        std::array<std::int64_t, 16> estimates{};
        std::size_t written = 0;
        native(cudssDataGet(p.handle, p.data, CUDSS_DATA_MEMORY_ESTIMATES,
            estimates.data(), sizeof(estimates), &written), "batch memory estimate");
        if (written != sizeof(estimates) || estimates[0] < 0 || estimates[1] < estimates[0])
            throw std::runtime_error("cuDSS wave invalid memory estimate");
        const auto native_peak = static_cast<std::uint64_t>(estimates[1]);
        const auto owned = p.private_bytes();
        if (native_peak > Impl::estimated_budget || owned > Impl::estimated_budget - native_peak)
            throw std::runtime_error("cuDSS wave estimated storage exceeds bounded candidate budget");
        std::size_t free_bytes = 0, total_bytes = 0;
        check_cuda(cudaMemGetInfo(&free_bytes, &total_bytes), "wave free device memory");
        if (native_peak > free_bytes) throw std::runtime_error("cuDSS wave insufficient device memory");
        p.stats.estimated_peak_device_bytes = std::max(p.stats.estimated_peak_device_bytes, native_peak + owned);
        p.analyzed = true;
    }
    if (need_factor) {
        auto result = p.launch_native(CUDSS_PHASE_FACTORIZATION);
        if (!result.success()) return result;
        result = p.complete_native();
        if (!result.success()) return result;
        p.factored = true;
        for (int lane = 0; lane < p.capacity; ++lane) if (active(tasks[lane])) {
            p.known_values[lane] = tasks[lane].values;
            p.known_tokens[lane] = tasks[lane].token;
            p.ready[lane] = true;
        }
    }
    if (std::none_of(selected.begin(), selected.end(), [](bool b) { return b; })) {
        guard.complete = true;
        return {};
    }
    auto result = p.solve_wave(tasks, selected, false);
    for (int retry = 0; result.success() && retry < 2; ++retry) {
        auto& refine = p.refine;
        for (int lane = 0; lane < p.capacity; ++lane)
            refine[lane] = selected[lane] && p.completed[2 * lane + 1] == 1;
        if (std::none_of(refine.begin(), refine.end(), [](bool b) { return b; })) break;
        result = p.solve_wave(tasks, refine, true);
    }
    guard.complete = result.success();
    return result;
}
} // namespace arch::cuda::experimental
