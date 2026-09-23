/**
 * @file CudaGravityExecution.h
 * @brief Declare the CUDA execution factory without duplicating gravity mathematics.
 *
 * Workflow:
 * 1. Use the selected CUDA device, stream and backend state lease.
 * 2. Declare the CUDA execution factory without duplicating gravity mathematics.
 * 3. Publish or retire resident results only after the required stream ordering.
 */

#pragma once

#include <cuda_runtime.h>

#include "physics/gravity/GravityExecution.h"

namespace arch::cuda {
std::shared_ptr<Physical::Gravity::GravityExecution> make_cuda_gravity_execution(
    cudaStream_t stream,int device,std::shared_ptr<void> lifetime);
}
