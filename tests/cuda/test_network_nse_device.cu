#include "numerics/linalg/DenseWrap.h"
#include "physics/network/aprox13/NetAprox13.h"
#include "physics/network/aprox19/NetAprox19.h"
#include "physics/network/aprox21/NetAprox21.h"
#include "physics/network/iso7/NetIso7.h"
#include "physics/nse/nse_solver.h"

#if defined(__CUDACC__)
#include <cuda_runtime.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

constexpr int kInvalidNseCases = 17;
#if defined(__CUDACC__) || defined(ARCH_C2_INSTRUMENTED_HOST)
constexpr bool kInstructionEquivalentHost = false;
#else
constexpr bool kInstructionEquivalentHost = true;
#endif

struct IterationBoundaryNetwork
{
    static constexpr int NUM_SPECIES = 2;
    inline static constexpr std::array<double, NUM_SPECIES> AION{0.1, 0.1};
    inline static constexpr std::array<double, NUM_SPECIES> ZION{0.05, 0.05};
    inline static constexpr std::array<double, NUM_SPECIES> BINDING_E{0.0, 0.0};
    inline static constexpr std::array<double, NUM_SPECIES> SPIN{
        1.0, 7.225973768125749e86}; // exp(200), requiring 100 capped updates.
    static constexpr double ENERGY_CONVERSION = 1.0;
    static constexpr double NSE_ENERGY_CONVERSION = 1.0;

    ARCH_INLINE static constexpr double aion(int) { return 0.1; }
    ARCH_INLINE static constexpr double zion(int) { return 0.05; }
    ARCH_INLINE static constexpr double binding_energy(int) { return 0.0; }
    ARCH_INLINE static constexpr double spin_weight(int i)
    {
        return i == 0 ? 1.0 : (i == 1 ? 7.225973768125749e86 : 0.0);
    }
};

struct NsePolicyProbe
{
    bool converged{};
    bool output_preserved{};
    double enuc{};
    int line_search_limit{};
};

inline constexpr NsePolicyProbe kIterationBoundaryAuthority{false, true, 0.0,
                                                            24};

ARCH_INLINE NsePolicyProbe evaluate_nse_iteration_boundary()
{
    const double old_x[2]{0.5, 0.5};
    double output[2]{91.0, 92.0};
    double enuc = -1.0;
    NsePolicyProbe result{};
    result.converged = NSESolver<IterationBoundaryNetwork>::solve(
        5.0e9, 1.0e7, 0.5, old_x, output, enuc);
    result.output_preserved = output[0] == 91.0 && output[1] == 92.0;
    result.enuc = enuc;
    result.line_search_limit =
        NSESolver<IterationBoundaryNetwork>::line_search_limit();
    return result;
}

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message)
{
    if (!condition) fail(message);
}

void compare_field(double actual, double expected, double abs_tol,
                   double rel_tol,
                   const char* field, int index)
{
    if (std::isnan(expected)) {
        require(std::isnan(actual), std::string(field) + " NaN class at "
                                    + std::to_string(index));
        return;
    }
    if (std::isinf(expected)) {
        require(actual == expected, std::string(field) + " infinity at "
                                    + std::to_string(index));
        return;
    }
    require(std::isfinite(actual), std::string(field) + " finite class at "
                                   + std::to_string(index));
    if (expected == 0.0) {
        require(actual == expected && std::signbit(actual) == std::signbit(expected),
                std::string(field) + " signed zero at " + std::to_string(index));
        return;
    }
    const double error = std::abs(actual - expected);
    const double scale = std::abs(expected);
    const double budget = std::max(abs_tol, rel_tol * scale);
    if (error > budget) {
        std::cerr << std::setprecision(17) << field << '[' << index
                  << "] device=" << actual << " host=" << expected
                  << " absolute_error=" << error
                  << " relative_error=" << error / scale
                  << " absolute_budget=" << abs_tol
                  << " relative_budget=" << rel_tol << '\n';
        fail(std::string(field) + " tolerance at " + std::to_string(index));
    }
}

void compare_field(double actual, double expected, double rel_tol,
                   const char* field, int index)
{
    compare_field(actual, expected, 0.0, rel_tol, field, index);
}

struct DenseProbe
{
    bool solve_ok{};
    bool factor_ok{};
    bool below_ok{};
    bool exact_ok{};
    bool above_ok{};
    double solution[3]{};
    double factor_solution[3]{};
    double below_rhs{};
    double exact_rhs{};
    double above_rhs{};
};

ARCH_INLINE DenseProbe evaluate_dense()
{
    DenseProbe out{};
    DenseMatrixData matrix{};
    matrix.set(1, 1, 3.0);  matrix.set(1, 2, 2.0);  matrix.set(1, 3, -1.0);
    matrix.set(2, 1, 2.0);  matrix.set(2, 2, -2.0); matrix.set(2, 3, 4.0);
    matrix.set(3, 1, -1.0); matrix.set(3, 2, 0.5);  matrix.set(3, 3, -1.0);
    double rhs[BurnLimits::MAX_ODE_NEQ]{};
    rhs[0] = 1.0; rhs[1] = -2.0;
    out.solve_ok = DenseLUSolver::solve<3, BurnLimits::MAX_ODE_NEQ>(matrix, rhs);
    for (int i = 0; i < 3; ++i) out.solution[i] = rhs[i];

    DenseMatrixData factors{};
    factors.set(1, 1, 3.0);  factors.set(1, 2, 2.0);  factors.set(1, 3, -1.0);
    factors.set(2, 1, 2.0);  factors.set(2, 2, -2.0); factors.set(2, 3, 4.0);
    factors.set(3, 1, -1.0); factors.set(3, 2, 0.5);  factors.set(3, 3, -1.0);
    int permutation[BurnLimits::MAX_ODE_NEQ]{};
    double factor_rhs[BurnLimits::MAX_ODE_NEQ]{};
    factor_rhs[0] = 1.0; factor_rhs[1] = -2.0;
    out.factor_ok = DenseLUSolver::factorize<3, BurnLimits::MAX_ODE_NEQ>(
        factors, permutation);
    if (out.factor_ok) {
        DenseLUSolver::solve_with_factors<3, BurnLimits::MAX_ODE_NEQ>(
            factors, permutation, factor_rhs);
    }
    for (int i = 0; i < 3; ++i) out.factor_solution[i] = factor_rhs[i];

    const double pivots[3]{
        std::nextafter(1.0e-20, 0.0),
        1.0e-20,
        std::nextafter(1.0e-20, std::numeric_limits<double>::infinity())};
    bool* statuses[3]{&out.below_ok, &out.exact_ok, &out.above_ok};
    double* outputs[3]{&out.below_rhs, &out.exact_rhs, &out.above_rhs};
    for (int i = 0; i < 3; ++i) {
        DenseMatrixData one{};
        one.set(1, 1, pivots[i]);
        double one_rhs[BurnLimits::MAX_ODE_NEQ]{};
        one_rhs[0] = 7.0;
        *statuses[i] = DenseLUSolver::solve<1, BurnLimits::MAX_ODE_NEQ>(
            one, one_rhs);
        *outputs[i] = one_rhs[0];
    }
    return out;
}

template <typename Network> struct NetworkTraits;

template <typename Network>
ARCH_INLINE double network_binding_energy(int i)
{
#if defined(__CUDA_ARCH__)
    return Network::binding_energy(i);
#else
    return Network::BINDING_E[i];
#endif
}

template <typename Network>
ARCH_INLINE double network_spin_weight(int i)
{
#if defined(__CUDA_ARCH__)
    return Network::spin_weight(i);
#else
    return Network::SPIN[i];
#endif
}

template <typename Network>
ARCH_INLINE double network_energy_weight(int i)
{
#if defined(__CUDA_ARCH__)
    return Network::energy_weight(i);
#else
    return Network::ENERGY_WEIGHTS[i];
#endif
}

template <> struct NetworkTraits<NetAprox13>
{
    static constexpr int rate_count = timmes_aprox13_detail::nrat;
    template <typename Scalar, typename Accessor>
    ARCH_INLINE static void rates(const Scalar* y, double rho, double,
                                  double temperature_value,
                                  const Scalar& temperature,
                                  std::array<Scalar, rate_count>& result)
    {
        NetAprox13::template fill_screened_rates<Scalar, Accessor>(
            y, rho, temperature_value, temperature, result);
    }
};

template <> struct NetworkTraits<NetAprox19>
{
    static constexpr int rate_count = timmes_aprox19_detail::nrat;
    template <typename Scalar, typename Accessor>
    ARCH_INLINE static void rates(const Scalar* y, double rho, double eta,
                                  double temperature_value,
                                  const Scalar& temperature,
                                  std::array<Scalar, rate_count>& result)
    {
        NetAprox19::template fill_screened_rates<Scalar, Accessor>(
            y, rho, eta, temperature_value, temperature, result);
    }
};

template <> struct NetworkTraits<NetAprox21>
{
    static constexpr int rate_count = timmes_aprox21_detail::nrat;
    template <typename Scalar, typename Accessor>
    ARCH_INLINE static void rates(const Scalar* y, double rho, double eta,
                                  double temperature_value,
                                  const Scalar& temperature,
                                  std::array<Scalar, rate_count>& result)
    {
        NetAprox21::template fill_screened_rates<Scalar, Accessor>(
            y, rho, eta, temperature_value, temperature, result);
    }
};

template <> struct NetworkTraits<NetIso7>
{
    static constexpr int rate_count = timmes_iso7_detail::nrat;
    template <typename Scalar, typename Accessor>
    ARCH_INLINE static void rates(const Scalar* y, double rho, double,
                                  double temperature_value,
                                  const Scalar& temperature,
                                  std::array<Scalar, rate_count>& result)
    {
        NetIso7::template fill_screened_rates<Scalar, Accessor>(
            y, rho, temperature_value, temperature, result);
    }
};

template <int N, int R>
struct NetworkProbe
{
    double aion[N]{};
    double zion[N]{};
    double binding[N]{};
    double spin[N]{};
    double energy_weight[N]{};
    double rates[R]{};
    double below_temperature_rates[R]{};
    double exact_temperature_rates[R]{};
    double above_temperature_rates[R]{};
    double rhs[N]{};
    double enuc{};
    double jacobian[N * N]{};
    double denuc_dx[N]{};
    double drhs_dt[N]{};
    double denuc_dt{};
    double frozen_rhs[N]{};
    double clamp_rhs[3][N]{};
    double clamp_enuc[3]{};
    bool nse_ok{};
    double nse_x[N]{};
    double nse_enuc{};
    bool nse_invalid_status[kInvalidNseCases]{};
    bool nse_invalid_preserved[kInvalidNseCases]{};
    double nse_invalid_enuc[kInvalidNseCases]{};
    double nse_invalid_x[kInvalidNseCases][N]{};
};

template <typename Network>
ARCH_INLINE auto evaluate_network()
{
    constexpr int N = Network::NUM_SPECIES;
    constexpr int R = NetworkTraits<Network>::rate_count;
    NetworkProbe<N, R> out{};
    double state[N + 1]{};
    double y[N]{};
    const double normalization = 0.5 * N * (N + 1.0);
    for (int i = 0; i < N; ++i) {
        state[i] = (i + 1.0) / normalization;
        y[i] = state[i] / Network::aion(i);
        out.aion[i] = Network::aion(i);
        out.zion[i] = Network::zion(i);
        out.binding[i] = network_binding_energy<Network>(i);
        out.spin[i] = network_spin_weight<Network>(i);
        out.energy_weight[i] = network_energy_weight<Network>(i);
    }
    state[N] = 2.0e9;

    std::array<double, R> rates{};
    NetworkTraits<Network>::template rates<double, timmes::RateValueAccessor>(
        y, 1.0e6, 0.25, state[N], state[N], rates);
    for (int i = 0; i < R; ++i) out.rates[i] = rates[i];
    std::array<double, R> low_rates{};
    const double low_temperature = std::nextafter(1.0e6, 0.0);
    NetworkTraits<Network>::template rates<double, timmes::RateValueAccessor>(
        y, 1.0e6, 0.25, low_temperature, low_temperature, low_rates);
    for (int i = 0; i < R; ++i) out.below_temperature_rates[i] = low_rates[i];
    std::array<double, R> exact_rates{};
    NetworkTraits<Network>::template rates<double, timmes::RateValueAccessor>(
        y, 1.0e6, 0.25, 1.0e6, 1.0e6, exact_rates);
    for (int i = 0; i < R; ++i) out.exact_temperature_rates[i] = exact_rates[i];
    std::array<double, R> above_rates{};
    const double above_temperature = std::nextafter(
        1.0e6, std::numeric_limits<double>::infinity());
    NetworkTraits<Network>::template rates<double, timmes::RateValueAccessor>(
        y, 1.0e6, 0.25, above_temperature, above_temperature, above_rates);
    for (int i = 0; i < R; ++i) out.above_temperature_rates[i] = above_rates[i];

    Network::eval_rhs(state, 1.0e6, 0.25, out.rhs, out.enuc);
    DenseMatrixData jacobian{};
    Network::eval_jacobian(state, 1.0e6, 0.25, jacobian, out.denuc_dx);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            out.jacobian[i * N + j] = jacobian.data[i][j];
    Network::eval_temperature_derivative(
        state, 1.0e6, 0.25, out.drhs_dt, out.denuc_dt);

    using AD = timmes::Dual<N>;
    AD y_ad[N]{};
    AD frozen_dydt[N]{};
    for (int i = 0; i < N; ++i)
        y_ad[i] = AD::variable(y[i], i);
    Network::template molar_rhs_frozen_screening<AD>(
        y_ad, 1.0e6, 0.25, state[N], frozen_dydt);
    for (int i = 0; i < N; ++i)
        out.frozen_rhs[i] = frozen_dydt[i].value * Network::aion(i);

    for (int which = 0; which < 3; ++which) {
        double threshold_state[N + 1]{};
        for (int i = 0; i < N; ++i) threshold_state[i] = state[i];
        threshold_state[N] = state[N];
        threshold_state[0] = which == 0
                           ? std::nextafter(Network::aion(0) * 1.0e-30, 0.0)
                           : (which == 1
                              ? Network::aion(0) * 1.0e-30
                              : std::nextafter(Network::aion(0) * 1.0e-30,
                                               std::numeric_limits<double>::infinity()));
        threshold_state[N - 1] = which == 0
                               ? std::nextafter(Network::aion(N - 1), 0.0)
                               : (which == 1
                                  ? Network::aion(N - 1)
                                  : std::nextafter(Network::aion(N - 1),
                                                   std::numeric_limits<double>::infinity()));
        Network::eval_rhs(threshold_state, 1.0e6, 0.25,
                          out.clamp_rhs[which], out.clamp_enuc[which]);
    }

    double ye = 0.0;
    for (int i = 0; i < N; ++i)
        ye += state[i] * Network::zion(i) / Network::aion(i);
    out.nse_ok = NSESolver<Network>::solve(
        5.0e9, 1.0e7, ye, state, out.nse_x, out.nse_enuc);

    int invalid = 0;
    auto record_invalid = [&](double temperature, double density,
                              double electron_fraction, const double* old_x,
                              bool null_output, int poison_old) {
        double saved = state[0];
        if (poison_old == 1)
            state[0] = std::numeric_limits<double>::quiet_NaN();
        if (poison_old == 2)
            state[0] = std::numeric_limits<double>::infinity();
        if (poison_old == 3)
            state[0] = -2.0e-12;
        if (poison_old == 4)
            state[0] = -1.0e-12;
        if (poison_old == 5)
            state[0] = std::nextafter(-1.0e-12,
                                      -std::numeric_limits<double>::infinity());
        if (poison_old == 6)
            state[0] = std::nextafter(-1.0e-12, 0.0);
        double sentinel[N];
        for (int i = 0; i < N; ++i) sentinel[i] = 4096.0 + i;
        double energy = -7.0;
        out.nse_invalid_status[invalid] = NSESolver<Network>::solve(
            temperature, density, electron_fraction, old_x,
            null_output ? nullptr : sentinel, energy);
        out.nse_invalid_enuc[invalid] = energy;
        bool preserved = true;
        for (int i = 0; i < N; ++i) {
            preserved &= sentinel[i] == 4096.0 + i;
            out.nse_invalid_x[invalid][i] = sentinel[i];
        }
        out.nse_invalid_preserved[invalid] = preserved;
        state[0] = saved;
        ++invalid;
    };
    record_invalid(5.0e9, 1.0e7, ye, nullptr, false, 0);
    record_invalid(5.0e9, 1.0e7, ye, state, true, 0);
    record_invalid(std::numeric_limits<double>::quiet_NaN(), 1.0e7, ye,
                   state, false, 0);
    record_invalid(std::numeric_limits<double>::infinity(), 1.0e7, ye,
                   state, false, 0);
    record_invalid(0.0, 1.0e7, ye, state, false, 0);
    record_invalid(5.0e9, std::numeric_limits<double>::quiet_NaN(), ye,
                   state, false, 0);
    record_invalid(5.0e9, 0.0, ye, state, false, 0);
    record_invalid(5.0e9, 1.0e7, std::numeric_limits<double>::quiet_NaN(),
                   state, false, 0);
    record_invalid(5.0e9, 1.0e7, -1.0e-12, state, false, 0);
    record_invalid(5.0e9, 1.0e7, 1.0 + 1.0e-12, state, false, 0);
    record_invalid(5.0e9, 1.0e7, ye, state, false, 1);
    record_invalid(5.0e9, 1.0e7, ye, state, false, 2);
    record_invalid(5.0e9, -1.0, ye, state, false, 0);
    record_invalid(5.0e9, 1.0e7, ye, state, false, 3);
    record_invalid(5.0e9, 1.0e7, ye, state, false, 4);
    record_invalid(5.0e9, 1.0e7, ye, state, false, 5);
    record_invalid(5.0e9, 1.0e7, ye, state, false, 6);
    return out;
}

template <typename T>
void print_double_array(const char* prefix, const T* values, int count)
{
    for (int i = 0; i < count; ++i)
        std::cout << prefix << '[' << i << "]=" << std::hexfloat
                  << values[i] << '\n';
}

void print_dense(const DenseProbe& value)
{
    std::cout << "dense.solve=" << value.solve_ok
              << "\ndense.factor=" << value.factor_ok
              << "\ndense.below=" << value.below_ok
              << "\ndense.exact=" << value.exact_ok
              << "\ndense.above=" << value.above_ok << '\n';
    print_double_array("dense.solution", value.solution, 3);
    print_double_array("dense.factor_solution", value.factor_solution, 3);
    print_double_array("dense.threshold_rhs", &value.below_rhs, 3);
}

template <int N, int R>
void print_network(const char* name, const NetworkProbe<N, R>& value)
{
    auto emit = [&](const char* field, const double* data, int count) {
        const std::string prefix = std::string(name) + '.' + field;
        print_double_array(prefix.c_str(), data, count);
    };
    emit("aion", value.aion, N); emit("zion", value.zion, N);
    emit("binding", value.binding, N); emit("spin", value.spin, N);
    emit("energy_weight", value.energy_weight, N);
    emit("rates", value.rates, R);
    emit("below_temperature_rates", value.below_temperature_rates, R);
    emit("exact_temperature_rates", value.exact_temperature_rates, R);
    emit("above_temperature_rates", value.above_temperature_rates, R);
    emit("rhs", value.rhs, N); emit("enuc", &value.enuc, 1);
    emit("jacobian", value.jacobian, N * N);
    emit("denuc_dx", value.denuc_dx, N);
    emit("drhs_dt", value.drhs_dt, N); emit("denuc_dt", &value.denuc_dt, 1);
    emit("frozen_rhs", value.frozen_rhs, N);
    emit("clamp_rhs", &value.clamp_rhs[0][0], 3 * N);
    emit("clamp_enuc", value.clamp_enuc, 3);
    std::cout << name << ".nse_ok=" << value.nse_ok << '\n';
    emit("nse_x", value.nse_x, N); emit("nse_enuc", &value.nse_enuc, 1);
    for (int i = 0; i < kInvalidNseCases; ++i) {
        std::cout << name << ".invalid_status[" << i << "]="
                  << value.nse_invalid_status[i] << '\n'
                  << name << ".invalid_preserved[" << i << "]="
                  << value.nse_invalid_preserved[i] << '\n';
    }
    emit("invalid_enuc", value.nse_invalid_enuc, kInvalidNseCases);
    emit("invalid_x", &value.nse_invalid_x[0][0], kInvalidNseCases * N);
}

