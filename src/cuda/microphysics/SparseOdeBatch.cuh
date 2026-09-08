/**
 * @brief Bounded device-resident sparse ODE execution with a host-API sparse provider.
 *
 * This executor contains only launch/response/linear-provider scheduling. It
 * invokes the same selected shared ODE continuation as ordinary CPU execution; no RHS,
 * Jacobian, adaptive step, or energy-closure formula is duplicated here.
 */
#pragma once

#include "CuDssSparseSolver.h"
#include "cuda/common/DeviceEosStatus.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/linalg/CsrMatrixView.h"

#include <cuda_runtime.h>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace arch::cuda {
template <class Network, template <class, class, class> class Solver>
using SparseOdePolicy = Solver<Network, CsrMatrixView<Network::ODE_NEQ>, void>;

/** Non-owning slice of the runtime's bounded burn pool, not one dense matrix
 * allocation per grid cell. A single immutable pattern is shared by all lanes.
 * states: lane-major ODE_NEQ; coefficients: lane-major nnz. The pool owner must
 * outlive the executor, its cuDSS factors, and its stream.
 */
template <class Network, template <class, class, class> class Solver>
struct SparseOdeBatchView
{
    int capacity = 0;
    int nonzeros = 0;
    const int* row_offsets = nullptr;
    const int* column_indices = nullptr;
    typename SparseOdePolicy<Network, Solver>::Continuation* contexts = nullptr;
    double* coefficients = nullptr;
    double* states = nullptr;
    double* densities = nullptr;
    double* intervals = nullptr;
    double* solutions = nullptr;
    int* requests = nullptr;
    int* responses = nullptr;
    double* jacobians = nullptr; // Required only by methods with a reused Jacobian.
    // Optional for non-EOS manufactured policies; required for a table EOS with
    // an internal failure hook. The caller clears once before preparing a chunk,
    // never between continuation launches, and retains it through energy commit.
    int* eos_statuses = nullptr;
    [[no_unique_address]] Network network{}; // Explicit immutable backend view.
};

namespace sparse_burn_detail {
// One existing request integer also acknowledges a rejected previous response.
// Reserve -1 for a structural error; -(request + 2) carries rejection even when
// the continuation has terminated. No extra transfer/fence or numeric download.
inline constexpr int invalid_structure_request = -1;
ARCH_HOST_DEVICE constexpr bool rejected_previous_response(int message)
{
    return message < invalid_structure_request;
}
ARCH_HOST_DEVICE constexpr int encode_request(OdeLinearRequest request, bool rejected)
{
    const int value = static_cast<int>(request);
    return rejected ? -(value + 2) : value;
}
ARCH_HOST_DEVICE constexpr OdeLinearRequest decode_request(int message)
{
    return static_cast<OdeLinearRequest>(
        rejected_previous_response(message) ? -message - 2 : message);
}

inline void checked(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(error));
}

template <class Network, template <class, class, class> class Solver>
__global__ void validate_corrections(SparseOdeBatchView<Network, Solver> batch, int count)
{
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane >= count || batch.responses[lane] == 0) return;
    const auto request = decode_request(batch.requests[lane]);
    if (request == OdeLinearRequest::Complete || request == OdeLinearRequest::Factorize) return;
    double* rhs = batch.contexts[lane].b;
    const double* solution = batch.solutions + static_cast<std::size_t>(lane) * Network::ODE_NEQ;
    CsrMatrixView<Network::ODE_NEQ> matrix{
        batch.row_offsets, batch.column_indices,
        batch.coefficients + static_cast<std::size_t>(lane) * batch.nonzeros,
        batch.nonzeros, true};
    if (!matrix.solution_accurate(rhs, solution)) {
        batch.responses[lane] = 0;
        return;
    }
    for (int i = 0; i < Network::ODE_NEQ; ++i) rhs[i] = solution[i];
}

