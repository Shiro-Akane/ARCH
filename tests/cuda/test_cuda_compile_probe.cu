#include "core/ArchPortability.h"
#include "cuda/runtime/burn/CudaBackendBurn.h"
#include "cuda/runtime/burn/CudaBackendBurnSparse.h"
#include "cuda/runtime/hydro/CudaBackendHydro.h"
#include "cuda/runtime/diffusion/CudaBackendDiffusion.h"
#include "cuda/common/CudaLaunchConfig.h"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/microphysics/common.h"
#include "physics/constant/PhysicalConstants.h"

// Declaration-only ABIs must remain usable without completing every EOS.
// The actual owner TUs include their selected thermodynamic implementation.
template <class T>
constexpr bool complete_type = requires { sizeof(T); };
static_assert(!complete_type<IdealGasView>);
static_assert(!complete_type<HelmEosView>);
static_assert(!complete_type<Tabular3DEOSView>);
static_assert(!complete_type<Tabular4DEOSView>);
struct Grid;
static_assert(!complete_type<Grid>);

ARCH_HOST_DEVICE ARCH_FORCE_INLINE int arch_cuda_probe_value(int value)
{
    return value + 1;
}

ARCH_INLINE int arch_cuda_inline_probe_value(int value)
{
    return value * 2;
}

ARCH_FORCEINLINE int arch_cuda_forceinline_probe_value(int value)
{
    return value - 42;
}

__global__ void arch_cuda_probe_kernel(int* result)
{
    *result = arch_cuda_probe_value(41);
}

// Scalar constexpr constants (including <numbers> and derived quantities)
// must be usable by device code without a backend-owned constant table.
__global__ void arch_cuda_constants_probe_kernel(double* result)
{
    using namespace arch::constants;
    result[0] = math::pi;
    result[1] = statistical::cgs::boltzmann;
    result[2] = statistical::avogadro;
    result[3] = atomic::cgs::atomic_mass_unit;
    result[4] = quantum::cgs::planck;
    result[5] = relativity::cgs::speed_of_light;
    result[6] = gravity::cgs::gravitational_constant;
    result[7] = radiation::cgs::stefan_boltzmann;
    result[8] = radiation::cgs::energy_density;
    result[9] = electromagnetic::cgs::elementary_charge_squared;
    result[10] = units::erg_per_mev;
}

int main()
{
    // The target is a compile/link probe only; it intentionally owns no
    // production physics, runner, boundary, or integrator implementation.
    return arch_cuda_inline_probe_value(0) + arch_cuda_forceinline_probe_value(42);
}
