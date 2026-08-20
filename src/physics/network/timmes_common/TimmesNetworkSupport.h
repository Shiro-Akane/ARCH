// ARCH-owned generic policy adapter for the Timmes-derived network equations.
// Upstream/source boundaries: docs/physics/TimmesNetworks.md.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "Dual.h"
#include "NuclearConstants.h"
#include "RatePair.h"

#include "../../../core/RuntimeParams.h"
#include "../../../numerics/linalg/DenseWrap.h"
#include "../../../numerics/linalg/SparseWrap.h"
#include "../../species/Species.h"

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

    static void eval_rhs(const double* state, double rho, double eta, double* rhs, double& enuc)
    {
        constexpr int N = Derived::NUM_SPECIES;
        double y[N];
        double dydt[N];
#pragma omp simd
        for (int i = 0; i < N; ++i) {
            y[i] = std::clamp(state[i] / Derived::AION[i], 1.0e-30, 1.0);
        }
        Derived::template molar_rhs<double>(y, rho, eta, state[N], dydt);

#pragma omp simd
        for (int i = 0; i < N; ++i) {
            rhs[i] = dydt[i] * Derived::AION[i];
        }

        long double mass_sum = 0.0L;
        for (int i = 0; i < N; ++i) {
            mass_sum += static_cast<long double>(dydt[i])
                      * Derived::ENERGY_WEIGHTS[i];
        }
        enuc = Derived::ENERGY_CONVERSION * static_cast<double>(mass_sum);
    }

    template <typename MatrixType>
    static void eval_jacobian(const double* state, double rho, double eta, MatrixType& jac,
                              double* denuc_dX = nullptr)
    {
        constexpr int N = Derived::NUM_SPECIES;
        using AD = Dual<N>;
        AD y[N];
        AD dydt[N];
        for (int i = 0; i < N; ++i) {
            AD x = AD::variable(state[i], i);
            y[i] = clamp_by_value(x / Derived::AION[i], 1.0e-30, 1.0);
        }
        // Timmes' dfdy_isotopes_* differentiates the abundance algebra and
        // explicit equilibrium closures while holding screened base rates
        // fixed.  In particular, it does not differentiate the screening
        // factor through abar/zbar/z2bar.
        Derived::template molar_rhs_frozen_screening<AD>(
            y, rho, eta, state[N], dydt);
        for (int i = 0; i < N; ++i) {
            const AD dXdt = dydt[i] * Derived::AION[i];
#pragma omp simd
            for (int j = 0; j < N; ++j) {
                jac.set(i + 1, j + 1, dXdt.deriv[j]);
            }
        }

        if (denuc_dX != nullptr) {
            for (int j = 0; j < N; ++j) {
                long double mass_sum = 0.0L;
                for (int i = 0; i < N; ++i) {
                    mass_sum += static_cast<long double>(dydt[i].deriv[j])
                              * Derived::ENERGY_WEIGHTS[i];
                }
                denuc_dX[j] = Derived::ENERGY_CONVERSION
                            * static_cast<double>(mass_sum);
            }
        }
    }

    static void eval_temperature_derivative(const double* state, double rho, double eta,
                                            double* drhs_dT, double& denuc_dT)
    {
        constexpr int N = Derived::NUM_SPECIES;
        using AD = Dual<1>;
        AD y[N];
        AD dydt[N];
        for (int i = 0; i < N; ++i) {
            y[i] = clamp_by_value(AD(state[i] / Derived::AION[i]), 1.0e-30, 1.0);
        }

        const AD temperature = AD::variable(state[N], 0);
        Derived::template molar_rhs_impl<AD, RateTemperatureAccessor>(
            y, rho, eta, state[N], temperature, dydt);

        long double mass_sum = 0.0L;
        for (int i = 0; i < N; ++i) {
            drhs_dT[i] = dydt[i].deriv[0] * Derived::AION[i];
            mass_sum += static_cast<long double>(dydt[i].deriv[0])
                      * Derived::ENERGY_WEIGHTS[i];
        }
        denuc_dT = Derived::ENERGY_CONVERSION * static_cast<double>(mass_sum);
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
