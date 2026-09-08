/**
 * @brief Check a real generated network's detailed balance and NSE burn routes.
 *
 * Build with ARCH_TEST_NETWORK_HEADER, ARCH_TEST_NETWORK_TYPE and
 * ARCH_TEST_GENERATED_NAMESPACE selecting one unmodified generated package.
 * One-way flows call the package's actual rate and species algebra; matching
 * forward/reverse names supplies the independent detailed-balance constraint.
 */
#include ARCH_TEST_NETWORK_HEADER
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/linalg/DenseWrap.h"
#include <array>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>

using Network = ARCH_TEST_NETWORK_TYPE;
namespace generated = ARCH_TEST_GENERATED_NAMESPACE;
constexpr int count = Network::NUM_SPECIES;
constexpr int equations = Network::ODE_NEQ;
using State = std::array<double, equations>;

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct ConstantCv {
    double cv = 1.0e9;
    double get_eint_from_T(double, double t, const double*) const { return cv * t; }
    double get_cv(double, double, const double*) const { return cv; }
    double get_eta(double, double, const double*) const { return 0.0; }
    template <int N> void get_cv_gradient(double, double, const double*, double* values) const {
        for (int i = 0; i < N; ++i) values[i] = 0.0;
    }
    template <int N> void get_energy_composition_gradient(double, double, const double*, double* values) const {
        for (int i = 0; i < N - 1; ++i) values[i] = 0.0;
    }
    template <int N> void get_energy_composition_hessian_action(
        double, double, const double*, const double*, double* values) const {
        for (int i = 0; i < N; ++i) values[i] = 0.0;
    }
};

BurnConfigView settings()
{
    BurnConfig value;
    value.use_burn = true;
    value.use_nse = true;
    value.nseTempThreshold = 4.5e9;
    value.nseDensThreshold = 1.0e6;
    value.odeconfig.rtol = 1.0e-9;
    value.odeconfig.atol = 1.0e-14;
    value.odeconfig.max_substeps = 10000;
    return make_burn_config_view(value);
}

double charge(const State& state)
{
    arch::math::CompensatedSum value;
    for (int i = 0; i < count; ++i) value.add(state[i] * Network::zion(i) / Network::aion(i));
    return value.value();
}

State uniform(double temperature)
{
    State state{};
    for (int i = 0; i < count; ++i) state[i] = 1.0 / count;
    state[count] = temperature;
    return state;
}

std::array<double, count> one_way_flow(const generated::burn_t& burn,
                                      const generated::rate_t& rates, int selected)
{
    generated::Array1D<double, 1, generated::NumSpec> molar{}, rhs{};
    generated::Array1D<double, 1, generated::Rates::NumRates> isolated{};
    for (int i = 0; i < count; ++i) molar(i + 1) = burn.xn[i] / Network::aion(i);
    isolated(selected) = rates.screened_rates(selected);
    generated::rhs_nuc(burn, rhs, molar, isolated);
    std::array<double, count> result{};
    for (int i = 0; i < count; ++i) result[i] = rhs(i + 1) * Network::aion(i);
    return result;
}

void detailed_balance(double temperature, double rho)
{
    const State initial = uniform(temperature);
    State state = initial;
    double energy = 0.0;
    require(NSESolver<Network>::solve(temperature, rho, charge(initial),
                                      initial.data(), state.data(), energy), "real network NSE solve failed");
    generated::burn_t burn{};
    burn.rho = rho; burn.T = temperature;
    for (int i = 0; i < count; ++i) burn.xn[i] = state[i];
    generated::compute_ye(burn);
    generated::rate_t rates{};
    generated::evaluate_rates<0>(burn, rates);
    std::array<double, count> total_scale{};
    double worst_pair = 0.0;
    int pairs = 0;
    for (int i = 1; i <= generated::Rates::NumRates; ++i) {
        const std::string& name = generated::Rates::rate_names.at(i);
        if (!name.ends_with("_reaclib")) continue;
        const std::string base = name.substr(0, name.size() - std::string("_reaclib").size());
        const auto separator = base.find("_to_");
        require(separator != std::string::npos, "unrecognized generated forward-rate name");
        const std::string reversed = base.substr(separator + 4) + "_to_"
                                   + base.substr(0, separator) + "_derived";
        int inverse = 0;
        for (int j = 1; j <= generated::Rates::NumRates; ++j)
            if (generated::Rates::rate_names.at(j) == reversed) inverse = j;
        require(inverse != 0, "generated reverse reaction is missing");
        const auto forward = one_way_flow(burn, rates, i);
        const auto reverse = one_way_flow(burn, rates, inverse);
        for (int species = 0; species < count; ++species) {
            const double scale = std::abs(forward[species]) + std::abs(reverse[species]);
            total_scale[species] += scale;
            if (scale > 0.0)
                worst_pair = std::max(worst_pair, std::abs(forward[species] + reverse[species]) / scale);
        }
        ++pairs;
    }
    require(2 * pairs == generated::Rates::NumRates, "rate-pair coverage is incomplete");
    std::array<double, count> rhs{};
    Network::eval_rhs(state.data(), rho, 0.0, rhs.data(), energy);
    double worst_rhs = 0.0;
    for (int i = 0; i < count; ++i) {
        require(std::isfinite(rhs[i]) && total_scale[i] > 0.0, "invalid or inactive detailed-balance witness");
        worst_rhs = std::max(worst_rhs, std::abs(rhs[i]) / total_scale[i]);
    }
    std::cout << "BALANCE T=" << temperature << " rho=" << rho << " pairs=" << pairs
              << " pair_residual=" << worst_pair << " rhs_residual=" << worst_rhs << '\n';
    require(worst_pair <= 1.0e-10 && worst_rhs <= 1.0e-10,
            "NSE composition disagrees with generated detailed balance");
}

