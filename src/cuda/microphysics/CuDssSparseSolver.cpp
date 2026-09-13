/**
 * @file CuDssSparseSolver.cpp
 * @brief Own cuDSS descriptors, factors and private equilibration buffers.
 *
 * The provider borrows the caller's device CSR data and stream. It validates
 * symbolic metadata, scales through shared LinearEquilibration helpers and
 * checks completed library/device status before returning a linear response.
 * It never advances an ODE or downloads numerical cell states for a CPU solve.
 */

#include "CuDssSparseSolver.h"
#include "SparseEquilibration.h"
#include "cuda/common/DeviceAllocation.h"

#include <cudss.h>
#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// The 0.8 API introduced explicit offset types in CSR descriptors. Do not build
// silently against an unreviewed ABI. NVIDIA's wheel patch is not this API number.
#if CUDSS_VERSION_MAJOR != 0 || CUDSS_VERSION_MINOR != 8
#error "ARCH cuDSS provider requires the reviewed cuDSS 0.8 API"
#endif

namespace arch::cuda {
namespace {
struct CacheBudgetExceeded : std::runtime_error {
    CacheBudgetExceeded() : std::runtime_error("optional cuDSS factor cache budget exceeded") {}
};
void checked(cudssStatus_t status, const char* operation)
{
    if (status != CUDSS_STATUS_SUCCESS)
        throw std::runtime_error(std::string("cuDSS ") + operation
                                 + " failed (status " + std::to_string(status) + ")");
}
void checked_cuda(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess)
        throw std::runtime_error(std::string("cuDSS ") + operation + ": "
                                 + cudaGetErrorString(status));
}
void quiesce_or_terminate(int device, cudaStream_t stream) noexcept
{
    // Releasing caller-visible storage while its stream might still consume it
    // is never a recoverable destructor fallback. Match the backend owner's
    // lifetime rule, including when this standalone provider outlives a device
    // selection change in its caller.
    if (cudaSetDevice(device) != cudaSuccess
        || cudaStreamSynchronize(stream) != cudaSuccess)
        std::terminate();
}
void device_buffer(const void* pointer, int device)
{
    if (pointer == nullptr) throw std::invalid_argument("cuDSS requires non-null device buffers");
    cudaPointerAttributes attributes{};
    checked_cuda(cudaPointerGetAttributes(&attributes, pointer), "buffer attributes");
    if (attributes.type != cudaMemoryTypeDevice || attributes.device != device)
        throw std::invalid_argument("cuDSS buffers must reside on the selected CUDA device");
}
} // namespace

void CuDssResult::require_success() const
{
    if (!success())
        throw std::runtime_error("cuDSS execution failed (library="
            + std::to_string(library_status) + ", device_info=" + std::to_string(device_info)
            + ", cuda=" + std::to_string(cuda_status) + ")");
}

struct CuDssSparseSolver::Impl
{
    // One factor is guaranteed; additional owners are bounded by both count
    // and their conservative native peak estimate. Children cannot recurse.
    static constexpr std::size_t max_cached_lanes = 31;
    // The measured 150-isotope BTF factor peak is ~4.33 MB per lane; a
    // 64 MiB cache thrashes the 32-lane pool. Keep an explicit bounded cap
    // that accommodates that pool, still subject to available-device checks.
    static constexpr std::uint64_t extra_cache_budget = 256ull * 1024 * 1024;
    struct CachedLane {
        const double* values = nullptr;
        std::uint64_t last_use = 0;
        std::unique_ptr<CuDssSparseSolver> solver;
    };
    bool cache_enabled = true;
    std::uint64_t native_budget = std::numeric_limits<std::uint64_t>::max();
    const double* primary_values = nullptr;
    CuDssSparseSolver* active_lane = nullptr;
    std::vector<CachedLane> cached_lanes;
    std::uint64_t cache_clock = 0, peak_total_bytes = 0;
    std::array<std::uint64_t,5> retired_counts{};
    cudssHandle_t handle = nullptr;
    cudssConfig_t config = nullptr;
    cudssData_t data = nullptr;
    cudssMatrix_t matrix = nullptr, rhs = nullptr, solution = nullptr;
    cudaStream_t stream = nullptr;
    int device = 0;
    bool analyzed = false, factored = false, reset_required = false;
    std::uint64_t matrix_token = 0, analyses = 0;
    std::uint64_t synchronizations = 0;
    std::uint64_t kernels = 0, downloaded_bytes = 0, uploaded_bytes = 0;
    std::uint64_t peak_device_bytes = 0;
    int extent = 0, nonzeros = 0;
    const int* row_offsets = nullptr;
    const int* column_indices = nullptr;
    const double* original_values = nullptr;
    const double* original_rhs = nullptr;
    DeviceAllocation<double> scaled_values, row_divisors, column_divisors,
        scaled_rhs, scaled_solution, original_residual, correction;
    DeviceAllocation<int> column_offsets, column_slots;
    DeviceAllocation<int> invalid_equilibration;
    double* caller_solution = nullptr;
    std::array<int, 2> completion{}; // Invalid arithmetic; original residual state.