template <int N, int R>
struct FrozenNetworkAuthority
{
    std::array<std::string_view, N> species_names;
    // Concatenated big-endian hexadecimal IEEE-754 binary64 words, in the
    // field order consumed by check_network_authority.  These literals were
    // characterized from af0bf7b with the checked-in network arrays as host
    // authority; no production helper computes an expected value.
    std::string_view numeric_bits;
    std::uint32_t invalid_status_mask;
    std::uint32_t invalid_preserved_mask;
    bool nse_ok;
    bool charge_degenerate;
    // Per numeric word: abs budget then relative budget, encoded like
    // numeric_bits and frozen at nextafter(4 * measured error, +inf) from
    // BASE-vs-Release-fast-math-NVCC-host/H100-device measurements.
    std::string_view tolerance_bits;
};

constexpr int hex_digit(char value)
{
    return value >= '0' && value <= '9' ? value - '0'
         : value >= 'a' && value <= 'f' ? value - 'a' + 10
         : value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
}

std::uint64_t frozen_bits_at(std::string_view bits, std::size_t index)
{
    require(bits.size() % 16 == 0, "frozen authority hex width");
    require(index < bits.size() / 16, "frozen authority numeric extent");
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < 16; ++i) {
        const int digit = hex_digit(bits[index * 16 + i]);
        require(digit >= 0, "frozen authority hex digit");
        result = (result << 4) | static_cast<std::uint64_t>(digit);
    }
    return result;
}

template <int N, int R>
void check_network_authority(
    const char* name, const NetworkProbe<N, R>& actual,
    const FrozenNetworkAuthority<N, R>& authority, bool exact_host_bits)
{
    constexpr std::size_t expected_values =
        N * N + 30 * N + 4 * R + 23;
    require(authority.numeric_bits.size() == expected_values * 16,
            std::string(name) + ".frozen authority numeric extent");
    if (!exact_host_bits) {
        require(authority.tolerance_bits.size() == expected_values * 32,
                std::string(name) + ".frozen tolerance numeric extent");
    }
    std::size_t cursor = 0;
    auto group = [&](const char* field, const double* values, int count) {
        const std::string full = std::string(name) + ".authority." + field;
        for (int i = 0; i < count; ++i) {
            const std::size_t field_index = cursor++;
            const std::uint64_t expected_bits = frozen_bits_at(
                authority.numeric_bits, field_index);
            const double expected = std::bit_cast<double>(expected_bits);
            if (exact_host_bits) {
                require(std::bit_cast<std::uint64_t>(values[i]) == expected_bits,
                        full + " raw bit at " + std::to_string(i));
            } else {
                const double frozen_abs = std::bit_cast<double>(frozen_bits_at(
                    authority.tolerance_bits, 2 * field_index));
                const double frozen_rel = std::bit_cast<double>(frozen_bits_at(
                    authority.tolerance_bits, 2 * field_index + 1));
                require(frozen_abs >= 0.0 && frozen_rel >= 0.0,
                        full + " frozen tolerance sign at "
                        + std::to_string(i));
                compare_field(values[i], expected, frozen_abs, frozen_rel,
                              full.c_str(), i);
            }
        }
    };

    group("aion", actual.aion, N);
    group("zion", actual.zion, N);
    group("binding", actual.binding, N);
    group("spin", actual.spin, N);
    group("energy_weight", actual.energy_weight, N);
    group("rates", actual.rates, R);
    group("below_temperature_rates", actual.below_temperature_rates, R);
    group("exact_temperature_rates", actual.exact_temperature_rates, R);
    group("above_temperature_rates", actual.above_temperature_rates, R);
    group("rhs", actual.rhs, N);
    group("enuc", &actual.enuc, 1);
    group("jacobian", actual.jacobian, N * N);
    group("denuc_dx", actual.denuc_dx, N);
    group("drhs_dt", actual.drhs_dt, N);
    group("denuc_dt", &actual.denuc_dt, 1);
    group("frozen_rhs", actual.frozen_rhs, N);
    group("clamp_rhs", &actual.clamp_rhs[0][0], 3 * N);
    group("clamp_enuc", actual.clamp_enuc, 3);
    group("nse_x", actual.nse_x, N);
    group("nse_enuc", &actual.nse_enuc, 1);
    group("invalid_enuc", actual.nse_invalid_enuc, kInvalidNseCases);
    group("invalid_x", &actual.nse_invalid_x[0][0], kInvalidNseCases * N);
    require(cursor == expected_values,
            std::string(name) + ".frozen authority cursor");
    require(actual.nse_ok == authority.nse_ok,
            std::string(name) + ".frozen NSE status");
    for (int i = 0; i < kInvalidNseCases; ++i) {
        const bool expected_status =
            (authority.invalid_status_mask & (1u << i)) != 0;
        const bool expected_preserved =
            (authority.invalid_preserved_mask & (1u << i)) != 0;
        require(actual.nse_invalid_status[i] == expected_status,
                std::string(name) + ".frozen invalid status "
                + std::to_string(i));
        require(actual.nse_invalid_preserved[i] == expected_preserved,
                std::string(name) + ".frozen invalid preservation "
                + std::to_string(i));
    }
}

template <typename Network, int N, int R>
void check_species_authority(
    const char* name, const FrozenNetworkAuthority<N, R>& authority)
{
    static_assert(Network::NUM_SPECIES == N);
    double q_min = std::numeric_limits<double>::infinity();
    double q_max = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < N; ++i) {
        require(std::string_view(Network::SPECIES_NAMES[i])
                    == authority.species_names[i],
                std::string(name) + ".frozen species name/order "
                + std::to_string(i));
        if (Network::SPIN[i] > 0.0) {
            const double q = Network::ZION[i] / Network::AION[i];
            q_min = std::min(q_min, q);
            q_max = std::max(q_max, q);
        }
    }
    const bool charge_degenerate =
        q_max - q_min <= 128.0 * std::numeric_limits<double>::epsilon();
    require(charge_degenerate == authority.charge_degenerate,
            std::string(name) + ".frozen NSE normal/degenerate route");
}

inline constexpr FrozenNetworkAuthority<13, 67> kAprox13Authority{
    {"he4", "c12", "o16", "ne20", "mg24", "si28", "s32", "ar36",
     "ca40", "ti44", "cr48", "fe52", "ni56"},
    "40100000000000004028000000000000403000000000000040340000000000004038000000000000403c000000000000"
    "40400000000000004042000000000000404400000000000040460000000000004048000000000000404a000000000000"
    "404c00000000000040000000000000004018000000000000402000000000000040240000000000004028000000000000"
    "402c00000000000040300000000000004032000000000000403400000000000040360000000000004038000000000000"
    "403a000000000000403c000000000000403c4bc89f40a28740570a6d9be4cd75405fe7bd512ec6bd406414bb6ed67770"
    "4068c840b780346e406d91367a0f90974070fc851eb851ec40732b85f06f6944407560e8a71de69b407777a29c779a6b"
    "4079b7810624dd2f407bfb53f7ced917407e400c49ba5e353ff00000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff00000000000003ff00000000000003ff00000000000003b2010d7aef917503b3815400c10b984"
    "3b400cdc2d7141203b440fc40203d1313b481168304c88043b4c12dd0e75467b3b500a9423cd5c753b520bc4967f0d29"
    "3b540ce6f377444b3b560e4cf94a85993b580f5833cc13393b5a105ab3d216cf3b5c115b39a69393407a2be455869dac"
    "3e94e9a0ca8e1cff409f60002bb0036f3df1f47088c3b3e14081744fdadcedff3fca03cc274aed183ee78cde671da1d5"
    "4122bf3ac66328063fbd90f889493eda414e45a2c8ab76283d845964db837712412d12df6daf21233d0a5fcbccd528ef"
    "4141a051bfbe67864209a4c6f1274e1141e524e0c96e69793cfa41377f0bcf0c40e92c8fbec2a5033e5f2970453d08e3"
    "4112aef196f9b56e4220635dce571cd241e6e4e823c56bb23e502c2ccd59b04940d7294ed66049323e75cb3bfd45a424"
    "40f6b6bed32a70f141ee592143037eda41dab67d832eb50a3e82d1fe3ba8bdd74078bf5cedd036683de2bc19e18bc088"
    "40d119bb738cdae7417a3972ae40f63c41b05c009577b8423e70404d6d817cb24085cb9dd334d5393ef0ec40d16d4b8c"
    "4020dcbc3465d1c741e6299b51a5ddad41c0f40465241cca3e64094a85b0bc883fc6140398daaf0a3cd8de922c87e5fc"
    "4053ca52301afe224087c46ec15efcbe41b72c0d6d2d75023e95e08d31b527e23f81e08b4aa5630f3c733558d642a91e"
    "40386262761b44c63fe2531b016a6f8541a6fa8be93b8cc43ef06cccdbf6d8c43f5fc417db46cebd3c489930c0975408"
    "400bfbc2b3068e353f8cec146ba87c2e418cf4a16d57876a3ef5ad70072121a83fea87f3bf0971463fed6e2cb4d09afb"
    "3fe63877c7ebe9f93fb75009666b18663feadce2643e1b423ec069354c004fc43e2984ce5f100e363deff68cd6d4373f"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000274cea9d4df9a833000000000000000029919e00c1b0c96e00000000000000000000000000000000"
    "0000000000000000000000000000000022854012bd16555600000000000000001c8b18a1c3c9127f0000000000000000"
    "00000000000000000000000000000000000000000000000031020e46c3f3cf623137f8e93a4bf64d0000000000000000"
    "11fdaebf931d0844000000000000000000000000000000003003f827821a28b62f3e0d20fae2c6450000000000000000"
    "0d2885cc699725c3000000000000000000000000000000002ba5a1bb308a85792d2ad54fa8097f040000000000000000"
    "0000000000000000000000000000000000000000000000002799bbe9dc6f22222b22e92e648c64850000000000000000"
    "0000000000000000000000000000000000000000000000002a5430c0460c351d29c5ab9aa30d7fd50000000000000000"
    "0000000000000000000000000000000000000000000000000c80347ef543a3e027d2127286b0b2cb0000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000261efe3ac6a615860000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000002456b2c5dfe5e89f0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000274cea9d4df9a833000000000000000029919e00c1b0c96e0000000000000000"
    "00000000000000000000000000000000000000000000000022854012bd16555600000000000000001c8b18a1c3c9127f"
    "000000000000000000000000000000000000000000000000000000000000000031020e46c3f3cf623137f8e93a4bf64d"
    "000000000000000011fdaebf931d0844000000000000000000000000000000003003f827821a28b62f3e0d20fae2c645"
    "00000000000000000d2885cc699725c3000000000000000000000000000000002ba5a1bb308a85792d2ad54fa8097f04"
    "00000000000000000000000000000000000000000000000000000000000000002799bbe9dc6f22222b22e92e648c6485"
    "00000000000000000000000000000000000000000000000000000000000000002a5430c0460c351d29c5ab9aa30d7fd5"
    "00000000000000000000000000000000000000000000000000000000000000000c80347ef543a3e027d2127286b0b2cb"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000261efe3ac6a61586"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000002456b2c5dfe5e89f"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000c0626f72566127c5bfc26549362f5f7cc04bbc43c3949be5"
    "c0799965d0e5e55e4077391ba01f711c406bf087de109e9e4012f6a7325628594020eac50d52a9e64010c00eec754b74"
    "3fc7aaf448e3e8383f9275bae05fbf2c3f7ec0673ecc2f263f565f19c95a559644314d508cbf22aec0ca36ae788f8734"
    "bff7f6dd2412c2c0c07a5e905f38e5bfc0a10830410b1f43c08348011545b8a0c03de19609ea200bc031e895d31bfd2c"
    "c013ead329910561bfc892c931f0bde6bf9449a094276789bf76dfc29a115231bf483c5bef76be733cb8c269952e8a55"
    "c0260e73cf8ddb48c01e405767b36524bf324bcf712ff2a0000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000c0b3b836809022b7401d6b4487e12445c09a5e90f19be6ea3fb7a72d3aa0ff150000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000c0e2333628e9a6873ffb47713d0d04af40a07b1a5a02c65ac0c54a59e246705c"
    "3d80f5296198388f00000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000040e082999cf9ec503f3b71b86cf7d58c3f324bd048a53909"
    "40c98c5a1ef29adcc0acec019fe894fa3d0fef7b58b623a6000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000040d3dd0087dc27a13f4002563f9091e7"
    "3f35590ac4af83c8000000000000000040b0df00f29d0190c06a256348acdc0a3e64240133a460d80000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000407af6b5ba8bd9fe"
    "00000000000000003e394d450e8f526900000000000000000000000000000000406de19609ea200bc061e895d34a0654"
    "3e854d8269d2f07100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "40880dd02fab1ed600000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "406425a88d9961f8c046682d91c20c653e35d6720dd1766f000000000000000000000000000000000000000000000000"
    "00000000000000004077d1149e1fda8a0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000004048e587f59f54eabffeb77b817560c53eeed41d5a644e970000000000000000"
    "000000000000000000000000000000004030d39444bc1713000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000004000e4ea532b08b0bfcbe64c1652169d"
    "3d649d1a645dae4e000000000000000000000000000000003ffa3f65b708966a00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3fce6f04d86199fcbfb127d1f38e65723d2830adbcdf4a6e00000000000000003fe5dcc966a53b210000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003fb295ce1d2ed5acbf83b10ab2914c633cf41df5c935d0663fbfcf38aa4c834a"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003f8534d071885788bcf5aa1c6288b90b"
    "449899df5bf203f943c8ae5bc094e035443a19b57574ca984470984c1adcb10044542342045fa49a4405b862ef6b43ac"
    "43f8e2946030f0c343dd56af3659a40b438a5874f8f02fe34360553dc3d2c5554343019da0b7fd254314479dcfac877a"
    "c084b7c9c2b673d7bea377b5b05bac87be0c7b650f0c6525be7f8dc94a5d3ce0bebd685d80525c1a3eb796a739a81e08"
    "3eaf911670debf5d3e52a0e6ef146d523e6c592fea4b26263e619cd0a11e69883e18c66b3158adde3de54d6802fa9790"
    "3dd4895d74d35d653db01c168a81a77242729f853b84c52ac0626f72566127c5bfc26549362f5f7dc04bbc43c3949be5"
    "c0799965d0e5e55c4077391ba01f7119406bf087de109e9e4012f6a7325628594020eac50d52a9e64010c00eec754b74"
    "3fc7aaf448e3e8383f9275bae05fbf2a3f7ec0673ecc2f263f565f19c95a55963f7db2a680786734bfa15f719be12c49"
    "3f7a0b9478f0f3703f94c66281f70a683ef0a3a766df98a03ef36b2a71ce97e03e537b678c338558be5b301b790ab1a0"
    "3ec57d44fa46a6ffbec7a57c22ad20a5bd40461f18304ed63d5ce70608981fc6bd5fe4a9de479c773f7db2a680786734"
    "bfa15f719be12c493f7a0b9478f0f3703f94c66281f70a683ef0a3a766df98a03ef36b2a71ce97e03e537b678c338557"
    "be5b301b790ab1a03ec57d44fa46a6ffbec7a57c22ad20a5bd40461f18304ed83d5ce70608981fc8bd5fe4a9de479c78"
    "3f7db2a680786734bfa15f719be12c493f7a0b9478f0f3703f94c66281f70a683ef0a3a766df98a03ef36b2a71ce97e0"
    "3e537b678c338557be5b301b790ab1a03ec57d44fa46a6ffbec7a57c22ad20a5bd40461f18304ed83d5ce70608981fc8"
    "bd5fe4a9de479c78432fe95466e5ea6b432fe95466e5ea6b432fe95466e5ea6b3fa533686b80e8cb3e8996307ea4dffe"
    "3ea9722ef97d8f793e438e90cf669a9a3ed13a033f71b44f3f80bcbe808dcd3a3f8b99cd77923ccd3f8577557630963e"
    "3f946c0030c0f1b23f3c69b6380bec163f6d86c371b625573faac1f0e23934bf3feb34f44a4b66f643763440eb571c69"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000043836ddfad93f6b6000000000000000043836ddfad93f6b640b0000000000000"
    "40b001000000000040b002000000000040b003000000000040b004000000000040b005000000000040b0060000000000"
    "40b007000000000040b008000000000040b009000000000040b00a000000000040b00b000000000040b00c0000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b000000000000040b001000000000040b002000000000040b003000000000040b0040000000000"
    "40b005000000000040b006000000000040b007000000000040b008000000000040b009000000000040b00a0000000000"
    "40b00b000000000040b00c000000000040b000000000000040b001000000000040b002000000000040b0030000000000"
    "40b004000000000040b005000000000040b006000000000040b007000000000040b008000000000040b0090000000000"
    "40b00a000000000040b00b000000000040b00c000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b000000000000040b0010000000000"
    "40b002000000000040b003000000000040b004000000000040b005000000000040b006000000000040b0070000000000"
    "40b008000000000040b009000000000040b00a000000000040b00b000000000040b00c000000000040b0000000000000"
    "40b001000000000040b002000000000040b003000000000040b004000000000040b005000000000040b0060000000000"
    "40b007000000000040b008000000000040b009000000000040b00a000000000040b00b000000000040b00c0000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b000000000000040b001000000000040b002000000000040b003000000000040b0040000000000"
    "40b005000000000040b006000000000040b007000000000040b008000000000040b009000000000040b00a0000000000"
    "40b00b000000000040b00c000000000040b000000000000040b001000000000040b002000000000040b0030000000000"
    "40b004000000000040b005000000000040b006000000000040b007000000000040b008000000000040b0090000000000"
    "40b00a000000000040b00b000000000040b00c000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b000000000000040b0010000000000"
    "40b002000000000040b003000000000040b004000000000040b005000000000040b006000000000040b0070000000000"
    "40b008000000000040b009000000000040b00a000000000040b00b000000000040b00c000000000040b0000000000000"
    "40b001000000000040b002000000000040b003000000000040b004000000000040b005000000000040b0060000000000"
    "40b007000000000040b008000000000040b009000000000040b00a000000000040b00b000000000040b00c0000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c00000000003fa533686b80e8cb3e8996307ea4dffe3ea9722ef97d8f793e438e90cf669a9a3ed13a033f71b44f"
    "3f80bcbe808dcd3a3f8b99cd77923ccd3f8577557630963e3f946c0030c0f1b23f3c69b6380bec163f6d86c371b62557"
    "3faac1f0e23934bf3feb34f44a4b66f640b000000000000040b001000000000040b002000000000040b0030000000000"
    "40b004000000000040b005000000000040b006000000000040b007000000000040b008000000000040b0090000000000"
    "40b00a000000000040b00b000000000040b00c00000000003fa533686b80e8cb3e8996307ea4dffe3ea9722ef97d8f79"
    "3e438e90cf669a9a3ed13a033f71b44f3f80bcbe808dcd3a3f8b99cd77923ccd3f8577557630963e3f946c0030c0f1b2"
    "3f3c69b6380bec163f6d86c371b625573faac1f0e23934bf3feb34f44a4b66f6"
    , 81920u, 49151u, true, true,
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d500000000000013cc39034784db991"
    "3bc40000000000013d1e9a81f28204f93db30000000000013d0360e45b53d9693b278000000000013d24f1014d4740aa"
    "3dc10000000000013d2f2ac24d0c5239000000000000000000000000000000003c274000000000013d2f978d12f13508"
    "3e200000000000013ceb4fa0fb89cfe23ce18000000000013d12f0c3cf1692793e5a0000000000013cfb7beff1f4ff5a"
    "3ab90000000000013d23a823d99ecbf43e2c0000000000013ceed17611f3d1d73a3e8000000000013d2280bfeb22fb86"
    "3e560000000000013d03f860322dba103ef80000000000013cddf2ec92e7af223ed80000000000013ce2294007764d77"
    "3a3b40000000000100000000000000003e178000000000013d1ddf3bfd433db13b9e0000000000013d2ece8fc03414f1"
    "3e4f8000000000013d2af9d1b02cfa653f578000000000013d26f18320f772793ed00000000000013cd65d20593cd6b1"
    "3b7e0000000000013d1dae0e3d43e7283e060000000000013d1e653c647f62653bb58000000000013d2f918885b7e72e"
    "3e0a0000000000013d025097de5a1c563f100000000000013d10def17f099a883f134000000000013d270f6020d51f6b"
    "3bc70000000000013d338da9a219ceb93db80000000000013d2f088e69034efc3b2b8000000000013d377c59aefab5ee"
    "3e018000000000013d205fafae0a458b3ea80000000000013d1d4923687e434d3edf0000000000013d1e51a957100e31"
    "3bb04000000000013d2fff6789b7f3813db50000000000013d1ed50ccc3fb3753c290000000000013d27a2fc3688566c"
    "3d000000000000013cce5d1aa8101b583f0a0000000000013d12c53a7716d5b73ef08000000000013d1f250303e3fbed"
    "3ba38000000000013d2f24bb6d2b847d3d058000000000013d2f29781d5dbec33a224000000000010000000000000000"
    "3d93c000000000013d2fef4fb5390d3e3dc68000000000013d2e4b302bbc04b83ee58000000000013d1db0df1666d977"
    "3bd58000000000013d2f72c5d63566273cd1a000000000013d3f8c775bc2838439c7c000000000010000000000000000"
    "3d778000000000013d2ed6e942357ce83d224000000000013d2fdea2eaf918e73e900000000000013cd64810d316af3c"
    "3c1f0000000000013d1e32a770c8a0073c540000000000013ce425b7b345636339778000000000010000000000000000"
    "3d4b4000000000013d2f294a1ec6fb0f3cce0000000000013d3098a4344b57d03ebb0000000000013d1dd6bb961572a6"
    "3c350000000000013d2efff9513d1bb6000000000000000000000000000000003cec0000000000013cee71da961b19d3"
    "3d0a0000000000013d12b8acb74f35263c900000000000013cc5f65b591ce2db3cfa0000000000013cfef8dfd6a93b96"
    "3bf00000000000013d1f32da5a6939463b6a0000000000013d304d3dc205e3303b224000000000013d22456545615e5a"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000024ac900000000001000000000000000000000000000000000000000000000000"
    "26f1b0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000001fe53000000000010000000000000000"
    "0000000000000000000000000000000019eb100000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "2e51c0000000000100000000000000002e87e00000000001000000000000000000000000000000000000000000000000"
    "0f6da0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "2d63d0000000000100000000000000002c9df00000000001000000000000000000000000000000000000000000000000"
    "0ab260000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "2905a0000000000100000000000000002a8ac00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "24f990000000000100000000000000002882d00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "27b400000000000100000000000000002725900000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "09f028000000000100000000000000002532000000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000237ef00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000021b6d00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000024ac8000000000010000000000000000"
    "0000000000000000000000000000000026f1d00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "1fe530000000000100000000000000000000000000000000000000000000000019eaf000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002e51c0000000000100000000000000002e87e000000000010000000000000000"
    "000000000000000000000000000000000f6da00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002ce000000000000100000000000000002c9df000000000010000000000000000"
    "000000000000000000000000000000000ab2600000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002905a0000000000100000000000000002a8ac000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000248000000000000100000000000000002882d000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000027b4000000000001000000000000000027257000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000009f0280000000001000000000000000025320000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000237ed000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000021b6d000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d6c0000000000013cf84d186ff8217c3ce40000000000013d11653a23c368d23d480000000000013cebb0b341f64569"
    "3d8e0000000000013d02c025edcc579c3d8a0000000000013d01e9c87ff567d03d720000000000013cf49daceba8d8a8"
    "3d080000000000013ce43fdb34e2194b3d500000000000013d1e43ea041ede403d410000000000013d203d13eb883dfa"
    "3cf30000000000013d19b05d2971a8713cd2c000000000013d30405f84b277bd3cbe0000000000013d2f37c92286934d"
    "3c960000000000013d2f77f7fa4ccd7941c12a00000000013d7fbeaf89ec13f13dd80000000000013cfd4c3a9dcbf80d"
    "3cf00000000000013ce55d776a4e71dc3d780000000000013ced1feae222b8c63db00000000000013cfe0fa350362509"
    "3d880000000000013cf3ea6b96814c763d660000000000013d178f5a087d55d13d620000000000013d2014eb5e44ea80"
    "3d450000000000013d20dea936cd18233cf78000000000013d1e9a2aeff6e3953cd44000000000013d2ff0d0939231d2"
    "3cb60000000000013d2ec6f7ade92e823c878000000000013d2f074c3ad45ccf3a032000000000010000000000000000"
    "3d3a0000000000013d02dc56518d60dd3d468000000000013d17ccf45a9b03ab00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003db00000000000013ce9f6cb4dd989d13d310000000000013d027dcf24f6793c"
    "3d980000000000013ced1fea40740a033cdc0000000000013d12f0c3cf16927900000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003df20000000000013cffa5f53a0cae9d"
    "3d3a8000000000013d2f160ac5df215f3da00000000000013cef10fa515329433dd40000000000013cfe0f79912ec637"
    "3ab48000000000013d2357a03458ac050000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3df20000000000013d01719d67819cab0000000000000000000000000000000000000000000000000000000000000000"
    "3dd80000000000013cfe0f8e70a3f74a3db60000000000013cf857670d48b2c13a470000000000013d270be57c4c0e39"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003de00000000000013cf9c6b4a6a438c300000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003db40000000000013cf2f7a36ac448ce"
    "3d930000000000013d174105bf3c89e63ba08000000000013d2a3734a791e71b00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d780000000000013cec7b93fcb320c5"
    "000000000000000000000000000000003b790000000000013d2f9e464bc1a2e000000000000000000000000000000000"
    "000000000000000000000000000000003d960000000000013d178f5a087d55d13d920000000000013d2014eb5e1b9387"
    "3bc44000000000013d2e6b27e7b729790000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3db78000000000013d1f4357008043d70000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d940000000000013d1fc42fbd3636ef3d778000000000013d20c7cf972562203b760000000000013d301e72321ea9e3"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003da90000000000013d20cb8017f225db00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d7a0000000000013d20b587e724aa36"
    "3d2d8000000000013d1ebb812af489d93c26c000000000013d279d4d08f19ab800000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d5d0000000000013d1b93594ce94d43"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d308000000000013d1f40da248c5e5c3d0bc000000000013d2fd41301ca2120"
    "3aae4000000000013d377ac3b70449910000000000000000000000000000000000000000000000000000000000000000"
    "3d3b0000000000013d3075680cdb9f200000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d0e8000000000013d3008ed71f0fa8d3cf08000000000013d2ec6f7ade6a9053a720000000000013d37cfb4384e6b24"
    "000000000000000000000000000000003d254000000000013d2f1a8301940cee00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003cf20000000000013d2efe10af1f3fc4"
    "3cc34000000000013d2f484cda8531343a3f00000000000100000000000000003cff0000000000013d2f2f89917ef014"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003cc48000000000013d2eef276d39c8f93a40c000000000010000000000000000"
    "421e5400000000013d73b989a071542641360000000000013d5c861bfe55755341400000000000013cf39dd57ba6697d"
    "419f0000000000013d1de3808fd8d51c41e24e00000000013d7d165285f5ae124194ba00000000013d7e8937c118042f"
    "41885200000000013d7f46155e942a2d41658c00000000013d77805f4012262c4102ac00000000013d66adeadc9d59ec"
    "41021000000000013d91b1bb7dc9bcb140b99800000000013d658ba4387d5faf40b07f00000000013d8a0797a88747ee"
    "3df7a000000000013d623ebd15b91f7e3bb60000000000013d0214d0b9f52eb53b338000000000013d15e897c0529b46"
    "3b680000000000013cd856df178365da3bd00000000000013d016911475d88fe3bca0000000000013d01a2be92e75c6b"
    "3bb80000000000013cf8545371a863673b600000000000013cfb7c1774657b513b928000000000013d14e20dbc6fa1d6"
    "3b920000000000013d205a1a9ae7965c3b430000000000013d188a7190bba4173b25c000000000013d305611f925bdc6"
    "3b128000000000013d2cd39c8f30c0443aed8000000000013d2d4c90bbce88143fe43800000000013d615ef1a44e7e09"
    "3d700000000000013cfbc5d2c91b93fc3ce30000000000013d108690d52cd6c73d480000000000013cebb0b341f64569"
    "3d8c0000000000013d018023667a73e83d8c0000000000013d034a8927570d593d720000000000013cf49daceba8d8a8"
    "3d180000000000013cf43fdb34e2194b3d500000000000013d1e43ea041ede403d418000000000013d20b75845487c09"
    "3cf40000000000013d1b0a7d033445853cd28000000000013d3008e6c731cb7c3cbd4000000000013d2e6ffdb4dccf9e"
    "3c960000000000013d2f77f7fa4ccd793cb80000000000013d29dc46cab7fc013ce00000000000013d2d78a7da766744"
    "3ca18000000000013d1580451ac05a5e3cd7c000000000013d324a90850733263be80000000000013ce713f34cc4c8c1"
    "3be00000000000013cda5dcfbd6c90683b920000000000013d2d90d056ce75203b98c000000000013d2d216e0a20f89d"
    "3bff8000000000013d2774184d9c111b3c010000000000013d27016e6f6e9da13a880000000000013d37989691f1a42c"
    "3aa6c000000000013d39302d1e4a64c43aa92000000000013d393589060dd37c3cb80000000000013d29dc46cab7fc01"
    "3ce00000000000013d2d78a7da7667443ca18000000000013d1580451ac05a5e3cd7c000000000013d324a9085073326"
    "3be80000000000013ce713f34cc4c8c13be00000000000013cda5dcfbd6c90683b924000000000013d2df9efad586fa5"
    "3b990000000000013d2d6cc1e60772133bff8000000000013d2774184d9c111b3c010000000000013d27016e6f6e9da1"
    "3a878000000000013d371abe1991f0be3aa6c000000000013d39302d1e4a64c23aa94000000000013d3955a4739dd0b7"
    "3cb80000000000013d29dc46cab7fc013ce00000000000013d2d78a7da7667443ca18000000000013d1580451ac05a5e"
    "3cd7c000000000013d324a90850733263be80000000000013ce713f34cc4c8c13be00000000000013cda5dcfbd6c9068"
    "3b924000000000013d2df9efad586fa53b990000000000013d2d6cc1e60772133bff8000000000013d2774184d9c111b"
    "3c010000000000013d27016e6f6e9da13a878000000000013d371abe1991f0be3aa6c000000000013d39302d1e4a64c2"
    "3aa94000000000013d3955a4739dd0b740c6b200000000013d86c21f7fe2f31b40c6b200000000013d86c21f7fe2f31b"
    "40c6b200000000013d86c21f7fe2f31b3d1d7800000000013d663d5de7e53c8f3bff1800000000013d637197241561a4"
    "3c189000000000013d5ee38f53c72d3d3bac0000000000013d56e85330b14b723c40a000000000013d5ee1e84c556b2f"
    "3d01a800000000013d70e0e48c58362d3d02c000000000013d65bd0eccddf12e3ce0d000000000013d49100e42ac134a"
    "3cd74000000000013d32373bf633d0523cc61400000000013d78dd8d13f5e15c3cf8cc00000000013d7adfcdfde7f912"
    "3d317400000000013d74df61f8d582353d2ec000000000013d32156c03bb776440e0a000000000013d57f5a33ee6120c"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000040e25000000000013d4e292a0aa99967"
    "0000000000000000000000000000000040e25000000000013d4e292a0aa9996700000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d1d7800000000013d663d5de7e53c8f3bff1800000000013d637197241561a4"
    "3c189000000000013d5ee38f53c72d3d3bac0000000000013d56e85330b14b723c40a000000000013d5ee1e84c556b2f"
    "3d01a800000000013d70e0e48c58362d3d02c000000000013d65bd0eccddf12e3ce0d000000000013d49100e42ac134a"
    "3cd74000000000013d32373bf633d0523cc61400000000013d78dd8d13f5e15c3cf8cc00000000013d7adfcdfde7f912"
    "3d317400000000013d74df61f8d582353d2ec000000000013d32156c03bb776400000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d1d7800000000013d663d5de7e53c8f3bff1800000000013d637197241561a43c189000000000013d5ee38f53c72d3d"
    "3bac0000000000013d56e85330b14b723c40a000000000013d5ee1e84c556b2f3d01a800000000013d70e0e48c58362d"
    "3d02c000000000013d65bd0eccddf12e3ce0d000000000013d49100e42ac134a3cd74000000000013d32373bf633d052"
    "3cc61400000000013d78dd8d13f5e15c3cf8cc00000000013d7adfcdfde7f9123d317400000000013d74df61f8d58235"
    "3d2ec000000000013d32156c03bb7764"};
