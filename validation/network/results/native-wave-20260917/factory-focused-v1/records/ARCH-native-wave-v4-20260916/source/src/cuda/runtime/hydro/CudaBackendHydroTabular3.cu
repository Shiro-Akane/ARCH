/**
 * @file CudaBackendHydroTabular3.cu
 * @brief Instantiate shared CUDA hydro launch bindings for Tabular3DEOSView.
 *
 * CudaBackendHydroInstantiation owns the typed dispatch; numerical policies
 * remain shared with host execution. This file supplies only the EOS type.
 */

#include "physics/eos/Tabular3DEOS.h"
#define ARCH_CUDA_HYDRO_EOS_TYPE Tabular3DEOSView
#include "cuda/runtime/hydro/CudaBackendHydroInstantiation.cuh"