    std::uint64_t equilibration_bytes() const
    {
        return (scaled_values.size() + row_divisors.size() + column_divisors.size()
            + scaled_rhs.size() + scaled_solution.size() + original_residual.size()
            + correction.size()) * sizeof(double)
            + (invalid_equilibration.size() + column_offsets.size() + column_slots.size())
                * sizeof(int);
    }

    std::uint64_t cached_bytes() const
    {
        std::uint64_t total = 0;
        for (const auto& lane : cached_lanes) total += lane.solver->peak_device_bytes();
        return total;
    }

    void retire_lane(std::size_t index)
    {
        auto& solver = cached_lanes[index].solver;
        if (active_lane == solver.get()) active_lane = nullptr;
        retired_counts[0] += solver->analysis_count();
        retired_counts[1] += solver->synchronization_count();
        retired_counts[2] += solver->kernel_count();
        retired_counts[3] += solver->bytes_d2h();
        retired_counts[4] += solver->bytes_h2d();
        cached_lanes.erase(cached_lanes.begin() + index);
    }

    ~Impl()
    {
        // Exceptional exits may still have queued work. Destructors cannot
        // report errors, but must not release descriptors before that work ends.
        if (handle != nullptr) quiesce_or_terminate(device, stream);
        cached_lanes.clear();
        if (data != nullptr) cudssDataDestroy(handle, data);
        if (matrix != nullptr) cudssMatrixDestroy(matrix);
        if (rhs != nullptr) cudssMatrixDestroy(rhs);
        if (solution != nullptr) cudssMatrixDestroy(solution);
        if (config != nullptr) cudssConfigDestroy(config);
        if (handle != nullptr) cudssDestroy(handle);
    }

    CuDssResult execute(int phase, bool correction_solve = false)
    {
        CuDssResult result;
        result.library_status = cudssExecute(handle, phase, config, data, matrix, solution, rhs);
        if (result.library_status != CUDSS_STATUS_SUCCESS) return result;
        if (phase == CUDSS_PHASE_SOLVE) {
            result.cuda_status = equilibrate_sparse_vector(extent, scaled_solution.get(),
                column_divisors.get(), correction_solve ? correction.get() : caller_solution,
                invalid_equilibration.get(), false, stream);
            if (result.cuda_status != cudaSuccess) return result;
            ++kernels;
            if (correction_solve) {
                result.cuda_status = accumulate_sparse_correction(extent, correction.get(),
                    caller_solution, invalid_equilibration.get(), stream);
                if (result.cuda_status != cudaSuccess) return result;
                ++kernels;
            }
            result.cuda_status = original_sparse_residual(extent, row_offsets, column_indices,
                original_values, original_rhs, caller_solution, original_residual.get(),
                invalid_equilibration.get() + 1, stream);
            if (result.cuda_status != cudaSuccess) return result;
            ++kernels;
        }
        // Only a scalar failure latch returns to Host, using the completion
        // fence already required by cuDSS. Matrix/RHS preparation stays on GPU.
        const auto status_bytes = (phase == CUDSS_PHASE_SOLVE ? 2 : 1) * sizeof(int);
        result.cuda_status = cudaMemcpyAsync(completion.data(),
            invalid_equilibration.get(), status_bytes, cudaMemcpyDeviceToHost, stream);
        if (result.cuda_status != cudaSuccess) return result;
        downloaded_bytes += status_bytes;
        result.cuda_status = cudaStreamSynchronize(stream);
        ++synchronizations;
        if (result.cuda_status != cudaSuccess) return result;
        if (completion[0] != 0) {
            result.cuda_status = cudaErrorInvalidValue;
            return result;
        }
        std::size_t bytes = 0;
        result.library_status = cudssDataGet(handle, data, CUDSS_DATA_INFO,
            &result.device_info, sizeof(result.device_info), &bytes);
        if (result.library_status == CUDSS_STATUS_SUCCESS && bytes != sizeof(int))
            result.library_status = CUDSS_STATUS_INTERNAL_ERROR;
        return result;
    }
};

