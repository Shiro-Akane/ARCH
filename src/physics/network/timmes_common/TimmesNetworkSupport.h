#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "../../../core/RuntimeParams.h"
#include "../../species/Species.h"
#include "../../../numerics/linalg/DenseWrap.h"
#include "../../../numerics/linalg/SparseWrap.h"

#include "Dual.h"
#include "NuclearConstants.h"

namespace timmes {

template <typename Derived>
struct TimmesNetworkSupport {
    static std::string get_network_name() { return Derived::NETWORK_NAME; }

    static void RegisterSpecies(SpeciesManager& specs)
    {
        for (int i = 0; i < Derived::NUM_SPECIES; ++i) {
            specs.add_species(Derived::SPECIES_NAMES[i], Derived::AION[i],
                              Derived::ZION[i], 1.6667, 0.0);
        }
    }

    static void SetupInitialFractions(SimConfig& config, const SpeciesManager& specs,
                                      std::vector<double>& x_out)
    {
        x_out.assign(specs.count(), 1.0e-20);
        double sum = 0.0;
        for (int i = 0; i < specs.count(); ++i) {
            std::string target = "x" + specs.get_name(i);
            std::transform(target.begin(), target.end(), target.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            for (const auto& entry : config.custom_params) {
                std::string key = entry.first;
                std::transform(key.begin(), key.end(), key.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (key == target) {
                    x_out[i] += entry.second;
                    break;
                }
            }
            sum += x_out[i];
        }
        if (sum > 0.0) {
            for (double& value : x_out) value /= sum;
        }
    }

    static void eval_rhs(const double* state, double rho, double* rhs, double& enuc)
    {
        constexpr int N = Derived::NUM_SPECIES;
        double y[N];
        double dydt[N];
#pragma omp simd
        for (int i = 0; i < N; ++i) {
            y[i] = std::clamp(state[i] / Derived::AION[i], 1.0e-30, 1.0);
        }
        Derived::template molar_rhs<double>(y, rho, state[N], dydt);

#pragma omp simd
        for (int i = 0; i < N; ++i) {
            rhs[i] = dydt[i] * Derived::AION[i];
        }

        long double mass_sum = 0.0L;
        for (int i = 0; i < N; ++i) {
            mass_sum += static_cast<long double>(dydt[i]) * Derived::MION[i];
        }
        enuc = constants::enuc_conv2 * static_cast<double>(mass_sum);
    }

    template <typename MatrixType>
    static void eval_jacobian(const double* state, double rho, MatrixType& jac)
    {
        constexpr int N = Derived::NUM_SPECIES;
        using AD = Dual<N>;
        AD y[N];
        AD dydt[N];
        for (int i = 0; i < N; ++i) {
            AD x = AD::variable(state[i], i);
            y[i] = clamp_by_value(x / Derived::AION[i], 1.0e-30, 1.0);
        }
        Derived::template molar_rhs<AD>(y, rho, state[N], dydt);
        for (int i = 0; i < N; ++i) {
            const AD dXdt = dydt[i] * Derived::AION[i];
#pragma omp simd
            for (int j = 0; j < N; ++j) {
                jac.set(i + 1, j + 1, dXdt.deriv[j]);
            }
        }
    }
};

template <std::size_t N>
constexpr std::array<double, N> isotope_masses(const std::array<double, N>& a,
                                                const std::array<double, N>& z,
                                                const std::array<double, N>& binding)
{
    std::array<double, N> masses{};
    for (std::size_t i = 0; i < N; ++i) {
        masses[i] = (a[i] - z[i]) * constants::mn + z[i] * constants::mp
                  - binding[i] * constants::mev2gr;
    }
    return masses;
}

} // namespace timmes