template <template <class, class, class> class Method>
void method_cases(const char* name)
{
    using Solver = Method<Network, DenseMatrixData<equations>, DenseLUSolver>;
    static_assert(equations <= 31, "this focused trajectory targets the compact test network");
    constexpr double rho = 1.0e7;
    const auto config = settings();
    const ConstantCv eos{};
    State initial = uniform(5.0e9), state = initial;
    double next_dt = 1.0e-6;
    const auto projected = Solver::integrate_report(
        state.data(), rho, next_dt, eos, config, next_dt);
    require(projected.status == BurnOdeStatus::NseSuccess && projected.nse_attempts == 1,
            "generated network did not enter the selected NSE branch");
    const double source = OdeMath::integrated_composition_energy<Network>(state.data(), initial.data());
    const double closure = std::abs(eos.cv * (state[count] - initial[count]) - source)
                         / std::max(eos.cv * state[count], 1.0);
    require(closure <= 1.0e-12 && source == projected.energy_change,
            "generated NSE energy handoff violates the shared first law");
    require(std::abs(charge(state) - charge(initial)) <= 1.0e-12,
            "generated NSE handoff changed charge");

    // An initially tightly bound mixture just above the threshold requires a
    // colder equilibrium. Failed projection must return to the actual ODE.
    int most_bound = 0;
    for (int i = 1; i < count; ++i)
        if (Network::binding_energy(i) / Network::aion(i)
            > Network::binding_energy(most_bound) / Network::aion(most_bound)) most_bound = i;
    initial = {};
    initial[most_bound] = 1.0;
    initial[count] = 4.500000001e9;
    state = initial;
    std::array<double, count> rhs{};
    double heating = 0.0, largest_rate = 1.0;
    Network::eval_rhs(state.data(), rho, 0.0, rhs.data(), heating);
    for (double value : rhs) largest_rate = std::max(largest_rate, std::abs(value));
    const double interval = std::min(1.0e-12, 1.0e-8 / largest_rate);
    next_dt = interval;
    const auto fallback = Solver::integrate_report(
        state.data(), rho, interval, eos, config, next_dt);
    std::cout << "METHOD " << name << " closure=" << closure
              << " fallback_status=" << static_cast<int>(fallback.status)
              << " nse_failures=" << fallback.nse_failures
              << " steps=" << fallback.attempted_substeps << " dt=" << interval << '\n';
    require(fallback.status == BurnOdeStatus::OdeSuccess && fallback.nse_failures > 0
            && fallback.attempted_substeps > 0, "failed NSE did not continue with ordinary burning");
    double change = 0.0;
    for (int i = 0; i < count; ++i) change = std::max(change, std::abs(state[i] - initial[i]));
    require(change > 1.0e-15 && std::isfinite(fallback.energy_change), "fallback did not resolve real composition evolution");
    const double fallback_source = OdeMath::integrated_composition_energy<Network>(state.data(), initial.data());
    const double thermal_scale = eos.cv * initial[count];
    require(std::abs(eos.cv * (state[count] - initial[count]) - fallback.energy_change)
                <= 5.0e-15 * thermal_scale
            && std::abs(fallback_source - fallback.energy_change) <= 5.0e-15 * thermal_scale,
            "ordinary fallback lost the common nuclear/thermal energy accounting");
    require(std::abs(charge(state) - charge(initial)) <= 1.0e-12,
            "ordinary fallback changed charge");

    // The capability choice does not invent a new entry criterion: all three
    // ODE policies retain the same strict density and temperature thresholds.
    for (const auto boundary : {std::array<double, 2>{4.4e9, rho},
                                std::array<double, 2>{5.0e9, config.nseDensThreshold}}) {
        state = initial;
        state[count] = boundary[0];
        next_dt = 1.0e-20;
        const auto gated = Solver::integrate_report(
            state.data(), boundary[1], next_dt, eos, config, next_dt);
        require(gated.status == BurnOdeStatus::OdeSuccess && gated.nse_attempts == 0,
                "generated network bypassed the configured NSE threshold");
    }
}
} // namespace

int main()
{
    try {
        static_assert(Network::SUPPORTS_NSE);
        std::cout << std::setprecision(17) << Network::NETWORK_NAME << '\n';
        for (double temperature : {4.5e9, 5.0e9, 7.0e9})
            for (double rho : {1.0e6, 1.0e7, 1.0e9}) detailed_balance(temperature, rho);
        method_cases<Solver_BE_NR>("BE_NR");
        method_cases<Solver_BD>("BD");
        method_cases<Solver_ROS4>("ROS4");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