CuDssSparseSolver::CuDssSparseSolver(
    int extent, int nonzeros, const int* row_offsets, const int* column_indices,
    const double* values, const double* rhs, double* solution, cudaStream_t stream)
    : impl_(std::make_unique<Impl>())
{
    if (extent <= 0 || nonzeros < extent)
        throw std::invalid_argument("cuDSS requires a nonempty square CSR pattern with diagonals");
    auto& p = *impl_;
    p.stream = stream;
    p.extent = extent;
    p.nonzeros = nonzeros;
    p.primary_values = values;
    p.row_offsets = row_offsets;
    p.column_indices = column_indices;
    checked_cuda(cudaGetDevice(&p.device), "current device");
    for (const void* pointer : {static_cast<const void*>(row_offsets),
            static_cast<const void*>(column_indices), static_cast<const void*>(values),
            static_cast<const void*>(rhs), static_cast<const void*>(solution)})
        device_buffer(pointer, p.device);
    if (rhs == solution)
        throw std::invalid_argument("cuDSS RHS and solution must have distinct storage");
    std::vector<int> offsets(static_cast<std::size_t>(extent) + 1), columns(nonzeros);
    std::vector<int> column_offsets(static_cast<std::size_t>(extent) + 1, 0),
        column_slots(nonzeros);
    // Declared AFTER staging vectors, so exceptional unwinding fences their
    // asynchronous copies BEFORE destroying the destination buffers. Impl has
    // no cuDSS handle yet and cannot protect these constructor-local vectors.
    struct MetadataCopyGuard {
        int device;
        cudaStream_t stream;
        bool complete = false;
        ~MetadataCopyGuard() {
            if (!complete) quiesce_or_terminate(device, stream);
        }
    } metadata_copy{p.device, stream};
    checked_cuda(cudaMemcpyAsync(offsets.data(), row_offsets, offsets.size() * sizeof(int),
                                  cudaMemcpyDeviceToHost, stream), "CSR row metadata");
    p.downloaded_bytes += offsets.size() * sizeof(int);
    checked_cuda(cudaMemcpyAsync(columns.data(), column_indices, columns.size() * sizeof(int),
                                  cudaMemcpyDeviceToHost, stream), "CSR column metadata");
    p.downloaded_bytes += columns.size() * sizeof(int);
    checked_cuda(cudaStreamSynchronize(stream), "CSR metadata completion");
    metadata_copy.complete = true;
    ++p.synchronizations;
    if (offsets.front() != 0 || offsets.back() != nonzeros)
        throw std::invalid_argument("cuDSS CSR offsets disagree with its nonzero count");
    for (int row = 0; row < extent; ++row) {
        if (offsets[row] < 0 || offsets[row + 1] < offsets[row] || offsets[row + 1] > nonzeros)
            throw std::invalid_argument("cuDSS CSR row offsets are invalid");
        int previous = -1;
        bool diagonal = false;
        for (int slot = offsets[row]; slot < offsets[row + 1]; ++slot) {
            const int column = columns[slot];
            if (column <= previous || column >= extent)
                throw std::invalid_argument("cuDSS CSR columns must be sorted, unique, and in range");
            diagonal = diagonal || column == row;
            previous = column;
        }
        if (!diagonal) throw std::invalid_argument("cuDSS ODE pattern is missing a diagonal");
    }
    int major = -1, minor = -1;
    checked(cudssGetProperty(MAJOR_VERSION, &major), "runtime major version");
    checked(cudssGetProperty(MINOR_VERSION, &minor), "runtime minor version");
    if (major != CUDSS_VERSION_MAJOR || minor != CUDSS_VERSION_MINOR)
        throw std::runtime_error("cuDSS runtime and compiled header versions disagree");
    checked(cudssCreate(&p.handle), "create");
    checked(cudssSetStream(p.handle, stream), "set stream");
    p.scaled_values.allocate(nonzeros);
    p.row_divisors.allocate(extent);
    p.column_divisors.allocate(extent);
    p.scaled_rhs.allocate(extent);
    p.scaled_solution.allocate(extent);
    p.original_residual.allocate(extent);
    p.correction.allocate(extent);
    p.column_offsets.allocate(column_offsets.size());
    p.column_slots.allocate(column_slots.size());
    p.invalid_equilibration.allocate(2);
    p.peak_device_bytes = p.equilibration_bytes();
    // Immutable index permutation, not numeric state: gather each CSR column
    // in linear work without an atomic floating reduction or a dense N*N scan.
    for (const int column : columns) ++column_offsets[column + 1];
    for (int column = 0; column < extent; ++column)
        column_offsets[column + 1] += column_offsets[column];
    auto next_slot = column_offsets;
    for (int slot = 0; slot < nonzeros; ++slot)
        column_slots[next_slot[columns[slot]]++] = slot;
    metadata_copy.complete = false;
    checked_cuda(cudaMemcpyAsync(p.column_offsets.get(), column_offsets.data(),
        column_offsets.size() * sizeof(int), cudaMemcpyHostToDevice, stream), "column offsets upload");
    p.uploaded_bytes += column_offsets.size() * sizeof(int);
    checked_cuda(cudaMemcpyAsync(p.column_slots.get(), column_slots.data(),
        column_slots.size() * sizeof(int), cudaMemcpyHostToDevice, stream), "column slots upload");
    p.uploaded_bytes += column_slots.size() * sizeof(int);
    checked_cuda(cudaStreamSynchronize(stream), "column metadata completion");
    metadata_copy.complete = true;
    ++p.synchronizations;
    checked(cudssConfigCreate(&p.config), "create config");
    // General burn Jacobians can couple temperature to extremely small trace
    // abundances. BTF/COLAMD preserves nonsymmetric sparse structure and uses
    // the library's global-pivot factorization. Nested-dissection/local-block
    // factors can lose componentwise trace accuracy despite successful status
    // and refinement. Matching is NOT supported with BTF; leave it disabled.
    // Every solve must satisfy ARCH's original-matrix residual gate.
    const cudssReorderingAlg_t ordering = CUDSS_REORDERING_ALG_BTF_COLAMD;
    checked(cudssConfigSet(p.config, CUDSS_CONFIG_REORDERING_ALG,
                           &ordering, sizeof(ordering)), "select nonsymmetric sparse ordering");
    const int disabled = 0;
    checked(cudssConfigSet(p.config, CUDSS_CONFIG_HYBRID_EXECUTE_MODE,
                           &disabled, sizeof(disabled)), "disable CPU numeric execution");
    checked(cudssConfigSet(p.config, CUDSS_CONFIG_HYBRID_MEMORY_MODE,
                           &disabled, sizeof(disabled)), "disable host factor spill");
    // Direct LU can return success yet miss componentwise accuracy on mixed
    // abundance/temperature scales (even the CPU dense oracle can do so).
    // Refine the equilibrated device equations before handing the solution
    // to ARCH's original-matrix residual gate. Zero IR_TOL avoids
    // replacing that gate by cuDSS's different global two-norm criterion.
    const int refinement_steps = ARCH_CUDSS_IR_STEPS;
    const double no_library_tolerance = 0.0;
    checked(cudssConfigSet(p.config, CUDSS_CONFIG_IR_N_STEPS,
                           &refinement_steps, sizeof(refinement_steps)),
            "set iterative refinement passes");
    checked(cudssConfigSet(p.config, CUDSS_CONFIG_IR_TOL,
                           &no_library_tolerance, sizeof(no_library_tolerance)),
            "retain ARCH residual acceptance criterion");
    checked(cudssDataCreate(p.handle, &p.data), "create solver data");
    checked(cudssMatrixCreateCsr(&p.matrix, extent, extent, nonzeros,
        // cuDSS currently accepts only three-array CSR: an N+1 offset array
        // and null rowEnd. A separate rowEnd requests unsupported four-array CSR.
        row_offsets, nullptr, column_indices, p.scaled_values.get(),
        CUDSS_R_32I, CUDSS_R_32I, CUDSS_R_64F, CUDSS_MTYPE_GENERAL,
        CUDSS_MVIEW_FULL, CUDSS_BASE_ZERO), "create CSR descriptor");
    checked(cudssMatrixCreateDn(&p.rhs, extent, 1, extent, p.scaled_rhs.get(),
                               CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR), "create RHS descriptor");
    checked(cudssMatrixCreateDn(&p.solution, extent, 1, extent, p.scaled_solution.get(),
                               CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR), "create solution descriptor");
}

