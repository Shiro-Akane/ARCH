/**
 * @file CudaBackendMacroStep.cu
 * @brief Traverse accepted Device cells through the original shared validation.
 */
#include "cuda/runtime/control/CudaBackendMacroStep.h"
#include "cuda/hydro/policies/CheckedHydroEos.cuh"
#include "physics/eos/IdealGas.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/tabular/Tabular3DEOS.h"
#include "physics/eos/tabular/Tabular4DEOS.h"

namespace arch::cuda {
namespace {
template<class Eos>
__global__ void validate_current(DeviceStateView input, DeviceGridView grid,
    Eos eos, state::Bounds bounds, SpeciesWorkspaceView workspace, int* status) {
    const int lane=blockIdx.x*blockDim.x+threadIdx.x;
    detail::SpeciesLaneScratch<1> scratch(workspace,lane);
    double* fractions=scratch.array(0);
    const CheckedHydroEosView<Eos> checked(eos,status);
    for(int linear=lane;linear<grid.active_cell_count();linear+=blockDim.x*gridDim.x) {
        const int cell=grid.active_cell(linear);
        for(int species=0;species<input.n_species;++species)
            fractions[species]=input.species(species,cell);
        if(state::validate_eos(input.load(cell),fractions,input.n_species,bounds,checked)
            !=state::Status::valid)atomicExch(status,1);
    }
}
template<class Eos>
cudaError_t launch(DeviceStateView input,DeviceGridView grid,Eos eos,
    state::Bounds bounds,SpeciesWorkspaceView workspace,int* status,cudaStream_t stream) {
    if(!valid_hydro_view(input)||!valid_hydro_grid(grid)||input.total_size!=grid.total_size
        ||!state::valid_bounds(bounds)||!status
        ||!valid_species_workspace(workspace,input.n_species,1))
        return cudaErrorInvalidValue;
    const auto error=cudaMemsetAsync(status,0,sizeof(int),stream);
    if(error!=cudaSuccess)return error;
    validate_current<<<detail::species_launch_blocks(grid.active_cell_count(),workspace),
        detail::species_launch_threads(workspace),0,stream>>>(input,grid,eos,bounds,workspace,status);
    return cudaGetLastError();
}
}
#define ARCH_DEFINE_CURRENT_VALIDATION(EOS) \
    cudaError_t launch_cuda_validate_current(DeviceStateView input,DeviceGridView grid,EOS eos, \
        state::Bounds bounds,SpeciesWorkspaceView workspace,int* status,cudaStream_t stream) { \
        return launch(input,grid,eos,bounds,workspace,status,stream); }
ARCH_DEFINE_CURRENT_VALIDATION(IdealGasView)
ARCH_DEFINE_CURRENT_VALIDATION(HelmEosView)
ARCH_DEFINE_CURRENT_VALIDATION(Tabular3DEOSView)
ARCH_DEFINE_CURRENT_VALIDATION(Tabular4DEOSView)
#undef ARCH_DEFINE_CURRENT_VALIDATION
} // namespace arch::cuda
