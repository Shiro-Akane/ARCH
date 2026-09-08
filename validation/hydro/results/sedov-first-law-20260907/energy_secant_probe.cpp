#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

#if defined(ARCH_ENERGY_SECANT_NEGATIVE)
#include "FluxFunctions-energy-secant-negative.h"
#else
#include "numerics/flux/FluxFunctions.h"
#endif
#include "physics/eos/IdealGas.h"

// Diagnostic: call the actual shared leaf and compare its ideal-gas acoustic
// identity. The archived negative header retains the original implementation.
int main() {
    const IdealGasView eos{};
    double max_error = 0.;
    std::cout << std::setprecision(17)
              << "energy,velocity_ratio,ulps,delta_e,kappa,c2,ideal_c2,error\n";
    for (double energy : {1e-4, 1., 1e8, 1e19})
    for (double ratio : {0., -1., 1.})
    for (int ulps : {1, 2, 4}) {
        double next_energy = energy;
        for (int i = 0; i < ulps; ++i)
            next_energy = std::nextafter(next_energy, std::numeric_limits<double>::infinity());
        const double speed = ratio * std::sqrt(energy);
        const FluidVector left{1., -speed, 0., 0., energy + .5*speed*speed};
        const FluidVector right{1., speed, 0., 0., next_energy + .5*speed*speed};
        const double el = eos_utils::extract_specific_internal_energy(left);
        const double er = eos_utils::extract_specific_internal_energy(right);
        const double pl = eos.get_pressure(left, nullptr), pr = eos.get_pressure(right, nullptr);
        const auto state = calc_glaister_state(left, right, pl, pr, el, er,
            left.eng + pl, right.eng + pr, nullptr, nullptr, 0, nullptr, eos);
        const double kinetic = .5 * (state.u_hat*state.u_hat + state.v_hat*state.v_hat + state.w_hat*state.w_hat);
        const double expected = (eos.global_gamma - 1.) * (state.H_hat - kinetic);
        const double value = state.c_hat * state.c_hat;
        const double error = std::isfinite(value)
            ? std::abs(value-expected)/std::max(1., std::abs(expected))
            : std::numeric_limits<double>::infinity();
        max_error = std::max(max_error, error);
        std::cout << energy << ',' << ratio << ',' << ulps << ',' << er-el << ','
                  << state.kappa << ',' << value << ',' << expected << ',' << error << '\n';
    }
    std::cout << "max_error=" << max_error << '\n';
    return max_error <= 3e-14 ? 0 : 1;
}