CuDssSparseSolver::~CuDssSparseSolver() = default;

CuDssResult CuDssSparseSolver::factorize(const double* values, std::uint64_t token)
{
    auto& p = *impl_;
    struct FailureGuard {
        CuDssSparseSolver& owner;
        bool complete = false;
        ~FailureGuard() { if (!complete) owner.invalidate(); }
    } guard{*this};
    p.active_lane = nullptr;
    device_buffer(values, p.device);
    if (p.cache_enabled) {
        if (p.cache_clock == std::numeric_limits<std::uint64_t>::max()) {
            for (auto& lane : p.cached_lanes) lane.last_use = 0;
            p.cache_clock = 0;
        }
        ++p.cache_clock;
        if (p.primary_values == values && p.factored && !p.reset_required
            && p.matrix_token == token) {
            guard.complete = true;
            return {};
        }
        if (p.primary_values != values) {
            auto found = std::find_if(p.cached_lanes.begin(), p.cached_lanes.end(),
                [values](const auto& lane) { return lane.values == values; });
            if (found == p.cached_lanes.end()) {
                const auto estimated_lane = p.peak_device_bytes;
                std::size_t free_bytes = 0, total_bytes = 0;
                checked_cuda(cudaMemGetInfo(&free_bytes,&total_bytes), "optional factor cache memory budget");
                if (p.analyzed && estimated_lane <= Impl::extra_cache_budget && estimated_lane <= free_bytes) {
                    while (!p.cached_lanes.empty() && (p.cached_lanes.size() >= Impl::max_cached_lanes
                        || p.cached_bytes() + estimated_lane > Impl::extra_cache_budget)) {
                        const auto oldest = std::min_element(p.cached_lanes.begin(),p.cached_lanes.end(),
                            [](const auto& a,const auto& b) { return a.last_use < b.last_use; });
                        p.retire_lane(static_cast<std::size_t>(oldest-p.cached_lanes.begin()));
                    }
                    auto child = std::make_unique<CuDssSparseSolver>(p.extent,p.nonzeros,
                        p.row_offsets,p.column_indices,values,p.scaled_rhs.get(),p.scaled_solution.get(),p.stream);
                    child->impl_->cache_enabled = false;
                    p.cached_lanes.push_back({values,p.cache_clock,std::move(child)});
                    found = p.cached_lanes.end()-1;
                }
            }
            if (found != p.cached_lanes.end()) {
                const auto index = static_cast<std::size_t>(found-p.cached_lanes.begin());
                auto& lane = *found;
                lane.last_use = p.cache_clock;
                auto& child = *lane.solver->impl_;
                child.native_budget = Impl::extra_cache_budget - (p.cached_bytes()-lane.solver->peak_device_bytes());
                try {
                    CuDssResult result;
                    if (!child.factored || child.reset_required || child.matrix_token != token)
                        result = lane.solver->factorize(values,token);
                    p.peak_total_bytes = std::max(p.peak_total_bytes,p.peak_device_bytes+p.cached_bytes());
                    p.active_lane = lane.solver.get();
                    guard.complete = result.success();
                    return result;
                } catch (const CacheBudgetExceeded&) {
                    // A later native estimate can exceed its earlier prediction.
                    // Retire the optional owner BEFORE falling back to the one
                    // guaranteed factor. Never turn this into CPU execution.
                    p.retire_lane(index);
                }
            }
            p.primary_values = values;
        }
    }
    p.factored = false;
    p.original_values = values;
    checked_cuda(equilibrate_sparse_rows(p.extent, p.row_offsets, values,
        p.row_divisors.get(), p.scaled_values.get(), p.invalid_equilibration.get(), p.stream),
        "normalize matrix row units");
    ++p.kernels;
    checked_cuda(equilibrate_sparse_columns(p.extent, p.column_offsets.get(), p.column_slots.get(),
        p.scaled_values.get(), p.column_divisors.get(), p.invalid_equilibration.get(), p.stream),
        "normalize matrix column units");
    ++p.kernels;
    // DATA_INFO storage is not initialized before analysis, and should not be
    // treated as a generic writable status slot. On an actual numerical failure,
    // discard the failed factor state before the ODE retries a smaller step.
    if (p.reset_required) {
        checked(cudssDataDestroy(p.handle, p.data), "discard failed factor state");
        p.data = nullptr;
        checked(cudssDataCreate(p.handle, &p.data), "recreate failed factor state");
        p.analyzed = false;
        p.reset_required = false;
    }
    if (!p.analyzed) {
        const auto result = p.execute(CUDSS_PHASE_ANALYSIS);
        if (!result.success()) {
            p.reset_required = true;
            return result;
        }
        p.analyzed = true;
        ++p.analyses;
        // A caller may catch a resource/estimate exception and reuse this owner.
        // Do not let that retry bypass the budget guard merely because symbolic
        // analysis finished: rebuild and validate the data on its next attempt.
        p.reset_required = true;
        std::array<std::int64_t, 16> estimates{};
        std::size_t bytes = 0;
        checked(cudssDataGet(p.handle, p.data, CUDSS_DATA_MEMORY_ESTIMATES,
            estimates.data(), sizeof(estimates), &bytes), "analysis memory estimates");
        if (bytes != sizeof(estimates) || estimates[0] < 0 || estimates[1] < 0)
            throw std::runtime_error("cuDSS returned an invalid device memory estimate");
        const auto factor_peak = static_cast<std::uint64_t>(estimates[1]);
        p.peak_device_bytes = std::max(p.peak_device_bytes,
            factor_peak + p.equilibration_bytes());
        if (factor_peak + p.equilibration_bytes() > p.native_budget)
            throw CacheBudgetExceeded();
        std::size_t free_bytes = 0, total_bytes = 0;
        checked_cuda(cudaMemGetInfo(&free_bytes, &total_bytes), "numeric factor memory budget");
        // The row workspaces are already allocated and excluded from free_bytes.
        if (factor_peak > free_bytes)
            throw std::runtime_error("cuDSS analysis predicts insufficient GPU memory for one sparse factorization");
        p.reset_required = false;
    }
    const auto result = p.execute(CUDSS_PHASE_FACTORIZATION);
    p.reset_required = !result.success();
    if (result.success()) {
        p.factored = true;
        p.matrix_token = token;
    }
    p.peak_total_bytes = std::max(p.peak_total_bytes,p.peak_device_bytes+p.cached_bytes());
    guard.complete = result.success();
    return result;
}