template <class Network, template <class, class, class> class Solver, class Eos>
__global__ void advance_ode(
    SparseOdeBatchView<Network, Solver> batch, int count, Eos eos,
    BurnConfigView config, bool initialize)
{
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    if (lane >= count) return;
    using Ode = SparseOdePolicy<Network, Solver>;
    auto& context = batch.contexts[lane];
    double* state = batch.states + static_cast<std::size_t>(lane) * Network::ODE_NEQ;
    const bool had_request = !initialize
        && decode_request(batch.requests[lane]) != OdeLinearRequest::Complete;
    const bool previous_failure = had_request && batch.responses[lane] == 0;
    if (initialize) {
        Ode::begin(context, state, batch.densities[lane], batch.intervals[lane],
                   config, batch.intervals[lane], batch.network);
    }
    int* const eos_status = batch.eos_statuses == nullptr ? nullptr : batch.eos_statuses + lane;
    if (eos_status != nullptr && *eos_status != 0) {
        context.report.status = BurnOdeStatus::EosFailure;
        context.phase = Ode::Phase::Complete;
        batch.requests[lane] = encode_request(OdeLinearRequest::Complete, previous_failure);
        return; // A failed preparation/earlier query must never be resumed.
    }
    const auto checked_eos = bind_device_eos_status(eos, eos_status);
    if (had_request) {
        Ode::complete_linear_solve(context, batch.responses[lane] != 0);
    }
    CsrMatrixView<Network::ODE_NEQ> matrix{
        batch.row_offsets, batch.column_indices,
        batch.coefficients + static_cast<std::size_t>(lane) * batch.nonzeros,
        batch.nonzeros, true};
    CsrMatrixView<Network::ODE_NEQ> jacobian{
        batch.row_offsets, batch.column_indices,
        batch.jacobians == nullptr ? nullptr
            : batch.jacobians + static_cast<std::size_t>(lane) * batch.nonzeros,
        batch.nonzeros, true};
    const auto request = Ode::advance(context, jacobian, matrix, state, checked_eos, config);
    if (eos_status != nullptr && *eos_status != 0) {
        context.report.status = BurnOdeStatus::EosFailure;
        context.phase = Ode::Phase::Complete;
        batch.requests[lane] = encode_request(OdeLinearRequest::Complete, previous_failure);
        return; // Preserve the failure for commit; it is not an ODE retry.
    }
    batch.requests[lane] = !matrix.valid()
            || (Ode::USES_JACOBIAN_WORKSPACE && !jacobian.valid())
        ? invalid_structure_request : encode_request(request, previous_failure);
}
} // namespace sparse_burn_detail

/** A conservative synchronous host scheduler over device-resident contexts.
 * Only request/response integers cross the PCIe boundary; numerical cell states
 * and matrices never return to the CPU. Parallel cuDSS batches can optimize the
 * scheduling later without changing this public pool or shared ODE contract.
 */
template <class Network, template <class, class, class> class Solver>
class SparseOdeBatchExecutor
{
public:
    explicit SparseOdeBatchExecutor(SparseOdeBatchView<Network, Solver> batch, cudaStream_t stream)
        : batch_(batch), stream_(stream)
    {
        if (batch.capacity <= 0 || batch.nonzeros < Network::ODE_NEQ
            || batch.row_offsets == nullptr || batch.column_indices == nullptr
            || batch.contexts == nullptr || batch.coefficients == nullptr
            || batch.states == nullptr || batch.densities == nullptr
            || batch.intervals == nullptr || batch.solutions == nullptr
            || batch.requests == nullptr || batch.responses == nullptr)
            throw std::invalid_argument("Sparse ODE batch pool is incomplete");
        if constexpr (SparseOdePolicy<Network, Solver>::USES_JACOBIAN_WORKSPACE) {
            if (batch.jacobians == nullptr)
                throw std::invalid_argument("Sparse ODE method needs a Jacobian pool");
        }
        requests_.resize(batch.capacity);
        responses_.resize(batch.capacity);
        factor_tokens_.resize(batch.capacity);
        int device = 0;
        sparse_burn_detail::checked(cudaGetDevice(&device), "Sparse ODE selected device");
        sparse_burn_detail::checked(cudaDeviceGetAttribute(&threads_, cudaDevAttrWarpSize, device),
                                   "Sparse ODE device warp width");
        if (threads_ <= 0) throw std::runtime_error("Sparse ODE device has no valid warp width");
    }

    const SparseOdeBatchView<Network, Solver>& view() const { return batch_; }
    cudaStream_t stream() const { return stream_; }
    int launch_threads() const { return threads_; }
    std::uint64_t kernel_count() const { return kernel_count_ + (provider_ ? provider_->kernel_count() : 0); }
    std::uint64_t bytes_d2h() const { return bytes_d2h_ + (provider_ ? provider_->bytes_d2h() : 0); }
    std::uint64_t bytes_h2d() const { return bytes_h2d_ + (provider_ ? provider_->bytes_h2d() : 0); }
    std::uint64_t synchronization_count() const {
        return synchronization_count_ + (provider_ ? provider_->synchronization_count() : 0);
    }

