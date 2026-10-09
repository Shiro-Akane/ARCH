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

#include "data/GlobalDefs.h"
#include "core/ArchPortability.h"
#include <cmath>
#include <type_traits>
#include <limits>

namespace DiffFlux
{
    /** One endpoint redistribution item, separate from heat-flux accounting. */
    struct DiffusionEnergyActivityTerm {
        double signed_energy_change = 0.0, absolute_energy_change = 0.0;
        bool valid = false;
    };

    /** Shared Host/device scalar term; original GridMetrics supplies true V.
     * No normalization, clipping, EOS, recurrence or coefficient is involved.
     */
    ARCH_HOST_DEVICE inline DiffusionEnergyActivityTerm diffusion_energy_activity_term(
        double volume, double current_energy, double seed_energy)
    {
        if (!std::isfinite(volume) || !(volume > 0.0)
            || !std::isfinite(current_energy) || !std::isfinite(seed_energy)) return {};
        const double difference = current_energy - seed_energy;
        const double signed_change = volume * difference;
        const double absolute_change = volume * std::abs(difference);
        if (!std::isfinite(difference) || !std::isfinite(signed_change)
            || !std::isfinite(absolute_change)) return {};
        return {signed_change, absolute_change, true};
    }

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
        double value = std::numeric_limits<double>::max();
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
