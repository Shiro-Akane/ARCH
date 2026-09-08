#pragma once

#include <limits>

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
    double rotation = 0.;
    double stationary_transport = 0.;
    ARCH_INLINE bool passed() const {
        return std::isfinite(reflection) && reflection <= 3e-14
            && std::isfinite(ideal_sound_speed) && ideal_sound_speed <= 3e-14
            && std::isfinite(ideal_pressure_jump) && ideal_pressure_jump <= 3e-14
            && std::isfinite(rotation) && rotation <= 3e-14
            && stationary_transport == 0.;
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

// Cyclically rotate x to the requested face normal, keeping handedness.
ARCH_INLINE FluidVector rotate(FluidVector state, int direction) {
    for (int d = 0; d < direction; ++d) {
        const double x = state.mom_u;
        state.mom_u = state.mom_w;
        state.mom_w = state.mom_v;
        state.mom_v = x;
    }
    return state;
}

ARCH_INLINE double rotation_error(const FluidVector& left, const FluidVector& right,
    const double* xl, const double* xr, int count, const IdealGasView& eos, int direction) {
    FluidVector flux{}, rotated{};
    double species[2]{}, rotated_species[2]{};
    FluxRoe<PCMReconstruction>::compute_face_flux(left, right, xl, xr, count,
        eos, 0, .1, flux, species);
    FluxRoe<PCMReconstruction>::compute_face_flux(rotate(left, direction),
        rotate(right, direction), xl, xr, count, eos, direction, .1, rotated, rotated_species);
    const auto expected = rotate(flux, direction);
    double error = std::max({scaled_error(rotated.rho, expected.rho),
        scaled_error(rotated.mom_u, expected.mom_u), scaled_error(rotated.mom_v, expected.mom_v),
        scaled_error(rotated.mom_w, expected.mom_w), scaled_error(rotated.eng, expected.eng)});
    for (int s = 0; s < count; ++s)
        error = std::max(error, scaled_error(rotated_species[s], species[s]));
    return error;
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
    // Mirror-related Riemann states have an exactly stationary contact. Its
    // mass, tangential-momentum and energy fluxes vanish algebraically even
    // when the incoming gas is moving; do not obtain zero by subtracting two
    // large fluxes. Test several scales, expansion/compression and all normals.
    for (int scale = 0; scale < 3; ++scale)
    for (double speed : {-.31, .17, .73})
    for (int direction = 0; direction < 3; ++direction) {
        const double rho = 1.1 * (scale+1), e = 2.5 * (scale+1);
        const FluidVector base{rho, rho*speed, rho*.2, rho*-.1,
                              rho*(e+.5*(speed*speed+.2*.2+.1*.1))};
        const auto left = rotate(base, direction), right = reflect(left, direction);
        FluidVector flux{};
        FluxHLLC<PCMReconstruction>::compute_face_flux(left, right, nullptr, nullptr,
            0, IdealGasView{}, direction, 0., flux, nullptr);
        // Rotate the flux back to x; division by its zero mass flux is invalid.
        const auto local = rotate(flux, (3-direction)%3);
        const double transport = std::max({std::abs(local.rho), std::abs(local.mom_v),
                                           std::abs(local.mom_w), std::abs(local.eng)});
        result.stationary_transport = std::isfinite(transport)
            ? std::max(result.stationary_transport, transport)
            : std::numeric_limits<double>::infinity();
    }
    // A few energy ulps must not change the acoustic relation by O(1).
    // Cover dimensional scales above/below the near-zero guard, and recover
    // internal energy from conservative states as the actual flux does.
    // Both expanding and compressing velocity jumps have nontrivial Roe
    // kinetic averaging, so this is not only a stationary-state check.
    for (double energy : {1e-4, 1., 1e8, 1e19})
    for (double velocity_ratio : {0., -1., 1.})
    for (int ulps : {1, 2, 4}) {
        double adjacent_energy = energy;
        for (int i = 0; i < ulps; ++i)
            adjacent_energy = std::nextafter(adjacent_energy,
                std::numeric_limits<double>::infinity());
        const double speed = velocity_ratio * std::sqrt(energy);
        const FluidVector left{1., -speed, 0., 0., energy + .5*speed*speed};
        const FluidVector right{1., speed, 0., 0., adjacent_energy + .5*speed*speed};
        const IdealGasView eos{};
        const double el = eos_utils::extract_specific_internal_energy(left);
        const double er = eos_utils::extract_specific_internal_energy(right);
        const double pl = eos.get_pressure(left, nullptr), pr = eos.get_pressure(right, nullptr);
        const auto state = calc_glaister_state(left, right, pl, pr, el, er,
            left.eng + pl, right.eng + pr, nullptr, nullptr, 0, nullptr, eos);
        const double kinetic = .5 * (state.u_hat*state.u_hat
            + state.v_hat*state.v_hat + state.w_hat*state.w_hat);
        const double expected_c2 = (eos.global_gamma - 1.) * (state.H_hat - kinetic);
        result.ideal_sound_speed = std::max(result.ideal_sound_speed,
            scaled_error(state.c_hat * state.c_hat, expected_c2));
    }
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
            result.rotation = std::max(result.rotation,
                rotation_error(left, right, xl, xr, count, eos, direction));
            result.reflection = std::max({result.reflection,
                reflection_error<FluxHLL<PCMReconstruction>>(left, right, xl, xr, count, eos, direction),
                reflection_error<FluxHLLC<PCMReconstruction>>(left, right, xl, xr, count, eos, direction),
                reflection_error<FluxRoe<PCMReconstruction>>(left, right, xl, xr, count, eos, direction)});
        }
        if (!mixture) {
            const double pl = eos.get_pressure(left, xl), pr = eos.get_pressure(right, xr);
            const double hl = (left.eng + pl) / rl, hr = (right.eng + pr) / rr;
            const auto state = calc_glaister_state(left, right, pl, pr, el, er, hl, hr,
                                                  xl, xr, 0, nullptr, eos);
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
