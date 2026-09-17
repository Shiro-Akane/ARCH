#pragma once
// Real CUDA/cuDSS contract test, NOT a replacement for nuclear/ODE validation.
#include "CuDssSparseWaveSolver.h"
#include "cuda/common/DeviceAllocation.h"
#include "numerics/linalg/CsrMatrixView.h"
#include <cudss.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

using namespace arch::cuda;
using namespace arch::cuda::experimental;
using Op = SparseWaveOperation;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejected(F&& f, const char* message, const char* expected_reason) {
    bool failed = false;
    try { f(); }
    catch (const std::logic_error& error) {
        if (std::string(error.what()).find(expected_reason) == std::string::npos) throw;
        failed = true;
    }
    // Runtime/allocator/CUDA failures must escape and fail the whole test.
    require(failed, message);
}
bool numeric_rejection(const CuDssResult& result) {
    // Used only for the deliberately malformed matrices below. Opaque INFO
    // is not interpreted as a particular singularity or an ODE stiffness code.
    // The provider maps its shared invalid-arithmetic flag to InvalidValue.
    require(result.cuda_status == cudaSuccess || result.cuda_status == cudaErrorInvalidValue,
        "negative contract encountered an unexpected CUDA execution/resource failure");
    require(result.library_status == CUDSS_STATUS_SUCCESS
        || result.library_status == CUDSS_STATUS_EXECUTION_FAILED
        || result.library_status == CUDSS_STATUS_IR_FAILED,
        "negative contract encountered an unexpected cuDSS API/resource failure");
    return !result.success();
}
void verify_negative_gate() {
    rejected([] { throw std::invalid_argument("expected input rejection"); },
        "negative gate missed its expected logic error", "expected input rejection");
    bool escaped = false;
    try {
        rejected([] { throw std::runtime_error("synthetic runtime failure"); },
            "runtime failure was accepted", "expected input rejection");
    } catch (const std::runtime_error& error) {
        escaped = std::string(error.what()) == "synthetic runtime failure";
    }
    require(escaped, "negative gate swallowed an infrastructure exception");
    escaped = false;
    try {
        rejected([] { throw std::bad_alloc(); },
            "allocation failure was accepted", "expected input rejection");
    } catch (const std::bad_alloc&) { escaped = true; }
    require(escaped, "negative gate swallowed Host allocation failure");
    escaped = false;
    try {
        rejected([] { throw std::invalid_argument("wrong input reason"); },
            "wrong rejection was accepted", "expected input rejection");
    } catch (const std::logic_error& error) {
        escaped = std::string(error.what()) == "wrong input reason";
    }
    require(escaped, "negative gate accepted the wrong rejection reason");
    for (auto status : {cudaErrorMemoryAllocation, cudaErrorLaunchOutOfResources,
                        cudaErrorLaunchFailure, cudaErrorIllegalAddress}) {
        CuDssResult result;
        result.cuda_status = status;
        escaped = false;
        try { (void)numeric_rejection(result); }
        catch (const std::runtime_error&) { escaped = true; }
        require(escaped, "negative gate credited a CUDA infrastructure failure");
    }
    for (auto status : {CUDSS_STATUS_NOT_INITIALIZED, CUDSS_STATUS_ALLOC_FAILED,
                        CUDSS_STATUS_INVALID_VALUE, CUDSS_STATUS_NOT_SUPPORTED,
                        CUDSS_STATUS_INTERNAL_ERROR}) {
        CuDssResult result;
        result.library_status = status;
        escaped = false;
        try { (void)numeric_rejection(result); }
        catch (const std::runtime_error&) { escaped = true; }
        require(escaped, "negative gate credited a cuDSS infrastructure failure");
    }
    require(!numeric_rejection({}), "negative gate rejected a successful result");
    CuDssResult invalid;
    invalid.cuda_status = cudaErrorInvalidValue;
    require(numeric_rejection(invalid), "negative gate missed shared invalid arithmetic");
    for (auto status : {CUDSS_STATUS_EXECUTION_FAILED, CUDSS_STATUS_IR_FAILED}) {
        CuDssResult result;
        result.library_status = status;
        require(numeric_rejection(result), "negative gate missed an allowed native failure result");
    }
    CuDssResult info;
    info.device_info = 1; // Opaque nonzero status, not a claimed singularity code.
    require(numeric_rejection(info), "negative gate missed nonzero native INFO");
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

} // namespace
