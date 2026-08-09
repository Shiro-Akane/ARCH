/**
 * @file ProblemHelper.cpp
 * @brief Builds problem-dependent configuration and initial-condition helpers.
 *
 * Workflow:
 * 1. Read or derive the configuration value from its canonical source.
 * 2. Validate it before exposing it to problem setup and solver dispatch.
 * 3. Keep policy decisions out of low-level numerical kernels.
 */

#include "ProblemHelper.h"
#include "../data/GlobalDefs.h"
#include "../data/UserTypes.h"
#include "../physics/species/Species.h"
#include "../amr/AMRControl.h"
#include "../physics/eos/eosdispatch.h"
#include "../numerics/burnsolver/Networks.h"
#include <stdexcept>
#include <functional>
#include <algorithm>

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
            throw std::runtime_error("Unknown network_name in SetupNetworkAndFractions: " + net_type);
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
                                std::fill(data.mass_fractions.begin(), data.mass_fractions.end(), 0.0);

                                init_callback(p, data);

                                b.fluid_state.rho[idx] = data.rho;
                                b.fluid_state.mom_u[idx] = data.rho * data.u;
                                b.fluid_state.mom_v[idx] = data.rho * data.v;
                                b.fluid_state.mom_w[idx] = data.rho * data.w;
                                b.fluid_state.eng[idx] = eos.get_total_energy_primitive(data.rho, data.u, data.v, data.w, data.p, data.mass_fractions.data());

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
