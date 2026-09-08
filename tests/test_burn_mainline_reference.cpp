#include "core/FileFingerprint.h"
#include "numerics/burnsolver/Networks.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "physics/eos/HelmEos.h"
#include "fixtures/BurnMainlineReference.h"
#include "fixtures/BurnTimeReference.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
namespace Ref = BurnMainlineReference;

template<class Action>
void require_rejected(Action action, const std::string& label)
{
    try {
        action();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("reference checker accepted " + label);
}

// Test the test: physics drift, a no-op, non-finite state, and changes to the
// time-step/energy policy must not pass just because both backends agree.
template<class Net>
void check_rejection_contract(const Ref::Reference& reference)
{
    auto validate = [&](const auto& state, double dt, double old_e, double new_e) {
        Ref::validate(reference, state.data(), Net::NUM_SPECIES, dt, old_e, new_e);
    };
    const double dt = reference.dt_recommended;
    const double old_e = reference.eint_old;
    const double new_e = reference.eint_new;
    validate(reference.state, dt, old_e, new_e);

    auto state = reference.state;
    // Preserve total mass: this must be caught by the immutable species values.
    const double perturbation = 10.0 * std::max(
        Ref::state_budget(state[0]), Ref::state_budget(state[1]));
    state[0] += perturbation;
    state[1] -= perturbation;
    require_rejected([&] { validate(state, dt, old_e, new_e); }, "species drift");
    state = reference.state;
    state[Net::NUM_SPECIES] += 10.0 * Ref::state_budget(state[Net::NUM_SPECIES]);
    require_rejected([&] { validate(state, dt, old_e, new_e); }, "temperature drift");
    state = reference.state;
    state[0] = std::numeric_limits<double>::quiet_NaN();
    require_rejected([&] { validate(state, dt, old_e, new_e); }, "NaN state");
    state = Ref::make_state<Net>(reference.variant);
    require_rejected([&] { validate(state, dt, old_e, old_e); }, "a burn no-op");
    require_rejected([&] { validate(reference.state, dt * 1.001, old_e, new_e); },
                     "recommended-dt drift");
    require_rejected([&] {
        validate(reference.state, dt, old_e, old_e + 1.01 * (new_e - old_e));
    }, "1% integrated burn-energy drift");
}

template<class Net>
void check_network(const std::string& name, int variant, const std::string& path)
{
    SpeciesManager species;
    Net::RegisterSpecies(species);
    HelmEos eos(path, &species);
    BurnTimeReference::check_route<Net, Solver_BE_NR>((name + ".be_nr").c_str(), variant, eos);
    BurnTimeReference::check_route<Net, Solver_BD>((name + ".bd").c_str(), variant, eos);
    BurnTimeReference::check_route<Net, Solver_ROS4>((name + ".ros4").c_str(), variant, eos);
    for (const char* solver : {"be_nr", "bd", "ros4"})
        check_rejection_contract<Net>(Ref::find(name + "." + solver));
    const auto& reference = BurnTimeReference::find(name);
    const auto initial = Ref::make_state<Net>(variant);
    const double old_e = eos.get_eint_from_T(Ref::kRho, initial[Net::NUM_SPECIES], initial.data());
    const double new_e = eos.get_eint_from_T(Ref::kRho, reference.state[Net::NUM_SPECIES], reference.state.data());
    auto validate = [&](const auto& state, double dt, double energy) {
        BurnTimeReference::validate(reference, state.data(), dt, old_e, energy, eos);
    };
    validate(reference.state, Ref::kDt, new_e);
    auto perturbed = reference.state;
    perturbed[0] += 1.0e-6;
    perturbed[1] -= 1.0e-6;
    require_rejected([&] { validate(perturbed, Ref::kDt, new_e); }, "time-reference species drift");
    perturbed = reference.state;
    perturbed[Net::NUM_SPECIES] *= 1.001;
    require_rejected([&] { validate(perturbed, Ref::kDt, new_e); }, "time-reference temperature drift");
    perturbed = reference.state;
    perturbed[0] = std::numeric_limits<double>::quiet_NaN();
    require_rejected([&] { validate(perturbed, Ref::kDt, new_e); }, "time-reference NaN");
    require_rejected([&] { validate(initial, Ref::kDt, old_e); }, "time-reference burn no-op");
    require_rejected([&] { validate(reference.state, Ref::kDt, old_e + 1.1 * (new_e - old_e)); },
                     "time-reference heating drift");
    require_rejected([&] { validate(reference.state, 0.0, new_e); }, "time-reference invalid next step");
    std::cout << name << ": independent time reference and historical checker controls passed\n";
}

// Read-only interface for independent SciPy TIME integrators. This binary
// already owns these types; no second library or reaction/EOS implementation.
template<class Net>
void serve_rhs(const std::string& path)
{
    const auto& reference = BurnTimeReference::find(Net::NETWORK_NAME);
    SpeciesManager species;
    Net::RegisterSpecies(species);
    HelmEos eos(path, &species);
    auto state = Ref::make_state<Net>(reference.variant);
    std::array<double, Net::ODE_NEQ> rhs{};
    std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
              << std::scientific;
    std::cout << "BURN_RHS_READY " << Net::ODE_NEQ << ' ' << Ref::kRho << ' ' << Ref::kDt << '\n';
    for (int i = 0; i < Net::ODE_NEQ; ++i) std::cout << state[i] << ' ';
    std::cout << '\n';
    for (int i = 0; i < Net::ODE_NEQ; ++i) std::cout << reference.state[i] << ' ';
    std::cout << '\n';
    for (int i = 0; i < Net::NUM_SPECIES; ++i) std::cout << Net::aion(i) << ' ';
    std::cout << '\n';
    for (int i = 0; i < Net::NUM_SPECIES; ++i) std::cout << Net::zion(i) << ' ';
    std::cout << '\n' << std::flush;
    while (std::cin >> state[0]) {
        for (int i = 1; i < Net::ODE_NEQ; ++i)
            if (!(std::cin >> state[i])) throw std::runtime_error("incomplete RHS input");
        for (int i = 0; i < Net::ODE_NEQ; ++i)
            if (!std::isfinite(state[i])) throw std::runtime_error("nonfinite RHS input");
        const auto burn = OdeMath::eval_burn_rhs<Net>(state.data(), Ref::kRho, eos, rhs.data());
        for (double value : rhs) std::cout << value << ' ';
        std::cout << eos.get_eint_from_T(Ref::kRho, state[Net::NUM_SPECIES], state.data())
                  << ' ' << burn.energy << '\n' << std::flush;
    }
    if (!std::cin.eof()) throw std::runtime_error("invalid RHS request");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        const std::string path = std::string(ARCH_SOURCE_DIR)
            + "/EOS_toolkit/tables/helmholtz/helm_table.dat";
        if (arch::core::file_sha256(path) != Ref::kTableSha256)
            throw std::runtime_error("frozen-main Helmholtz table SHA-256 mismatch");
        if (argc == 3 && std::string(argv[1]) == "--rhs") {
            const std::string name = argv[2];
            if (name == NetAprox13::NETWORK_NAME) serve_rhs<NetAprox13>(path);
            else if (name == NetAprox19::NETWORK_NAME) serve_rhs<NetAprox19>(path);
            else if (name == NetAprox21::NETWORK_NAME) serve_rhs<NetAprox21>(path);
            else if (name == NetIso7::NETWORK_NAME) serve_rhs<NetIso7>(path);
            else throw std::runtime_error("unknown RHS network: " + name);
            return 0;
        }
        if (argc != 1) throw std::runtime_error("usage: arch_burn_mainline_reference [--rhs NETWORK]");
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
        check_network<NetAprox13>("aprox13", 0, path);
        check_network<NetAprox19>("aprox19", 1, path);
        check_network<NetAprox21>("aprox21", 2, path);
        check_network<NetIso7>("iso7", 3, path);
        std::cout << "12 burn routes passed independent time-integration references; "
                  << "historical data from main " << Ref::kMainCommit << " preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
