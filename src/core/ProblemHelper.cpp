/**
 * @file ProblemHelper.cpp
 * @brief Builds problem-dependent configuration and initial-condition helpers.
 *
 * Workflow:
 * 1. Read or derive the configuration value from its canonical source.
 * 2. Validate it before exposing it to problem setup and solver dispatch.
 * 3. Keep policy decisions out of low-level numerical kernels.
 */

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

#include "ProblemHelper.h"

#include "../amr/AMRControl.h"
#include "../amr/AmrDefines.h"
#include "../data/GlobalDefs.h"
#include "../data/UserTypes.h"
#include "../numerics/burnsolver/Networks.h"
#include "../physics/eos/eos_Utils.h"
#include "../physics/eos/eosdispatch.h"
#include "../physics/species/Species.h"

namespace ProblemHelper
{
    void SetupNetworkAndFractions(SimConfig &config, SpeciesManager &specs, std::vector<double> &default_X)
    {
        std::string net_type = config.physics.burn.network_name;
        if (net_type == "aprox19") {
            if (specs.count() == 0) NetAprox19::RegisterSpecies(specs);
            NetAprox19::SetupInitialFractions(config, specs, default_X);
        } else if (net_type == "aprox21") {
            if (specs.count() == 0) NetAprox21::RegisterSpecies(specs);
            NetAprox21::SetupInitialFractions(config, specs, default_X);
        } else if (net_type == "aprox13") {
            if (specs.count() == 0) NetAprox13::RegisterSpecies(specs);
            NetAprox13::SetupInitialFractions(config, specs, default_X);
        } else if (net_type == "iso7") {
            if (specs.count() == 0) NetIso7::RegisterSpecies(specs);
            NetIso7::SetupInitialFractions(config, specs, default_X);
        } else {
            bool custom_dispatched = false;
#define ARCH_SETUP_CUSTOM_NETWORK(runtime_name, network_type)               \
            if (!custom_dispatched && net_type == runtime_name) {            \
                if (specs.count() == 0) network_type::RegisterSpecies(specs); \
                network_type::SetupInitialFractions(                         \
                    config, specs, default_X);                               \
                custom_dispatched = true;                                   \
            }
            ARCH_FOR_EACH_CUSTOM_NETWORK(ARCH_SETUP_CUSTOM_NETWORK)
#undef ARCH_SETUP_CUSTOM_NETWORK
            if (!custom_dispatched)
                throw std::runtime_error(
                    "Unknown network_name in SetupNetworkAndFractions: " + net_type);
        }
    }