inline constexpr FrozenNetworkAuthority<19, 100> kAprox19Authority{
    {"h1", "he3", "he4", "c12", "n14", "o16", "ne20", "mg24",
     "si28", "s32", "ar36", "ca40", "ti44", "cr48", "fe52", "fe54",
     "ni56", "neut", "prot"},
    "3ff0000000000000400800000000000040100000000000004028000000000000402c0000000000004030000000000000"
    "40340000000000004038000000000000403c000000000000404000000000000040420000000000004044000000000000"
    "40460000000000004048000000000000404a000000000000404b000000000000404c0000000000003ff0000000000000"
    "3ff00000000000003ff0000000000000400000000000000040000000000000004018000000000000401c000000000000"
    "402000000000000040240000000000004028000000000000402c00000000000040300000000000004032000000000000"
    "403400000000000040360000000000004038000000000000403a000000000000403a000000000000403c000000000000"
    "00000000000000003ff00000000000000000000000000000401edf6d330941c8403c4bc89f40a28740570a6d9be4cd75"
    "405a2a3d1cc100e7405fe7bd512ec6bd406414bb6ed677704068c840b780346e406d91367a0f90974070fc851eb851ec"
    "40732b85f06f6944407560e8a71de69b407777a29c779a6b4079b7810624dd2f407bfb53f7ced917407d7c504816f007"
    "407e400c49ba5e350000000000000000000000000000000000000000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff0000000000000400000000000000040000000000000003b002d35e95cfa093b1835a3280d0832"
    "3b2010d7aef917503b3815400c10b9843b3c1a5f2593a2463b400cdc2d7141203b440fc40203d1313b481168304c8804"
    "3b4c12dd0e75467b3b500a9423cd5c753b520bc4967f0d293b540ce6f377444b3b560e4cf94a85993b580f5833cc1339"
    "3b5a105ab3d216cf3b5b1037ced5dfc33b5c115b39a693933b0032eb3e227d9a3b002d35e95cfa094079edccc3e64028"
    "3e94b8033f9ade99409f159028ab31eb408119e01aa6c6033fc976c9c7628bb73ee71ff3a55777243df1c9d76e0048ef"
    "41228404a3636abe3fbd33964df4665d414dce4e0c8108353d84092da5e251ad412c9298d1c974083d09eb6e483da51c"
    "4141528cffd9b4cb420933a29177fcaf41e4ee8de25928fe3cf9fdc32d45be0140e8b0d8f374be353e5e904c64041882"
    "411253208ebb2fd8422012d454b44be841e6a116fcef6a3e3e4ff88996badccb40d6ad27eca9721d3e7556699420628f"
    "40f63cfe009cc18341edb674566e72ca41da5ce16920979a3e8292dc00cee6be407830fdeb367b5a3de2505229e63b65"
    "40d0b75a968c70fd4179a294914d5a8741b01eb7087730c33e70036ba62a79e0408546c794e89ba63ef0851cea197237"
    "402075f6e111318541e5a2883b18a90841c0addfc5e3b8b83e63b66450c01e663fc586fc09e07cbe3cd83fb6375cd07c"
    "40534be7e150341540872c9d05b1023b41b6c32122ca0fbc3e957d7deff312433f8169f39c3e60463c72b5ec5a1d90ca"
    "4037c09fffc28a213fe1d98b5a165a6841a68989fbd260b83ef01c05f6b25bc83f5eeafe3717588f3c47f1137e92e103"
    "400b3c82fd94bf263f8c266a48b4e579418c60d84f17f9a13ef53ecc6881e6f5426934861926f4423d9fcb0c91eea80a"
    "427c5f22d3848d523ca7a6b0c24ffcd041924d3370fa76143fb9130b54f6b463420654b6fa85fae03844eef1d0584032"
    "4217b143ddb2598041c8fac1830941a241ca13e6ca9055813fe2867ba35847f53ee237efc6b312c43eede092fe2bb556"
    "00000000000000003e0c6dd33b24fe6f42897e538c2c31874195d9f569277df641ed1bd5053a995f41b25a85b61f183b"
    "3feffb6077120c0d3f427e23b7cfcc004190f305a5bd5898417004b82ece697000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000274cea9d4df9a833000000000000000029919e00c1b0c96e00000000000000000000000000000000"
    "0000000000000000000000000000000022854012bd16555600000000000000001c8b18a1c3c9127f0000000000000000"
    "00000000000000000000000000000000000000000000000031020e46c3f3cf623137f8e93a4bf64d0000000000000000"
    "11fdaebf931d0844000000000000000000000000000000003003f827821a28b62f3e0d20fae2c6450000000000000000"
    "0d2885cc699725c3000000000000000000000000000000002ba5a1bb308a85792d2ad54fa8097f040000000000000000"
    "0000000000000000000000000000000000000000000000002799bbe9dc6f22222b22e92e648c64850000000000000000"
    "0000000000000000000000000000000000000000000000002a5430c0460c351d29c5ab9aa30d7fd50000000000000000"
    "0000000000000000000000000000000000000000000000000c80347ef543a3e027d2127286b0b2cb0000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000261efe3ac6a615860000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000002456b2c5dfe5e89f0000000000000000"
    "426c90ac0ef6e07c0000000000000000426e1b6f51c41559000000000000000025431d364bb5ab8c0000000000000000"
    "41680dc379061000000000000000000042257fa2f185c657000000000000000040529a08597245ce0000000000000000"
    "0000000000000000000000000000000000000000000000003cdff90982675c9e3b779f708173b76a3a22cccf4bb40287"
    "3979336e31bf4588382a0496bb35f8ac3feff8c362a6fae83f4cf2756414600036f21ff514a8852825de403081d0a9f4"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000274cea9d4df9a833"
    "000000000000000029919e00c1b0c96e0000000000000000000000000000000000000000000000000000000000000000"
    "22854012bd16555600000000000000001c8b18a1c3c9127f000000000000000000000000000000000000000000000000"
    "000000000000000031020e46c3f3cf623137f8e93a4bf64d000000000000000011fdaebf931d08440000000000000000"
    "00000000000000003003f827821a28b62f3e0d20fae2c64500000000000000000d2885cc699725c30000000000000000"
    "00000000000000002ba5a1bb308a85792d2ad54fa8097f04000000000000000000000000000000000000000000000000"
    "00000000000000002799bbe9dc6f22222b22e92e648c6485000000000000000000000000000000000000000000000000"
    "00000000000000002a5430c0460c351d29c5ab9aa30d7fd5000000000000000000000000000000000000000000000000"
    "00000000000000000c80347ef543a3e027d2127286b0b2cb000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000261efe3ac6a61586000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002456b2c5dfe5e89f0000000000000000426c90ac0ef6e07c0000000000000000"
    "426e1b6f51c41559000000000000000025431d364bb5ab8c000000000000000041680dc3790610010000000000000000"
    "42257fa2f185c657000000000000000040529a08597245ce000000000000000000000000000000000000000000000000"
    "00000000000000003cdff90982675c9e3b779f708173b76a3a22cccf4bb402843979336e31bf4588382a0496bb35f8ac"
    "3feff8c362a6fae83f4cf2756414600036f21ff514a8852825de403081d0a9f400000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000418488ed7a61151bc19eda9a3c05c4fe41a525d4933d095d"
    "c11a6c097f2e8a48411eb81c5fb44415c052da0847c93380409f8bd89b69355f407cd48c363481b7406e680dd0ae3e23"
    "40156b530fe0dd7e4020a5528b320e60400f0de71052d3b03fc545955d4669053f9042e79f9d7459c1f82143888c7030"
    "41f90e3c017f43b141247532323478c9c1b190a40f08cf06c185bce78d9f49c045a870c9a91884bdc16a259354a43b7a"
    "41fe88534b575f9cbc51b51929b0c968c14a259354a404d2bf40ef83752ee902bf55810624dd2f1b0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3efb53e88fe01496c216e63e7888b2b8bc6a8fa5be892e1c000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000003c4023b74ecd0f50420e88534b65b5a0c0ec7a2f0aa9c956c00261594754edd9c0dbbfcd6f822186"
    "c082b5e4463ab979c0a8187416e36ebbc08b4669c278cabac0451a8fc3cf618ec03944e30f3774eec01bfea25282eab8"
    "bfd13be482a9546bbf9c6c6215a284dbbf8001e331337793bf50f25887ede56d3f21368945ed40803cb8193292afd9e0"
    "41ccaab9e87b06d141d9dfe00de235a7c1939c2e7f7b039e0000000000000000c024ebeb89a685b1c1739c2f1bdabf06"
    "3f696399a14cd2cabf312799f6eca0980000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000004196e0e0ea0f84380000000000000000c0fafa7992333851"
    "4176e0e0ea0f8438c0f02fe299bd1b373f82d0e560418938000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000bc56e08f56a293d90000000000000000"
    "c0b2a7f3d10d08854024f09dd899a7543ec392f7dd8e8a13c0a2b5ef4b5e548b3fb75c783e5d1eb10000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000040ff37ac354e99403ff99a032666f58940f71fd5eb0754d840a763642cb3795cc0ce1eae5032985f"
    "3d80b250b4e7441000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000040dc87aab7bad2f93f39bb68329416a000000000000000003f31279acc62b9c0"
    "40d2125fd3d7aa70c0b474cf51da98103d0f70d0d611608e000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000040ce16f8427ce9853f3e054ee5acc5100000000000000000"
    "3f340418a740e91c000000000000000040b7dd9c8a29b165c072773dcb55755c3e63ccb3d923448e0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000040753234dc96ab1c0000000000000000"
    "00000000000000003e37d5ca073e96910000000000000000000000000000000040751a8fc3cf618ec06944e30f64b689"
    "3e84eb75bd1f8f7800000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000408078ef04bd2896"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "406c6d7f7137d874c04f7e769fc4649e3e355cae384e7e1d000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "406ebb16d07470f500000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000040517f257463054cc0058adda4cf6e823eee185227b25bf60000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000040250ce4c63732270000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000004007b25a3479b39ebfd38b07d9e2d068"
    "3d64196daf0c3cfc00000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003ff0178a8b49554a000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3fd55191ca98a950bfb802d4c9ce92303d2790350341cc1a000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000003fda0b60e819a3d900000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003fba03112ff46056c2331a5576c49c3b3f5bf89f11a188d03cf39479172ee106"
    "c22fd68e709cfea43f578dff3dc5a60c000000000000000000000000000000003e13bad0459d541b0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000004233d66c717d6396c15d48524647ac06"
    "3d56a074acbc32e142308805093dd2fcc158a8b119922322000000000000000000000000000000003fb28910cf8eff4c"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000003f8da81ae5b1987a"
    "415e5df681a77683bd57cb55ed9ea5b900000000000000004159927ec515e30a00000000000000000000000000000000"
    "3644bf589edcbc4800000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "c1e782df5719599537b4b90a8d6eaf950000000000000000c1e72d115af9d649c1c9dfe00de2338e0000000000000000"
    "00000000000000003dc762263f8b0e590000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000003da2b4eb6608d848c1115a43b7de9ed43d0ad121fc216bb5c1bcaab9e87b05e2c1c9e1b3a9396e5b"
    "454ea4f97ece35a345e4896dbb28f77944bad3919c8ff918452ea4fa06391f6444aa35dc8ecef869444285129e415b96"
    "44777a262ad3ea23445c7c9115bba491440eae1b27f40b6444018e783f9b738243e49e4e9e57441643927a105f6e852e"
    "4366e1f6f1d406c2434a99f5b77b4ef945dd96f9cd74251844f634e7a0e0c196c0f1511797755d9d45ddf6293af23530"
    "45c32650204be1c53fb38c23c3252b97bfcd554fc9624628bfd971937a2fa3bebf28d12e25c5c52a3f2c5789e8b6fff5"
    "be858b931c059bf73ed34e626fb116693ebd8c8e06ee63203eb1417bd582d6243e56ea2a277133803e6c17152a5e8bf2"
    "3e605a36056c0c403e164f003cc01d333de2d17555e0bc8e3fd9f3662e243889bfdb12c88d71185b3f60864db64fd7ac"
    "3fd29bd55be9aa6f3fd19b2343b61214c3c80714795f0b57418488ed7a61151bc19eda9a3c05c4fe41a525d4933d095c"
    "c11a6c097f2e8a48411eb81c5fb44415c052da0847c93380409f8bd89b69355f407cd48c363481b6406e680dd0ae3e21"
    "40156b530fe0dd7d4020a5528b320e61400f0de71052d3b13fc545955d4669053f9042e79f9d7458c1f82143888c7030"
    "41f90e3c017f43b141247532323478cac1b190a40f08cf06c185bce78d9f49bf41849ee0aa02c503c19eee50ff177862"
    "41f2ab9f4391f4e9bfc7ff8eb92de878c09bb7f04f716be3c05365947ff9fdfe409fc97ff1c5a733407e6592f8d1ab6c"
    "406fbff03179552840164eda3e1e118540214a4f35b3099b40109077848176033fc6dacf73bcf7ce3f91860cd640d36c"
    "c1f82143888c683641f9082309d80a32415bd9c60556a39bc1e43450901ba7e6c1e259a34d022c0041849ee0aa02c503"
    "c19eee50ff17786241f2ab9f4391f4eabfc7ff8eb92de878c09bb7f04f716be3c05365947ff9fdfe409fc97ff1c5a733"
    "407e6592f8d1ab6c406fbff03179552840164eda3e1e118540214a4f35b3099b40109077848176033fc6dacf73bcf7ce"
    "3f91860cd640d36cc1f82143888c683641f9082309d80a32415bd9c60556a39bc1e43450901ba7e7c1e259a34d022c01"
    "41849ee0aa02c503c19eee50ff17786241f2ab9f4391f4eabfc7ff8eb92de878c09bb7f04f716be3c05365947ff9fdfe"
    "409fc97ff1c5a733407e6592f8d1ab6c406fbff03179552840164eda3e1e118540214a4f35b3099b4010907784817603"
    "3fc6dacf73bcf7ce3f91860cd640d36cc1f82143888c683641f9082309d80a32415bd9c60556a39bc1e43450901ba7e7"
    "c1e259a34d022c0145ddb5ad5031daaa45ddb5ad5031daab45ddb5ad5031daab00000000000000003db43016bc154791"
    "3fa4a38b49461e9f3e879aff996f4a9d3d80a96dc2c2abc63ea6da7ad385d2053e411947928c45373ecd5302ee161f7e"
    "3f7bbc6f9338054e3f864356ca782f873f80db174325270a3f8f38b74442b2363f3524bf262b5ad83f6563c542ab1911"
    "3fa2debeef73b2093fd18977069fc3a93fe2ada86d68581a3e671a3c356ef1eb3f95920bbde14dab43b7487beebbde80"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000043b7487beebbde80000000000000000043b7487beebbde8040b0000000000000"
    "40b001000000000040b002000000000040b003000000000040b004000000000040b005000000000040b0060000000000"
    "40b007000000000040b008000000000040b009000000000040b00a000000000040b00b000000000040b00c0000000000"
    "40b00d000000000040b00e000000000040b00f000000000040b010000000000040b011000000000040b0120000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b000000000000040b001000000000040b002000000000040b003000000000040b0040000000000"
    "40b005000000000040b006000000000040b007000000000040b008000000000040b009000000000040b00a0000000000"
    "40b00b000000000040b00c000000000040b00d000000000040b00e000000000040b00f000000000040b0100000000000"
    "40b011000000000040b012000000000040b000000000000040b001000000000040b002000000000040b0030000000000"
    "40b004000000000040b005000000000040b006000000000040b007000000000040b008000000000040b0090000000000"
    "40b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e000000000040b00f0000000000"
    "40b010000000000040b011000000000040b012000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b000000000000040b0010000000000"
    "40b002000000000040b003000000000040b004000000000040b005000000000040b006000000000040b0070000000000"
    "40b008000000000040b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d0000000000"
    "40b00e000000000040b00f000000000040b010000000000040b011000000000040b012000000000040b0000000000000"
    "40b001000000000040b002000000000040b003000000000040b004000000000040b005000000000040b0060000000000"
    "40b007000000000040b008000000000040b009000000000040b00a000000000040b00b000000000040b00c0000000000"
    "40b00d000000000040b00e000000000040b00f000000000040b010000000000040b011000000000040b0120000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b000000000000040b001000000000040b002000000000040b003000000000040b0040000000000"
    "40b005000000000040b006000000000040b007000000000040b008000000000040b009000000000040b00a0000000000"
    "40b00b000000000040b00c000000000040b00d000000000040b00e000000000040b00f000000000040b0100000000000"
    "40b011000000000040b012000000000040b000000000000040b001000000000040b002000000000040b0030000000000"
    "40b004000000000040b005000000000040b006000000000040b007000000000040b008000000000040b0090000000000"
    "40b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e000000000040b00f0000000000"
    "40b010000000000040b011000000000040b012000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b000000000000040b0010000000000"
    "40b002000000000040b003000000000040b004000000000040b005000000000040b006000000000040b0070000000000"
    "40b008000000000040b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d0000000000"
    "40b00e000000000040b00f000000000040b010000000000040b011000000000040b012000000000040b0000000000000"
    "40b001000000000040b002000000000040b003000000000040b004000000000040b005000000000040b0060000000000"
    "40b007000000000040b008000000000040b009000000000040b00a000000000040b00b000000000040b00c0000000000"
    "40b00d000000000040b00e000000000040b00f000000000040b010000000000040b011000000000040b0120000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000000000000000000003db43016bc1547913fa4a38b49461e9f3e879aff996f4a9d3d80a96dc2c2abc6"
    "3ea6da7ad385d2053e411947928c45373ecd5302ee161f7e3f7bbc6f9338054e3f864356ca782f873f80db174325270a"
    "3f8f38b74442b2363f3524bf262b5ad83f6563c542ab19113fa2debeef73b2093fd18977069fc3a93fe2ada86d68581a"
    "3e671a3c356ef1eb3f95920bbde14dab40b000000000000040b001000000000040b002000000000040b0030000000000"
    "40b004000000000040b005000000000040b006000000000040b007000000000040b008000000000040b0090000000000"
    "40b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e000000000040b00f0000000000"
    "40b010000000000040b011000000000040b012000000000000000000000000003db43016bc1547913fa4a38b49461e9f"
    "3e879aff996f4a9d3d80a96dc2c2abc63ea6da7ad385d2053e411947928c45373ecd5302ee161f7e3f7bbc6f9338054e"
    "3f864356ca782f873f80db174325270a3f8f38b74442b2363f3524bf262b5ad83f6563c542ab19113fa2debeef73b209"
    "3fd18977069fc3a93fe2ada86d68581a3e671a3c356ef1eb3f95920bbde14dab"
    , 81920u, 49151u, true, false,
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d500000000000013cc3bf0da40020d3"
    "3bc40000000000013d1ee3cb470a63a13db60000000000013d06a5ec792912d73dc0c000000000013d2f57d29b7b6e37"
    "000000000000000000000000000000003c270000000000013d2fd3c90be279a13b280000000000013d259648d795ed90"
    "3e240000000000013cf1485ab9b96b363ce18000000000013d132d55b529e42b3e580000000000013cf9c4485e794aea"
    "3ab90000000000013d23f6d68e7a013d3e300000000000013cf1eb5326217b5e3a3e0000000000013d2284cdf240ae4d"
    "3e540000000000013d02791e8f3cff9d3ef80000000000013cde7960f14324413ee00000000000013ce875d82076216c"
    "3a3b40000000000100000000000000003e170000000000013d1dcf046a17df3f3b9d8000000000013d2ee2e7acd446b5"
    "3e4e8000000000013d2aa1758c3643b33f570000000000013d26e50e7b80a15a3ed00000000000013cd6a025d291a4ee"
    "3b7e0000000000013d1e0700a4be435d3e060000000000013d1f0ba63c5882843bb54000000000013d2fde63550d3f0c"
    "3e060000000000013cffa83c0ad749c13f0e0000000000013d10279a823ed5283f12c000000000013d26c26376bdd70f"
    "3bc68000000000013d3361dce1a81c313db74000000000013d2ec1382d609edc3b2b0000000000013d3796bd7aeb3716"
    "3e010000000000013d20458895961dd23ea78000000000013d1d55b5a26489dc3edf0000000000013d1ec4eec39eab42"
    "3bb00000000000013d2ff92a29c03db43db40000000000013d1e1485567401473c278000000000013d26c2a4e3b7ac98"
    "3d000000000000013ccf1aacff48d8ed3f0a0000000000013d133a6b44d10f403eef0000000000013d1dbcd64b3b86e1"
    "3ba34000000000013d2f3fcf5b9aba933d054000000000013d2f967b8603489b3a21e000000000010000000000000000"
    "3d934000000000013d2fec419aef52253dc64000000000013d2eb9463e8904013ee50000000000013d1d85c8fbea45de"
    "3bd54000000000013d2fa46f755df8c93cd14000000000013d3fb2e8ba5907fa39c72000000000010000000000000000"
    "3d770000000000013d2efc7d661ba59a3d21c000000000013d2fd234a639b4f93e900000000000013cd6b7ca73cf3d57"
    "3c1f0000000000013d1eca12e50f22bd3c480000000000013cd8d706b4b91acb39778000000000010000000000000000"
    "3d4b0000000000013d2fb8e7a4df59453ccd4000000000013d30a00c41ba942b3ebb8000000000013d1f0275b8ab80c6"
    "3c350000000000013d2fa16978e82d1f000000000000000000000000000000003ad00000000000013d201aa5cf301fef"
    "3f500000000000013cc20bd3b66e8ae539e780000000000100000000000000003ec10000000000013d1db967457695f6"
    "3cf24000000000013d274a6af720c1d8000000000000000000000000000000003584c000000000010000000000000000"
    "000000000000000000000000000000003edc0000000000013d01ef48346350a03ea00000000000013cc3a233cdb28c81"
    "3d020000000000013d0f17b2e573a2a33bf60000000000013d03522e2fc0a29500000000000000000000000000000000"
    "000000000000000000000000000000003ae00000000000013cc20280ad9a03ef3f960000000000013cfb9d6b9868e890"
    "3e800000000000013cd76e557dbfa8263ee80000000000013cea6243cfe2d15c3ea00000000000013cdbe57c74460efc"
    "00000000000000000000000000000000000000000000000000000000000000003ea00000000000013cfe352db08e63af"
    "3e680000000000013ce7f8edcfbc4d540000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000024ac900000000001000000000000000000000000000000000000000000000000"
    "26f1b0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000001fe53000000000010000000000000000"
    "0000000000000000000000000000000019eb100000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "2e51c0000000000100000000000000002e87e00000000001000000000000000000000000000000000000000000000000"
    "0f6da0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "2d63d0000000000100000000000000002c9df00000000001000000000000000000000000000000000000000000000000"
    "0ab260000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "2905a0000000000100000000000000002a8ac00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "24f990000000000100000000000000002882d00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "27b400000000000100000000000000002725900000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "09f028000000000100000000000000002532000000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000237ef00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000021b6d00000000001000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003f400000000000013cc10183cce4291a"
    "0000000000000000000000000000000022a3000000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d878000000000013d2436956894363800000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3a13400000000001000000000000000038c7a0000000000100000000000000003782f000000000010000000000000000"
    "36c900000000000100000000000000003579e00000000001000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000034420000000000010000000000000000233e4000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000024ac9000000000010000000000000000"
    "0000000000000000000000000000000026f1d00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "1fe530000000000100000000000000000000000000000000000000000000000019eaf000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002e51c0000000000100000000000000002e87e000000000010000000000000000"
    "000000000000000000000000000000000f6da00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002ce000000000000100000000000000002c9df000000000010000000000000000"
    "000000000000000000000000000000000ab2600000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002905a0000000000100000000000000002a8ac000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000248000000000000100000000000000002882d000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000027b4000000000001000000000000000027257000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000009f0280000000001000000000000000025320000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000237ed000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000021b6d000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3f400000000000013cc10183cce4291a0000000000000000000000000000000022a30000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d878000000000013d24369568943638"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003a13400000000001000000000000000038c7a000000000010000000000000000"
    "3700000000000001000000000000000036c900000000000100000000000000003579e000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000034420000000000010000000000000000"
    "233e40000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3e8c0000000000013cf5d1091c484d943ea40000000000013cf4be2f58d3e9453e980000000000013ce2286eab9e7b6e"
    "3e100000000000013ce360b5f82b610c3e100000000000013ce0aac7b7d49d723d540000000000013cf0f97fcfb32d1b"
    "3d880000000000013cd8585e4e14d9d63d8a0000000000013cfcdbcac7003f523d6c0000000000013ced77a8f13ce897"
    "3d180000000000013cf1ed84f78352123d518000000000013d20d23198f1e2dd3d400000000000013d207cbc27f839a8"
    "3cf18000000000013d1a536f8f901df13cd10000000000013d30ba0e5fa85a583ed00000000000013cc537ecbafb9fcf"
    "3ed00000000000013cc46f3ebc2d2efe3e530000000000013d1db83f8e914b643e900000000000013ccd261c59422fa9"
    "3e9a0000000000013d03231768559bdc4312b800000000013d588228013d9d563e680000000000013ced5f657e6f45ae"
    "3f0c0000000000013cfd5883a84c70c13c71b51929b0c96900000000000000003e480000000000013ced5f657e6f8314"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3c100000000000013d02bc4b33a6266f3f240000000000013cfbf2c687de87363c8a8fa5be892e1d0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003c50476e9d9a1ea100000000000000003f1c0000000000013cfd5883a83ea963"
    "3de40000000000013ce6795bc086ed823d040000000000013cf168f410d3c1943dd80000000000013cebad2b82effcd6"
    "3d840000000000013cf11a49715a1b293db40000000000013cfa8f9a95ff7fae3d940000000000013cf776f46d80d355"
    "3d6f0000000000013d1780bfb269edb73d690000000000013d1fa8c3806e6cad3d4d0000000000013d209318226843d2"
    "3d008000000000013d1ea31eff16a6993cdc8000000000013d300b0aebf87c993cbf0000000000013d2efc583f0f962f"
    "3c908000000000013d2f28155805cf6b3c608000000000013d2eaca7a393daa63a026000000000010000000000000000"
    "3edc0000000000013cff416c67c40c0c3ee20000000000013cf642e3d5005c663e900000000000013cea1be870631f2e"
    "000000000000000000000000000000003d3e0000000000013d06f15dc075715c3e700000000000013cea1be7a030984e"
    "000000000000000000000000000000003c200000000000013cddd89764e851b700000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3e940000000000013cebf954786a33b1000000000000000000000000000000003df00000000000013ce2fa66ef1fbd38"
    "3e740000000000013cebf954786a33b13de80000000000013ce7b9009530104700000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003c76e28f56a293da000000000000000000000000000000000000000000000000"
    "3db40000000000013cf12710c6a6b1033d3e0000000000013d06ec3870ec2d6700000000000000000000000000000000"
    "3da40000000000013cf11a3f5ea97f103cdc0000000000013d132d55b529e42b00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003de80000000000013cd89a02fdffd7963d390000000000013d2f3f7f29b284df"
    "3df40000000000013cebad2b720e49463da80000000000013cf06b22fd3cf2a63dda0000000000013cfb9f7bbf89c7bd"
    "3ab50000000000013d241fb98d9182460000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003dea0000000000013cfd298ef796e9d5"
    "3c200000000000013cd3e5b94baeb704000000000000000000000000000000003c200000000000013cddd895f1861285"
    "3ddc0000000000013cf8ca3fff0a79083dc00000000000013cf9076b1f7858de3a468000000000013d26e677a734fff1"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3dd40000000000013cf5450c48c9eff13c200000000000013cd10e0c8a03784c00000000000000000000000000000000"
    "3c200000000000013cd9945c6592f9e9000000000000000000000000000000003dc20000000000013cf8229502eae7fe"
    "3d9b0000000000013d17650598d141903ba08000000000013d2aaacc45be83ac00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d7c0000000000013cf522cd44991b5400000000000000000000000000000000"
    "000000000000000000000000000000003b784000000000013d30474c0b3f895900000000000000000000000000000000"
    "000000000000000000000000000000003d9f0000000000013d1780bfb269edb73d990000000000013d1fa8c38035b942"
    "3bc40000000000013d2e97d3ec69be9f0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003db08000000000013d2006dd1a73b036"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d9c0000000000013d1f84be14cc96dc3d804000000000013d2082d651c67a963b754000000000013d2fd50977711e83"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3da04000000000013d20ebcedc97db830000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d828000000000013d20eae1e64d2b79"
    "3d350000000000013d1f31b926d08f9c3c254000000000013d2698594f90e8c200000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d518000000000013d1a9a553c6364be00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d368000000000013d1e624d481974953d134000000000013d2f8524eb9806f1"
    "3aadc000000000013d37aeb08f2a2b860000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d304000000000013d30283a449f4354"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d154000000000013d2fe5a0a8f7cb633cf70000000000013d2ea70cf98f0bc13a716000000000013d379893e8c3d786"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d190000000000013d2eb77af6c43b400000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003cf94000000000013d2f100727b5ce4f"
    "000000000000000000000000000000003c9ac000000000013d2e9a59ac25b5bd3a3dc000000000010000000000000000"
    "3f000000000000013cc014d3c0e9c01d3c968000000000013d2e9132a7fdf69900000000000000000000000000000000"
    "000000000000000000000000000000003b57c000000000013d3342a0a0a1822300000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003f100000000000013cc9cf40e370d5823e8a0000000000013d1c69b1ce02382f"
    "3a9c0000000000013d33ccaf1c7d4fec3f100000000000013ccef8b4442100373e870000000000013d1dd8e04965c83c"
    "00000000000000000000000000000000000000000000000000000000000000003cf18000000000013d2e36624e2d113b"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003cccc000000000013d2f058e14d76ad5"
    "3e8b8000000000013d1cfa91a77fb6723a9dc000000000013d34013b18137bf800000000000000000000000000000000"
    "3e878000000000013d1d68294d32f1170000000000000000000000000000000000000000000000000000000000000000"
    "3389c0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3ec00000000000013cc5c6debfe7342634ff000000000001000000000000000000000000000000000000000000000000"
    "3ec00000000000013cc6177e9476d8003ed20000000000013cf642e3d5005e3400000000000000000000000000000000"
    "000000000000000000000000000000003b0c4000000000013d335477f20de1d900000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003ae68000000000013d333e9299c30f4a3e3f0000000000013d1c95681aa19e0f"
    "3a50a000000000013d33d695740b8e943ecc0000000000013cff416c67c40d103ed00000000000013cf3c848906d3d37"
    "42be3000000000013d5f85d9dab9448b43294000000000013d33ac067eae807642366000000000013d6ab0a97597eecb"
    "42a43800000000013d651cf5fcbfc04641f3c000000000013d381cd1532b748341b5e000000000013d62e60faab06aac"
    "41a78000000000013d2003fccb3494d241c7d000000000013d5abfda9cfe0ff64173d000000000013d54aa343e6a239a"
    "418bf400000000013d7979909dab33d5417c1600000000013d85cb7c63316d7441190400000000013d75a98b9999ae08"
    "40f14c00000000013d783048805d59e940d15400000000013d74d842667bc6564344e800000000013d569bf34c05ecb8"
    "4271a800000000013d69715c8abba7e43e77e000000000013d760f3e66ecc3bd4315c000000000013d273ad18c164042"
    "42f50000000000013d218bc6df795c853cb40000000000013cf05ed590fe915d3ccc0000000000013cee8ba8af5b4941"
    "3cf30000000000013d07e55c16f81ed13c000000000000013cc4a1847333e66f3c100000000000013cd210aa1a131fd8"
    "3b780000000000013ce1d2af185ef7ad3bb00000000000013cca851e396a10b33bca0000000000013cfc2815b1c75e4c"
    "3ba80000000000013ce640de579f49dc3b720000000000013d0922fe88751aac3b990000000000013d1c7acebcb1922d"
    "3b900000000000013d1f4f774a2fd2f83b420000000000013d19d1d410a67b6a3b228000000000013d2f757b48990dbc"
    "3cc00000000000013cd3bacaef7f7f9b3cc00000000000013cd2e95cb17d52273c8e0000000000013d1d0c2cf2017442"
    "3cdc0000000000013cf81321746274e93cdc0000000000013cf97223ef88d3b441205000000000013d45b997560069ab"
    "3e900000000000013cf8eee5d72e0f843ea80000000000013cf8e438d0fe4b1f00000000000000000000000000000000"
    "3e140000000000013ce838e37636394f3e140000000000013ce4d579a5c9c4ce3d540000000000013cf0f97fcfb32d1b"
    "3d940000000000013ce449a3ebbc0ade3d880000000000013cfaa3801a279cea3d760000000000013cf727294fd46d9d"
    "3d240000000000013cfde132f1dade1f3d510000000000013d205728ddb7c6703d400000000000013d207cbc27f839a7"
    "3cf10000000000013d1992e166e3c5503cd08000000000013d303c1d027dc11a3ed00000000000013cc537ecbafb9fcf"
    "3ed00000000000013cc46f3ebc2d2efe3e530000000000013d1db83f8e914b6200000000000000000000000000000000"
    "3e920000000000013cfa7f5b7cc54df63e900000000000013cf8d45b766189273ea80000000000013cf8d45b765207b5"
    "3ee00000000000013cdb6c4f854f60833ce60000000000013d0d55dfcae39ed53d940000000000013ce716d9fc147ac8"
    "3d500000000000013cea65677df7c03c3d880000000000013cd829261fabff3f3d900000000000013d00d8095b8c7cf7"
    "3d6c0000000000013cec387eee7ae9fc3d2a0000000000013d02a5e395bc99813d520000000000013d20a821d085004d"
    "3d3f0000000000013d1df1a1a41d07983cf28000000000013d19e719bff1eda53cd24000000000013d30a9c831c9e842"
    "3ed00000000000013cc537ecbafba6d33ed00000000000013cc47439058c1e873e898000000000013d1d4c924d4b0c50"
    "3ed80000000000013ce3017c60ed7e863ed80000000000013ce4ed1f7cd610b13e900000000000013cf8d45b76618927"
    "3ea80000000000013cf8d45b765207b53ef00000000000013ceb6c4f854f60813ce60000000000013d0d55dfcae39ed5"
    "3d940000000000013ce716d9fc147ac83d500000000000013cea65677df7c03c3d880000000000013cd829261fabff3f"
    "3d900000000000013d00d8095b8c7cf73d6c0000000000013cec387eee7ae9fc3d220000000000013cf9d2000a679977"
    "3d528000000000013d211e948816eafa3d3f0000000000013d1df1a1a41d07983cf28000000000013d19e719bff1eda5"
    "3cd24000000000013d30a9c831c9e8423ed00000000000013cc537ecbafba6d33ed00000000000013cc47439058c1e87"
    "3e8a0000000000013d1ddfa43abaf3753ee40000000000013cefad24a18bd2dd3ee40000000000013cf17044e807b892"
    "3e900000000000013cf8d45b766189273ea80000000000013cf8d45b765207b53ef00000000000013ceb6c4f854f6081"
    "3ce60000000000013d0d55dfcae39ed53d940000000000013ce716d9fc147ac83d500000000000013cea65677df7c03c"
    "3d880000000000013cd829261fabff3f3d900000000000013d00d8095b8c7cf73d6c0000000000013cec387eee7ae9fc"
    "3d220000000000013cf9d2000a6799773d528000000000013d211e948816eafa3d3f0000000000013d1df1a1a41d0798"
    "3cf28000000000013d19e719bff1eda53cd24000000000013d30a9c831c9e8423ed00000000000013cc537ecbafba6d3"
    "3ed00000000000013cc47439058c1e873e8a0000000000013d1ddfa43abaf3753ee40000000000013cefad24a18bd2dd"
    "3ee40000000000013cf17044e807b89243363000000000013d47e5df4944fbcd43366000000000013d481992920056a0"
    "43366000000000013d481992920056a0000000000000000000000000000000003b10c000000000013d4a8cf5e9dd5755"
    "3d026000000000013d4c7d6e84beb6b03be40000000000013d4b1cc42d7251b13ae25000000000013d5195c93fd4a9ce"
    "3c000000000000013d46675478079e903bafa000000000013d5d97c25a350e873c413800000000013d62ca36ef86fc7c"
    "3cf3c800000000013d66d27fb281a6583d02a800000000013d6ad0d469429fc33cce8000000000013d3cf390237bf6c7"
    "3cedc000000000013d4e7de431378f6d3cbc4c00000000013d7569c4b9a81bc23ceca000000000013d7569826b38d31b"
    "3d315600000000013d7d65fad95370583d449800000000013d62c9f3c678a0dc3d4ed000000000013d5a64e9ce240a75"
    "3bca6000000000013d5244352a50308e3cf29000000000013d4b89a174ca79c340e70000000000013d1f9c610126cf83"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000040e70000000000013d1f9c610126cf83"
    "0000000000000000000000000000000040e70000000000013d1f9c610126cf8300000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003b10c000000000013d4a8cf5e9dd5755"
    "3d026000000000013d4c7d6e84beb6b03be40000000000013d4b1cc42d7251b13ae25000000000013d5195c93fd4a9ce"
    "3c000000000000013d46675478079e903bafa000000000013d5d97c25a350e873c413800000000013d62ca36ef86fc7c"
    "3cf3c800000000013d66d27fb281a6583d02a800000000013d6ad0d469429fc33cce8000000000013d3cf390237bf6c7"
    "3cedc000000000013d4e7de431378f6d3cbc4c00000000013d7569c4b9a81bc23ceca000000000013d7569826b38d31b"
    "3d315600000000013d7d65fad95370583d449800000000013d62c9f3c678a0dc3d4ed000000000013d5a64e9ce240a75"
    "3bca6000000000013d5244352a50308e3cf29000000000013d4b89a174ca79c300000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003b10c000000000013d4a8cf5e9dd57553d026000000000013d4c7d6e84beb6b0"
    "3be40000000000013d4b1cc42d7251b13ae25000000000013d5195c93fd4a9ce3c000000000000013d46675478079e90"
    "3bafa000000000013d5d97c25a350e873c413800000000013d62ca36ef86fc7c3cf3c800000000013d66d27fb281a658"
    "3d02a800000000013d6ad0d469429fc33cce8000000000013d3cf390237bf6c73cedc000000000013d4e7de431378f6d"
    "3cbc4c00000000013d7569c4b9a81bc23ceca000000000013d7569826b38d31b3d315600000000013d7d65fad9537058"
    "3d449800000000013d62c9f3c678a0dc3d4ed000000000013d5a64e9ce240a753bca6000000000013d5244352a50308e"
    "3cf29000000000013d4b89a174ca79c3"};
