#include "core/problem/InitialStateConversion.h"
#include "core/config/RuntimeParams.h"
#include "physics/eos/IdealGas.h"
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
int main() {
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
    const auto config = RuntimeParams::LoadText("nblockx1=2\nnblockx2=0\nnblockx3=0\nx_pos=.3\n");
    require(config.grid.dim == 1 && config.grid.nblockx1 == 2, "memory configuration");
    require(config.Get<double>("x_pos", .5) == .3, "custom memory parameter");
    require(config.Get<double>("absent", .7) == .7, "custom fallback");
    std::cout << "Initial conversion and in-memory parser passed\n";
}