    double GetPressureFromRhoT(const SimConfig &config, const SpeciesManager &specs, double rho, double T, const double *X)
    {
        double p_out = 0.0;
        EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos) {
            p_out = eos.get_pressure_from_rho_T(rho, T, X);
        });
        return p_out;
    }

    double GetRootCellWidth(const SimConfig &config, int logical_axis)
    {
        double lower = 0.0;
        double upper = 0.0;
        int root_blocks = 0;
        int active_cells_per_block = 0;

        switch (logical_axis) {
        case 1:
            lower = config.grid.x1_min;
            upper = config.grid.x1_max;
            root_blocks = config.grid.nblockx1;
            active_cells_per_block = amr::BLOCK_NX;
            break;
        case 2:
            if (config.grid.dim < 2) {
                throw std::invalid_argument("Root cell width requested for inactive x2 axis.");
            }
            lower = config.grid.x2_min;
            upper = config.grid.x2_max;
            root_blocks = config.grid.nblockx2;
            active_cells_per_block = amr::BLOCK_NY;
            break;
        case 3:
            if (config.grid.dim < 3) {
                throw std::invalid_argument("Root cell width requested for inactive x3 axis.");
            }
            lower = config.grid.x3_min;
            upper = config.grid.x3_max;
            root_blocks = config.grid.nblockx3;
            active_cells_per_block = amr::BLOCK_NZ;
            break;
        default:
            throw std::invalid_argument("Root cell width logical_axis must be 1, 2, or 3.");
        }

        if (root_blocks <= 0 || !std::isfinite(lower) || !std::isfinite(upper) || upper <= lower) {
            throw std::invalid_argument("Root cell width requires a positive block count and domain extent.");
        }

        return (upper - lower) /
            (static_cast<double>(root_blocks) * active_cells_per_block);
    }

    IsentropicState GetIsentropicStateAtPressureFactor(
        const SimConfig &config, const SpeciesManager &specs,
        double reference_rho, double reference_temperature,
        const double *X, double pressure_factor)
    {
        if (!std::isfinite(reference_rho) || reference_rho <= 0.0 ||
            !std::isfinite(reference_temperature) || reference_temperature <= 0.0 ||
            !std::isfinite(pressure_factor) || pressure_factor <= 0.0) {
            throw std::invalid_argument("Invalid reference state or pressure factor for isentropic initialization.");
        }

        IsentropicState result{};
        EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos) {
            const eos_utils::IsentropicState state =
                eos_utils::get_isentropic_state_at_pressure_factor(
                    eos, reference_rho, reference_temperature, X, pressure_factor);
            result.rho = state.rho;
            result.temperature = state.temperature;
            result.pressure = state.pressure;
            result.sound_speed = state.sound_speed;
        });
        return result;
    }

    namespace detail
    {
    void PopulateState(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs,
                       std::function<void(const PointCoords&, PrimitiveData&)> init_callback)
    {
        int n_species = specs.count();
        const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();

        EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos) {
#pragma omp parallel
            {
                PrimitiveData data{};
                data.mass_fractions.resize(n_species, 0.0);

#pragma omp for schedule(dynamic)
                for (size_t b_idx = 0; b_idx < active_blocks.size(); ++b_idx)
                {
                    amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[b_idx]);

                    for (int k = 0; k < b.grid.GetTotalZ(); ++k) {
                        for (int j = 0; j < b.grid.GetTotalY(); ++j) {
                            for (int i = 0; i < b.grid.GetTotalX(); ++i) {
                                // Skip padding zone
                                if (i >= b.grid.GetTotalX()) continue;

                                int idx = b.grid.GetIndex(i, j, k);

                                // Compute logical physical coordinate (assuming center of cell)
                                PointCoords p = b.grid.GetPhysicalCoords(i, j, k);

                                data.rho = 0.0;
                                data.u = 0.0;
                                data.v = 0.0;
                                data.w = 0.0;
                                data.p = 0.0;
                                data.temperature = 0.0;
                                data.has_temperature = false;
                                std::fill(data.mass_fractions.begin(), data.mass_fractions.end(), 0.0);

                                init_callback(p, data);

                                b.fluid_state.rho[idx] = data.rho;
                                b.fluid_state.mom_u[idx] = data.rho * data.u;
                                b.fluid_state.mom_v[idx] = data.rho * data.v;
                                b.fluid_state.mom_w[idx] = data.rho * data.w;
                                if (data.has_temperature) {
                                    const double specific_internal_energy = eos.get_eint_from_T(
                                        data.rho, data.temperature, data.mass_fractions.data());
                                    if (!std::isfinite(specific_internal_energy)) {
                                        throw std::runtime_error("EOS returned non-finite internal energy for temperature-based initialization.");
                                    }
                                    const double kinetic_energy = 0.5 * data.rho *
                                        (data.u * data.u + data.v * data.v + data.w * data.w);
                                    b.fluid_state.eng[idx] = data.rho * specific_internal_energy + kinetic_energy;
                                } else {
                                    b.fluid_state.eng[idx] = eos.get_total_energy_primitive(
                                        data.rho, data.u, data.v, data.w, data.p,
                                        data.mass_fractions.data());
                                }

                                for (int s = 0; s < n_species; ++s)
                                    b.fluid_state.X(s, idx) = data.mass_fractions[s];
                            }
                        }
                    }
                }
            }
        });
    }
    } // namespace detail
} // namespace ProblemHelper
