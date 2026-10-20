#include "core/problem/InitialStateConversion.h"
#include "physics/eos/IdealGas.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool good, const char *message) {
    if (!good) throw std::runtime_error(message);
}
struct DensityBoundedIdealGas : IdealGas {
    using IdealGas::IdealGas;
    double get_temperature(double rho, double e, const double* fractions) const {
        return rho <= 3.0 ? IdealGas::get_temperature(rho, e, fractions)
                          : std::numeric_limits<double>::quiet_NaN();
    }
};

// Exact binary states independently fix the requested caloric state and K.
// This is an initialization arithmetic check, not a physical high-velocity
// model or a qualification of subnormal-density evolution on a full grid.
static void test_extreme_ideal_initialization() {
    SpeciesManager material;
    material.add_species("binary", 1., 1., 2., 1.);
    IdealGas eos(2., material);
    NumericsConfig limits;
    limits.sml_rho = std::numeric_limits<double>::denorm_min();
    limits.max_eint = std::numeric_limits<double>::max();
    const arch::state::Bounds bounds{limits.sml_rho, limits.min_eint, limits.max_eint};
    struct Reference {
        double rho, velocity, internal, momentum, pressure;
        long double kinetic, energy;
    };
    const std::array<Reference, 2> references{{
        {std::ldexp(1., -1074), std::ldexp(1., 20), std::ldexp(1., 42),
         std::ldexp(1., -1054), std::ldexp(1., -1032),
         std::ldexp(3.L, -1035), std::ldexp(11.L, -1035)},
        {std::ldexp(1., -100), std::ldexp(1., 512), std::ldexp(1., 1020),
         std::ldexp(1., 412), std::ldexp(1., 920),
         std::ldexp(3.L, 923), std::ldexp(25.L, 920)}
    }};
    for (const auto& reference : references) {
        PrimitiveData data{};
        data.rho = reference.rho;
        data.u = data.v = data.w = reference.velocity;
        data.p = reference.pressure;
        data.mass_fractions = {1.};
        for (bool temperature_input : {false, true}) {
            if (temperature_input) data.SetTemperature(reference.internal);
            arch::state::Repair receipt;
            const auto state = ProblemHelper::detail::InitialConservedState(data, eos, limits, &receipt);
            require(receipt.status == arch::state::Status::valid,
                    "representable extreme Init must not need an energy or density repair");
            require(state.rho == reference.rho
                    && state.mom_u == reference.momentum
                    && state.mom_v == reference.momentum
                    && state.mom_w == reference.momentum,
                    "extreme Init changed independently specified conserved momentum");
            require(state.eng == static_cast<double>(reference.energy),
                    "extreme Init lost the independently specified total energy");
            const auto thermal = arch::state::recover(state);
            require(thermal.status == arch::state::Status::valid
                    && thermal.kinetic == static_cast<double>(reference.kinetic)
                    && thermal.internal == reference.internal,
                    "extreme Init changed the requested resolvable caloric state");
            require(arch::state::validate_eos(state, data.mass_fractions.data(), 1, bounds, eos)
                        == arch::state::Status::valid
                    && eos.get_temperature(state.rho, thermal.internal, data.mass_fractions.data())
                        == reference.internal
                    && eos.get_pressure(state, data.mass_fractions.data()) == reference.pressure,
                    "actual IdealGas rejected or changed a representable extreme Init state");
            const double sound = eos.get_sound_speed(state, reference.pressure,
                                                     data.mass_fractions.data());
            require(std::isfinite(sound) && sound > 0.
                    && sound == std::sqrt(std::ldexp(reference.internal, 1)),
                    "actual IdealGas sound speed differs from the independent gamma=2 state");
        }
    }

    // K alone is finite, but rho*u cannot be stored in FluidVector. This
    // primitive must fail through the existing finite-conservative gate.
    PrimitiveData invalid{};
    invalid.rho = std::ldexp(1.5, 1023);
    invalid.u = 1.5;
    invalid.p = std::ldexp(invalid.rho, -6);
    invalid.mass_fractions = {1.};
    const long double finite_kinetic = .5L * static_cast<long double>(invalid.rho) * 2.25L;
    require(std::isfinite(static_cast<double>(finite_kinetic))
            && !std::isfinite(invalid.rho * invalid.u)
            && !std::isfinite(eos_utils::calc_kinetic_energy(invalid.rho, invalid.u, 0., 0.)),
            "unrepresentable conservative momentum was silently replaced by a finite K");
    for (bool temperature_input : {false, true}) {
        if (temperature_input) invalid.SetTemperature(std::ldexp(1., -6));
        bool rejected = false;
        try { (void)ProblemHelper::detail::InitialConservedState(invalid, eos, limits); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Init accepted an unrepresentable conserved momentum");
    }
    std::cout << "INITIAL_RANGE_PASS states=4 actual_ideal_eos=true no_repair=true\n";
}

int main() {
    test_extreme_ideal_initialization();
    SpeciesManager empty;
    IdealGas air(1.4, empty);
    require(std::abs(air.get_eint_from_T(1, 300, nullptr) - 2.154e9) < 1e-5, "CGS air Cv times temperature");
    require(std::abs(air.get_temperature(1, 2.154e9, nullptr) - 300) < 1e-10, "CGS energy to kelvin");
    require(std::abs(air.get_pressure_from_rho_T(1, 300, nullptr) - 8.616e8) < 1e-4, "CGS ideal pressure from density and temperature");
    SpeciesManager species;
    species.add_species("test", 1, 1, 1.4, 2.0);
    IdealGas eos(1.4, species);
    PrimitiveData data{};
    data.rho = 2;
    data.u = 3;
    data.v = 4;
    data.p = 5;
    data.mass_fractions = {1};
    const auto pressure_state = ProblemHelper::detail::InitialConservedState(data, eos);
    require(std::abs(pressure_state.eng - 37.5) < 1e-12, "pressure initial energy");
    require(pressure_state.mom_u == 6 && pressure_state.mom_v == 8, "initial momentum");
    data.SetTemperature(10);
    const auto thermal_state = ProblemHelper::detail::InitialConservedState(data, eos);
    require(std::abs(thermal_state.eng - 65) < 1e-12, "temperature must own thermal energy");
    NumericsConfig limits;
    limits.sml_rho = 4.0;
    arch::state::Repair repair;
    const auto repaired = ProblemHelper::detail::InitialConservedState(data, eos, limits, &repair);
    require(repaired.rho == 4.0 && repaired.eng == 130.0 &&
            repair.status == arch::state::Status::repaired, "valid initial repair");
    DensityBoundedIdealGas bounded(1.4, species);
    bool invalid_repair = false;
    try { (void)ProblemHelper::detail::InitialConservedState(data, bounded, limits); }
    catch (const std::runtime_error&) { invalid_repair = true; }
    require(invalid_repair, "a repair must not publish a state outside the EOS domain");
    data.temperature = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try { (void)ProblemHelper::detail::InitialConservedState(data, eos); }
    catch (const std::runtime_error &) { rejected = true; }
    require(rejected, "invalid temperature conversion must fail");
    std::cout << "Initial state conversion passed\n";
}
