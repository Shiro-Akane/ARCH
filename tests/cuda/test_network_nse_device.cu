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
#include <type_traits>

namespace {

constexpr double kRateRelTol = 2.0e-12;
constexpr double kRhsRelTol = 2.0e-12;
constexpr double kJacRelTol = 2.0e-12;
constexpr double kTemperatureRelTol = 2.0e-12;
constexpr double kEnergyRelTol = 2.0e-12;
constexpr double kNseRelTol = 2.0e-12;
constexpr int kInvalidNseCases = 13;

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
};

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

void compare_field(double actual, double expected, double rel_tol,
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
    if (error > rel_tol * scale) {
        std::cerr << std::setprecision(17) << field << '[' << index
                  << "] device=" << actual << " host=" << expected
                  << " relative_error=" << error / scale << '\n';
        fail(std::string(field) + " tolerance at " + std::to_string(index));
    }
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
        double sentinel[N];
        for (int i = 0; i < N; ++i) sentinel[i] = 4096.0 + i;
        double energy = -7.0;
        out.nse_invalid_status[invalid] = NSESolver<Network>::solve(
            temperature, density, electron_fraction, old_x,
            null_output ? nullptr : sentinel, energy);
        out.nse_invalid_enuc[invalid] = energy;
        bool preserved = true;
        for (int i = 0; i < N; ++i) preserved &= sentinel[i] == 4096.0 + i;
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
}

template <int N, int R>
void compare_network(const char* name, const NetworkProbe<N, R>& device,
                     const NetworkProbe<N, R>& host)
{
    auto compare = [&](const char* field, const double* actual,
                       const double* expected, int count, double tolerance) {
        const std::string full = std::string(name) + '.' + field;
        for (int i = 0; i < count; ++i)
            compare_field(actual[i], expected[i], tolerance, full.c_str(), i);
    };
    compare("aion", device.aion, host.aion, N, 0.0);
    compare("zion", device.zion, host.zion, N, 0.0);
    compare("binding", device.binding, host.binding, N, 0.0);
    compare("spin", device.spin, host.spin, N, 0.0);
    compare("energy_weight", device.energy_weight, host.energy_weight, N, 0.0);
    compare("rates", device.rates, host.rates, R, kRateRelTol);
    compare("below_temperature_rates", device.below_temperature_rates,
            host.below_temperature_rates, R, 0.0);
    compare("exact_temperature_rates", device.exact_temperature_rates,
            host.exact_temperature_rates, R, kRateRelTol);
    compare("above_temperature_rates", device.above_temperature_rates,
            host.above_temperature_rates, R, kRateRelTol);
    compare("rhs", device.rhs, host.rhs, N, kRhsRelTol);
    compare("enuc", &device.enuc, &host.enuc, 1, kEnergyRelTol);
    compare("jacobian", device.jacobian, host.jacobian, N * N, kJacRelTol);
    compare("denuc_dx", device.denuc_dx, host.denuc_dx, N, kJacRelTol);
    compare("drhs_dt", device.drhs_dt, host.drhs_dt, N, kTemperatureRelTol);
    compare("denuc_dt", &device.denuc_dt, &host.denuc_dt, 1, kTemperatureRelTol);
    compare("frozen_rhs", device.frozen_rhs, host.frozen_rhs, N, kRhsRelTol);
    compare("clamp_rhs", &device.clamp_rhs[0][0], &host.clamp_rhs[0][0],
            3 * N, kRhsRelTol);
    compare("clamp_enuc", device.clamp_enuc, host.clamp_enuc, 3, kEnergyRelTol);
    require(device.nse_ok == host.nse_ok && device.nse_ok,
            std::string(name) + ".nse status");
    compare("nse_x", device.nse_x, host.nse_x, N, kNseRelTol);
    compare("nse_enuc", &device.nse_enuc, &host.nse_enuc, 1, kNseRelTol);
    for (int i = 0; i < kInvalidNseCases; ++i) {
        require(!device.nse_invalid_status[i]
                && device.nse_invalid_status[i] == host.nse_invalid_status[i],
                std::string(name) + ".invalid status " + std::to_string(i));
        require(device.nse_invalid_preserved[i]
                && device.nse_invalid_preserved[i] == host.nse_invalid_preserved[i],
                std::string(name) + ".invalid preservation " + std::to_string(i));
        require(device.nse_invalid_enuc[i] == 0.0
                && device.nse_invalid_enuc[i] == host.nse_invalid_enuc[i],
                std::string(name) + ".invalid enuc " + std::to_string(i));
    }
}

template <int N, int R>
void validate_network_host(const char* name, const NetworkProbe<N, R>& value)
{
    require(value.nse_ok, std::string(name) + ".host NSE status");
    for (int i = 0; i < R; ++i)
        require(value.below_temperature_rates[i] == 0.0,
                std::string(name) + ".below 1e6 rate " + std::to_string(i));
    for (int i = 0; i < kInvalidNseCases; ++i) {
        require(!value.nse_invalid_status[i],
                std::string(name) + ".host invalid status " + std::to_string(i));
        require(value.nse_invalid_preserved[i],
                std::string(name) + ".host invalid preservation "
                + std::to_string(i));
        require(value.nse_invalid_enuc[i] == 0.0,
                std::string(name) + ".host invalid enuc " + std::to_string(i));
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
    double sentinel[N];
    for (int i = 0; i < N; ++i) sentinel[i] = 4096.0 + i;
    double energy = -7.0;
    out->nse_invalid_status[invalid] = NSESolver<Network>::solve(
        temperature, density, electron_fraction, old_x,
        null_output ? nullptr : sentinel, energy);
    out->nse_invalid_enuc[invalid] = energy;
    bool preserved = true;
    for (int i = 0; i < N; ++i) preserved &= sentinel[i] == 4096.0 + i;
    out->nse_invalid_preserved[invalid] = preserved;
}

template <typename Network>
void run_network_device(const char* name)
{
    using Probe = NetworkProbe<Network::NUM_SPECIES,
                               NetworkTraits<Network>::rate_count>;
    const Probe host = evaluate_network<Network>();
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
    compare_network(name, device, host);
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
        require(!host_policy.converged && host_policy.output_preserved
                && host_policy.enuc == 0.0,
                "NSE 100-iteration host failure preservation");
        const auto host_aprox13 = evaluate_network<NetAprox13>();
        const auto host_aprox19 = evaluate_network<NetAprox19>();
        const auto host_aprox21 = evaluate_network<NetAprox21>();
        const auto host_iso7 = evaluate_network<NetIso7>();
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
        require(policy.converged == host_policy.converged
                && policy.output_preserved == host_policy.output_preserved
                && policy.enuc == host_policy.enuc,
                "NSE 100-iteration host/device status");
        run_network_device<NetAprox13>("aprox13");
        run_network_device<NetAprox19>("aprox19");
        run_network_device<NetAprox21>("aprox21");
        run_network_device<NetIso7>("iso7");
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