inline constexpr FrozenNetworkAuthority<21, 112> kAprox21Authority{
    {"h1", "he3", "he4", "c12", "n14", "o16", "ne20", "mg24",
     "si28", "s32", "ar36", "ca40", "ti44", "cr48", "cr56", "fe52",
     "fe54", "fe56", "ni56", "neut", "prot"},
    "3ff0000000000000400800000000000040100000000000004028000000000000402c0000000000004030000000000000"
    "40340000000000004038000000000000403c000000000000404000000000000040420000000000004044000000000000"
    "40460000000000004048000000000000404c000000000000404a000000000000404b000000000000404c000000000000"
    "404c0000000000003ff00000000000003ff00000000000003ff000000000000040000000000000004000000000000000"
    "4018000000000000401c000000000000402000000000000040240000000000004028000000000000402c000000000000"
    "403000000000000040320000000000004034000000000000403600000000000040380000000000004038000000000000"
    "403a000000000000403a000000000000403a000000000000403c00000000000000000000000000003ff0000000000000"
    "0000000000000000401edf6d330941c8403c4bc89f40a28740570a6d9be4cd75405a2a3d1cc100e7405fe7bd512ec6bd"
    "406414bb6ed677704068c840b780346e406d91367a0f90974070fc851eb851ec40732b85f06f6944407560e8a71de69b"
    "407777a29c779a6b4079b7810624dd2f407e87f3b645a1cb407bfb53f7ced917407d7c504816f007407ec3eb851eb852"
    "407e400c49ba5e350000000000000000000000000000000000000000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff00000000000003ff00000000000003ff000000000000040000000000000004000000000000000"
    "3b002d35e95cfa093b1835a3280d08323b2010d7aef917503b3815400c10b9843b3c1a5f2593a2463b400cdc2d714120"
    "3b440fc40203d1313b481168304c88043b4c12dd0e75467b3b500a9423cd5c753b520bc4967f0d293b540ce6f377444b"
    "3b560e4cf94a85993b580f5833cc13393b5c11733638bfe93b5a105ab3d216cf3b5b1037ced5dfc33b5c10938a1e39fd"
    "3b5c115b39a693933b0032eb3e227d9a3b002d35e95cfa094079fb4bb586b3883e94c2cbf65a0184409f25be08888517"
    "40812ca41b710f823fc9943529e0f2843ee7366b3fafb8d33df1d319aed61327412290df4ba761cd3fbd47dc129c9225"
    "414de82dc39616a33d841a922f6701e8412caa5aef9180a43d0a00fbab4ed29b414160f4589040754209489720d96be6"
    "41e4fa5baaec2c093cfa0c6b59e3d30a40e8c8003e6fcd7f3e5eacf58675d9a84112644f99f0f6c6422021e6f57a7b45"
    "41e6afd10a6bd80e3e5006abfaac672540d6c48a8018eac43e756c6ab322584940f653ece8c59b2341edd518815ec850"
    "41da7053edcb27513e82a08f92186c9840784bea3189f9f33de264b3de04ee6740d0c9f5255f510f4179bf1c29b53c6d"
    "41b02c01f2a075c13e7010a00e7cb13040855ff5580a4c8e3ef098a9ae26f83a40208971b7d27f1c41e5bc2292e1be63"
    "41c0bd14064b0cba3e63c85c69139f813fc5a1bfbdb4fa143cd85ddc2580b403405363e5923bff5f4087496ced864b04"
    "41b6d9dbca6b10553e9592f36d9e4e8a3f818077b6812d7c3c72ce1db118fe224037df563535427a3fe1f09fbe40d854"
    "41a6a201297469d83ef02d82d62b629b3f5f14346525b15f3c4810fd29489987400b60d0e72e5bec3f8c4beff9940d3e"
    "418c79b75ebd4f563ef5516b1591a83e426934861926f4423d9fcb0c91eea80a427c5f22d3848d523ca7a6b0c24ffcd0"
    "419261dd6587f5033fb92f5aee473fac420654b6fa85fae03844eef1d05840324217b143ddb2598041c8fac1830941a2"
    "41ca1508161ee9063fe287492664ff0e3ee237efc6b312c43eede092fe2bb55600000000000000003e0c6f0e9c2e489e"
    "428982bf16a742624195ddbf4bfee85941ed23675b8b707c41b26017af0b78263feffb6077120c0d3f427e23b7cfcc00"
    "4190f8e6c3cc2e3941700e72be7be8f9429da95fe807dd7c3e9b9bcbbd826d0942a997cf0de52b8c3de975fb0000f851"
    "3ffb18f78bb06b7440d642260eb12a2241aafabe37a591293f5046081d446d2700000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000274cea9d4df9a833"
    "000000000000000029919e00c1b0c96e0000000000000000000000000000000000000000000000000000000000000000"
    "22854012bd16555600000000000000001c8b18a1c3c9127f000000000000000000000000000000000000000000000000"
    "000000000000000031020e46c3f3cf623137f8e93a4bf64d000000000000000011fdaebf931d08440000000000000000"
    "00000000000000003003f827821a28b62f3e0d20fae2c64500000000000000000d2885cc699725c30000000000000000"
    "00000000000000002ba5a1bb308a85792d2ad54fa8097f04000000000000000000000000000000000000000000000000"
    "00000000000000002799bbe9dc6f22222b22e92e648c6485000000000000000000000000000000000000000000000000"
    "00000000000000002a5430c0460c351d29c5ab9aa30d7fd5000000000000000000000000000000000000000000000000"
    "00000000000000000c80347ef543a3e027d2127286b0b2cb000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000261efe3ac6a61586000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002456b2c5dfe5e89f0000000000000000426c90ac0ef6e07c0000000000000000"
    "426e1b6f51c41559000000000000000025431d364bb5ab8c000000000000000041680dc3790610000000000000000000"
    "42257fa2f185c65700000000000000004051c1342fb504a6000000000000000000000000000000000000000000000000"
    "00000000000000003cdff90982675c9e3b779f708173b76a3a22cccf4bb402873979336e31bf4588382a0496bb35f8ac"
    "3feff8c362a6fae83f4cf2756414600036f21ff514a8852825de403081d0a9f43e948cd47db3866d0000000000000000"
    "3f049af59bd6c01900000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000274cea9d4df9a833000000000000000029919e00c1b0c96e"
    "000000000000000000000000000000000000000000000000000000000000000022854012bd1655560000000000000000"
    "1c8b18a1c3c9127f000000000000000000000000000000000000000000000000000000000000000031020e46c3f3cf62"
    "3137f8e93a4bf64d000000000000000011fdaebf931d0844000000000000000000000000000000003003f827821a28b6"
    "2f3e0d20fae2c64500000000000000000d2885cc699725c3000000000000000000000000000000002ba5a1bb308a8579"
    "2d2ad54fa8097f0400000000000000000000000000000000000000000000000000000000000000002799bbe9dc6f2222"
    "2b22e92e648c648500000000000000000000000000000000000000000000000000000000000000002a5430c0460c351d"
    "29c5ab9aa30d7fd500000000000000000000000000000000000000000000000000000000000000000c80347ef543a3e0"
    "27d2127286b0b2cb00000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "261efe3ac6a6158600000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "2456b2c5dfe5e89f0000000000000000426c90ac0ef6e07c0000000000000000426e1b6f51c415590000000000000000"
    "25431d364bb5ab8c000000000000000041680dc379061001000000000000000042257fa2f185c6570000000000000000"
    "4051c1342fb504a600000000000000000000000000000000000000000000000000000000000000003cdff90982675c9e"
    "3b779f708173b76a3a22cccf4bb402843979336e31bf4588382a0496bb35f8ac3feff8c362a6fae83f4cf27564146000"
    "36f21ff514a8852825de403081d0a9f43e948cd47db386e800000000000000003f049af59bd6c06c0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000417bcdb075b8737ec194e336af937474419e81bcfa2bae80c111e4aef2e962c24114cda412b70a92"
    "c049938492f98ffc409562fc0e7b03e5407392ff97f053164064a1c2df2f3e26400d0ffd36fee2df401698d8324a6eed"
    "4005190f1f1f296b3fbce9f49f2f5b413f861b7d528d53aa0000000000000000c1f358f80c970dc6c225adecc139a7c2"
    "42291628de7e66ba4120532656a97ab4c1e0152177dfeae8c180974ca229b2c045d1c16e4cd8a0c6c1658721e15f79e1"
    "41f9215dcb2f1220bc7b8d7d43d083c8c1458721e15f4339bf40ef83752ee902bf55810624dd2f1b0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003efb53e8671d9ce1c212d906586a789abc94aa1df2dc62d60000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000003c4187bcd2b8fff4"
    "4209215dcb3d6824c0e77b73eaf1d475bffe49b54296b080c0d6e0d106162139c07edcd601d6e05ec0a3e2d0e3a55eb7"
    "c086804d23890f46c0416a75161a9429c034dbda2952c0a1c0171fac7bc03d76bfcc7b323615c77fbf977de727cf5361"
    "bf7a77294b5816330000000000000000bf4c05d4f626aecb3f1f7d82240baa9641366caab26d2a4a3cb83951b7698871"
    "41c7e6949734cf3541d5cb6f7581678dc19025596907726b0000000000000000c0213efc394521a1c1702559e9f7815c"
    "3f696399a14cd2cabf2c58e1b24628400000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000004192d63da5335ad2"
    "0000000000000000c0f63e209d7bff214172d63da5335ad2c0eab0f40dd18b693f82d0e5604189380000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000bc4fa1916a339f120000000000000000c0aec5ddc5c5b09b40214221171ba191"
    "3ec392f7dd8e8a12c09edcebf444125b3fb76cb00ee3a81e000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000040f9bb0d6d5520de3ff525da73cc9a8c40f310ae3dd7ff4240a34a0c91184b04c0c8dba2646ac902"
    "3d80c0cf2780819600000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000040d78cdb7bb6eb243f3542aa868c6c7b"
    "00000000000000003f2c58e35e1090a440cdd44ae6fc193ec0b0e039daa6cb7a3d0f88ea2749920a0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000040c8d29e63fa63ae3f38cdc6f24e7e9000000000000000003f3089a5232618230000000000000000"
    "40b3b0437f17ed61c06e7a4ce6ae83483e63dd92e83b826a000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000040717b9e529ff9f70000000000000000"
    "00000000000000003e33acb8282915420000000000000000000000000000000040716a75161a9429c064dbda298028cd"
    "3e84feb4fc4783d000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000407b2fe41d2965d00000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000406777556e96a34ec04a03a20e2c16983e3573e926e21f500000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000040696225ae79bf310000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "404ce7979c5432f6c001ccff634afea83eee3bec15a8f703000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000402164c6299c881f0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000400394b28600bbd6bfd02713f306e8a1"
    "3d64326ae5164b1f00000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003fea9912c75267970000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003fd19eb5edabb285bfb3d95ef883712e00000000000000003d27aeac9e112a5e"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003fd54ad4f8c1bb0c0000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000003fb580d18d3850fe0000000000000000c2317547d35c60aa"
    "3f5995fc76fd1aa800000000000000003cf3ae926505bedcc22beed9522d62eb3f54b65eafc3a1360000000000000000"
    "0000000000000000be4dae551a5064610000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000004232212d0cb888d1c2648b88512b6f414172ebb0068c1bae3d58ffcdaf3e243b"
    "c25f4ce19a158f3741659cea7ddcc57f000000000000000000000000000000003e50a1c4e47109800000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "42654e469ce5c1dac1739f155c1f8501000000000000000042621c226bc34b2dc170d1808d1413c00000000000000000"
    "00000000000000003fb058bc3549f2810000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003f88851a4feeebc2415bba68a54ce4580000000000000000bd5a419deb71cb15"
    "000000000000000041567254bd66a003000000000000000000000000000000003646e703521b6fd30000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000000000000000000000c1e57ca72b855fc8"
    "c21859754598dd8c39c6a8d345e5f29a0000000000000000c217381b85127f8cc1c5c6a150c6cda60000000000000000"
    "00000000000000003e046d5402af838a0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003da107ad07e2ccc9c10fb07798775de6c1266caab26d2a4a3d0da0f3c636afae"
    "c1b7e6949734ce46c1c5cd09e945ca9845493b22000c506145e0e7387d6b2f5c44b61efb22deef0b45293b226face0f3"
    "44a59c06f76e07d4443e8c52d1c445bb4473602be571ca2e4457802b6d5e992c440951976b3f120343fcfc12793ff648"
    "43e107e81f102216438e890372fad0c84362e99a7d687c7c4345fd8e68e12d50800000000000000045db0ac6fd06bb10"
    "460a13d5e049e2f145025883c111da26c0f31de3d031ade046096c32b8f0cee245c01ec5fb3d26283faa757cdc0043d3"
    "bfc3da3682c7b905bfd46db3cd243a85bf20c6f436e150bc3f2328b83a5b48c2be7d33674350362f3eca27411563dab6"
    "3eb40cb6c568744d3ea7670ab223b43f3e4f0f83204395b73e630e7f47b0260b3e56362e091005353e0e504bc809685e"
    "3dd993808ba2086000000000000000003fd4ceb81c9f4524c00d064f22e2bb87400b49278b20eeb33f5a57f1d5538b9d"
    "3fba1bf2e7b175253fcb09dca8d82d4dc3b4e31793885d35417bcdb075b8737ec194e336af937474419e81bcfa2bae80"
    "c111e4aef2e962c24114cda412b70a92c049938492f98ffc409562fc0e7b03e5407392ff97f053164064a1c2df2f3e26"
    "400d0ffd36fee2df401698d8324a6eee4005190f1f1f296c3fbce9f49f2f5b413f861b7d528d53ab0000000000000000"
    "c1f358f80c970dc6c225adecc139a7c242291628de7e66ba4120532656a97ab3c1e0152177dfeae8c180974ca229b2c0"
    "417beb280013148ec194f05e001e329e41f0fe339a17775dbfc044eaa57ccc0cc092cb41cd4afde1c04a4f096457eef3"
    "40958c8d3b2e897040749f26415e2b0e40658a8ad9671acc400e4484df8124614017778d0349832e40067e45a7143def"
    "3fbf09ff04ddd1643f87ccafe34725540000000000000000c1f358f80c970735c225ac93d568a3fd422914100f75b480"
    "41586e001e2ba65ac1f04bfc25fc6c74c1e0c6cd1fb5c13f417beb280013148ec194f05e001e329e41f0fe339a17775e"
    "bfc044eaa57ccc0cc092cb41cd4afde1c04a4f096457eef340958c8d3b2e897040749f26415e2b0e40658a8ad9671acc"
    "400e4484df8124614017778d0349832e40067e45a7143def3fbf09ff04ddd1643f87ccafe34725540000000000000000"
    "c1f358f80c970735c225ac93d568a3fd422914100f75b48041586e001e2ba65cc1f04bfc25fc6c74c1e0c6cd1fb5c140"
    "417beb280013148ec194f05e001e329e41f0fe339a17775ebfc044eaa57ccc0cc092cb41cd4afde1c04a4f096457eef3"
    "40958c8d3b2e897040749f26415e2b0e40658a8ad9671acc400e4484df8124614017778d0349832e40067e45a7143def"
    "3fbf09ff04ddd1643f87ccafe34725540000000000000000c1f358f80c970735c225ac93d568a3fd422914100f75b480"
    "41586e001e2ba65cc1f04bfc25fc6c74c1e0c6cd1fb5c14045e51901cf85f73d45e51901cf85f70b45e51901cf85f70b"
    "00000000000000003da7f3ff069c4dd23fa40a1fdabb85b53e859bb843aaf6ab3d7e0ee06948eb223ea44ffb13881550"
    "3e3d83383fcf08a13ec89275f58758a13f769101fd42796d3f819668da129d1c3f79dbc58eab162c3f8740c84dbd4751"
    "3f2e9473b977bdec3f5e09a3cc14bf153c6ca8e667150b9b3f99bad89305a5f23fe00363c919a82e3f63c65944a6528c"
    "3fd8ba97c96ffcf73e72e7d205af04833f89f9380fceb49843b5b1ca6279bdbb00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "43b5b1ca6279bdbb000000000000000043b5b1ca6279bdbb40b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b013000000000040b0140000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b013000000000040b014000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b013000000000040b0140000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b013000000000040b014000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b013000000000040b0140000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b013000000000040b014000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b013000000000040b0140000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b013000000000040b014000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b013000000000040b0140000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b013000000000040b014000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b013000000000040b0140000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b013000000000040b014000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b007000000000040b0080000000000"
    "40b009000000000040b00a000000000040b00b000000000040b00c000000000040b00d000000000040b00e0000000000"
    "40b00f000000000040b010000000000040b011000000000040b012000000000040b013000000000040b0140000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b013000000000040b014000000000000000000000000003da7f3ff069c4dd23fa40a1fdabb85b5"
    "3e859bb843aaf6ab3d7e0ee06948eb223ea44ffb138815503e3d83383fcf08a13ec89275f58758a13f769101fd42796d"
    "3f819668da129d1c3f79dbc58eab162c3f8740c84dbd47513f2e9473b977bdec3f5e09a3cc14bf153c6ca8e667150b9b"
    "3f99bad89305a5f23fe00363c919a82e3f63c65944a6528c3fd8ba97c96ffcf73e72e7d205af04833f89f9380fceb498"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b007000000000040b008000000000040b009000000000040b00a000000000040b00b0000000000"
    "40b00c000000000040b00d000000000040b00e000000000040b00f000000000040b010000000000040b0110000000000"
    "40b012000000000040b013000000000040b014000000000000000000000000003da7f3ff069c4dd23fa40a1fdabb85b5"
    "3e859bb843aaf6ab3d7e0ee06948eb223ea44ffb138815503e3d83383fcf08a13ec89275f58758a13f769101fd42796d"
    "3f819668da129d1c3f79dbc58eab162c3f8740c84dbd47513f2e9473b977bdec3f5e09a3cc14bf153c6ca8e667150b9b"
    "3f99bad89305a5f23fe00363c919a82e3f63c65944a6528c3fd8ba97c96ffcf73e72e7d205af04833f89f9380fceb498"
    , 81920u, 49151u, true, false,
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003bc48000000000013d1f990ad9c292a33db50000000000013d059326f3179110"
    "3dc0c000000000013d2f35935a72c783000000000000000000000000000000003c26c000000000013d2f5cc083956ff0"
    "3b288000000000013d25fdf7fcbbb6403e240000000000013cf13c63849911e73ce18000000000013d13200eb92c7bf3"
    "3e580000000000013cf9adfd9e7d597f3ab88000000000013d237fb1dff9d5e73e2c0000000000013cef41d3f5821632"
    "3a3e0000000000013d227574b60437343e540000000000013d0269cf024c93483ef00000000000013cd44014b60e0c85"
    "3ee00000000000013ce86814b934edab3a3b80000000000100000000000000003e170000000000013d1db32aa4b4e8a8"
    "3b9d8000000000013d2ec60c235c1bad3e4e8000000000013d2a8893e557c61e3f570000000000013d26cfaa695a8a2f"
    "3ed00000000000013cd69175f0e6e9073b7f0000000000013d1ef3182bf956113e058000000000013d1e37dbc03fc2a0"
    "3bb54000000000013d2fbda7bce76a773e0a0000000000013d02a1a6f349b83a3f100000000000013d11299c8eb2ef9c"
    "3f12c000000000013d26b1a5cca9acef3bc6c000000000013d338a94519fd0643db7c000000000013d2f47b94bbe6655"
    "3b2b0000000000013d377c99f6f706ef3e010000000000013d203380c3e79cee3ea78000000000013d1d3534240c51f6"
    "3edf0000000000013d1eaba4407cba853bb00000000000013d2fdee24c01a0033db48000000000013d1eb0b6f8fa179c"
    "3c280000000000013d27233be6edee643d000000000000013ccef609435890e43f0a0000000000013d1323c4d7e7e8a3"
    "3ef08000000000013d1f8b3bd2e1fea83ba3c000000000013d2ff279b7fae5a63d058000000000013d2fce1337fdd48c"
    "3a2220000000000100000000000000003d93c000000000013d304bffeeebceda3dc6c000000000013d2f43277ee5192c"
    "3ee58000000000013d1e1bab06721b1a3bd54000000000013d2f84f63b4d61953cd18000000000013d3fff251e86b1fc"
    "39c760000000000100000000000000003d778000000000013d2f803476e677453d224000000000013d3046caec565d2b"
    "3e900000000000013cd69f3bb942d0943c1f0000000000013d1ea8ca907723463c480000000000013cd8b6166f75bce0"
    "397780000000000100000000000000003d4b0000000000013d2f8ed7110299103ccd8000000000013d30ae30be7c7457"
    "3ebb0000000000013d1e57882c387ceb3c34c000000000013d2f25b75093235100000000000000000000000000000000"
    "3ad00000000000013d201aa5cf301fef3f500000000000013cc20bd3b66e8ae539e78000000000010000000000000000"
    "3ec10000000000013d1d97fd642554b63cf24000000000013d27303c7e7d0c2a00000000000000000000000000000000"
    "3584c000000000010000000000000000000000000000000000000000000000003edc0000000000013d01ef48346350a0"
    "3ea00000000000013cc3a15a089fed453d020000000000013d0f165a06f2ef853bf60000000000013d03522e2fc0a295"
    "00000000000000000000000000000000000000000000000000000000000000003ae00000000000013cc201b8eb5738c4"
    "3f980000000000013cfe1ae00f66b5d93e700000000000013cc76a4632afc5453ee40000000000013ce5f6d741561842"
    "3ea00000000000013cdbdd079fb9f3a70000000000000000000000000000000000000000000000000000000000000000"
    "3e9c0000000000013cfa655ffbf673753e680000000000013ce7ea6762b044b33fcd8000000000013d1fd35d140eeea3"
    "3bac0000000000013d003a12414a74543fca0000000000013d104123260d117b3b230000000000013d27e139de75c194"
    "3d5e0000000000013d51b6bb2a7b53153e395000000000013d523204a6c5a8333f25ec00000000013d6a00525f3166f6"
    "3cc96000000000013d68f2cd17af106c0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000024ac9000000000010000000000000000"
    "0000000000000000000000000000000026f1b00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "1fe530000000000100000000000000000000000000000000000000000000000019eb1000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002e51c0000000000100000000000000002e87e000000000010000000000000000"
    "000000000000000000000000000000000f6da00000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002d63d0000000000100000000000000002c9df000000000010000000000000000"
    "000000000000000000000000000000000ab2600000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000002905a0000000000100000000000000002a8ac000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000024f990000000000100000000000000002882d000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000027b4000000000001000000000000000027259000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000009f0280000000001000000000000000025320000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000237ef000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000021b6d000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3f400000000000013cc10183cce4291a0000000000000000000000000000000022a30000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d898000000000013d26fad5f7ca3003"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003a13400000000001000000000000000038c7a000000000010000000000000000"
    "3782f00000000001000000000000000036c900000000000100000000000000003579e000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000034420000000000010000000000000000"
    "233e40000000000100000000000000003bdec000000000013d37f1041eb7430600000000000000000000000000000000"
    "3c49c000000000013d33feae36e9c5950000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "24ac80000000000100000000000000000000000000000000000000000000000026f1d000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000001fe5300000000001000000000000000000000000000000000000000000000000"
    "19eaf0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000002e51c000000000010000000000000000"
    "2e87e000000000010000000000000000000000000000000000000000000000000f6da000000000010000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000002ce00000000000010000000000000000"
    "2c9df000000000010000000000000000000000000000000000000000000000000ab26000000000010000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000002905a000000000010000000000000000"
    "2a8ac0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000024800000000000010000000000000000"
    "2882d0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000027b40000000000010000000000000000"
    "272570000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000009f02800000000010000000000000000"
    "253200000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "237ed0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "21b6d0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003f400000000000013cc10183cce4291a00000000000000000000000000000000"
    "22a300000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d860000000000013d23d3634435f73a0000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003a134000000000010000000000000000"
    "38c7a0000000000100000000000000003700000000000001000000000000000036c90000000000010000000000000000"
    "3579e0000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "34420000000000010000000000000000233e40000000000100000000000000003bd4c000000000013d3027d712a549fd"
    "000000000000000000000000000000003c3f0000000000013d28124b853741fe00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003e8a0000000000013cfdeca01fa015e33ea40000000000013cfea3e7e3b92908"
    "3e880000000000013cd92cbaaec31cae3e100000000000013cec9d33c24c83b63e140000000000013ceec3ade81948ca"
    "3d4c0000000000013cf184295ca41ebf3d880000000000013ce1f482adf113633d860000000000013d01fb9ba0f130be"
    "3d680000000000013cf29caa4c2844513d0c0000000000013ceed484e1a71a663d470000000000013d20490a3b531c48"
    "3d360000000000013d20af237ce15b133ce80000000000013d1a8fc6f12ab5663cc70000000000013d30a561d7f7a964"
    "00000000000000000000000000000000000000000000000000000000000000003f588000000000013d2214e965c5c3e0"
    "3f590000000000013d1fe3bbc85411e53e4f0000000000013d1e621a4a10396b3f0c0000000000013d1bdb360446c058"
    "3e920000000000013d015bda024f6ae343518800000000013d6f987ec9d7f58f3e580000000000013ce1d65c9c5c0a9c"
    "3f080000000000013cfe8f8833a8338a3c71ca0af0bdf0e100000000000000003e380000000000013ce1d65c9c5c37e6"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003c110000000000013d03e81004915cea"
    "3f240000000000013d00fa681cac52513c8aaf10691ce951000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003c530f79a571ffe90000000000000000"
    "3f180000000000013cfe8f883396c47b3de40000000000013ceb41304866501d3d000000000000013cf0e788992b8676"
    "3dc80000000000013ce0c8d7f0adfd4d3d800000000000013cf096f271e5da5e3db00000000000013cf9bf2b718b09f3"
    "3d900000000000013cf6c11e13f8cecc3d690000000000013d16f7b3abe283f13d658000000000013d207de95fa5bb95"
    "3d478000000000013d2042a6a8ed77fa3cfb8000000000013d1ee5c4f67ffef33cd7c000000000013d302d04979a690a"
    "3cba0000000000013d2f6feb5cb7260b000000000000000000000000000000003c8b8000000000013d2f672c7240aa91"
    "3c604000000000013d30835681fae48e3eb23c00000000013d6a054aacc2ef4a3a02c000000000010000000000000000"
    "3edc0000000000013d02be850321e33e3ee80000000000013d019e75eff6df423e800000000000013cdfb5f9f9877fd0"
    "000000000000000000000000000000003d380000000000013d064417a98e96c83e600000000000013cdfb5f8fc4b1393"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003e800000000000013cdb2e43fa74246a"
    "000000000000000000000000000000003df00000000000013ce704d04efcdbba3e600000000000013cdb2e43fa74246a"
    "3de00000000000013ce32ead81db593d0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003c6fa9916a339f130000000000000000"
    "000000000000000000000000000000003db00000000000013cf0a35478b05d3b3d380000000000013d0640094183ef9f"
    "000000000000000000000000000000003da00000000000013cf096e6a5d9a7423cdc0000000000013d13200eb92c7bf2"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003de80000000000013cddd8ff3b55ca653d348000000000013d2f0509c648b0e5"
    "3de80000000000013ce42436451d40423da40000000000013cf096ec960687a13dd40000000000013cf9bf0d1daa2cd4"
    "3ab50000000000013d240e508e9a92df0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003de40000000000013cfb2d0bdcdea2a600000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003dd80000000000013cf9bf1c4791ad0f"
    "3db80000000000013cf6c11e13f8cec43a468000000000013d26d4f78efde6c400000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003dd00000000000013cf4a052645c3f4000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3dbc0000000000013cf6c11e13f8cec73d960000000000013d17194cd5d3b7e53ba08000000000013d2a94266ebd35c1"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d680000000000013ce5f6dee894b23300000000000000000000000000000000"
    "000000000000000000000000000000003b734000000000013d2f4f2c3c20835600000000000000000000000000000000"
    "000000000000000000000000000000003d990000000000013d16f7b3abe283f13d958000000000013d207de95f81d505"
    "3bc40000000000013d2e7bc81e5759600000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003dab8000000000013d202f25329d95b300000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d980000000000013d205d2f0190a6ff"
    "3d7a0000000000013d1ffb87f0e15cef3b758000000000013d300904367042c300000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d9a8000000000013d20b42ebf43b88500000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3d7e0000000000013d209b37d1b506783d310000000000013d1e8f7b1fcc77043c25c000000000013d2705380b4db6e9"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d4d0000000000013d1aad28cfb8791a00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d330000000000013d1f0cfdc9fe42753d104000000000013d3018afc1c90f34"
    "3aae0000000000013d37c416c137d86e0000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d2b4000000000013d30646a2e70b1af00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003d11c000000000013d301e3a8f5f13ad"
    "3cf38000000000013d2f6feb5cb4f7b4000000000000000000000000000000003a720000000000013d38526aa83d225c"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d154000000000013d2fefb88769687600000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3cf50000000000013d2f404c6e57104c000000000000000000000000000000003f200000000000013cdd53caedf5b891"
    "3c9a8000000000013d309256de0e6cd3000000000000000000000000000000003a3e8000000000010000000000000000"
    "3f000000000000013cc2545ed4b996563c944000000000013d2f491f459b5c5200000000000000000000000000000000"
    "000000000000000000000000000000003bc8f400000000013d6ae71a4c28798200000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003f100000000000013ccc3db9eea0c94a"
    "3f948000000000013d1fee099c2b164a3eeec000000000013d6a00da239da26e3a9f0000000000013d33d7322bec42d9"
    "3f914000000000013d21a2b6f004e1bc3eeaa800000000013d73bbc90dd068da00000000000000000000000000000000"
    "000000000000000000000000000000003bc9b000000000013d68b626ff66136400000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3f950000000000013d1f8a6f3003ae0d3eefe800000000013d6a0479ec63447000000000000000000000000000000000"
    "3f920000000000013d1fce49663309dd3eeb5000000000013d69fbc66cc4744500000000000000000000000000000000"
    "000000000000000000000000000000003cf00000000000013d2f524af47d3bfe00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003cc84000000000013d2fa5d111b3df48"
    "3e8a0000000000013d1e016edeb6219e000000000000000000000000000000003aa06000000000013d33f504b3125109"
    "000000000000000000000000000000003e858000000000013d1ea6881827083c00000000000000000000000000000000"
    "00000000000000000000000000000000338c800000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003ed00000000000013cd7d414e157a9d7"
    "3f480000000000013d1f8a6f3003ae0d3706c00000000001000000000000000000000000000000000000000000000000"
    "3f450000000000013d1cf10ce684555d3ed60000000000013d002a2725eb93d200000000000000000000000000000000"
    "000000000000000000000000000000003b7d2000000000013d66d0187410891800000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003ae50000000000013d33badb2f0f1edf"
    "3e3e0000000000013d1e4b4b033264e73ea23c00000000013d6a054aacc2ef4a3a526000000000013d33d879dc944193"
    "3ecc0000000000013d02be850321e3f93eda0000000000013d0314edbcf4c5d142b63000000000013d5c23d71115f754"
    "42f40000000000013d02ee6b004f8c0d42212800000000013d58d16a0a1c2db1429af000000000013d611508bfe0dec5"
    "42120800000000013d5ab384a07edba341ad6000000000013d5ec5672bcede9f41ee2800000000013d68e7016326bbe6"
    "41e25200000000013d78f233f166be614185dc00000000013d6ba0b65b75f5eb413e4000000000013d30b2d03bc78825"
    "417e2800000000013d8c54b4a38440194126b400000000013d87cace0e3640bb40c5a000000000013d524b75bfa2a58b"
    "40d74c00000000013d80f3565e27ec06000000000000000000000000000000004351d000000000013d651407e704de57"
    "43932c00000000013d7786aad548f02a4295a400000000013d82dfa24eb872023e7a7400000000013d7623e8267d8850"
    "438bb400000000013d716f6beca9eb4143154000000000013d45176f14029a913cb20000000000013cf5c5035bd940a6"
    "3ccc0000000000013cf69109433d0e633cf10000000000013d0aa1226abc00323c100000000000013cde8486357e4e9d"
    "000000000000000000000000000000003b700000000000013ce188a51f09ed853bb00000000000013cd393ac8f8f697a"
    "3bc00000000000013cf9895dd005767a3ba80000000000013cf068935b5e65c03b778000000000013d1835f319945f64"
    "3b940000000000013d20cac435936c643b858000000000013d1ef9893b8c70033b380000000000013d1955ca29c24acc"
    "3b1a0000000000013d3043dfd39df07e0000000000000000000000000000000000000000000000000000000000000000"
    "3d4e0000000000013d3089a4bcfe487d3d4f4000000000013d3253177851ede83c860000000000013d1ab94c9ffc0d0e"
    "3d020000000000013d360fab777ec51b3cde0000000000013d01c0a07c4ecb0a41394800000000013d735db4ce0bdd9b"
    "3e8a0000000000013cfdeca01fa015e33ea40000000000013cfea3e7e3b929083e880000000000013cd92cbaaec31cae"
    "3e100000000000013cec9d33c24c83b63e140000000000013ceec3ade81948ca3d4c0000000000013cf184295ca41ebf"
    "3d880000000000013ce1f482adf113633d820000000000013cfd6d2d35e7c41e3d680000000000013cf29caa4c284451"
    "3d100000000000013cf19e02ca16583a3d468000000000013d1fdcd13c6af48a3d358000000000013d204e113a0ac78c"
    "3ce78000000000013d1a021d76cf26f43cc6c000000000013d307710159eb57500000000000000000000000000000000"
    "000000000000000000000000000000003f580000000000013d21b671b21018b23f588000000000013d1f4075778596a8"
    "3e500000000000013d1f5d025cf7f9373f0c0000000000013d1bdb360446c0583e920000000000013d015bda024f6ae3"
    "3e880000000000013cfb823133fbaa863ea40000000000013cfe90a8728ea8ec3ee80000000000013ce698fa94c5ef3f"
    "3ce10000000000013d10b7fcdd945a1d3d940000000000013cf106d81fb219813d500000000000013cf376123722d7ab"
    "3d900000000000013ce7c2804bcc10d53d880000000000013d029f05c0db5b7f3d680000000000013cf1d389bde64649"
    "3d120000000000013cf307bc5002a5903d480000000000013d205d083f9ce9e23d388000000000013d216d6d608e5ab1"
    "3ce98000000000013d1a4a1a79da11173cc84000000000013d304d85d24a487f00000000000000000000000000000000"
    "000000000000000000000000000000003f578000000000013d21590dfff183f73f588000000000013d1f43126b538f3f"
    "3e868000000000013d1d78eafb48dcbb3f110000000000013d10b0bc8516efa53ed80000000000013ce6e399fce47ea7"
    "3e880000000000013cfb823133fbaa863ea40000000000013cfe90a8728ea8ec3ee80000000000013ce698fa94c5ef3e"
    "3ce10000000000013d10b7fcdd945a1d3d940000000000013cf106d81fb219813d480000000000013ced311b52b44380"
    "3d900000000000013ce7c2804bcc10d53d880000000000013d029f05c0db5b7f3d680000000000013cf1d389bde64649"
    "3cf80000000000013cd95fa5c003876a3d498000000000013d2162d8c396b8803d388000000000013d216d6d608e5ab1"
    "3ce90000000000013d19c623fefdf2a33cc8c000000000013d30a3932329816a00000000000000000000000000000000"
    "000000000000000000000000000000003f578000000000013d21590dfff183f73f588000000000013d1f43126b538f3f"
    "3e858000000000013d1c299695181cde3f100000000000013d0f6acc45d0d2283ed80000000000013ce6e399fce47ea5"
    "3e880000000000013cfb823133fbaa863ea40000000000013cfe90a8728ea8ec3ee80000000000013ce698fa94c5ef3e"
    "3ce10000000000013d10b7fcdd945a1d3d940000000000013cf106d81fb219813d480000000000013ced311b52b44380"
    "3d900000000000013ce7c2804bcc10d53d880000000000013d029f05c0db5b7f3d680000000000013cf1d389bde64649"
    "3cf80000000000013cd95fa5c003876a3d498000000000013d2162d8c396b8803d388000000000013d216d6d608e5ab1"
    "3ce90000000000013d19c623fefdf2a33cc8c000000000013d30a3932329816a00000000000000000000000000000000"
    "000000000000000000000000000000003f578000000000013d21590dfff183f73f588000000000013d1f43126b538f3f"
    "3e858000000000013d1c299695181cde3f100000000000013d0f6acc45d0d2283ed80000000000013ce6e399fce47ea5"
    "43462000000000013d50c772a3a4cc27434c4000000000013d556c9544a707c2434c4000000000013d556c9544a707c2"
    "000000000000000000000000000000003b0f1000000000013d54bfb607f481033d078000000000013d52c34d43d8cd8b"
    "3bf6b000000000013d60cc92d6bf1e183aece800000000013d5ec612b342cb873c0b7000000000013d559cc56f8b3276"
    "3bb32c00000000013d64c9a9d22246153c47a400000000013d6ec9747f947bf93cf18000000000013d68d0cc2306a02d"
    "3d031c00000000013d71626c907ae5be3ce4b000000000013d5999d375608d793ce3c000000000013d4b2de18b4eb462"
    "3cb93c00000000013d7a67ff7b5084403ceb9800000000013d7d657cc90a71a539d87000000000010000000000000000"
    "3d239e00000000013d7865d0bca204b73d41a000000000013d519c44ded4b6e43cd4c800000000013d60d077e8bcb83c"
    "3d4a9800000000013d6134e3f19503853bd3d000000000013d50c47f18f0d7073cf1a800000000013d55c0c0112d42bb"
    "40e90000000000013d22701770cd36b50000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "40e90000000000013d22701770cd36b50000000000000000000000000000000040e90000000000013d22701770cd36b5"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003b0f1000000000013d54bfb607f481033d078000000000013d52c34d43d8cd8b"
    "3bf6b000000000013d60cc92d6bf1e183aece800000000013d5ec612b342cb873c0b7000000000013d559cc56f8b3276"
    "3bb32c00000000013d64c9a9d22246153c47a400000000013d6ec9747f947bf93cf18000000000013d68d0cc2306a02d"
    "3d031c00000000013d71626c907ae5be3ce4b000000000013d5999d375608d793ce3c000000000013d4b2de18b4eb462"
    "3cb93c00000000013d7a67ff7b5084403ceb9800000000013d7d657cc90a71a539d87000000000010000000000000000"
    "3d239e00000000013d7865d0bca204b73d41a000000000013d519c44ded4b6e43cd4c800000000013d60d077e8bcb83c"
    "3d4a9800000000013d6134e3f19503853bd3d000000000013d50c47f18f0d7073cf1a800000000013d55c0c0112d42bb"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003b0f1000000000013d54bfb607f481033d078000000000013d52c34d43d8cd8b"
    "3bf6b000000000013d60cc92d6bf1e183aece800000000013d5ec612b342cb873c0b7000000000013d559cc56f8b3276"
    "3bb32c00000000013d64c9a9d22246153c47a400000000013d6ec9747f947bf93cf18000000000013d68d0cc2306a02d"
    "3d031c00000000013d71626c907ae5be3ce4b000000000013d5999d375608d793ce3c000000000013d4b2de18b4eb462"
    "3cb93c00000000013d7a67ff7b5084403ceb9800000000013d7d657cc90a71a539d87000000000010000000000000000"
    "3d239e00000000013d7865d0bca204b73d41a000000000013d519c44ded4b6e43cd4c800000000013d60d077e8bcb83c"
    "3d4a9800000000013d6134e3f19503853bd3d000000000013d50c47f18f0d7073cf1a800000000013d55c0c0112d42bb"};
