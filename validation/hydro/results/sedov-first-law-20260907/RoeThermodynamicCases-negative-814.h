#pragma once

#include "numerics/flux/FluxHLL.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/FluxRoe.h"
#include "physics/eos/IdealGas.h"

// Independent identities, not sampled implementation snapshots. The same
// traversal runs on Host and Device; the actual EOS view supplies input states.
namespace RoeThermodynamicCases {
struct Result {
    double reflection = 0.;
    double ideal_sound_speed = 0.;
    double ideal_pressure_jump = 0.;
    ARCH_INLINE bool passed() const {
        return std::isfinite(reflection) && reflection <= 3e-14
            && std::isfinite(ideal_sound_speed) && ideal_sound_speed <= 3e-14
            && std::isfinite(ideal_pressure_jump) && ideal_pressure_jump <= 3e-14;
    }
};

ARCH_INLINE FluidVector reflect(FluidVector state, int direction) {
    if (direction == 0) state.mom_u = -state.mom_u;
    else if (direction == 1) state.mom_v = -state.mom_v;
    else state.mom_w = -state.mom_w;
    return state;
}

ARCH_INLINE double scaled_error(double value, double expected) {
    return std::isfinite(value) && std::isfinite(expected)
        ? std::abs(value - expected) / std::max(1., std::abs(expected))
        : std::numeric_limits<double>::infinity();
}

template<class Flux>
ARCH_INLINE double reflection_error(const FluidVector& left, const FluidVector& right,
    const double* x_left, const double* x_right, int count, const IdealGasView& eos, int direction) {
    FluidVector flux{}, mirrored{};
    double species_flux[2]{}, mirrored_species[2]{};
    Flux::compute_face_flux(left, right, x_left, x_right, count, eos, direction, .1,
                           flux, species_flux);
    Flux::compute_face_flux(reflect(right, direction), reflect(left, direction),
        x_right, x_left, count, eos, direction, .1, mirrored, mirrored_species);
    const FluidVector expected = reflect(flux, direction) * -1.;
    double error = std::max({scaled_error(mirrored.rho, expected.rho),
        scaled_error(mirrored.mom_u, expected.mom_u), scaled_error(mirrored.mom_v, expected.mom_v),
        scaled_error(mirrored.mom_w, expected.mom_w), scaled_error(mirrored.eng, expected.eng)});
    for (int s = 0; s < count; ++s)
        error = std::max(error, scaled_error(mirrored_species[s], -species_flux[s]));
    return error;
}

ARCH_INLINE Result evaluate() {
    Result result{};
    const double a[2]{1., 2.}, z[2]{1., 1.}, gamma[2]{1.4, 5./3.}, cv[2]{2.5, 1.5};
    const double compositions[2][2]{{.8, .2}, {.3, .7}};
    for (int mixture = 0; mixture < 2; ++mixture)
    for (int density_case = 0; density_case < 2; ++density_case)
    for (int energy_case = 0; energy_case < 2; ++energy_case) {
        IdealGasView eos{};
        if (mixture) eos.species = {a, z, gamma, cv, 2};
        const int count = mixture ? 2 : 0;
        const double* xl = count ? compositions[0] : nullptr;
        const double* xr = count ? compositions[1] : nullptr;
        const double rl = 1., rr = density_case ? 4. : 1.;
        const double el = energy_case ? 64. : 2.5, er = energy_case ? 2.5e-5 : 2.5;
        const FluidVector left{rl, rl * .75, rl * -.2, rl * .1, rl * (el + .5*(.75*.75 + .2*.2 + .1*.1))};
        const FluidVector right{rr, rr * -.25, rr * .3, rr * -.15, rr * (er + .5*(.25*.25 + .3*.3 + .15*.15))};
        for (int direction = 0; direction < 3; ++direction) {
            result.reflection = std::max({result.reflection,
                reflection_error<FluxHLL<PCMReconstruction>>(left, right, xl, xr, count, eos, direction),
                reflection_error<FluxHLLC<PCMReconstruction>>(left, right, xl, xr, count, eos, direction),
                reflection_error<FluxRoe<PCMReconstruction>>(left, right, xl, xr, count, eos, direction)});
        }
        if (!mixture) {
            const double pl = eos.get_pressure(left, xl), pr = eos.get_pressure(right, xr);
            const double hl = (left.eng + pl) / rl, hr = (right.eng + pr) / rr;
            const auto state = calc_glaister_state(left, right, pl, pr, el, er, hl, hr, xl, eos);
            const double kinetic = .5 * (state.u_hat*state.u_hat + state.v_hat*state.v_hat + state.w_hat*state.w_hat);
            const double expected_c2 = (eos.global_gamma - 1) * (state.H_hat - kinetic);
            result.ideal_sound_speed = std::max(result.ideal_sound_speed,
                scaled_error(state.c_hat * state.c_hat, expected_c2));
            result.ideal_pressure_jump = std::max(result.ideal_pressure_jump,
                scaled_error(state.chi * (rr-rl) + state.kappa * (er-el), pr-pl));
        }
    }
    return result;
}
} // namespace RoeThermodynamicCases
