#include "CuDssSparseSolver.h"
#include "SparseEquilibration.h"
#include "cuda/common/DeviceAllocation.h"

#include <cudss.h>
#include <algorithm>
#include <array>
#include <exception>
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
    int extent = 0;
    const int* row_offsets = nullptr;
    DeviceAllocation<double> scaled_values, row_divisors, column_divisors,
        scaled_rhs, scaled_solution;
    DeviceAllocation<int> column_offsets, column_slots;
    DeviceAllocation<int> invalid_equilibration;
    double* caller_solution = nullptr;
    int equilibration_error = 0;

    std::uint64_t equilibration_bytes() const
    {
        return (scaled_values.size() + row_divisors.size() + column_divisors.size()
            + scaled_rhs.size() + scaled_solution.size()) * sizeof(double)
            + (invalid_equilibration.size() + column_offsets.size() + column_slots.size())
                * sizeof(int);
    }

    ~Impl()
    {
        // Exceptional exits may still have queued work. Destructors cannot
        // report errors, but must not release descriptors before that work ends.
        if (handle != nullptr) quiesce_or_terminate(device, stream);
        if (data != nullptr) cudssDataDestroy(handle, data);
        if (matrix != nullptr) cudssMatrixDestroy(matrix);
        if (rhs != nullptr) cudssMatrixDestroy(rhs);
        if (solution != nullptr) cudssMatrixDestroy(solution);
        if (config != nullptr) cudssConfigDestroy(config);
        if (handle != nullptr) cudssDestroy(handle);
    }

    CuDssResult execute(int phase)
    {
        CuDssResult result;
        result.library_status = cudssExecute(handle, phase, config, data, matrix, solution, rhs);
        if (result.library_status != CUDSS_STATUS_SUCCESS) return result;
        if (phase == CUDSS_PHASE_SOLVE) {
            result.cuda_status = equilibrate_sparse_vector(extent, scaled_solution.get(),
                column_divisors.get(), caller_solution, invalid_equilibration.get(), false, stream);
            if (result.cuda_status != cudaSuccess) return result;
            ++kernels;
        }
        // Only a scalar failure latch returns to Host, using the completion
        // fence already required by cuDSS. Matrix/RHS preparation stays on GPU.
        result.cuda_status = cudaMemcpyAsync(&equilibration_error,
            invalid_equilibration.get(), sizeof(int), cudaMemcpyDeviceToHost, stream);
        if (result.cuda_status != cudaSuccess) return result;
        downloaded_bytes += sizeof(int);
        result.cuda_status = cudaStreamSynchronize(stream);
        ++synchronizations;
        if (result.cuda_status != cudaSuccess) return result;
        if (equilibration_error != 0) {
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
    p.row_offsets = row_offsets;
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
    p.column_offsets.allocate(column_offsets.size());
    p.column_slots.allocate(column_slots.size());
    p.invalid_equilibration.allocate(1);
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
    // Every solve still faces ARCH's unchanged original-matrix residual gate.
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
    // to ARCH's unchanged ORIGINAL-matrix residual gate. Zero IR_TOL avoids
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
    p.factored = false;
    device_buffer(values, p.device);
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
    return result;
}

CuDssResult CuDssSparseSolver::solve(
    const double* rhs, double* solution, std::uint64_t token)
{
    auto& p = *impl_;
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
    const auto result = p.execute(CUDSS_PHASE_SOLVE);
    if (!result.success()) {
        p.factored = false;
        p.reset_required = true;
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
}
std::uint64_t CuDssSparseSolver::analysis_count() const { return impl_->analyses; }
std::uint64_t CuDssSparseSolver::synchronization_count() const { return impl_->synchronizations; }
std::uint64_t CuDssSparseSolver::kernel_count() const { return impl_->kernels; }
std::uint64_t CuDssSparseSolver::bytes_d2h() const { return impl_->downloaded_bytes; }
std::uint64_t CuDssSparseSolver::bytes_h2d() const { return impl_->uploaded_bytes; }
std::uint64_t CuDssSparseSolver::peak_device_bytes() const { return impl_->peak_device_bytes; }
int CuDssSparseSolver::compiled_version() { return CUDSS_VERSION; }
} // namespace arch::cuda
