/**
 * @file NativeBurnState.cuh
 * @brief Borrow the shared native mean closure for the existing device burner.
 *
 * Only the target's original moments and immutable density neighbors are read.
 * The ordinary effective mean is transient ODE/handoff input; native moments
 * remain in their original arrays and only accepted E/X/enuc are published.
 * Workflow: borrow center moments -> read immutable rho support -> reuse the
 * common density/inertia closure -> return its effective mean and status.
 */
#pragma once

#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "numerics/state/RzNativeClosure.h"

namespace arch::cuda {
namespace native_burn_detail {
struct DensityReader {
    const double* density;
    FluidVector target;
    int target_index;

    ARCH_INLINE FluidVector operator()(int index) const
    {
        if (index == target_index) return target;
        FluidVector sample{};
        sample.rho = density[index];
        return sample;
    }
};
} // namespace native_burn_detail

/** Use the actual centered density support and existing configured bounds.
 * The reader never borrows neighboring energy/species while other cells burn.
 * This changes neither the native density polynomial nor its inertia formula.
 */
ARCH_INLINE arch::state::Status prepare_native_burn_mean(
    DeviceStateView state, const DeviceGridView& grid, int index, int i,
    const arch::state::Bounds& bounds, FluidVector& thermo)
{
    const native_burn_detail::DensityReader read{state.rho, thermo, index};
    const auto closure = RzThermodynamics::make_cell(
        read, index, make_grid_geometry_view(grid), i, bounds);
    if (closure.valid()) thermo = closure.effective_mean;
    return closure.status;
}
} // namespace arch::cuda
