/**
 * @file TimmesNetworkSupport.h
 * @brief ARCH policy adapter for the unchanged Timmes-derived network equations.
 * Workflow:
 * 1. Convert mass fractions into bounded molar abundances.
 * 2. Evaluate shared screened rates and the requested RHS/Jacobian/thermal row.
 * 3. Convert back to mass-fraction rates and compensated nuclear energy.
 * Source boundaries: docs/physics/TimmesNetworks.md.
 */
#pragma once
#include "numerics/state/StateAdmissibility.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "physics/network/timmes_common/Dual.h"
#include "physics/network/timmes_common/NuclearConstants.h"
#include "physics/network/timmes_common/RatePair.h"

#include "data/GlobalDefs.h"
#include "core/CompensatedSum.h"
#include "physics/species/Species.h"
#include "physics/network/InitialComposition.h"

namespace timmes {

template <typename Derived>
struct TimmesNetworkSupport {
    static constexpr bool SUPPORTS_NSE = true;

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
        x_out = arch::network::ReadInitialComposition(config, specs);
        if (!arch::state::normalize_composition(x_out.data(), specs.count(), 1, config.physics.burn.smallx))
            throw std::invalid_argument("Initial network composition must contain finite nonnegative fractions with a positive sum");
    }

    ARCH_HEAVY_INLINE static void eval_rhs(const double* state, double rho, double eta,
                                   double* rhs, double& enuc)
    {
        constexpr int N = Derived::NUM_SPECIES;
        double y[N];
        double dydt[N];
#pragma omp simd
        for (int i = 0; i < N; ++i) {
            y[i] = clamp_by_value(state[i] / Derived::aion(i), 1.0e-30, 1.0);
        }
        Derived::template molar_rhs<double>(y, rho, eta, state[N], dydt);

#pragma omp simd
        for (int i = 0; i < N; ++i) {
            rhs[i] = dydt[i] * Derived::aion(i);
        }

        arch::math::CompensatedSum mass_sum;
        for (int i = 0; i < N; ++i) {
            mass_sum.add(dydt[i] * Derived::energy_weight(i));
        }
        enuc = Derived::ENERGY_CONVERSION * mass_sum.value();
    }

    template <typename MatrixType>
    ARCH_HEAVY_INLINE static void eval_jacobian(const double* state, double rho,
                                        double eta, MatrixType& jac,
                                        double* denuc_dX = nullptr,
                                        double* rhs = nullptr, double* enuc = nullptr)
    {
        constexpr int N = Derived::NUM_SPECIES;
        // Timmes' dfdy_isotopes_* holds screened base rates fixed while
        // differentiating abundance algebra and equilibrium closures. Reuse
        // the already present generated molar Jacobian when the network has
        // one; iso7 retains the generic Dual path below.
        if constexpr (requires(const double* y, double* rhs, double* derivatives) {
            Derived::molar_rhs_jacobian_frozen_screening(
                y, rho, eta, state[N], rhs, derivatives);
        }) {
            double y[N], dydt[N], molar_jacobian[N * N];
            double dy_dX[N];
            for (int j = 0; j < N; ++j) {
                const double raw = state[j] / Derived::aion(j);
                y[j] = clamp_by_value(raw, 1.0e-30, 1.0);
                // clamp_by_value returns a constant only OUTSIDE the closed
                // interval. At an exact endpoint its derivative remains 1/A.
                dy_dX[j] = (raw < 1.0e-30 || raw > 1.0)
                           ? 0.0 : 1.0 / Derived::aion(j);
            }
            Derived::molar_rhs_jacobian_frozen_screening(
                y, rho, eta, state[N], dydt, molar_jacobian);
            // The generated Jacobian already computes the screened species
            // RHS at this same state. Reuse it for the coupled ODE assembly.
            if (rhs != nullptr) {
                for (int i = 0; i < N; ++i)
                    rhs[i] = dydt[i] * Derived::aion(i);
            }
            if (enuc != nullptr) {
                arch::math::CompensatedSum mass_sum;
                for (int i = 0; i < N; ++i)
                    mass_sum.add(dydt[i] * Derived::energy_weight(i));
                *enuc = Derived::ENERGY_CONVERSION * mass_sum.value();
            }
            for (int i = 0; i < N; ++i) {
                const double aion = Derived::aion(i);
                for (int j = 0; j < N; ++j)
                    jac.set(i + 1, j + 1,
                            aion * molar_jacobian[i * N + j] * dy_dX[j]);
            }
            if (denuc_dX != nullptr) {
                for (int j = 0; j < N; ++j) {
                    arch::math::CompensatedSum mass_sum;
                    for (int i = 0; i < N; ++i)
                        mass_sum.add((molar_jacobian[i * N + j] * dy_dX[j])
                                     * Derived::energy_weight(i));
                    denuc_dX[j] = Derived::ENERGY_CONVERSION
                                * mass_sum.value();
                }
            }
        } else {
            // iso7 has a separate frozen-rate Dual path; preserve its
            // ordinary RHS as the value authority.
            if (rhs != nullptr || enuc != nullptr) {
                double unused_rhs[N], unused_energy;
                eval_rhs(state, rho, eta, rhs != nullptr ? rhs : unused_rhs,
                         enuc != nullptr ? *enuc : unused_energy);
            }
            using AD = Dual<N>;
            AD y[N];
            AD dydt[N];
            for (int i = 0; i < N; ++i) {
                AD x = AD::variable(state[i], i);
                y[i] = clamp_by_value(x / Derived::aion(i), 1.0e-30, 1.0);
            }
            Derived::template molar_rhs_frozen_screening<AD>(
                y, rho, eta, state[N], dydt);
            for (int i = 0; i < N; ++i) {
                const AD dXdt = dydt[i] * Derived::aion(i);
#pragma omp simd
                for (int j = 0; j < N; ++j)
                    jac.set(i + 1, j + 1, dXdt.deriv[j]);
            }
            if (denuc_dX != nullptr) {
                for (int j = 0; j < N; ++j) {
                    arch::math::CompensatedSum mass_sum;
                    for (int i = 0; i < N; ++i)
                        mass_sum.add(dydt[i].deriv[j] * Derived::energy_weight(i));
                    denuc_dX[j] = Derived::ENERGY_CONVERSION * mass_sum.value();
                }
            }
        }
    }

    ARCH_HEAVY_INLINE static void eval_temperature_derivative(
        const double* state, double rho, double eta,
        double* drhs_dT, double& denuc_dT)
    {
        constexpr int N = Derived::NUM_SPECIES;
        using AD = Dual<1>;
        AD y[N];
        AD dydt[N];
        for (int i = 0; i < N; ++i) {
            y[i] = clamp_by_value(AD(state[i] / Derived::aion(i)), 1.0e-30, 1.0);
        }

        const AD temperature = AD::variable(state[N], 0);
        Derived::template molar_rhs_impl<AD, RateTemperatureAccessor>(
            y, rho, eta, state[N], temperature, dydt);

        arch::math::CompensatedSum mass_sum;
        for (int i = 0; i < N; ++i) {
            drhs_dT[i] = dydt[i].deriv[0] * Derived::aion(i);
            mass_sum.add(dydt[i].deriv[0] * Derived::energy_weight(i));
        }
        denuc_dT = Derived::ENERGY_CONVERSION * mass_sum.value();
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