inline constexpr FrozenNetworkAuthority<7, 17> kIso7Authority{
    {"he4", "c12", "o16", "ne20", "mg24", "si28", "ni56"},
    "40100000000000004028000000000000403000000000000040340000000000004038000000000000403c000000000000"
    "404c00000000000040000000000000004018000000000000402000000000000040240000000000004028000000000000"
    "402c000000000000403c000000000000403c4bc89f40a28740570a6d9be4cd75405fe7bd512ec6bd406414bb6ed67770"
    "4068c840b780346e406d91367a0f9097407e400c49ba5e353ff00000000000003ff00000000000003ff0000000000000"
    "3ff00000000000003ff00000000000003ff00000000000003ff0000000000000403c4bc89f40a28740570a6d9be4cd75"
    "405fe7bd512ec6bd406414bb6ed677704068c840b780346e406d91367a0f9097407e400c49ba5e35409f0c0cd119094a"
    "3df08be3cb03c67d4079e5dd533804213e9345af01e0659840810dafc57668c23ee7110aa8a9c5743ee0627d8a5956e3"
    "41227c76d604ef403fba83e1d801409a414dbf1b9ee4fc4b3d81c24f5428e71d412c7ee054e56bf13d061aec39e45403"
    "408532f54c9129593ee8862f0fa146d20000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000029919e00c1b0c96e0000000000000000274cea9d4df9a833"
    "000000000000000000000000000000000000000000000000000000000000000022854012bd1655560000000000000000"
    "1c8b18a1c3c9127f00000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000029919e00c1b0c96e0000000000000000274cea9d4df9a8330000000000000000"
    "00000000000000000000000000000000000000000000000022854012bd16555600000000000000001c8b18a1c3c9127f"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000c095b00433d7a672bff7fa8036591089c0820e4581d2e8b3c0b09898ab72f4bb40b17f01fc6fd1c5"
    "409b238050db3ce6000000000000000044645118f68e9754c0e2fa2a08ba80efc01352e93c9f7905c0952087d0043561"
    "c0bb325d17d0133dc095b60040aecc243cd943571d973b710000000000000000c041b7668a6cdd78c0383cdc980008c7"
    "be6a43cca1ecee490000000000000000000000000000000000000000000000000000000000000000c0cf99240fcaaba3"
    "4037a7a83c2424cbc0b52087d0087f5a3fb5364e466766e2000000000000000000000000000000000000000000000000"
    "c0fd0b5b29fb3bae4015a7baa159689440ba68a9c4070ca5c0e0ff80cfda82073d7d992ee19981300000000000000000"
    "000000000000000040fe9e4379c387923e73c576d9b616f63e6a5c9e779d73f240e465c9cc0abba1c0c048803083191c"
    "3d02f28156316c95000000000000000040e7bf1046bf2f483e77110aa8a9c5743e83fa370aa642fb0000000000000000"
    "40c2ff403898f2a0bd061aec39e454030000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000044b1c6e6af520f1343e3d09521190208"
    "4454e95c48652c29448a7fab82051b014466ace21dfdef45c0aa629d60ab97f40000000000000000bed598914fc67621"
    "be4290a74d759d2abeb4a68b8c2000c3bef321802d1e7b543ef3622bfe3976493ed9c8cd4209454b0000000000000000"
    "42a4ad5cd58b0029c095b00433d7a672bff7fa8036591089c0820e4581d2e8b3c0b09898ab72f4bb40b17f01fc6fd1c5"
    "409b238050db3ce500000000000000003fb008b1d50ef8b8bfd6ea21891cc2963f883e0d879441ae3fd2260487f828cc"
    "3e4858817ebc183f3e5377f8cffb7d2e00000000000000003fb008b1d50ef8b8bfd6ea21891cc2963f883e0d879441ae"
    "3fd2260487f828cc3e4858817ebc183f3e5377f8cffb7d2e00000000000000003fb008b1d50ef8b8bfd6ea21891cc296"
    "3f883e0d879441ae3fd2260487f828cc3e4858817ebc183f3e5377f8cffb7d2e0000000000000000436c05111fe1c2dd"
    "436c05111fe1c2dd436c05111fe1c2dd3fa55e71ccb07cc13e8a333f5abe12c43eaa43455ec557e93e44583c8ed2a17e"
    "3ed210080e010c613f81b04ef0bec9b23fee634c8d56939243917a533d8b195500000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "439f0269f86119d40000000000000000439f0269f86119d440b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b000000000000040b0010000000000"
    "40b002000000000040b003000000000040b004000000000040b005000000000040b006000000000040b0000000000000"
    "40b001000000000040b002000000000040b003000000000040b004000000000040b005000000000040b0060000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b000000000000040b001000000000040b002000000000040b003000000000040b0040000000000"
    "40b005000000000040b006000000000040b000000000000040b001000000000040b002000000000040b0030000000000"
    "40b004000000000040b005000000000040b006000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b000000000000040b0010000000000"
    "40b002000000000040b003000000000040b004000000000040b005000000000040b006000000000040b0000000000000"
    "40b001000000000040b002000000000040b003000000000040b004000000000040b005000000000040b0060000000000"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b006000000000040b000000000000040b001000000000040b002000000000040b003000000000040b0040000000000"
    "40b005000000000040b006000000000040b000000000000040b001000000000040b002000000000040b0030000000000"
    "40b004000000000040b005000000000040b006000000000040b000000000000040b001000000000040b0020000000000"
    "40b003000000000040b004000000000040b005000000000040b006000000000040b000000000000040b0010000000000"
    "40b002000000000040b003000000000040b004000000000040b005000000000040b00600000000003fa55e71ccb07cc1"
    "3e8a333f5abe12c43eaa43455ec557e93e44583c8ed2a17e3ed210080e010c613f81b04ef0bec9b23fee634c8d569392"
    "40b000000000000040b001000000000040b002000000000040b003000000000040b004000000000040b0050000000000"
    "40b00600000000003fa55e71ccb07cc13e8a333f5abe12c43eaa43455ec557e93e44583c8ed2a17e3ed210080e010c61"
    "3f81b04ef0bec9b23fee634c8d569392"
    , 81920u, 49151u, true, true,
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003db30000000000013d03954a801e9859"
    "3b260000000000013d254600f87683fc3d600000000000013cd3c51a8bfc38403bc20000000000013d1de33a3ffdf127"
    "3dc08000000000013d2ef621c601f7753c26c000000000013d2f8f9262ceb4993c204000000000013d2fbca391f7d536"
    "3e200000000000013cebb24433c623473ce00000000000013d134f48cc83aa0b3e580000000000013cf9d1724bfef92b"
    "3ab60000000000013d23d2272c5ce6c33e240000000000013ce675a8501783aa3a398000000000010000000000000000"
    "3db40000000000013d1e30a56ef92bb93c218000000000013d26d5aa42beec2e00000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "26f1b0000000000100000000000000000000000000000000000000000000000024ac9000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000001fe5300000000001000000000000000000000000000000000000000000000000"
    "19eb10000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000026f1d000000000010000000000000000"
    "0000000000000000000000000000000024ac900000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "1fe530000000000100000000000000000000000000000000000000000000000019eb1000000000010000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003d9c0000000000013cf4a8311d8669923d1a0000000000013d11594efad026dd"
    "3d800000000000013cec5b4b7e5aae9b3dbc0000000000013cfafe8c387146873dc20000000000013d0075f63e74a38b"
    "3d880000000000013cdc4c9183a1f9ab0000000000000000000000000000000041640000000000013cef804466520c2a"
    "3de80000000000013cf43c1c1c6427b33d080000000000013ce3df2deb3844f63d900000000000013ce83bfbad4f0315"
    "3dc60000000000013cf9e2ada44fd9fd3d900000000000013ce79523edb24a403a0d0000000000010000000000000000"
    "000000000000000000000000000000003d540000000000013d020ff6489233f73d620000000000013d17c3bc3b5681a6"
    "3ba9c000000000013d2f5f6bcdd08e330000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003dcc0000000000013cec5b256beaf1d5"
    "3d4c0000000000013d02f061516f49c13db00000000000013ce83bfbad4a17a03cda0000000000013d139c85efb5b8b3"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "3e0a0000000000013cfca55594c7733d3d550000000000013d2f0825575496dc3db40000000000013ce83bfbad4d5edd"
    "3dec0000000000013cfa5b1f89f9da633ab24000000000013d23bb16b44e83f800000000000000000000000000000000"
    "000000000000000000000000000000003e0e0000000000013cff5a9815709cf03bb38000000000013d2f8f9262ceb499"
    "3baa0000000000013d2f8f9262ceb4993df20000000000013cfc3d15031ee0873dc00000000000013cef71853cedb854"
    "3a358000000000010000000000000000000000000000000000000000000000003de00000000000013ce58fabd2021800"
    "3bb6c000000000013d2f8f9262ceb4993bc3c000000000013d2fa2c0293d29b000000000000000000000000000000000"
    "3dc00000000000013ceaf396c6829e003a39800000000001000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000041c20000000000013d00336430d15ee040fc0000000000013d069c010602b92c"
    "41500000000000013ce87beb5dba9bb241960000000000013cfa913c8e4b945a41600000000000013ce69461513259d5"
    "3dde8000000000013d227ec5e776fc50000000000000000000000000000000003bdc0000000000013cf4be9f01e66e19"
    "3b6c0000000000013d1821a0e727f3f43ba80000000000013ce2985a8524c3683c020000000000013cfe1bc14f746579"
    "3c060000000000013d0228e803bbc7413bc00000000000013cd3db632641e3ba00000000000000000000000000000000"
    "3fa00000000000013ce8c2f6a16e44fb3d9c0000000000013cf4a8311d8669923d1a0000000000013d11594efad026dd"
    "3d800000000000013cec5b4b7e5aae9b3dbc0000000000013cfafe8c387146873dc20000000000013d0075f63e74a38b"
    "3d800000000000013cd2ddb657c1511d000000000000000000000000000000003cef8000000000013d2f6eeb2ccdf4ed"
    "3d174000000000013d303bf52890651e3cad0000000000013d1323d89b8e48553d130000000000013d30c02d3040b873"
    "3b880000000000013d2f8bab02566ec93b934000000000013d2fa4002cf5ba1100000000000000000000000000000000"
    "3cef8000000000013d2f6eeb2ccdf4ed3d174000000000013d303bf52890651e3cad0000000000013d1323d89b8e4855"
    "3d130000000000013d30c02d3040b8733b880000000000013d2f8bab02566ec93b934000000000013d2fa4002cf5ba11"
    "000000000000000000000000000000003cef8000000000013d2f6eeb2ccdf4ed3d174000000000013d303bf52890651e"
    "3cad0000000000013d1323d89b8e48553d130000000000013d30c02d3040b8733b880000000000013d2f8bab02566ec9"
    "3b934000000000013d2fa4002cf5ba110000000000000000000000000000000040ac0000000000013d2ffa369e7df832"
    "40ac0000000000013d2ffa369e7df83240ac0000000000013d2ffa369e7df8323cf46000000000013d3e82f808c1d421"
    "3be30000000000013d4734b8a78424753bf92000000000013d3e9d197c960da23bb6a800000000013d61d163ee7d544d"
    "3c41d800000000013d5f9cbbde4eb4e53d007800000000013d6dcb03a0223e103cf60000000000013cf72ac8c326a90d"
    "40bd0000000000013d1a8c441968de710000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "40bd0000000000013d0ded274d859e9e0000000000000000000000000000000040bd0000000000013d0ded274d859e9e"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000000000000000000000000000000000000000000000000000003cf46000000000013d3e82f808c1d421"
    "3be30000000000013d4734b8a78424753bf92000000000013d3e9d197c960da23bb6a800000000013d61d163ee7d544d"
    "3c41d800000000013d5f9cbbde4eb4e53d007800000000013d6dcb03a0223e103cf60000000000013cf72ac8c326a90d"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000003cf46000000000013d3e82f808c1d4213be30000000000013d4734b8a7842475"
    "3bf92000000000013d3e9d197c960da23bb6a800000000013d61d163ee7d544d3c41d800000000013d5f9cbbde4eb4e5"
    "3d007800000000013d6dcb03a0223e103cf60000000000013cf72ac8c326a90d"};

