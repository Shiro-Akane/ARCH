/** Independent equilibrium and thermodynamic checks for generated-data NSE. */
#include "../math/GeneratedNseCases.h"
#include "../fixtures/NseReference.h"
#include "physics/network/aprox13/NetAprox13.h"
#include "physics/network/aprox19/NetAprox19.h"
#include "physics/network/aprox21/NetAprox21.h"
#include "physics/network/iso7/NetIso7.h"
#include <iostream>

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

int main()
{
    const auto result = arch::test::generated_nse::run();
    const bool builtins = builtin_reference<NetIso7>("iso7")
        && builtin_reference<NetAprox13>("aprox13")
        && builtin_reference<NetAprox19>("aprox19")
        && builtin_reference<NetAprox21>("aprox21");
    std::cout << "rank_two=" << result.rank_two << " rank_one=" << result.rank_one
              << " partition=" << result.partition << " energy=" << result.energy
              << " rejection=" << result.rejection << " gauge=" << result.gauge
              << " builtins=" << builtins << '\n';
    return result.passed() && builtins ? 0 : 1;
}
