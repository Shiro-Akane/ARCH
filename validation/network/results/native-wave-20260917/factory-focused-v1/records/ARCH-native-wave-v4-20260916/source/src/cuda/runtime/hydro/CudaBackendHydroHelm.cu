/**
 * @file CudaBackendHydroHelm.cu
 * @brief Instantiate shared CUDA hydro launch bindings for HelmEosView.
 *
 * CudaBackendHydroInstantiation owns the typed dispatch; numerical policies
 * remain shared with host execution. This file supplies only the EOS type.
 */

#include "physics/eos/HelmEos.h"
#define ARCH_CUDA_HYDRO_EOS_TYPE HelmEosView
#include "cuda/runtime/hydro/CudaBackendHydroInstantiation.cuh"
