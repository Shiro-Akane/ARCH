/**
 * @file CudaBackendHydroIdeal.cu
 * @brief Instantiate shared CUDA hydro launch bindings for IdealGasView.
 *
 * CudaBackendHydroInstantiation owns the typed dispatch; numerical policies
 * remain shared with host execution. This file supplies only the EOS type.
 */

#include "physics/eos/IdealGas.h"
#define ARCH_CUDA_HYDRO_EOS_TYPE IdealGasView
#include "cuda/runtime/hydro/CudaBackendHydroInstantiation.cuh"