template <int N, int R>
void validate_network_host(const char* name, const NetworkProbe<N, R>& value)
{
    require(value.nse_ok, std::string(name) + ".host NSE status");
    for (int i = 0; i < R; ++i)
        require(value.below_temperature_rates[i] == 0.0,
                std::string(name) + ".below 1e6 rate " + std::to_string(i));
    for (int i : {13, 15}) {
        require(!value.nse_invalid_status[i]
                    && value.nse_invalid_preserved[i]
                    && value.nse_invalid_enuc[i] == 0.0
                    && !std::signbit(value.nse_invalid_enuc[i]),
                std::string(name) + ".host X_old below -1e-12 contract "
                + std::to_string(i));
    }
}

#if defined(__CUDACC__)
void check_cuda(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess)
        fail(std::string(operation) + ": " + cudaGetErrorString(status));
}

__global__ void dense_kernel(DenseProbe* result)
{
    *result = evaluate_dense();
}

__global__ void nse_iteration_boundary_kernel(NsePolicyProbe* result)
{
    *result = evaluate_nse_iteration_boundary();
}

template <typename Network>
__device__ void make_device_state(double* state, double* y)
{
    constexpr int N = Network::NUM_SPECIES;
    const double normalization = 0.5 * N * (N + 1.0);
    for (int i = 0; i < N; ++i) {
        state[i] = (i + 1.0) / normalization;
        y[i] = state[i] / Network::aion(i);
    }
    state[N] = 2.0e9;
}

