#include "UserInterface.h"
#include "../physics/eos/eosdispatch.h"
#include "../numerics/burnsolver/NetPynucastro.h"
#include <functional>

namespace ProblemHelper
{
    void SetupNetworkAndFractions(SimConfig &config, SpeciesManager &specs, std::vector<double> &default_X)
    {
        if (specs.count() == 0)
        {
            NetPynucastro::RegisterSpecies(specs);
        }
        NetPynucastro::SetupInitialFractions(config, specs, default_X);
    }

    double GetPressureFromRhoT(const SimConfig &config, const SpeciesManager &specs, double rho, double T, const double *X)
    {
        double p_out = 0.0;
        EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos) {
            p_out = eos.get_pressure_from_rho_T(rho, T, X);
        });
        return p_out;
    }

    void PopulateState(FluidState &state, const Grid &grid, const SimConfig &config, const SpeciesManager &specs,
                       std::function<void(const PointCoords&, PrimitiveData&)> init_callback)
    {
        int n_species = state.GetNumSpecies();
        int stride_y = grid.stride_y;
        int stride_z = grid.stride_z;
        int total_size = grid.GetTotalSize();

        EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos) {
#pragma omp parallel
            {
                PrimitiveData data{};
                data.mass_fractions.resize(n_species, 0.0);

#pragma omp for schedule(static)
                for (int idx = 0; idx < total_size; ++idx)
                {
                    int k = idx / stride_z;
                    int rem = idx % stride_z;
                    int j = rem / stride_y;
                    int i = rem % stride_y;

                    PointCoords p = grid.GetPhysicalCoords(i, j, k);

                    data.rho = 0.0;
                    data.u = 0.0;
                    data.v = 0.0;
                    data.w = 0.0;
                    data.p = 0.0;
                    std::fill(data.mass_fractions.begin(), data.mass_fractions.end(), 0.0);

                    init_callback(p, data);

                    state.rho[idx] = data.rho;
                    state.mom_x[idx] = data.rho * data.u;
                    state.mom_y[idx] = data.rho * data.v;
                    state.mom_z[idx] = data.rho * data.w;
                    state.eng[idx] = eos.get_total_energy_primitive(data.rho, data.u, data.v, data.w, data.p, data.mass_fractions.data());

                    for (int s = 0; s < n_species; ++s)
                        state.X(s, idx) = data.mass_fractions[s];
                }
            }
        });
    }
}
