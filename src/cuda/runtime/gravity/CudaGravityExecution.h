#pragma once
#include "physics/gravity/GravityExecution.h"
#include <cuda_runtime.h>
namespace arch::cuda {
std::shared_ptr<Physical::Gravity::GravityExecution> make_cuda_gravity_execution(
    cudaStream_t stream,int device,std::shared_ptr<void> lifetime);
}
