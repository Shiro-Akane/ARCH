#pragma once

#include "../../numerics/flux/FluxHLL.h"
#include "../../numerics/flux/FluxHLLC.h"
#include "../../numerics/flux/FluxRoe.h"
#include "../../numerics/flux/FluxSW.h"
#include "../../numerics/flux/FluxVL.h"

namespace arch::cuda
{
template <template <typename> class OriginalFlux>
struct CudaOriginalFluxPolicy
{
    template <typename EosView>
    static ARCH_INLINE void compute(
        const FluidVector& left, const FluidVector& right,
        const double* species_left, const double* species_right, int n_species,
        const EosView& eos, int direction, double coefficient,
        FluidVector& flux, double* species_flux)
    {
        OriginalFlux<PCMReconstruction>::compute_face_flux(
            left, right, species_left, species_right, n_species, eos,
            direction, coefficient, flux, species_flux);
    }
};

using CudaHllFlux = CudaOriginalFluxPolicy<FluxHLL>;
using CudaHllcFlux = CudaOriginalFluxPolicy<FluxHLLC>;
using CudaRoeFlux = CudaOriginalFluxPolicy<FluxRoe>;
using CudaSwFlux = CudaOriginalFluxPolicy<FluxSW>;
using CudaVlFlux = CudaOriginalFluxPolicy<FluxVL>;
} // namespace arch::cuda