CuDssResult CuDssSparseSolver::solve(
    const double* rhs, double* solution, std::uint64_t token)
{
    auto& p = *impl_;
    if (p.active_lane != nullptr) {
        try {
            const auto result = p.active_lane->solve(rhs,solution,token);
            if (!result.success()) invalidate();
            return result;
        } catch (...) {
            invalidate();
            throw;
        }
    }
    if (!p.factored || p.matrix_token != token)
        throw std::logic_error("cuDSS solve requested with absent or stale numerical factors");
    device_buffer(rhs, p.device);
    device_buffer(solution, p.device);
    if (rhs == solution)
        throw std::invalid_argument("cuDSS RHS and solution must have distinct storage");
    checked_cuda(equilibrate_sparse_vector(p.extent, rhs, p.row_divisors.get(),
        p.scaled_rhs.get(), p.invalid_equilibration.get(), true, p.stream), "normalize RHS row units");
    ++p.kernels;
    p.caller_solution = solution;
    p.original_rhs = rhs;
    auto result = p.execute(CUDSS_PHASE_SOLVE);
    // Native refinement operates on the equilibrated equations and can miss a
    // componentwise error in the original system. Reuse these same GPU factors
    // for at most two original-system corrections. No numeric data returns to
    // Host, no unbounded retry, and no change to the ODE's final residual gate.
    // State 2 is unrefinable: leave numerical rejection to the existing caller.
    for (int retry = 0; result.success() && p.completion[1] == 1 && retry < 2; ++retry) {
        checked_cuda(equilibrate_sparse_vector(p.extent, p.original_residual.get(),
            p.row_divisors.get(), p.scaled_rhs.get(), p.invalid_equilibration.get(),
            true, p.stream), "normalize original residual row units");
        ++p.kernels;
        result = p.execute(CUDSS_PHASE_SOLVE, true);
    }
    if (!result.success()) {
        invalidate();
    }
    return result;
}

