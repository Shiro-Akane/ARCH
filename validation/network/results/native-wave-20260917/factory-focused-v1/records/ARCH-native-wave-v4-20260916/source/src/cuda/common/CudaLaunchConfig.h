/**
 * @file CudaLaunchConfig.h
 * @brief Ordinary-C++ lowering of a resolved execution plan for CUDA launches.
 */

#pragma once

#include "numerics/diffusion/DiffusionTypes.h"
#include "driver/dispatch/ResolvedExecutionPlan.h"
#include "data/GlobalDefs.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "physics/gravity/ExternalGravitySource.h"

#include <type_traits>
#include <cmath>
#include <stdexcept>

namespace arch::cuda {

struct CudaLaunchConfig {
    dispatch::ResolvedExecutionPlan plan{};
    BurnConfigView burn{};
    DiffFlux::DiffusionConfigView diffusion{};
    double density_floor = 0.0;
    double minimum_internal_energy = 0.0;
    double maximum_internal_energy = 0.0;
    double cfl = 0.0;
    double entropy_fix_coefficient = 0.0;
    double diffusion_cfl = 0.0;
    int diffusion_max_stages = 0;
    Physical::Gravity::ExternalGravityView gravity{};
};

inline CudaLaunchConfig make_cuda_launch_config(
    const dispatch::ResolvedExecutionPlan& plan, const SimConfig& config)
{
    const auto gravity = dispatch::parse_gravity(config.physics.gravity.type);
    if (!gravity.ok || gravity.value == dispatch::GravityId::Self)
        throw std::invalid_argument("CUDA launch requires none or external gravity");
    const bool external = gravity.value == dispatch::GravityId::External;
    const auto& g = config.physics.gravity;
    if (external && (!std::isfinite(g.g_x) || !std::isfinite(g.g_y) || !std::isfinite(g.g_z)))
        throw std::invalid_argument("external gravity components must be finite");
    return {
        plan,
        make_burn_config_view(config.physics.burn),
        DiffFlux::make_diffusion_config_view(config),
        config.numerics.sml_rho,
        config.numerics.min_eint,
        config.numerics.max_eint,
        config.numerics.cfl,
        config.numerics.entropy_fix_coeff,
        config.physics.diffusion.diff_cfl,
        config.physics.diffusion.max_stages,
        {g.g_x, g.g_y, g.g_z, external},
    };
}

static_assert(std::is_standard_layout_v<CudaLaunchConfig>);
static_assert(std::is_trivially_copyable_v<CudaLaunchConfig>);

} // namespace arch::cuda
