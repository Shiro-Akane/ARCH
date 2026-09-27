/** Independent equilibrium and thermodynamic checks for generated-data NSE. */
#include <algorithm>
#include <cmath>
#include <iostream>

#include "fixtures/network/NseReference.h"
#include "math/microphysics/GeneratedNseCases.h"
#include "numerics/linalg/DenseWrap.h"
#include "physics/network/aprox13/NetAprox13.h"
#include "physics/network/aprox19/NetAprox19.h"
#include "physics/network/aprox21/NetAprox21.h"
#include "physics/network/iso7/NetIso7.h"

template <class Network>
bool builtin_reference(const char* name)
{
    const auto& reference = NseReference::find(name);
    constexpr int count = Network::NUM_SPECIES;
    double initial[count], actual[count], energy = 0.0;
    for (int i = 0; i < count; ++i)
        initial[i] = (i + 1.0) / (0.5 * count * (count + 1.0));
    if (!NSESolver<Network>::solve(5.0e9, 1.0e7, reference.ye,
                                   initial, actual, energy)) return false;
    for (int i = 0; i < count; ++i)
        if (std::abs(actual[i] - reference.x[i]) > 1.0e-12) return false;
    return std::abs(energy - reference.enuc) <= 1.0e-12 * std::abs(reference.enuc);
}

/** Compare each built-in frozen-screening Jacobian with independent dual
 *  differentiation of its original abundance equations. The two normalized
 *  mixtures sample different branches; clamped trial columns must be zero. */
template <class Network>
bool jacobian_reference()
{
    constexpr int N = Network::NUM_SPECIES;
    using AD = timmes::Dual<N>;
    for (double rho : {1.0e6, 1.0e7, 1.0e8})
        for (double temperature : {8.0e8, 2.0e9, 4.0e9})
            for (int mixture = 0; mixture < 2; ++mixture) {
                double state[N + 1]{};
                double normalizer = 0.0;
                for (int i = 0; i < N; ++i) {
                    state[i] = mixture == 0 ? 1.0 / (i + 1.0) : i + 1.0;
                    normalizer += state[i];
                }
                for (int i = 0; i < N; ++i) state[i] /= normalizer;
                state[N] = temperature;
                DenseMatrixData<N> actual{};
                double actual_energy[N]{};
                Network::eval_jacobian(state, rho, 0.25, actual, actual_energy);
                AD y[N], dydt[N];
                for (int i = 0; i < N; ++i)
                    y[i] = timmes::clamp_by_value(
                        AD::variable(state[i], i) / Network::aion(i),
                        1.0e-30, 1.0);
                Network::template molar_rhs_frozen_screening<AD>(
                    y, rho, 0.25, temperature, dydt);
                for (int column = 0; column < N; ++column) {
                    arch::math::CompensatedSum energy;
                    for (int row = 0; row < N; ++row) {
                        const double expected = dydt[row].deriv[column]
                                              * Network::aion(row);
                        const double observed = actual.data[row][column];
                        if (!std::isfinite(expected) || !std::isfinite(observed)
                            || std::abs(observed - expected) > 3.0e-12
                               * std::max({1.0, std::abs(expected), std::abs(observed)}))
                            return false;
                        energy.add(dydt[row].deriv[column]
                                   * Network::energy_weight(row));
                    }
                    const double expected = Network::ENERGY_CONVERSION
                                          * energy.value();
                    const double observed = actual_energy[column];
                    if (!std::isfinite(expected) || !std::isfinite(observed)
                        || std::abs(observed - expected) > 1.0e-10
                           * std::max({1.0, std::abs(expected), std::abs(observed)}))
                        return false;
                }
            }
    for (double clipped : {0.0, 1.01}) {
        double state[N + 1]{};
        for (int i = 0; i < N; ++i) state[i] = 1.0 / N;
        state[0] = clipped == 0.0 ? 0.0 : clipped * Network::aion(0);
        state[N] = 2.0e9;
        DenseMatrixData<N> actual{};
        double energy[N]{};
        Network::eval_jacobian(state, 1.0e7, 0.25, actual, energy);
        for (int row = 0; row < N; ++row)
            if (actual.data[row][0] != 0.0) return false;
        if (energy[0] != 0.0) return false;
    }
    return true;
}

int main()
{
    const auto result = arch::test::generated_nse::run();
    const bool builtins = builtin_reference<NetIso7>("iso7")
        && builtin_reference<NetAprox13>("aprox13")
        && builtin_reference<NetAprox19>("aprox19")
        && builtin_reference<NetAprox21>("aprox21");
    const bool jacobian_iso7 = jacobian_reference<NetIso7>();
    const bool jacobian_aprox13 = jacobian_reference<NetAprox13>();
    const bool jacobian_aprox19 = jacobian_reference<NetAprox19>();
    const bool jacobian_aprox21 = jacobian_reference<NetAprox21>();
    const bool jacobians = jacobian_iso7 && jacobian_aprox13
        && jacobian_aprox19 && jacobian_aprox21;
    std::cout << "rank_two=" << result.rank_two << " rank_one=" << result.rank_one
              << " partition=" << result.partition << " energy=" << result.energy
              << " rejection=" << result.rejection << " gauge=" << result.gauge
              << " builtins=" << builtins << " jacobians=" << jacobians
              << " iso7=" << jacobian_iso7 << " aprox13=" << jacobian_aprox13
              << " aprox19=" << jacobian_aprox19 << " aprox21=" << jacobian_aprox21 << '\n';
    return result.passed() && builtins && jacobians ? 0 : 1;
}