CuDssResult CuDssSparseSolver::factorize_and_solve(
    const double* values, const double* rhs, double* solution, std::uint64_t token)
{
    const auto result = factorize(values, token);
    return result.success() ? solve(rhs, solution, token) : result;
}
void CuDssSparseSolver::invalidate()
{
    impl_->factored = false;
    impl_->reset_required = true;
    impl_->active_lane = nullptr;
    for (auto& lane : impl_->cached_lanes) lane.solver->invalidate();
}
std::uint64_t CuDssSparseSolver::analysis_count() const {
    auto total = impl_->analyses + impl_->retired_counts[0];
    for (const auto& lane : impl_->cached_lanes) total += lane.solver->analysis_count();
    return total;
}
std::uint64_t CuDssSparseSolver::synchronization_count() const {
    auto total = impl_->synchronizations + impl_->retired_counts[1];
    for (const auto& lane : impl_->cached_lanes) total += lane.solver->synchronization_count();
    return total;
}
std::uint64_t CuDssSparseSolver::kernel_count() const {
    auto total = impl_->kernels + impl_->retired_counts[2];
    for (const auto& lane : impl_->cached_lanes) total += lane.solver->kernel_count();
    return total;
}
std::uint64_t CuDssSparseSolver::bytes_d2h() const {
    auto total = impl_->downloaded_bytes + impl_->retired_counts[3];
    for (const auto& lane : impl_->cached_lanes) total += lane.solver->bytes_d2h();
    return total;
}
std::uint64_t CuDssSparseSolver::bytes_h2d() const {
    auto total = impl_->uploaded_bytes + impl_->retired_counts[4];
    for (const auto& lane : impl_->cached_lanes) total += lane.solver->bytes_h2d();
    return total;
}
std::uint64_t CuDssSparseSolver::peak_device_bytes() const {
    return std::max(impl_->peak_total_bytes,impl_->peak_device_bytes);
}
int CuDssSparseSolver::compiled_version() { return CUDSS_VERSION; }
} // namespace arch::cuda
