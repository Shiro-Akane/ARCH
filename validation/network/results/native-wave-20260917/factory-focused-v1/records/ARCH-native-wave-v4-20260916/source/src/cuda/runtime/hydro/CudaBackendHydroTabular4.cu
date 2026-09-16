/**
 * @file CudaBackendHydroTabular4.cu
 * @brief Instantiate shared CUDA hydro launch bindings for Tabular4DEOSView.
 *
 * CudaBackendHydroInstantiation owns the typed dispatch; numerical policies
 * remain shared with host execution. This file supplies only the EOS type.
 */

#include "physics/eos/Tabular4DEOS.h"
#define ARCH_CUDA_HYDRO_EOS_TYPE Tabular4DEOSView
#include "cuda/runtime/hydro/CudaBackendHydroInstantiation.cuh"
