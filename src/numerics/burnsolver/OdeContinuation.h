/**
 * @file OdeContinuation.h
 * @brief Linear-algebra suspension points shared by CPU and GPU ODE executors.
 * Continuations own numerical progress, not backend handles. A CPU executor
 * services requests synchronously; a GPU executor may keep contexts on device
 * while a host-API sparse provider works. Submission is never ODE completion.
 */
#pragma once

// Host factory seam for immutable network data. Stateless builtins remain an
// empty value; CUDA executors receive an explicitly backend-bound view instead.
template <class Network>
Network make_host_burn_network()
{
    if constexpr (requires { Network::host_view(); }) return Network::host_view();
    else return {};
}

// Shared storage contract, independent of the numerical function bodies and
// linear provider. Allocation/ABI consumers must not import NSE to size it.
template <typename MatrixType>
struct OdeMatrixWorkspace
{
    MatrixType jacobian;
    MatrixType system;
};

enum class OdeLinearRequest : unsigned char
{
    Complete,
    FactorizeAndSolve,
    Factorize,
    SolveWithFactors
};