template <typename Network>
__global__ void network_rates_kernel(
    NetworkProbe<Network::NUM_SPECIES, NetworkTraits<Network>::rate_count>* out)
{
    constexpr int N = Network::NUM_SPECIES;
    constexpr int R = NetworkTraits<Network>::rate_count;
    double state[N + 1]{};
    double y[N]{};
    make_device_state<Network>(state, y);
    for (int i = 0; i < N; ++i) {
        out->aion[i] = Network::aion(i);
        out->zion[i] = Network::zion(i);
        out->binding[i] = Network::binding_energy(i);
        out->spin[i] = Network::spin_weight(i);
        out->energy_weight[i] = Network::energy_weight(i);
    }
    std::array<double, R> rates{};
    NetworkTraits<Network>::template rates<double, timmes::RateValueAccessor>(
        y, 1.0e6, 0.25, state[N], state[N], rates);
    for (int i = 0; i < R; ++i) out->rates[i] = rates[i];
    std::array<double, R> low_rates{};
    const double low_temperature = nextafter(1.0e6, 0.0);
    NetworkTraits<Network>::template rates<double, timmes::RateValueAccessor>(
        y, 1.0e6, 0.25, low_temperature, low_temperature, low_rates);
    for (int i = 0; i < R; ++i) out->below_temperature_rates[i] = low_rates[i];
    std::array<double, R> exact_rates{};
    NetworkTraits<Network>::template rates<double, timmes::RateValueAccessor>(
        y, 1.0e6, 0.25, 1.0e6, 1.0e6, exact_rates);
    for (int i = 0; i < R; ++i) out->exact_temperature_rates[i] = exact_rates[i];
    std::array<double, R> above_rates{};
    const double above_temperature = nextafter(
        1.0e6, std::numeric_limits<double>::infinity());
    NetworkTraits<Network>::template rates<double, timmes::RateValueAccessor>(
        y, 1.0e6, 0.25, above_temperature, above_temperature, above_rates);
    for (int i = 0; i < R; ++i) out->above_temperature_rates[i] = above_rates[i];
}