    template <class Eos>
    void execute(int count, Eos eos, BurnConfigView config)
    {
        if constexpr (requires(Eos view) { view.device_error_status; }) {
            if (batch_.eos_statuses == nullptr)
                throw std::invalid_argument("Sparse table EOS requires a persistent device failure latch");
        }
        if (count < 0 || count > batch_.capacity)
            throw std::invalid_argument("Sparse ODE batch exceeds its pool capacity");
        if (count == 0) return;
        bool initialize = true;
        for (;;) {
            const int threads = threads_;
            sparse_burn_detail::advance_ode<Network, Solver>
                <<<(count + threads - 1) / threads, threads, 0, stream_>>>(
                    batch_, count, eos, config, initialize);
            sparse_burn_detail::checked(cudaGetLastError(), "Sparse ODE continuation launch");
            ++kernel_count_;
            sparse_burn_detail::checked(cudaMemcpyAsync(requests_.data(), batch_.requests,
                static_cast<std::size_t>(count) * sizeof(int), cudaMemcpyDeviceToHost, stream_),
                "Sparse ODE request download");
            bytes_d2h_ += static_cast<std::size_t>(count) * sizeof(int);
            sparse_burn_detail::checked(cudaStreamSynchronize(stream_), "Sparse ODE requests ready");
            ++synchronization_count_;
            initialize = false;
            int outstanding = 0;
            for (int lane = 0; lane < count; ++lane) {
                if (requests_[lane] == sparse_burn_detail::invalid_structure_request)
                    throw std::runtime_error("Sparse ODE network wrote outside its symbolic pattern");
                if (sparse_burn_detail::rejected_previous_response(requests_[lane])) {
                    provider_->invalidate();
                    cached_factor_token_ = 0;
                }
                const auto request = sparse_burn_detail::decode_request(requests_[lane]);
                if (request == OdeLinearRequest::Complete) continue;
                ++outstanding;
                double* values = batch_.coefficients + static_cast<std::size_t>(lane) * batch_.nonzeros;
                double* rhs = batch_.contexts[lane].b;
                double* solution = batch_.solutions + static_cast<std::size_t>(lane) * Network::ODE_NEQ;
                if (!provider_) {
                    provider_ = std::make_unique<CuDssSparseSolver>(
                        Network::ODE_NEQ, batch_.nonzeros, batch_.row_offsets,
                        batch_.column_indices, values, rhs, solution, stream_);
                }
                CuDssResult result;
                if (request == OdeLinearRequest::SolveWithFactors) {
                    // Keep one resident factorization, not one unpredictable
                    // fill-in allocation per cell/lane. Another lane can evict
                    // it; restore the original device matrix before resuming a
                    // factored solve for this token. No ODE policy is changed.
                    if (cached_factor_token_ != factor_tokens_[lane])
                        result = provider_->factorize(values, factor_tokens_[lane]);
                    if (result.success())
                        result = provider_->solve(rhs, solution, factor_tokens_[lane]);
                } else {
                    if (next_token_ == std::numeric_limits<std::uint64_t>::max())
                        throw std::overflow_error("Sparse ODE matrix token exhausted");
                    factor_tokens_[lane] = ++next_token_;
                    result = request == OdeLinearRequest::Factorize
                        ? provider_->factorize(values, factor_tokens_[lane])
                        : provider_->factorize_and_solve(values, rhs, solution, factor_tokens_[lane]);
                }
                cached_factor_token_ = result.success() ? factor_tokens_[lane] : 0;
                // DATA_INFO also includes opaque device/factor-capacity errors,
                // not only numerical singularities. Do not disguise any native
                // execution failure as physical stiffness. Only the original
                // matrix residual below supplies an adaptive numerical rejection.
                result.require_success();
                responses_[lane] = 1;
            }
            if (outstanding == 0) return;
            sparse_burn_detail::checked(cudaMemcpyAsync(batch_.responses, responses_.data(),
                static_cast<std::size_t>(count) * sizeof(int), cudaMemcpyHostToDevice, stream_),
                "Sparse ODE response upload");
            bytes_h2d_ += static_cast<std::size_t>(count) * sizeof(int);
            sparse_burn_detail::validate_corrections<Network, Solver>
                <<<(count + threads - 1) / threads, threads, 0, stream_>>>(batch_, count);
            sparse_burn_detail::checked(cudaGetLastError(), "Sparse ODE correction validation");
            ++kernel_count_;
        }
    }

private:
    SparseOdeBatchView<Network, Solver> batch_;
    cudaStream_t stream_;
    // One resident numerical factor set bounds fill-in memory independently of
    // pool capacity; a future budgeted cache can trade memory for fewer factors.
    std::unique_ptr<CuDssSparseSolver> provider_;
    std::vector<int> requests_, responses_;
    std::vector<std::uint64_t> factor_tokens_;
    std::uint64_t next_token_ = 0;
    std::uint64_t cached_factor_token_ = 0;
    int threads_ = 0;
    // Explicit ARCH scheduling transfers; provider-internal workspace traffic
    // is not represented as an ARCH state upload/download counter.
    std::uint64_t kernel_count_ = 0, bytes_d2h_ = 0, bytes_h2d_ = 0;
    std::uint64_t synchronization_count_ = 0;
};
} // namespace arch::cuda
