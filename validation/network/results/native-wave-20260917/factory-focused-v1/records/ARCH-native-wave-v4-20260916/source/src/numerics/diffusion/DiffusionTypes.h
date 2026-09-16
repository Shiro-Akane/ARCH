/**
 * @file DiffusionTypes.h
 * @brief Plain Host/device diffusion arguments and validity-bearing results.
 *
 * These records carry configuration, coefficients, and reduction candidates
 * without owning memory or grid traversal. DiffFlux.h owns the flux and
 * stability mathematics; executors preserve the validity flags when reducing
 * face or timestep results.
 */
#pragma once

#include "../../data/GlobalDefs.h"
#include <type_traits>

namespace DiffFlux
{
    struct DiffusionConfigView
    {
        bool use_diffusion = false;
        bool use_thermal_diffusion = false;
        bool use_viscous_diffusion = false;
        bool use_species_diffusion = false;
        double nu_visc = 0.0;
        double alpha_therm = 0.0;
        double D_spec = 0.0;
    };

    struct DiffusionCoefficients
    {
        double nu_visc = 0.0;
        double alpha_therm = 0.0;
        double D_spec = 0.0;
        bool valid = true;
    };

    struct DiffusionFaceStatus
    {
        bool active = false;
        bool valid = true;
    };

    struct DiffusionDtCandidate
    {
        double value = 1.0e10;
        bool valid = true;
    };

    static_assert(std::is_standard_layout_v<DiffusionConfigView>);
    static_assert(std::is_trivially_copyable_v<DiffusionConfigView>);
    static_assert(std::is_standard_layout_v<DiffusionCoefficients>);
    static_assert(std::is_trivially_copyable_v<DiffusionCoefficients>);
    static_assert(std::is_standard_layout_v<DiffusionFaceStatus>);
    static_assert(std::is_trivially_copyable_v<DiffusionFaceStatus>);
    static_assert(std::is_standard_layout_v<DiffusionDtCandidate>);
    static_assert(std::is_trivially_copyable_v<DiffusionDtCandidate>);

    inline DiffusionConfigView make_diffusion_config_view(const SimConfig& config)
    {
        return {
            config.physics.diffusion.use_diffusion,
            config.physics.diffusion.use_thermal_diffusion,
            config.physics.diffusion.use_viscous_diffusion,
            config.physics.diffusion.use_species_diffusion,
            config.physics.diffusion.nu_visc,
            config.physics.diffusion.alpha_therm,
            config.physics.diffusion.D_spec};
    }
} // namespace DiffFlux