template <typename Network>
__global__ void network_rhs_kernel(
    NetworkProbe<Network::NUM_SPECIES, NetworkTraits<Network>::rate_count>* out)
{
    constexpr int N = Network::NUM_SPECIES;
    double state[N + 1]{};
    double y[N]{};
    make_device_state<Network>(state, y);
    Network::eval_rhs(state, 1.0e6, 0.25, out->rhs, out->enuc);
}

template <typename Network>
__global__ void network_jacobian_kernel(
    NetworkProbe<Network::NUM_SPECIES, NetworkTraits<Network>::rate_count>* out)
{
    constexpr int N = Network::NUM_SPECIES;
    double state[N + 1]{};
    double y[N]{};
    make_device_state<Network>(state, y);
    DenseMatrixData jacobian{};
    Network::eval_jacobian(state, 1.0e6, 0.25, jacobian, out->denuc_dx);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            out->jacobian[i * N + j] = jacobian.data[i][j];
}

template <typename Network>
__global__ void network_temperature_kernel(
    NetworkProbe<Network::NUM_SPECIES, NetworkTraits<Network>::rate_count>* out)
{
    constexpr int N = Network::NUM_SPECIES;
    double state[N + 1]{};
    double y[N]{};
    make_device_state<Network>(state, y);
    Network::eval_temperature_derivative(
        state, 1.0e6, 0.25, out->drhs_dt, out->denuc_dt);
}

template <typename Network>
__global__ void network_frozen_kernel(
    NetworkProbe<Network::NUM_SPECIES, NetworkTraits<Network>::rate_count>* out)
{
    constexpr int N = Network::NUM_SPECIES;
    double state[N + 1]{};
    double y[N]{};
    make_device_state<Network>(state, y);
    using AD = timmes::Dual<N>;
    AD y_ad[N]{};
    AD frozen_dydt[N]{};
    for (int i = 0; i < N; ++i) y_ad[i] = AD::variable(y[i], i);
    Network::template molar_rhs_frozen_screening<AD>(
        y_ad, 1.0e6, 0.25, state[N], frozen_dydt);
    for (int i = 0; i < N; ++i)
        out->frozen_rhs[i] = frozen_dydt[i].value * Network::aion(i);
}

template <typename Network>
__global__ void network_clamp_kernel(
    NetworkProbe<Network::NUM_SPECIES, NetworkTraits<Network>::rate_count>* out)
{
    constexpr int N = Network::NUM_SPECIES;
    double state[N + 1]{};
    double y[N]{};
    make_device_state<Network>(state, y);
    for (int which = 0; which < 3; ++which) {
        double threshold_state[N + 1]{};
        for (int i = 0; i <= N; ++i) threshold_state[i] = state[i];
        threshold_state[0] = which == 0
                           ? nextafter(Network::aion(0) * 1.0e-30, 0.0)
                           : (which == 1
                              ? Network::aion(0) * 1.0e-30
                              : nextafter(Network::aion(0) * 1.0e-30,
                                          std::numeric_limits<double>::infinity()));
        threshold_state[N - 1] = which == 0
                               ? nextafter(Network::aion(N - 1), 0.0)
                               : (which == 1
                                  ? Network::aion(N - 1)
                                  : nextafter(Network::aion(N - 1),
                                              std::numeric_limits<double>::infinity()));
        Network::eval_rhs(threshold_state, 1.0e6, 0.25,
                          out->clamp_rhs[which], out->clamp_enuc[which]);
    }
}

template <typename Network>
__global__ void network_nse_kernel(
    NetworkProbe<Network::NUM_SPECIES, NetworkTraits<Network>::rate_count>* out)
{
    constexpr int N = Network::NUM_SPECIES;
    double state[N + 1]{};
    double y[N]{};
    make_device_state<Network>(state, y);
    double ye = 0.0;
    for (int i = 0; i < N; ++i)
        ye += state[i] * Network::zion(i) / Network::aion(i);
    out->nse_ok = NSESolver<Network>::solve(
        5.0e9, 1.0e7, ye, state, out->nse_x, out->nse_enuc);
}

template <typename Network>
__global__ void network_invalid_nse_kernel(
    NetworkProbe<Network::NUM_SPECIES, NetworkTraits<Network>::rate_count>* out,
    int invalid)
{
    constexpr int N = Network::NUM_SPECIES;
    double state[N + 1]{};
    double y[N]{};
    make_device_state<Network>(state, y);
    double ye = 0.0;
    for (int i = 0; i < N; ++i)
        ye += state[i] * Network::zion(i) / Network::aion(i);
    double temperature = 5.0e9;
    double density = 1.0e7;
    double electron_fraction = ye;
    const double* old_x = state;
    bool null_output = false;
    if (invalid == 0) old_x = nullptr;
    if (invalid == 1) null_output = true;
    if (invalid == 2) temperature = std::numeric_limits<double>::quiet_NaN();
    if (invalid == 3) temperature = std::numeric_limits<double>::infinity();
    if (invalid == 4) temperature = 0.0;
    if (invalid == 5) density = std::numeric_limits<double>::quiet_NaN();
    if (invalid == 6) density = 0.0;
    if (invalid == 7) electron_fraction = std::numeric_limits<double>::quiet_NaN();
    if (invalid == 8) electron_fraction = -1.0e-12;
    if (invalid == 9) electron_fraction = 1.0 + 1.0e-12;
    if (invalid == 10) state[0] = std::numeric_limits<double>::quiet_NaN();
    if (invalid == 11) state[0] = std::numeric_limits<double>::infinity();
    if (invalid == 12) density = -1.0;
    if (invalid == 13) state[0] = -2.0e-12;
    if (invalid == 14) state[0] = -1.0e-12;
    if (invalid == 15)
        state[0] = nextafter(-1.0e-12,
                             -std::numeric_limits<double>::infinity());
    if (invalid == 16) state[0] = nextafter(-1.0e-12, 0.0);
    double sentinel[N];
    for (int i = 0; i < N; ++i) sentinel[i] = 4096.0 + i;
    double energy = -7.0;
    out->nse_invalid_status[invalid] = NSESolver<Network>::solve(
        temperature, density, electron_fraction, old_x,
        null_output ? nullptr : sentinel, energy);
    out->nse_invalid_enuc[invalid] = energy;
    bool preserved = true;
    for (int i = 0; i < N; ++i) {
        preserved &= sentinel[i] == 4096.0 + i;
        out->nse_invalid_x[invalid][i] = sentinel[i];
    }
    out->nse_invalid_preserved[invalid] = preserved;
}

template <typename Network>
void run_network_device(
    const char* name,
    const FrozenNetworkAuthority<Network::NUM_SPECIES,
                                 NetworkTraits<Network>::rate_count>& authority)
{
    using Probe = NetworkProbe<Network::NUM_SPECIES,
                               NetworkTraits<Network>::rate_count>;
    Probe* device_result = nullptr;
    check_cuda(cudaMalloc(&device_result, sizeof(Probe)), "cudaMalloc network");
    check_cuda(cudaMemset(device_result, 0, sizeof(Probe)), "cudaMemset network");
    network_rates_kernel<Network><<<1, 1>>>(device_result);
    check_cuda(cudaGetLastError(), "network rates kernel launch");
    network_rhs_kernel<Network><<<1, 1>>>(device_result);
    check_cuda(cudaGetLastError(), "network RHS kernel launch");
    network_jacobian_kernel<Network><<<1, 1>>>(device_result);
    check_cuda(cudaGetLastError(), "network Jacobian kernel launch");
    network_temperature_kernel<Network><<<1, 1>>>(device_result);
    check_cuda(cudaGetLastError(), "network temperature kernel launch");
    network_frozen_kernel<Network><<<1, 1>>>(device_result);
    check_cuda(cudaGetLastError(), "network frozen kernel launch");
    network_clamp_kernel<Network><<<1, 1>>>(device_result);
    check_cuda(cudaGetLastError(), "network clamp kernel launch");
    network_nse_kernel<Network><<<1, 1>>>(device_result);
    check_cuda(cudaGetLastError(), "network NSE kernel launch");
    for (int invalid = 0; invalid < kInvalidNseCases; ++invalid) {
        network_invalid_nse_kernel<Network><<<1, 1>>>(device_result, invalid);
        check_cuda(cudaGetLastError(), "network invalid NSE kernel launch");
    }
    Probe device{};
    check_cuda(cudaMemcpy(&device, device_result, sizeof(Probe),
                          cudaMemcpyDeviceToHost), "network result copy");
    check_cuda(cudaFree(device_result), "cudaFree network");
    check_network_authority(name, device, authority, false);
}
#endif

void validate_dense(const DenseProbe& value)
{
    require(value.solve_ok && value.factor_ok, "DenseLU solve/factorize status");
    require(!value.below_ok && value.below_rhs == 7.0,
            "DenseLU below-threshold preservation");
    require(value.exact_ok && value.above_ok,
            "DenseLU exact/above threshold classification");
    const double expected[3]{1.0, -2.0, -2.0};
    for (int i = 0; i < 3; ++i) {
        compare_field(value.solution[i], expected[i], 2.0e-15,
                      "dense.solution", i);
        compare_field(value.factor_solution[i], expected[i], 2.0e-15,
                      "dense.factor_solution", i);
    }
}

void compare_dense(const DenseProbe& actual, const DenseProbe& expected)
{
    require(actual.solve_ok == expected.solve_ok
            && actual.factor_ok == expected.factor_ok
            && actual.below_ok == expected.below_ok
            && actual.exact_ok == expected.exact_ok
            && actual.above_ok == expected.above_ok,
            "DenseLU host/device branch status");
    for (int i = 0; i < 3; ++i) {
        compare_field(actual.solution[i], expected.solution[i], 0.0,
                      "dense.solution.raw", i);
        compare_field(actual.factor_solution[i], expected.factor_solution[i], 0.0,
                      "dense.factor_solution.raw", i);
    }
    compare_field(actual.below_rhs, expected.below_rhs, 0.0,
                  "dense.threshold_rhs.raw", 0);
    compare_field(actual.exact_rhs, expected.exact_rhs, 0.0,
                  "dense.threshold_rhs.raw", 1);
    compare_field(actual.above_rhs, expected.above_rhs, 0.0,
                  "dense.threshold_rhs.raw", 2);
}

} // namespace

int main()
{
    try {
        const DenseProbe host_dense = evaluate_dense();
        validate_dense(host_dense);
        const NsePolicyProbe host_policy = evaluate_nse_iteration_boundary();
        require(host_policy.converged == kIterationBoundaryAuthority.converged
                && host_policy.output_preserved
                    == kIterationBoundaryAuthority.output_preserved
                && std::bit_cast<std::uint64_t>(host_policy.enuc)
                    == std::bit_cast<std::uint64_t>(
                        kIterationBoundaryAuthority.enuc)
                && host_policy.line_search_limit
                    == kIterationBoundaryAuthority.line_search_limit,
                "NSE 100-iteration host failure preservation");
        const auto host_aprox13 = evaluate_network<NetAprox13>();
        const auto host_aprox19 = evaluate_network<NetAprox19>();
        const auto host_aprox21 = evaluate_network<NetAprox21>();
        const auto host_iso7 = evaluate_network<NetIso7>();
        check_species_authority<NetAprox13>("aprox13", kAprox13Authority);
        check_species_authority<NetAprox19>("aprox19", kAprox19Authority);
        check_species_authority<NetAprox21>("aprox21", kAprox21Authority);
        check_species_authority<NetIso7>("iso7", kIso7Authority);
        check_network_authority(
            "aprox13", host_aprox13, kAprox13Authority,
            kInstructionEquivalentHost);
        check_network_authority(
            "aprox19", host_aprox19, kAprox19Authority,
            kInstructionEquivalentHost);
        check_network_authority(
            "aprox21", host_aprox21, kAprox21Authority,
            kInstructionEquivalentHost);
        check_network_authority("iso7", host_iso7, kIso7Authority,
                                kInstructionEquivalentHost);
        validate_network_host("aprox13", host_aprox13);
        validate_network_host("aprox19", host_aprox19);
        validate_network_host("aprox21", host_aprox21);
        validate_network_host("iso7", host_iso7);
#if defined(__CUDACC__)
        DenseProbe* device_dense = nullptr;
        check_cuda(cudaMalloc(&device_dense, sizeof(DenseProbe)), "cudaMalloc dense");
        dense_kernel<<<1, 1>>>(device_dense);
        check_cuda(cudaGetLastError(), "dense kernel launch");
        DenseProbe dense{};
        check_cuda(cudaMemcpy(&dense, device_dense, sizeof(DenseProbe),
                              cudaMemcpyDeviceToHost), "dense result copy");
        check_cuda(cudaFree(device_dense), "cudaFree dense");
        validate_dense(dense);
        compare_dense(dense, host_dense);
        NsePolicyProbe* device_policy = nullptr;
        check_cuda(cudaMalloc(&device_policy, sizeof(NsePolicyProbe)),
                   "cudaMalloc NSE policy");
        nse_iteration_boundary_kernel<<<1, 1>>>(device_policy);
        check_cuda(cudaGetLastError(), "NSE iteration boundary kernel launch");
        NsePolicyProbe policy{};
        check_cuda(cudaMemcpy(&policy, device_policy, sizeof(policy),
                              cudaMemcpyDeviceToHost), "NSE policy result copy");
        check_cuda(cudaFree(device_policy), "cudaFree NSE policy");
        require(policy.converged == kIterationBoundaryAuthority.converged
                && policy.output_preserved
                    == kIterationBoundaryAuthority.output_preserved
                && std::bit_cast<std::uint64_t>(policy.enuc)
                    == std::bit_cast<std::uint64_t>(
                        kIterationBoundaryAuthority.enuc)
                && policy.line_search_limit
                    == kIterationBoundaryAuthority.line_search_limit,
                "NSE 100-iteration device frozen status/output");
        run_network_device<NetAprox13>("aprox13", kAprox13Authority);
        run_network_device<NetAprox19>("aprox19", kAprox19Authority);
        run_network_device<NetAprox21>("aprox21", kAprox21Authority);
        run_network_device<NetIso7>("iso7", kIso7Authority);
#else
        print_dense(host_dense);
        std::cout << "nse.iteration_boundary.status=" << host_policy.converged
                  << "\nnse.iteration_boundary.preserved="
                  << host_policy.output_preserved << '\n';
        print_double_array("nse.iteration_boundary.enuc", &host_policy.enuc, 1);
        print_network("aprox13", host_aprox13);
        print_network("aprox19", host_aprox19);
        print_network("aprox21", host_aprox21);
        print_network("iso7", host_iso7);
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
