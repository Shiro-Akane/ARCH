/**
 * @file test_eos_host_device_parity.cu
 * @brief Check EOS values and device-owner lifetime on both backends.
 *
 * Ideal, tabular and Helmholtz cases cover thermodynamics, composition,
 * upload descriptors, pointer ownership and delayed resource destruction.
 */
#include "physics/eos/IdealGas.h"

#include "cuda/microphysics/helm_eos_loader.h"
#include "cuda/common/DeviceAllocation.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"
#include "../fixtures/HelmReference.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

void cuda_check(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <class Function>
void require_invalid_argument(Function &&function, const char *message)
{
    try {
        function();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error(message);
}

double max_abs_error = 0.0;
double max_rel_error = 0.0;
std::map<std::string, std::pair<double, double>> field_errors;

void close(double actual, double authority, double tolerance, const std::string &field)
{
    if (std::isnan(authority)) {
        require(std::isnan(actual), field.c_str());
        return;
    }
    if (std::isinf(authority)) {
        require(actual == authority, field.c_str());
        return;
    }
    require(std::isfinite(actual), field.c_str());
    if (authority == 0.0) {
        require(actual == 0.0 && std::signbit(actual) == std::signbit(authority),
                field.c_str());
        return;
    }
    const double abs_error = std::abs(actual - authority);
    const double rel_error = abs_error / std::max(1.0, std::abs(authority));
    max_abs_error = std::max(max_abs_error, abs_error);
    max_rel_error = std::max(max_rel_error, rel_error);
    auto &statistics = field_errors[field];
    statistics.first = std::max(statistics.first, abs_error);
    statistics.second = std::max(statistics.second, rel_error);
    if (rel_error > tolerance) {
        std::cerr << std::setprecision(17) << field
                  << " actual=" << actual << " reference=" << authority
                  << " absolute_error=" << abs_error
                  << " relative_error=" << rel_error
                  << " tolerance=" << tolerance << '\n';
        throw std::runtime_error(std::string(field) + " value/reference mismatch");
    }
}

void frozen_bits(double actual, std::uint64_t authority, const char *field)
{
    if (std::bit_cast<std::uint64_t>(actual) != authority)
        throw std::runtime_error(std::string(field) + " changed from BASE raw bits");
}

void frozen_value(double actual, std::uint64_t authority_bits, double tolerance,
                  const char *field)
{
    close(actual, std::bit_cast<double>(authority_bits), tolerance, field);
}

void frozen_state(const eos_state_t &state,
                  const std::array<std::uint64_t, 9> &authority,
                  const char *prefix)
{
    const double values[9]{state.P, state.E, state.cv, state.sound_speed,
                           state.dp_drho, state.dp_dT, state.pele, state.xne,
                           state.eta};
    const char *names[9]{"P", "E", "cv", "sound_speed", "dp_drho", "dp_dT",
                         "pele", "xne", "eta"};
    for (int index = 0; index < 9; ++index) {
        const std::string field = std::string(prefix) + "." + names[index];
        frozen_bits(values[index], authority[index], field.c_str());
    }
}

template <class Function>
void require_exception_message(Function &&function, const char *authority,
                               const char *field)
{
    try {
        function();
    } catch (const std::exception &error) {
        require(std::string(error.what()) == authority, field);
        return;
    }
    throw std::runtime_error(std::string(field) + " did not throw");
}

struct Probe
{
    double gamma;
    double cv;
    double pressure_rho_T;
    double eint;
    double temperature;
    double pressure_rho_e;
    double sound_speed;
    double dp_drho;
    double dp_de;
    double total_energy;
    double state_P;
    double state_E;
    double state_cv;
    double state_sound_speed;
    double state_dp_drho;
    double state_dp_dT;
    double state_pele;
    double state_xne;
    double state_eta;
};

struct FrozenProbe
{
    std::array<std::uint64_t, 19> bits;
    std::array<double, 19> authority_margins;
};

// These margins compare the frozen BASE literals with the current host probe.
// They are deliberately independent of the host/device budgets in compare().
constexpr std::array<double, 19> exact_authority_margins{};
constexpr std::array<double, 19> helm_authority_margins{
    0.0, 0.0, 8.0e-14, 1.5e-15, 6.0e-16, 8.0e-14, 4.0e-10,
    1.6e-9, 5.3e-11, 1.6e-16, 8.0e-14, 1.5e-15, 0.0, 4.0e-10,
    8.0e-10, 2.5e-11, 8.0e-14, 0.0, 1.5e-15};
constexpr std::array<double, 19> tab3_table_authority_margins{
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.1e-15,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
constexpr std::array<double, 19> tab3_iteration_authority_margins{
    0.0, 0.0, 2.0e-16, 0.0, 0.0, 2.0e-16, 0.0, 1.7e-16, 0.0,
    3.0e-15, 2.0e-16, 0.0, 0.0, 0.0, 1.7e-16, 1.5e-16, 0.0, 0.0, 0.0};
constexpr std::array<double, 19> tab3_fd_authority_margins{
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 2.3e-11, 0.0, 1.1e-15,
    0.0, 0.0, 0.0, 0.0, 2.3e-11, 0.0, 0.0, 0.0, 0.0};
constexpr std::array<double, 19> tab4_table_authority_margins{
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 3.0e-16,
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
constexpr std::array<double, 19> tab4_iteration_authority_margins{
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 1.6e-16, 0.0, 0.0, 0.0, 0.0, 0.0};
constexpr std::array<double, 19> tab4_fd_authority_margins{
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 3.0e-16,
    0.0, 0.0, 0.0, 0.0, 0.0, 2.3e-10, 0.0, 0.0, 0.0};

constexpr FrozenProbe ideal_default_probe{{
    0x3ff6666666666666ULL, 0x4086700000000000ULL, 0x41e565e7bfffffffULL,
    0x41e565e7c0000000ULL, 0x414e848000000000ULL, 0x41e565e7bfffffffULL,
    0x40e394fbaf4fda70ULL, 0x41d11e52ffffffffULL, 0x3feffffffffffffeULL,
    0x41fabf61b0118000ULL, 0x41e565e7bfffffffULL, 0x41e565e7c0000000ULL,
    0x4086700000000000ULL, 0x40e394fbaf4fda70ULL, 0x41d11e52ffffffffULL,
    0x40866fffffffffffULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, exact_authority_margins};

constexpr FrozenProbe ideal_strict_probe{{
    0x3ff8f0f0f0f0f0f1ULL, 0x408a900000000000ULL, 0x41f1b1f3f8000000ULL,
    0x41e954fc40000000ULL, 0x414e848000000000ULL, 0x41f1b1f3f8000000ULL,
    0x40ea92c31f70f50aULL, 0x41dc4fecc0000000ULL, 0x3ff65a5a5a5a5a5aULL,
    0x41ffaa3b50118000ULL, 0x41f1b1f3f8000000ULL, 0x41e954fc40000000ULL,
    0x408a900000000000ULL, 0x40ea92c31f70f50aULL, 0x41dc4fecc0000000ULL,
    0x40928e0000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, exact_authority_margins};

constexpr FrozenProbe ideal_zero_probe{{
    0x3ff8cccccccccccdULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x3ff6000000000000ULL,
    0x3ff1800000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, exact_authority_margins};

constexpr FrozenProbe tab3_table_probe{{
    0x3ffaaaaaaaaaaaabULL, 0x412e848000000000ULL, 0x42d87152d40e0000ULL,
    0x42d6bcc41e911f80ULL, 0x4197d78400000000ULL, 0x42d87152d40e0000ULL,
    0x4173c9eb00000000ULL, 0x415afd2e00000000ULL, 0x4020266666666666ULL,
    0x42f15ba9e7e04f4bULL, 0x42d87152d40e0000ULL, 0x42d6bcc41e911f80ULL,
    0x412e848000000000ULL, 0x4173c9eb00000000ULL, 0x415afd2e00000000ULL,
    0x415ecdbe00000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, tab3_table_authority_margins};

constexpr FrozenProbe tab3_iteration_probe{{
    0x3ffaaaaaaaaaaaabULL, 0x412e848000000000ULL, 0x42d8a8de26589fd4ULL,
    0x42fe17eab16f84e6ULL, 0x41b1e1a2fff48a84ULL, 0x42d8a8de26587f6eULL,
    0x4173e1370403d032ULL, 0x415b067f9b34b9aeULL, 0x40202b49250dcc7cULL,
    0x42f2c35695e66f27ULL, 0x42d8a8de26589fd4ULL, 0x42fe17eab16f84e6ULL,
    0x412e848000000000ULL, 0x4173e1370403ddc9ULL, 0x415b067f9b34b9aeULL,
    0x415ed70f9b34bf1eULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, tab3_iteration_authority_margins};

constexpr FrozenProbe tab3_fd_probe{{
    0x3ffaaaaaaaaaaaabULL, 0x412e848000000000ULL, 0x42d87152d40e0000ULL,
    0x42d6bcc41e911f80ULL, 0x4197d78400000000ULL, 0x42d87152d40e0000ULL,
    0x4173c9eb00000000ULL, 0x422439320514d079ULL, 0x3f6a6be36ab51ab0ULL,
    0x430c6bf5349525b5ULL, 0x42d87152d40e0000ULL, 0x42d6bcc41e911f80ULL,
    0x412e848000000000ULL, 0x4173c9eb00000000ULL, 0x422439320514d079ULL,
    0x40c0f6f1e096bb99ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, tab3_fd_authority_margins};

constexpr FrozenProbe tab4_table_probe{{
    0x3ffaaaaaaaaaaaabULL, 0x412e848000000000ULL, 0x42e7dfcdece40000ULL,
    0x42d6bcc41e911580ULL, 0x4197d78400000000ULL, 0x42e7dfcdece40000ULL,
    0x417d905c00000000ULL, 0x41615b5c00000000ULL, 0x4024333333333333ULL,
    0x42ff01f1fc591c1aULL, 0x42e7dfcdece40000ULL, 0x42d6bcc41e911580ULL,
    0x412e848000000000ULL, 0x417d905c00000000ULL, 0x41615b5c00000000ULL,
    0x416343a400000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, tab4_table_authority_margins};

constexpr FrozenProbe tab4_iteration_probe{{
    0x3ffaaaaaaaaaaaabULL, 0x412e848000000000ULL, 0x42e7fb9396094feaULL,
    0x42fe17eab16f8265ULL, 0x41b1e1a2fff48a70ULL, 0x42e7fb9396093fb7ULL,
    0x417da7a80403d032ULL, 0x41616004cd9a5cd7ULL, 0x40243815f1da9949ULL,
    0x4300185bf4076d53ULL, 0x42e7fb9396094feaULL, 0x42fe17eab16f8265ULL,
    0x412e848000000000ULL, 0x417da7a80403ddc8ULL, 0x41616004cd9a5cd7ULL,
    0x4163484ccd9a5f8eULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, tab4_iteration_authority_margins};

constexpr FrozenProbe tab4_fd_probe{{
    0x3ffaaaaaaaaaaaabULL, 0x412e848000000000ULL, 0x42e7dfcdece40000ULL,
    0x42d6bcc41e911580ULL, 0x4197d78400000000ULL, 0x42e7dfcdece40000ULL,
    0x417d905c00000000ULL, 0x4224393205166079ULL, 0x3f6a6be36acb23cfULL,
    0x430c6bf5272e1d3eULL, 0x42e7dfcdece40000ULL, 0x42d6bcc41e911580ULL,
    0x412e848000000000ULL, 0x417d905c00000000ULL, 0x4224393205166079ULL,
    0x40c0f6f1e0aa64c3ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL}, tab4_fd_authority_margins};

void reference_probe(const Probe &actual, const std::array<double, 19>& authority,
                     const std::array<double, 19>& margins, const char *prefix)
{
    const double values[19]{
        actual.gamma, actual.cv, actual.pressure_rho_T, actual.eint,
        actual.temperature, actual.pressure_rho_e, actual.sound_speed,
        actual.dp_drho, actual.dp_de, actual.total_energy, actual.state_P,
        actual.state_E, actual.state_cv, actual.state_sound_speed,
        actual.state_dp_drho, actual.state_dp_dT, actual.state_pele,
        actual.state_xne, actual.state_eta};
    const char *names[19]{
        "gamma", "cv", "pressure_rho_T", "eint", "temperature",
        "pressure_rho_e", "sound_speed", "dp_drho", "dp_de", "total_energy",
        "state.P", "state.E", "state.cv", "state.sound_speed",
        "state.dp_drho", "state.dp_dT", "state.pele", "state.xne", "state.eta"};
    for (int index = 0; index < 19; ++index) {
        const std::string field = std::string(prefix) + "." + names[index];
        close(values[index], authority[index], margins[index], field);
    }
}

void frozen_probe(const Probe &actual, const FrozenProbe &authority,
                  const char *prefix)
{
    std::array<double, 19> values{};
    for (int i = 0; i < 19; ++i) values[i] = std::bit_cast<double>(authority.bits[i]);
    reference_probe(actual, values, authority.authority_margins, prefix);
}

// Independent fallback oracle, not a production constants/fallback invocation.
double ion_pressure_reference(double rho, double temperature, double ion_fraction)
{
    constexpr long double boltzmann = 1.380649e-16L;
    constexpr long double atomic_mass = 1.66053906892e-24L;
    return static_cast<double>(static_cast<long double>(rho) * temperature
                               * ion_fraction * boltzmann / atomic_mass);
}

void require_same_raw_probe(const Probe &wrapper, const Probe &leaf, const char *eos)
{
    const double wrapper_values[19]{
        wrapper.gamma, wrapper.cv, wrapper.pressure_rho_T, wrapper.eint,
        wrapper.temperature, wrapper.pressure_rho_e, wrapper.sound_speed,
        wrapper.dp_drho, wrapper.dp_de, wrapper.total_energy, wrapper.state_P,
        wrapper.state_E, wrapper.state_cv, wrapper.state_sound_speed,
        wrapper.state_dp_drho, wrapper.state_dp_dT, wrapper.state_pele,
        wrapper.state_xne, wrapper.state_eta};
    const double leaf_values[19]{
        leaf.gamma, leaf.cv, leaf.pressure_rho_T, leaf.eint, leaf.temperature,
        leaf.pressure_rho_e, leaf.sound_speed, leaf.dp_drho, leaf.dp_de,
        leaf.total_energy, leaf.state_P, leaf.state_E, leaf.state_cv,
        leaf.state_sound_speed, leaf.state_dp_drho, leaf.state_dp_dT,
        leaf.state_pele, leaf.state_xne, leaf.state_eta};
    for (int index = 0; index < 19; ++index) {
        if (std::bit_cast<std::uint64_t>(wrapper_values[index]) !=
            std::bit_cast<std::uint64_t>(leaf_values[index]))
            throw std::runtime_error(std::string(eos) +
                                     " CPU wrapper diverged from shared leaf");
    }
}

template <class View>
ARCH_INLINE Probe probe_leaf(View eos, const double* Xi, double rho, double T)
{
    Probe out{};
    out.gamma = eos.get_gamma(Xi);
    out.cv = eos.get_cv(rho, T, Xi);
    out.pressure_rho_T = eos.get_pressure_from_rho_T(rho, T, Xi);
    out.eint = eos.get_eint_from_T(rho, T, Xi);
    out.temperature = eos.get_temperature(rho, out.eint, Xi);
    out.pressure_rho_e = eos.get_pressure_from_rho_e(rho, out.eint, Xi);
    const FluidVector conservative{rho, 0.0, 0.0, 0.0, rho * out.eint};
    out.sound_speed = eos.get_sound_speed(conservative, out.pressure_rho_e, Xi);
    out.dp_drho = eos.get_dp_drho_e(rho, out.eint, Xi);
    out.dp_de = eos.get_dp_de_rho(rho, out.eint, Xi);
    out.total_energy = eos.get_total_energy_primitive(
        rho, 0.25, -0.5, 0.75, out.pressure_rho_T, Xi);
    eos_state_t state{};
    state.rho = rho;
    state.T = T;
    state.Xi = Xi;
    eos.evaluate_state(state);
    out.state_P = state.P;
    out.state_E = state.E;
    out.state_cv = state.cv;
    out.state_sound_speed = state.sound_speed;
    out.state_dp_drho = state.dp_drho;
    out.state_dp_dT = state.dp_dT;
    out.state_pele = state.pele;
    out.state_xne = state.xne;
    out.state_eta = state.eta;
    return out;
}

template <class Eos>
Probe probe_host_wrapper(const Eos &eos, const double *Xi, double rho, double T)
{
    Probe out{};
    out.gamma = eos.get_gamma(Xi);
    out.cv = eos.get_cv(rho, T, Xi);
    out.pressure_rho_T = eos.get_pressure_from_rho_T(rho, T, Xi);
    out.eint = eos.get_eint_from_T(rho, T, Xi);
    out.temperature = eos.get_temperature(rho, out.eint, Xi);
    out.pressure_rho_e = eos.get_pressure_from_rho_e(rho, out.eint, Xi);
    const FluidVector conservative{rho, 0.0, 0.0, 0.0, rho * out.eint};
    out.sound_speed = eos.get_sound_speed(conservative, out.pressure_rho_e, Xi);
    out.dp_drho = eos.get_dp_drho_e(rho, out.eint, Xi);
    out.dp_de = eos.get_dp_de_rho(rho, out.eint, Xi);
    out.total_energy = eos.get_total_energy_primitive(
        rho, 0.25, -0.5, 0.75, out.pressure_rho_T, Xi);
    eos_state_t state{};
    state.rho = rho; state.T = T; state.Xi = Xi;
    eos.evaluate_state(state);
    out.state_P = state.P; out.state_E = state.E; out.state_cv = state.cv;
    out.state_sound_speed = state.sound_speed; out.state_dp_drho = state.dp_drho;
    out.state_dp_dT = state.dp_dT; out.state_pele = state.pele;
    out.state_xne = state.xne; out.state_eta = state.eta;
    return out;
}

template <class View>
__global__ void probe_kernel(View eos, const double* Xi, double rho, double T, Probe* out)
{
    *out = probe_leaf(eos, Xi, rho, T);
}

template <class View>
Probe run_device(View view, const std::vector<double>& Xi, double rho, double T,
                 cudaStream_t stream)
{
    double* device_Xi = nullptr;
    Probe* device_probe = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_Xi), Xi.size() * sizeof(double)),
               "cudaMalloc Xi");
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_probe), sizeof(Probe)),
               "cudaMalloc probe");
    cuda_check(cudaMemcpyAsync(device_Xi, Xi.data(), Xi.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream), "copy Xi");
    probe_kernel<<<1, 1, 0, stream>>>(view, device_Xi, rho, T, device_probe);
    cuda_check(cudaGetLastError(), "probe launch");
    Probe result{};
    cuda_check(cudaMemcpyAsync(&result, device_probe, sizeof(result),
                               cudaMemcpyDeviceToHost, stream), "copy probe");
    std::vector<double> composition_after(Xi.size());
    cuda_check(cudaMemcpyAsync(composition_after.data(), device_Xi,
                               Xi.size() * sizeof(double), cudaMemcpyDeviceToHost, stream),
               "copy composition after probe");
    cuda_check(cudaStreamSynchronize(stream), "probe synchronize");
    require(composition_after == Xi, "EOS device leaf modified fixed composition");
    cuda_check(cudaFree(device_probe), "free probe");
    cuda_check(cudaFree(device_Xi), "free Xi");
    return result;
}

void compare(const Probe& device, const Probe& host, const char *eos)
{
    const std::string prefix = std::string(eos) + ".";
    // Frozen from the original H100 run; each is the observed per-field maximum
    // plus a 20-25% rounding margin, rather than a shared wide tolerance bucket.
    close(device.gamma, host.gamma, 0.0, prefix + "gamma");
    close(device.cv, host.cv, 3.0e-16, prefix + "cv");
    close(device.pressure_rho_T, host.pressure_rho_T, 4.1e-14,
          prefix + "pressure_rho_T");
    close(device.eint, host.eint, 6.0e-16, prefix + "eint");
    // The table-iteration path stops on the same tight Newton residual on both
    // sides, but host and device may land a few ULPs apart at the final update.
    const double temperature_tolerance =
        std::string(eos).find("iteration identity") != std::string::npos
            ? 8.0e-15
            : 5.0e-16;
    close(device.temperature, host.temperature, temperature_tolerance,
          prefix + "temperature");
    close(device.pressure_rho_e, host.pressure_rho_e, 4.1e-14,
          prefix + "pressure_rho_e");
    close(device.sound_speed, host.sound_speed, 5.0e-10, prefix + "sound_speed");
    close(device.dp_drho, host.dp_drho, 1.7e-9, prefix + "dp_drho");
    close(device.dp_de, host.dp_de, 2.5e-12, prefix + "dp_de");
    // Finite-difference tables amplify the last-ULP derivative difference in
    // the outer energy inversion.  Keep that path on its measured 8e-12
    // envelope while all analytic/table probes retain their 8e-15 gate.
    const double total_energy_tolerance =
        std::string(eos).find("finite difference") != std::string::npos
            ? 8.0e-12
            : 8.0e-15;
    close(device.total_energy, host.total_energy, total_energy_tolerance,
          prefix + "total_energy");
    close(device.state_P, host.state_P, 4.1e-14, prefix + "state.P");
    close(device.state_E, host.state_E, 6.0e-16, prefix + "state.E");
    close(device.state_cv, host.state_cv, 3.0e-16, prefix + "state.cv");
    close(device.state_sound_speed, host.state_sound_speed, 5.0e-10,
          prefix + "state.sound_speed");
    close(device.state_dp_drho, host.state_dp_drho, 1.1e-9,
          prefix + "state.dp_drho");
    close(device.state_dp_dT, host.state_dp_dT, 0.0, prefix + "state.dp_dT");
    close(device.state_pele, host.state_pele, 4.5e-14, prefix + "state.pele");
    close(device.state_xne, host.state_xne, 0.0, prefix + "state.xne");
    close(device.state_eta, host.state_eta, 0.0, prefix + "state.eta");
}

template <class View>
__global__ void pressure_kernel(View view, const double* Xi, double rho, double T,
                                double* result)
{
    *result = view.get_pressure_from_rho_T(rho, T, Xi);
}

template <class View>
double run_pressure(View view, const std::vector<double>& Xi, double rho, double T,
                  cudaStream_t stream)
{
    double* device_Xi = nullptr;
    double* device_result = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_Xi), Xi.size() * sizeof(double)),
               "cudaMalloc scalar Xi");
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_result), sizeof(double)),
               "cudaMalloc scalar result");
    cuda_check(cudaMemcpyAsync(device_Xi, Xi.data(), Xi.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream), "copy scalar Xi");
    pressure_kernel<<<1, 1, 0, stream>>>(view, device_Xi, rho, T, device_result);
    cuda_check(cudaGetLastError(), "pressure launch");
    double result = 0.0;
    cuda_check(cudaMemcpyAsync(&result, device_result, sizeof(result),
                               cudaMemcpyDeviceToHost, stream), "copy scalar result");
    cuda_check(cudaStreamSynchronize(stream), "scalar synchronize");
    cuda_check(cudaFree(device_result), "free scalar result");
    cuda_check(cudaFree(device_Xi), "free scalar Xi");
    return result;
}

template <class View>
__global__ void state_kernel(View view, const double *Xi, double rho, double T,
                            eos_state_t *result)
{
    eos_state_t state{};
    state.rho = rho; state.T = T; state.Xi = Xi;
    view.evaluate_state(state);
    state.Xi = nullptr; // Do not export a device-only borrowed pointer to Host.
    *result = state;
}

template <class View>
eos_state_t run_state(View view, const std::vector<double> &Xi,
                     double rho, double T, cudaStream_t stream)
{
    double *device_Xi = nullptr;
    eos_state_t *device_result = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_Xi),
                          Xi.size() * sizeof(double)), "cudaMalloc dp_dT Xi");
    cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_result), sizeof(eos_state_t)),
               "cudaMalloc dp_dT result");
    cuda_check(cudaMemcpyAsync(device_Xi, Xi.data(), Xi.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream), "copy dp_dT Xi");
    state_kernel<<<1, 1, 0, stream>>>(
        view, device_Xi, rho, T, device_result);
    cuda_check(cudaGetLastError(), "dp_dT launch");
    eos_state_t result{};
    cuda_check(cudaMemcpyAsync(&result, device_result, sizeof(result),
                               cudaMemcpyDeviceToHost, stream), "copy dp_dT result");
    cuda_check(cudaStreamSynchronize(stream), "dp_dT synchronize");
    cuda_check(cudaFree(device_result), "free dp_dT result");
    cuda_check(cudaFree(device_Xi), "free dp_dT Xi");
    return result;
}

template <class View>
double run_state_dp_dT(View view, const std::vector<double>& Xi,
                       double rho, double T, cudaStream_t stream)
{
    return run_state(view, Xi, rho, T, stream).dp_dT;
}

void assert_device_pointer(const void* pointer, const char* field)
{
    require(pointer != nullptr, field);
    cudaPointerAttributes attributes{};
    cuda_check(cudaPointerGetAttributes(&attributes, pointer), field);
    require(attributes.type == cudaMemoryTypeDevice, field);
}

void assert_released_device_pointer(const void *pointer, const char *field)
{
    cudaPointerAttributes attributes{};
    const cudaError_t status = cudaPointerGetAttributes(&attributes, pointer);
    if (status == cudaSuccess) {
        require(attributes.type != cudaMemoryTypeDevice, field);
    } else {
        require(status == cudaErrorInvalidValue, field);
        (void)cudaGetLastError();
    }
}

std::vector<double> table3(double base, double di, double dj, double dk)
{
    std::vector<double> data(27);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                data[(i * 3 + j) * 3 + k] = base + i * di + j * dj + k * dk;
    data.back() += 0.125;
    return data;
}

std::vector<double> energy3()
{
    std::vector<double> data(27);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                data[(i * 3 + j) * 3 + k] = 1.0e6 * pow(10.0, 7.0 + j)
                    + 1000.0 * i + 100.0 * k;
    data.back() += 0.25;
    return data;
}

std::vector<double> table4(double base, double di, double dj, double dk, double dl)
{
    std::vector<double> data(81);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l)
                    data[((i * 3 + j) * 3 + k) * 3 + l] =
                        base + i * di + j * dj + k * dk + l * dl;
    data.back() += 0.375;
    return data;
}

std::vector<double> energy4()
{
    std::vector<double> data(81);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l)
                    data[((i * 3 + j) * 3 + k) * 3 + l] =
                        1.0e6 * pow(10.0, 7.0 + j) + 1000.0 * i + 100.0 * k + 10.0 * l;
    data.back() += 0.5;
    return data;
}

Tabular3DEOSHostView make_tab3(
    const SpeciesManager& species, const std::array<std::vector<double>, 6>& tables,
    bool dp_drho, bool dp_dT)
{
    Tabular3DEOSHostView view{};
    view.n_rho = view.n_T = view.n_X = 3;
    view.log_rho_min = 0.0; view.log_rho_max = 2.0; view.dlog_rho = 1.0;
    view.log_T_min = 7.0; view.log_T_max = 9.0; view.dlog_T = 1.0;
    view.X_min = 0.0; view.X_max = 1.0; view.dX = 0.5;
    view.table_P = tables[0].data();
    view.table_E = tables[1].data();
    view.table_cs = tables[2].data();
    view.table_cv = tables[3].data();
    view.table_dP_drho = dp_drho ? tables[4].data() : nullptr;
    view.table_dP_dT = dp_dT ? tables[5].data() : nullptr;
    view.table_extents = {tables[0].size(), tables[1].size(), tables[2].size(),
                          tables[3].size(), dp_drho ? tables[4].size() : 0,
                          dp_dT ? tables[5].size() : 0};
    view.specs = species.get_host_view();
    view.target_species_id = 1;
    return view;
}

Tabular4DEOSHostView make_tab4(
    const SpeciesManager& species, const std::array<std::vector<double>, 6>& tables,
    bool dp_drho, bool dp_dT)
{
    Tabular4DEOSHostView view{};
    view.n_rho = view.n_T = view.n_A = view.n_Z = 3;
    view.log_rho_min = 0.0; view.log_rho_max = 2.0; view.dlog_rho = 1.0;
    view.log_T_min = 7.0; view.log_T_max = 9.0; view.dlog_T = 1.0;
    view.A_min = 1.0; view.A_max = 2.0; view.dA = 0.5;
    view.Z_min = 0.5; view.Z_max = 1.0; view.dZ = 0.25;
    view.table_P = tables[0].data();
    view.table_E = tables[1].data();
    view.table_cs = tables[2].data();
    view.table_cv = tables[3].data();
    view.table_dP_drho = dp_drho ? tables[4].data() : nullptr;
    view.table_dP_dT = dp_dT ? tables[5].data() : nullptr;
    view.table_extents = {tables[0].size(), tables[1].size(), tables[2].size(),
                          tables[3].size(), dp_drho ? tables[4].size() : 0,
                          dp_dT ? tables[5].size() : 0};
    view.specs = species.get_host_view();
    return view;
}

struct SixValues { double value[6]; };

struct CompositionSample { double energy[2], cv[3], hessian[3]; };

template <class View>
ARCH_HOST_DEVICE CompositionSample composition_sample(View view, double rho, double T, double first)
{
    const double fractions[]{first, 1.0 - first}, flow[]{-0.4, 0.4};
    CompositionSample result{};
    view.template get_energy_composition_gradient<3>(rho, T, fractions, result.energy);
    view.template get_cv_gradient<3>(rho, T, fractions, result.cv);
    view.template get_energy_composition_hessian_action<3>(rho, T, fractions, flow, result.hessian);
    return result;
}

template <class View>
__global__ void composition_kernel(View view, double rho, double T, double first, CompositionSample* result)
{ *result = composition_sample(view, rho, T, first); }

// Independently manufactured tables: free energy is a polynomial in ln(rho),
// ln(T) and the table composition coordinates. Value-format tables instead
// prescribe independently tabulated E and cv, exercising their distinct slopes.
template <bool FourDimensional>
void test_tabular_composition(cudaStream_t stream)
{
    using Host = std::conditional_t<FourDimensional, Tabular4DEOSHostView, Tabular3DEOSHostView>;
    using Owner = std::conditional_t<FourDimensional, arch::cuda::Tabular4DEOSDeviceOwner,
                                                    arch::cuda::Tabular3DEOSDeviceOwner>;
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 700.0);
    species.add_species("b", 4.0, 2.0, 1.6, 900.0);
    for (const bool free_energy : {false, true}) {
        Host host{};
        host.n_rho = host.n_T = 3;
        host.log_rho_min = host.log_T_min = 0.0;
        host.log_rho_max = host.log_T_max = 0.4;
        host.dlog_rho = host.dlog_T = 0.2;
        host.specs = species.get_host_view();
        if constexpr (FourDimensional) {
            host.n_A = host.n_Z = 3;
            host.A_min = 1.0; host.A_max = 4.0; host.dA = 1.5;
            host.Z_min = 0.5; host.Z_max = 2.0; host.dZ = 0.75;
        } else {
            host.n_X = 3; host.X_min = 0.0; host.X_max = 1.0; host.dX = 0.5;
            host.target_species_id = 1;
        }
        constexpr int count = FourDimensional ? 81 : 27;
        std::array<std::vector<double>, 6> values;
        std::array<std::vector<double>, tabular_eos::FieldCount> fields;
        for (auto& v : values) v.resize(count);
        for (auto& f : fields) f.resize(count);
        for (int ir = 0; ir < 3; ++ir) for (int it = 0; it < 3; ++it)
            for (int ia = 0; ia < 3; ++ia) for (int iz = 0; iz < (FourDimensional ? 3 : 1); ++iz) {
                const int index = FourDimensional ? ((ir * 3 + it) * 3 + ia) * 3 + iz
                                                  : (ir * 3 + it) * 3 + ia;
                const double r = std::log(10.0) * 0.2 * ir, t = std::log(10.0) * 0.2 * it;
                const double A = 1.0 + 1.5 * ia, Z = 0.5 + 0.75 * iz;
                const double C = FourDimensional ? 2.0 + 0.25 * A + 0.5 * Z + 0.125 * A * Z
                                                : 2.0 + 0.25 * ia;
                const double jet[]{10.0 + 2.0 * r - 0.5 * C * t * t,
                                   2.0, -C * t, 0.0, 0.0, -C, 0.0, 0.0, 0.0};
                for (int f = 0; f < tabular_eos::FieldCount; ++f) fields[f][index] = jet[f];
                values[0][index] = 2.0 * std::exp(r);
                values[1][index] = 10.0 + 2.0 * r + C * t;
                values[2][index] = 3.0;
                values[3][index] = 3.0 + C * t;
            }
        host.uses_free_energy = free_energy;
        if (!free_energy) {
            host.table_P = values[0].data(); host.table_E = values[1].data();
            host.table_cs = values[2].data(); host.table_cv = values[3].data();
            host.table_extents = {count, count, count, count, 0, 0};
        }
        for (int f = 0; f < tabular_eos::FieldCount; ++f) {
            host.free_energy_fields[f] = free_energy ? fields[f].data() : nullptr;
            host.free_energy_extents[f] = free_energy ? count : 0;
        }
        // Include both explicit-X and Ye coordinates for 3D, cell interiors,
        // temperature nodes, and the same analytic out-of-table fallback.
        for (int coordinate = 0; coordinate < (FourDimensional ? 1 : 2); ++coordinate) {
            if constexpr (!FourDimensional) host.target_species_id = coordinate == 0 ? 1 : -1;
            Owner owner(host, stream);
            arch::cuda::DeviceAllocation<CompositionSample> allocation;
            allocation.allocate(1);
            for (double first : {0.4, 0.6}) for (double T : {1.0, std::exp(0.25)})
                for (double rho : {std::exp(0.1), 100.0}) {
                    composition_kernel<<<1, 1, 0, stream>>>(owner.view(), rho, T, first, allocation.get());
                    cuda_check(cudaGetLastError(), "tabular composition launch");
                    CompositionSample device{};
                    cuda_check(cudaMemcpyAsync(&device, allocation.get(), sizeof(device), cudaMemcpyDeviceToHost, stream),
                               "tabular composition copy");
                    cuda_check(cudaStreamSynchronize(stream), "tabular composition completion");
                    const double y = first + (1.0 - first) / 4.0;
                    const double z = first + (1.0 - first) / 2.0;
                    const double t = std::log(T);
                    double C, gradient[2], hessian[2];
                    if constexpr (FourDimensional) {
                        C = 2.0 + 0.25 / y + 0.5 * z / y + 0.125 * z / (y * y);
                        const double cy = -0.25 / (y*y) - 0.5*z/(y*y) - 0.25*z/(y*y*y);
                        const double cz = 0.5/y + 0.125/(y*y);
                        const double cyy = 0.5/(y*y*y) + z/(y*y*y) + 0.75*z/(y*y*y*y);
                        const double cyz = -0.5/(y*y) - 0.25/(y*y*y);
                        gradient[0] = cy + cz; gradient[1] = cy/4.0 + cz/2.0;
                        hessian[0] = -0.3*cyy - 0.5*cyz;
                        hessian[1] = (-0.3*cyy - 0.2*cyz)/4.0 - 0.15*cyz;
                    } else {
                        C = 2.0 + 0.5 * (coordinate == 0 ? 1.0 - first : z);
                        gradient[0] = coordinate == 0 ? 0.0 : 0.5;
                        gradient[1] = coordinate == 0 ? 0.5 : 0.25;
                        hessian[0] = hessian[1] = 0.0;
                    }
                    CompositionSample expected{};
                    for (int i = 0; i < 2; ++i) {
                        expected.energy[i] = gradient[i] * (free_energy ? t - 0.5*t*t : t);
                        expected.cv[i] = gradient[i] * (free_energy ? (1.0-t)/T : t);
                        expected.hessian[i] = hessian[i] * (free_energy ? t - 0.5*t*t : t);
                    }
                    expected.cv[2] = free_energy ? C*(t-2.0)/(T*T) : C/T;
                    expected.hessian[2] = (-0.4*gradient[0] + 0.4*gradient[1]) * (free_energy ? (1.0-t)/T : 1.0/T);
                    if (rho == 100.0) {
                        const double coefficient = ion_pressure_reference(1.0, 1.0, 1.0) * 1.5;
                        expected = {};
                        expected.energy[0] = T*coefficient; expected.energy[1] = T*coefficient/4.0;
                        expected.cv[0] = coefficient; expected.cv[1] = coefficient/4.0;
                        expected.hessian[2] = -0.3*coefficient;
                    }
                    const auto check = [&](double actual, double reference, const char* field) {
                        // The manufactured dimensionless polynomial contains exact
                        // zero derivatives; an absolute + relative budget is needed.
                        require(std::isfinite(actual), field);
                        require(std::abs(actual-reference) <= 2e-10 * std::max(1.0, std::abs(reference)), field);
                    };
                    for (const auto& result : {composition_sample(host, rho, T, first), device}) {
                        for (int i = 0; i < 2; ++i) check(result.energy[i], expected.energy[i], "tabular analytic energy composition");
                        for (int i = 0; i < 3; ++i) {
                            check(result.cv[i], expected.cv[i], "tabular analytic cv derivative");
                            check(result.hessian[i], expected.hessian[i], "tabular analytic energy Hessian");
                        }
                    }
                }
        }
    }
    std::cout << "tabular_composition dimension=" << (FourDimensional ? 4 : 3)
              << " value/free_energy CPU/CUDA analytic derivatives PASS\n";
}

__global__ void tab3_final_kernel(Tabular3DEOSView view, SixValues* out)
{
    const int last = view.n_rho * view.n_T * view.n_X - 1;
    out->value[0] = view.table_P[last]; out->value[1] = view.table_E[last];
    out->value[2] = view.table_cs[last]; out->value[3] = view.table_cv[last];
    out->value[4] = view.table_dP_drho[last]; out->value[5] = view.table_dP_dT[last];
}

__global__ void tab4_final_kernel(Tabular4DEOSView view, SixValues* out)
{
    const int last = view.n_rho * view.n_T * view.n_A * view.n_Z - 1;
    out->value[0] = view.table_P[last]; out->value[1] = view.table_E[last];
    out->value[2] = view.table_cs[last]; out->value[3] = view.table_cv[last];
    out->value[4] = view.table_dP_drho[last]; out->value[5] = view.table_dP_dT[last];
}

struct HelmFinalValues { double f[9]; double ef[4]; };

__global__ void helm_final_kernel(HelmEosView view, HelmFinalValues* out)
{
    const int last = HelmEosView::imax * HelmEosView::jmax - 1;
    for (int i = 0; i < 9; ++i) out->f[i] = view.f[i][last];
    for (int i = 0; i < 4; ++i) out->ef[i] = view.ef_table[i][last];
}

__global__ void delayed_gamma_kernel(IdealGasView view, const double* Xi, double* result)
{
    const unsigned long long start = clock64();
    while (clock64() - start < 5000000ULL) {}
    *result = view.get_gamma(Xi);
}

template <class View>
__global__ void delayed_dp_dT_kernel(View view, const double *Xi, double rho, double T,
                                     double *result)
{
    const unsigned long long start = clock64();
    while (clock64() - start < 5000000ULL) {}
    eos_state_t state{};
    state.rho = rho; state.T = T; state.Xi = Xi;
    view.evaluate_state(state);
    *result = state.dp_dT;
}

template <class Factory>
void test_delayed_owner_destruction(Factory factory, const std::vector<double> &Xi,
                                    double rho, double T, double authority,
                                    cudaStream_t stream, const char *field)
{
    double *device_Xi = nullptr;
    double *device_result = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_Xi),
                          Xi.size() * sizeof(double)), "cudaMalloc delayed EOS Xi");
    cuda_check(cudaMalloc(reinterpret_cast<void **>(&device_result), sizeof(double)),
               "cudaMalloc delayed EOS result");
    cuda_check(cudaMemcpyAsync(device_Xi, Xi.data(), Xi.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream), "copy delayed EOS Xi");
    {
        auto owner = factory();
        // Consumer contract: every kernel using the borrowed view is enqueued on
        // the stream recorded by the owner. Immediate destruction synchronizes it.
        delayed_dp_dT_kernel<<<1, 1, 0, stream>>>(
            owner.view(), device_Xi, rho, T, device_result);
        cuda_check(cudaGetLastError(), "delayed EOS launch");
    }
    double result = 0.0;
    cuda_check(cudaMemcpy(&result, device_result, sizeof(double),
                          cudaMemcpyDeviceToHost), "copy delayed EOS result");
    close(result, authority, 5.0e-10, field);
    cuda_check(cudaFree(device_result), "free delayed EOS result");
    cuda_check(cudaFree(device_Xi), "free delayed EOS Xi");
}

void test_comparison_policy()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    close(1.0, 1.0, 0.0, "finite comparison policy");
    close(nan, nan, 0.0, "NaN comparison policy");
    close(inf, inf, 0.0, "+Inf comparison policy");
    close(-inf, -inf, 0.0, "-Inf comparison policy");
    close(0.0, 0.0, 0.0, "+0 comparison policy");
    close(-0.0, -0.0, 0.0, "-0 comparison policy");
    const auto rejects = [](double actual, double authority, const char *field) {
        try {
            close(actual, authority, 0.0, field);
        } catch (const std::runtime_error &) {
            return;
        }
        throw std::runtime_error(std::string(field) + " was accepted");
    };
    rejects(nan, 1.0, "finite authority/device NaN policy");
    rejects(inf, -inf, "infinity sign policy");
    rejects(-0.0, 0.0, "signed-zero authority policy");
}

void test_type_contracts()
{
    static_assert(std::is_trivially_copyable_v<SpeciesPODView>);
    static_assert(std::is_trivially_copyable_v<IdealGasView>);
    static_assert(std::is_trivially_copyable_v<HelmEosView>);
    static_assert(std::is_trivially_copyable_v<Tabular3DEOSView>);
    static_assert(std::is_trivially_copyable_v<Tabular4DEOSView>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::DeviceSpeciesOwner>);
    static_assert(!std::is_copy_assignable_v<arch::cuda::DeviceSpeciesOwner>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::HelmEosDeviceOwner>);
    static_assert(!std::is_copy_assignable_v<arch::cuda::HelmEosDeviceOwner>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::Tabular3DEOSDeviceOwner>);
    static_assert(!std::is_copy_assignable_v<arch::cuda::Tabular3DEOSDeviceOwner>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::Tabular4DEOSDeviceOwner>);
    static_assert(!std::is_copy_assignable_v<arch::cuda::Tabular4DEOSDeviceOwner>);
    static_assert(std::is_nothrow_move_constructible_v<arch::cuda::DeviceSpeciesOwner>);
    static_assert(std::is_nothrow_move_assignable_v<arch::cuda::DeviceSpeciesOwner>);
    static_assert(std::is_nothrow_move_constructible_v<arch::cuda::HelmEosDeviceOwner>);
    static_assert(std::is_nothrow_move_assignable_v<arch::cuda::HelmEosDeviceOwner>);
    static_assert(std::is_nothrow_move_constructible_v<arch::cuda::Tabular3DEOSDeviceOwner>);
    static_assert(std::is_nothrow_move_assignable_v<arch::cuda::Tabular3DEOSDeviceOwner>);
    static_assert(std::is_nothrow_move_constructible_v<arch::cuda::Tabular4DEOSDeviceOwner>);
    static_assert(std::is_nothrow_move_assignable_v<arch::cuda::Tabular4DEOSDeviceOwner>);
}

void test_species_upload_descriptor_lengths(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 700.0);
    auto descriptor = species.get_host_view();
    descriptor.extent = 0;
    require_invalid_argument(
        [&] { arch::cuda::DeviceSpeciesOwner rejected(descriptor, stream); },
        "species short descriptor was accepted");
    descriptor = species.get_host_view();
    descriptor.extent = 2;
    require_invalid_argument(
        [&] { arch::cuda::DeviceSpeciesOwner rejected(descriptor, stream); },
        "species long descriptor was accepted");
    descriptor = species.get_host_view();
    descriptor.host_owner = nullptr;
    require_invalid_argument(
        [&] { arch::cuda::DeviceSpeciesOwner rejected(descriptor, stream); },
        "species unowned raw descriptor was accepted");
    GasProperty safe_sentinel{"sentinel", 9.0, 9.0, 9.0, 9.0};
    descriptor = species.get_host_view();
    descriptor.host_data = &safe_sentinel;
    require_invalid_argument(
        [&] { arch::cuda::DeviceSpeciesOwner rejected(descriptor, stream); },
        "species data not owned by descriptor owner was accepted");
}

template <class HostView, class Owner>
void test_tabular_upload_descriptor_lengths(
    const HostView &valid, cudaStream_t stream, const char *prefix)
{
    const std::size_t expected = valid.table_extents[0];
    for (int index = 0; index < 4; ++index) {
        HostView short_view = valid;
        short_view.table_extents[index] = expected - 1;
        require_invalid_argument([&] { Owner rejected(short_view, stream); }, prefix);
        HostView long_view = valid;
        long_view.table_extents[index] = expected + 1;
        require_invalid_argument([&] { Owner rejected(long_view, stream); }, prefix);
    }
    for (int index = 4; index < 6; ++index) {
        HostView short_view = valid;
        short_view.table_extents[index] = expected - 1;
        require_invalid_argument([&] { Owner rejected(short_view, stream); }, prefix);
        HostView long_view = valid;
        long_view.table_extents[index] = expected + 1;
        require_invalid_argument([&] { Owner rejected(long_view, stream); }, prefix);
        HostView null_nonzero = valid;
        (index == 4 ? null_nonzero.table_dP_drho : null_nonzero.table_dP_dT) = nullptr;
        null_nonzero.table_extents[index] = expected;
        require_invalid_argument([&] { Owner rejected(null_nonzero, stream); }, prefix);
        HostView pointer_zero = valid;
        pointer_zero.table_extents[index] = 0;
        require_invalid_argument([&] { Owner rejected(pointer_zero, stream); }, prefix);
    }
}

void test_ideal_and_lifetime(cudaStream_t stream)
{
    SpeciesManager empty_species;
    const std::vector<double> dummy_Xi{1.0};
    IdealGas ideal_default(empty_species);
    frozen_bits(ideal_default.get_gamma(dummy_Xi.data()), 0x3ff6666666666666ULL,
                "ideal.default.gamma");
    frozen_bits(ideal_default.get_mixture_Cv(dummy_Xi.data()), 0x4086700000000000ULL,
                "ideal.default.cv");
    frozen_bits(ideal_default.get_pressure_from_rho_T(0.0, 1.0e5, dummy_Xi.data()),
                0x0000000000000000ULL, "ideal.invalid.pressure");
    arch::cuda::DeviceSpeciesOwner empty_owner(empty_species, stream);
    const Probe default_host = probe_host_wrapper(
        ideal_default, dummy_Xi.data(), 2.5, 4.0e6);
    frozen_probe(default_host, ideal_default_probe, "ideal.default.probe");
    const Probe default_device = run_device(
        empty_owner.ideal_gas_view(1.4), dummy_Xi, 2.5, 4.0e6, stream);
    compare(default_device, default_host, "Ideal default");

    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 700.0);
    species.add_species("b", 4.0, 2.0, 1.6, 900.0);
    const std::vector<double> Xi{0.25, 0.75};
    IdealGas host(1.55, species);
    const auto host_view = host.get_view();
    const std::vector<double> zero_Xi{0.0, 0.0};
    frozen_bits(host.get_gamma(Xi.data()), 0x3ff8f0f0f0f0f0f1ULL,
                "ideal.strict.gamma");
    frozen_bits(host.get_mixture_Cv(Xi.data()), 0x408a900000000000ULL,
                "ideal.strict.cv");
    frozen_bits(host.get_gamma(zero_Xi.data()), 0x3ff8cccccccccccdULL,
                "ideal.strict.zero_gamma");
    eos_state_t frozen{};
    frozen.rho = 2.5; frozen.T = 4.0e6; frozen.Xi = Xi.data();
    host.evaluate_state(frozen);
    frozen_state(frozen, {0x41f1b1f3f8000000ULL, 0x41e954fc40000000ULL,
                          0x408a900000000000ULL, 0x40ea92c31f70f50aULL,
                          0x41dc4fecc0000000ULL, 0x40928e0000000000ULL,
                          0x0000000000000000ULL, 0x0000000000000000ULL,
                          0x0000000000000000ULL}, "ideal.state");
    require_same_raw_probe(probe_host_wrapper(host, Xi.data(), 2.5, 4.0e6),
                           probe_leaf(host_view, Xi.data(), 2.5, 4.0e6), "Ideal");
    const Probe expected = probe_leaf(host_view, Xi.data(), 2.5, 4.0e6);
    frozen_probe(expected, ideal_strict_probe, "ideal.strict.probe");
    const Probe zero_expected = probe_leaf(host_view, zero_Xi.data(), 2.5, 4.0e6);
    frozen_probe(zero_expected, ideal_zero_probe, "ideal.zero.probe");
    const auto isentrope = eos_utils::get_isentropic_state_at_pressure_factor(
        host, 2.5, 4.0e6, Xi.data(), 1.01);
    frozen_bits(isentrope.rho, 0x400420c9637ee893ULL, "ideal.isentrope.rho");
    frozen_bits(isentrope.temperature, 0x414ea06af056b7adULL,
                "ideal.isentrope.temperature");
    frozen_bits(isentrope.pressure, 0x41f1df40a6000035ULL,
                "ideal.isentrope.pressure");
    frozen_bits(isentrope.sound_speed, 0x40ea9ee7f58caf30ULL,
                "ideal.isentrope.sound_speed");
    require(Xi == std::vector<double>({0.25, 0.75}),
            "Ideal isentrope changed composition");

    arch::cuda::DeviceSpeciesOwner first(species, stream);
    assert_device_pointer(first.view().A, "species A pointer");
    assert_device_pointer(first.view().Z, "species Z pointer");
    assert_device_pointer(first.view().gamma, "species gamma pointer");
    assert_device_pointer(first.view().Cv, "species Cv pointer");
    compare(run_device(first.ideal_gas_view(1.55), Xi, 2.5, 4.0e6, stream),
            expected, "Ideal");
    const Probe zero_device = run_device(
        first.ideal_gas_view(1.55), zero_Xi, 2.5, 4.0e6, stream);
    compare(zero_device, zero_expected, "Ideal zero composition");

    const double below = std::nextafter(1.0e-12, 0.0);
    const double exact = 1.0e-12;
    const double above = std::nextafter(
        1.0e-12, std::numeric_limits<double>::infinity());
    const double densities[8]{below, exact, above,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), 0.0, -0.0};
    const std::uint64_t density_pressure_bits[8]{
        0x3fa851eb851eb850ULL, 0x3fa851eb851eb851ULL, 0x3fa851eb851eb854ULL,
        0x7ff8000000000000ULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL,
        0x0000000000000000ULL, 0x8000000000000000ULL};
    for (int index = 0; index < 8; ++index) {
        const double rho = densities[index];
        const double authority = host_view.get_pressure_from_rho_T(
            rho, 1.0e8, Xi.data());
        frozen_value(authority, density_pressure_bits[index], 2.0e-17,
                     "ideal BASE nonfinite/threshold pressure");
        close(run_pressure(first.ideal_gas_view(1.55), Xi, rho, 1.0e8, stream),
              authority, 0.0,
              "Ideal nonfinite/threshold pressure policy");
    }

    const auto original_view = first.view();
    arch::cuda::DeviceSpeciesOwner moved(std::move(first));
    require(first.empty(), "moved-from species owner retained storage");
    require(moved.view().gamma == original_view.gamma,
            "species move changed allocation identity");
    compare(run_device(moved.ideal_gas_view(1.55), Xi, 2.5, 4.0e6, stream),
            expected, "Ideal moved");
    SpeciesManager target_species;
    target_species.add_species("old-a", 2.0, 1.0, 1.3, 600.0);
    arch::cuda::DeviceSpeciesOwner target(target_species, stream);
    const double *released_species_A = target.view().A;
    target = std::move(moved);
    require(moved.empty(), "move-assigned species source retained storage");
    require(target.view().gamma == original_view.gamma,
            "species move assignment changed allocation identity");
    assert_released_device_pointer(released_species_A,
                                   "species move assignment did not release target");
    compare(run_device(target.ideal_gas_view(1.55), Xi, 2.5, 4.0e6, stream),
            expected, "Ideal move-assigned target");

    double* device_Xi = nullptr;
    double* device_result = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_Xi), 2 * sizeof(double)),
               "cudaMalloc lifetime Xi");
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_result), sizeof(double)),
               "cudaMalloc lifetime result");
    cuda_check(cudaMemcpyAsync(device_Xi, Xi.data(), 2 * sizeof(double),
                               cudaMemcpyHostToDevice, stream), "copy lifetime Xi");
    {
        arch::cuda::DeviceSpeciesOwner async_owner(species, stream);
        delayed_gamma_kernel<<<1, 1, 0, stream>>>(
            async_owner.ideal_gas_view(1.55), device_Xi, device_result);
        cuda_check(cudaGetLastError(), "delayed gamma launch");
        species.species_list[0].gamma_ref = 9.0;
        species.species_list[1].Cv_ref = 1.0;
    }
    double result = 0.0;
    cuda_check(cudaMemcpy(&result, device_result, sizeof(double), cudaMemcpyDeviceToHost),
               "copy lifetime result");
    close(result, host_view.get_gamma(Xi.data()), 2.0e-13,
          "owner destruction did not synchronize pending kernel");
    cuda_check(cudaFree(device_result), "free lifetime result");
    cuda_check(cudaFree(device_Xi), "free lifetime Xi");

    for (int i = 0; i < 3; ++i) {
        SpeciesManager repeated_species;
        repeated_species.add_species("a", 1.0, 1.0, 1.4, 700.0);
        repeated_species.add_species("b", 4.0, 2.0, 1.6, 900.0);
        arch::cuda::DeviceSpeciesOwner repeated(repeated_species, stream);
        compare(run_device(repeated.ideal_gas_view(1.55), Xi, 2.5, 4.0e6, stream),
                expected, "Ideal repeated");
    }
}

struct ElectronSample {
    double rho, temperature, pressure, energy, cv;
    double derivatives[6]{};
};

template <class View>
ARCH_HOST_DEVICE void evaluate_electron_sample(const View& view, ElectronSample& sample)
{
    typename View::ThermodynamicDerivatives d;
    view.interpolate_ele_pos(sample.rho, sample.temperature, 1.0,
                             sample.pressure, sample.energy, &sample.cv, &d);
    sample.derivatives[0] = d.pressure_density;
    sample.derivatives[1] = d.pressure_temperature;
    sample.derivatives[2] = d.energy_z;
    sample.derivatives[3] = d.energy_zz;
    sample.derivatives[4] = d.cv_z;
    sample.derivatives[5] = d.cv_temperature;
}

__global__ void helm_polynomial_kernel(HelmEosView view, ElectronSample* samples,
                                      int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    auto& sample = samples[index];
    evaluate_electron_sample(view, sample);
}

// Independent monomial endpoint DATA exercise all 36 tensor-product degrees.
// No production basis functions are used to construct expected derivatives.
long double monomial(int degree, int derivative, long double x)
{
    if (degree < derivative) return 0.0L;
    long double result = 1.0L;
    for (int k = 0; k < derivative; ++k) result *= degree - k;
    for (int k = 0; k < degree - derivative; ++k) result *= x;
    return result;
}

struct TabularPolynomialData { double fields[tabular_eos::FieldCount][4]; };

ARCH_HOST_DEVICE tabular_eos::FreeEnergyState tabular_polynomial_sample(
    const TabularPolynomialData& data, int point)
{
    constexpr double coordinates[]{0.0, 0.125, 0.5, 0.875, 1.0};
    std::array<const double*, tabular_eos::FieldCount> fields{};
    for (int field = 0; field < tabular_eos::FieldCount; ++field) fields[field] = data.fields[field];
    return tabular_eos::interpolate_biquintic(fields, {0, 1, 2, 3},
        coordinates[point / 5], coordinates[point % 5], 0.125, 0.25, true);
}

__global__ void tabular_polynomial_kernel(TabularPolynomialData data,
                                         tabular_eos::FreeEnergyState* output)
{
    const int index = threadIdx.x;
    if (index < 25) output[index] = tabular_polynomial_sample(data, index);
}

// Reuse the independent monomial oracle, but exercise the tabular tensor
// directly, including third temperature derivatives and constant backgrounds.
void test_tabular_polynomials(cudaStream_t stream)
{
    arch::cuda::DeviceAllocation<tabular_eos::FreeEnergyState> device;
    device.allocate(25);
    double maximum = 0.0;
    constexpr double coordinates[]{0.0, 0.125, 0.5, 0.875, 1.0};
    constexpr int derivatives[7][2]{{0, 0}, {1, 0}, {0, 1}, {2, 0}, {1, 1}, {0, 2}, {0, 3}};
    constexpr int field_derivatives[9][2]{{0, 0}, {1, 0}, {0, 1}, {2, 0}, {1, 1}, {0, 2},
                                          {2, 1}, {1, 2}, {2, 2}};
    for (int polynomial = 0; polynomial <= 36; ++polynomial) {
        const bool background = polynomial == 36;
        const int px = background ? 0 : polynomial / 6, py = background ? 0 : polynomial % 6;
        const long double amplitude = background ? 0x1p60L : 1.0L;
        TabularPolynomialData data{};
        for (int field = 0; field < 9; ++field) {
            const int dx = field_derivatives[field][0], dy = field_derivatives[field][1];
            for (int corner = 0; corner < 4; ++corner)
                data.fields[field][corner] = static_cast<double>(amplitude
                    * monomial(px, dx, corner / 2) * monomial(py, dy, corner % 2)
                    / (std::pow(0.125L, dx) * std::pow(0.25L, dy)));
        }
        tabular_polynomial_kernel<<<1, 32, 0, stream>>>(data, device.get());
        cuda_check(cudaGetLastError(), "tabular polynomial launch");
        std::array<tabular_eos::FreeEnergyState, 25> gpu{};
        cuda_check(cudaMemcpyAsync(gpu.data(), device.get(), sizeof(gpu), cudaMemcpyDeviceToHost, stream),
                   "tabular polynomial download");
        cuda_check(cudaStreamSynchronize(stream), "tabular polynomial completion");
        for (int point = 0; point < 25; ++point) {
            const auto host = tabular_polynomial_sample(data, point);
            for (const auto sample : {host, gpu[point]}) {
                const double values[]{sample.a, sample.ax, sample.ay, sample.axx, sample.axy,
                                      sample.ayy, sample.ayyy};
                for (int field = 0; field < 7; ++field) {
                    const int dx = derivatives[field][0], dy = derivatives[field][1];
                    const long double expected = amplitude
                        * monomial(px, dx, coordinates[point / 5]) * monomial(py, dy, coordinates[point % 5])
                        / (std::pow(0.125L, dx) * std::pow(0.25L, dy));
                    const double error = static_cast<double>(std::abs(values[field] - expected)
                        / std::max(1.0L, std::abs(expected)));
                    maximum = std::max(maximum, error);
                    require(std::isfinite(values[field]) && error <= 2.e-12,
                            "tabular interpolation failed independent polynomial derivative");
                    if (background) require(values[field] == static_cast<double>(expected),
                            "constant tabular background generated a derivative");
                }
            }
        }
    }
    std::cout << "tabular_polynomials degrees=36 constant_background=exact points_per_case=25 max_scaled="
              << std::setprecision(17) << maximum << '\n';
}

void test_helm_polynomials(const HelmEosHostView& source, cudaStream_t stream)
{
    constexpr int density_cell = 400, temperature_cell = 100;
    constexpr int count = 9;
    const std::size_t extent = HelmEosView::imax * HelmEosView::jmax;
    const double dn = std::pow(10.0, source.dlo + density_cell * source.dstp);
    const double tn = std::pow(10.0, source.tlo + temperature_cell * source.tstp);
    const double dd = std::pow(10.0, source.dlo + (density_cell + 1) * source.dstp) - dn;
    const double dt = std::pow(10.0, source.tlo + (temperature_cell + 1) * source.tstp) - tn;
    constexpr int orders[9][2]{{0,0}, {1,0}, {0,1}, {2,0}, {0,2},
                              {1,1}, {2,1}, {1,2}, {2,2}};
    std::array<std::vector<double>, 9> fields;
    auto host = source;
    for (int k = 0; k < 9; ++k) {
        fields[k].resize(extent);
        host.f[k] = fields[k].data();
    }
    arch::cuda::DeviceAllocation<ElectronSample> device_samples;
    device_samples.allocate(count);
    double maximum_scaled_error = 0.0;
    for (int polynomial = 0; polynomial <= 36; ++polynomial) {
        const bool constant_background = polynomial == 36;
        const int px = constant_background ? 0 : polynomial / 6;
        const int py = constant_background ? 0 : polynomial % 6;
        const double amplitude = constant_background ? 0x1p60 : 1.0;
        for (int side_d = 0; side_d < 2; ++side_d)
            for (int side_t = 0; side_t < 2; ++side_t)
                for (int k = 0; k < 9; ++k) {
                    const int dx = orders[k][0], dy = orders[k][1];
                    const int index = (temperature_cell + side_t) * source.imax
                                    + density_cell + side_d;
                    fields[k][index] = static_cast<double>(amplitude
                        * monomial(px, dx, side_d) * monomial(py, dy, side_t)
                        / std::pow(static_cast<long double>(dd), dx)
                        / std::pow(static_cast<long double>(dt), dy));
                }
        arch::cuda::HelmEosDeviceOwner owner(host, stream);
        std::array<ElectronSample, count> cpu{}, gpu{};
        int index = 0;
        for (double x : {0.13, 0.47, 0.91})
            for (double y : {0.19, 0.53, 0.87})
                cpu[index++] = {dn + x * dd, tn + y * dt, 0.0, 0.0, 0.0};
        cuda_check(cudaMemcpyAsync(device_samples.get(), cpu.data(), sizeof(cpu),
            cudaMemcpyHostToDevice, stream), "copy polynomial inputs");
        helm_polynomial_kernel<<<1, count, 0, stream>>>(owner.view(), device_samples.get(), count);
        cuda_check(cudaGetLastError(), "polynomial launch");
        cuda_check(cudaMemcpyAsync(gpu.data(), device_samples.get(), sizeof(gpu),
            cudaMemcpyDeviceToHost, stream), "copy polynomial results");
        cuda_check(cudaStreamSynchronize(stream), "polynomial completion");
        for (int point = 0; point < count; ++point) {
            auto& h = cpu[point];
            evaluate_electron_sample(host, h);
            const long double x = (static_cast<long double>(h.rho) - dn) / dd;
            const long double y = (static_cast<long double>(h.temperature) - tn) / dt;
            const long double t = static_cast<long double>(h.temperature) / dt;
            const long double r = static_cast<long double>(h.rho) / dd;
            const long double expected[3]{monomial(px, 1, x) * monomial(py, 0, y),
                monomial(px, 0, x) * (monomial(py, 0, y) - t * monomial(py, 1, y)),
                -t * monomial(px, 0, x) * monomial(py, 2, y)};
            for (const auto& sample : {h, gpu[point]}) {
                if (constant_background) {
                    require(sample.pressure == 0.0 && sample.cv == 0.0
                            && sample.energy == amplitude && sample.derivatives[2] == amplitude
                            && sample.derivatives[0] == 0.0 && sample.derivatives[1] == 0.0
                            && sample.derivatives[3] == 0.0 && sample.derivatives[4] == 0.0
                            && sample.derivatives[5] == 0.0,
                            "constant Helm background generated thermodynamic derivatives");
                    continue;
                }
                const double actual[3]{sample.pressure * dd / (sample.rho * sample.rho),
                                       sample.energy, sample.cv * dt};
                for (int component = 0; component < 3; ++component) {
                    const double error = static_cast<double>(std::abs(actual[component] - expected[component])
                        / std::max(1.0L, std::abs(expected[component])));
                    maximum_scaled_error = std::max(maximum_scaled_error, error);
                    require(std::isfinite(error) && error <= 5.0e-11,
                            "Helm interpolation failed independent monomial derivative");
                }
                const long double fx = monomial(px, 1, x), fxx = monomial(px, 2, x);
                const long double f = monomial(px, 0, x), y0 = monomial(py, 0, y);
                const long double yt = monomial(py, 1, y), ytt = monomial(py, 2, y);
                const long double derivative_expected[]{
                    (2 * fx + r * fxx) * y0, fx * yt,
                    (f + r * fx) * (y0 - t * yt),
                    (2 * fx + r * fxx) * (y0 - t * yt),
                    -t * (f + r * fx) * ytt,
                    -f * (ytt + t * monomial(py, 3, y))};
                const double derivative_actual[]{sample.derivatives[0] * dd / sample.rho,
                    sample.derivatives[1] * dd * dt / (sample.rho * sample.rho),
                    sample.derivatives[2], sample.derivatives[3] * dd / sample.rho,
                    sample.derivatives[4] * dt, sample.derivatives[5] * dt * dt};
                for (int component = 0; component < 6; ++component) {
                    const double error = static_cast<double>(std::abs(derivative_actual[component] - derivative_expected[component])
                        / std::max(1.0L, std::abs(derivative_expected[component])));
                    maximum_scaled_error = std::max(maximum_scaled_error, error);
                    if (!(std::isfinite(error) && error <= 5e-11))
                        std::cerr << "Helm monomial px=" << px << " py=" << py
                                  << " point=" << point << " component=" << component
                                  << " actual=" << derivative_actual[component]
                                  << " expected=" << derivative_expected[component]
                                  << " scaled_error=" << error << '\n';
                    require(std::isfinite(error) && error <= 5e-11,
                            "Helm analytic pressure/composition derivative failed independent polynomial");
                }
            }
        }
    }
    std::cout << "helm_polynomials degrees=36 constant_background=exact points_per_case="
              << count << " maximum_scaled_error=" << maximum_scaled_error << '\n';
}

struct HelmDifferentialsSample { double values[10], energy_gradient[2], cv_gradient[3], hessian_action[3]; };

template <class View>
ARCH_HOST_DEVICE HelmDifferentialsSample helm_differentials(const View& view, double rho, double temperature)
{
    const double x[]{0.25, 0.75}, flow[]{-0.4, 0.4};
    double pressure, energy;
    typename View::ThermodynamicDerivatives d;
    view.calc_thermo_with_cv(rho, temperature, x, pressure, energy, nullptr, &d);
    HelmDifferentialsSample result{{d.pressure_density, d.pressure_temperature, d.energy_y, d.energy_z,
        d.energy_yy, d.energy_yz, d.energy_zz, d.cv_y, d.cv_z, d.cv_temperature}, {}, {}, {}};
    view.template get_energy_composition_gradient<3>(rho, temperature, x, result.energy_gradient);
    view.template get_cv_gradient<3>(rho, temperature, x, result.cv_gradient);
    view.template get_energy_composition_hessian_action<3>(rho, temperature, x, flow, result.hessian_action);
    return result;
}

__global__ void helm_differentials_kernel(HelmEosView view, double rho, double temperature,
                                        HelmDifferentialsSample* result)
{
    *result = helm_differentials(view, rho, temperature);
}

void test_helm(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 700.0);
    species.add_species("b", 4.0, 2.0, 1.6, 900.0);
    const std::vector<double> Xi{0.25, 0.75};
    HelmEos host(std::string(ARCH_SOURCE_DIR) +
                     "/EOS_toolkit/tables/helmholtz/helm_table.dat", &species);
    const auto host_view = host.get_view();
    test_helm_polynomials(host_view, stream);
    eos_state_t frozen{};
    frozen.rho = 1.0e6; frozen.T = 1.0e8; frozen.Xi = Xi.data();
    host.evaluate_state(frozen);
    // The independent endpoint-fit reference uses per-field accuracy budgets;
    // cv/xne use explicit rounding windows instead of same-implementation
    // bit comparisons.
    const double state_values[]{frozen.P, frozen.E, frozen.cv, frozen.sound_speed,
        frozen.dp_drho, frozen.dp_dT, frozen.pele, frozen.xne, frozen.eta};
    const double state_budgets[]{8.0e-14, 1.5e-15,
        8 * std::numeric_limits<double>::epsilon(), 4.0e-10, 8.0e-10, 2.5e-11,
        8.0e-14, 2 * std::numeric_limits<double>::epsilon(), 1.5e-15};
    for (int i = 0; i < 9; ++i)
        close(state_values[i], HelmReference::state[i], state_budgets[i],
              "helm.independent_state." + std::to_string(i));
    frozen_bits(host.get_temperature(frozen.rho, frozen.E, Xi.data()),
                0x4197d78400000000ULL, "helm.recovered_T");
    const double lower_rho = 1.0e-12 / species.calc_Ye(Xi.data());
    const double upper_rho = 1.0e15 / species.calc_Ye(Xi.data());
    close(host.get_pressure_from_rho_T(lower_rho, 1.0e3, Xi.data()),
          HelmReference::lower_bound_P, 2 * std::numeric_limits<double>::epsilon(),
          "helm.independent_lower_bound_P");
    close(host.get_pressure_from_rho_T(upper_rho, 1.0e13, Xi.data()),
          HelmReference::upper_bound_P, 2.0e-14, "helm.independent_upper_bound_P");
    require_same_raw_probe(probe_host_wrapper(host, Xi.data(), 1.0e6, 1.0e8),
                           probe_leaf(host_view, Xi.data(), 1.0e6, 1.0e8), "Helm");
    const Probe expected = probe_leaf(host_view, Xi.data(), 1.0e6, 1.0e8);
    auto independent_margins = helm_authority_margins;
    independent_margins[1] = independent_margins[12] =
        8 * std::numeric_limits<double>::epsilon();
    independent_margins[17] = 2 * std::numeric_limits<double>::epsilon();
    // Exact inverse energy includes reference-E rounding and iteration error;
    // reuse the existing inverse-energy contract rather than a measured margin.
    independent_margins[9] = 8.0e-15;
    reference_probe(expected, HelmReference::probe, independent_margins,
                    "helm.independent_probe");
    const auto isentrope = eos_utils::get_isentropic_state_at_pressure_factor(
        host, 1.0e6, 1.0e8, Xi.data(), 1.001);
    close(isentrope.rho, HelmReference::isentrope[0], 8.0e-14,
          "helm.independent_isentrope.rho");
    close(isentrope.temperature, HelmReference::isentrope[1], 3.0e-14,
          "helm.independent_isentrope.temperature");
    close(isentrope.pressure, HelmReference::isentrope[2], 8.0e-14,
          "helm.independent_isentrope.pressure");
    // Analytic derivatives determine the isentropic sound speed; apply the
    // independent state budget alongside the separate Host/Device budgets.
    close(isentrope.sound_speed, HelmReference::isentrope[3], state_budgets[3],
          "helm.independent_isentrope.sound_speed");
    require(Xi == std::vector<double>({0.25, 0.75}),
            "Helm isentrope changed composition");
    const std::size_t expected_extent =
        static_cast<std::size_t>(HelmEosHostView::imax) * HelmEosHostView::jmax;
    for (int index = 0; index < 9; ++index) {
        auto malformed = host_view;
        malformed.f_extents[index] = expected_extent - 1;
        require_invalid_argument(
            [&] { arch::cuda::HelmEosDeviceOwner rejected(malformed, stream); },
            "Helm short f descriptor was accepted");
        malformed.f_extents[index] = expected_extent + 1;
        require_invalid_argument(
            [&] { arch::cuda::HelmEosDeviceOwner rejected(malformed, stream); },
            "Helm long f descriptor was accepted");
    }
    for (int index = 0; index < 4; ++index) {
        auto malformed = host_view;
        malformed.ef_extents[index] = expected_extent - 1;
        require_invalid_argument(
            [&] { arch::cuda::HelmEosDeviceOwner rejected(malformed, stream); },
            "Helm short ef descriptor was accepted");
        malformed.ef_extents[index] = expected_extent + 1;
        require_invalid_argument(
            [&] { arch::cuda::HelmEosDeviceOwner rejected(malformed, stream); },
            "Helm long ef descriptor was accepted");
    }
    for (int axis = 0; axis < 2; ++axis) {
        for (int difference : {-1, 1}) {
            auto malformed = host_view;
            malformed.node_extents[axis] += difference;
            require_invalid_argument(
                [&] { arch::cuda::HelmEosDeviceOwner rejected(malformed, stream); },
                "Helm wrong grid descriptor length was accepted");
        }
        auto malformed = host_view;
        if (axis == 0) malformed.density_nodes = nullptr;
        else malformed.temperature_nodes = nullptr;
        require_invalid_argument(
            [&] { arch::cuda::HelmEosDeviceOwner rejected(malformed, stream); },
            "Helm missing grid descriptor was accepted");
    }
    auto missing_species = host_view;
    missing_species.specs = SpeciesHostView{};
    require_invalid_argument(
        [&] { arch::cuda::HelmEosDeviceOwner rejected(missing_species, stream); },
        "Helm descriptor without species metadata owner was accepted");
    arch::cuda::HelmEosDeviceOwner owner(host, stream);
    assert_device_pointer(owner.view().density_nodes, "Helm density node pointer");
    assert_device_pointer(owner.view().temperature_nodes, "Helm temperature node pointer");
    for (int axis = 0; axis < 2; ++axis) {
        const double* cpu_nodes = axis == 0 ? host_view.density_nodes : host_view.temperature_nodes;
        const double* gpu_nodes = axis == 0 ? owner.view().density_nodes : owner.view().temperature_nodes;
        std::vector<double> copy(host_view.node_extents[axis]);
        cuda_check(cudaMemcpyAsync(copy.data(), gpu_nodes, copy.size() * sizeof(double),
            cudaMemcpyDeviceToHost, stream), "copy Helm grid values");
        cuda_check(cudaStreamSynchronize(stream), "Helm grid completion");
        require(std::equal(copy.begin(), copy.end(), cpu_nodes), "Helm grid values changed on upload");
    }
    // Independent references cover strong-Coulomb and radiation-dominated
    // regimes in addition to the single-state backend comparison above.
    for (const auto& point : HelmReference::points) {
        eos_state_t cpu{};
        cpu.rho = point.rho; cpu.T = point.temperature; cpu.Xi = Xi.data();
        host.evaluate_state(cpu);
        const auto gpu = run_state(owner.view(), Xi, point.rho, point.temperature, stream);
        const double budgets[]{8e-14, 1.5e-15,
            32 * std::numeric_limits<double>::epsilon(), 8e-14,
            2 * std::numeric_limits<double>::epsilon(), 1.5e-15};
        for (const auto& state : {cpu, gpu}) {
            const double values[]{state.P, state.E, state.cv, state.pele, state.xne, state.eta};
            for (int i = 0; i < 6; ++i)
                close(values[i], point.values[i], budgets[i],
                      "helm.independent_domain." + std::to_string(i));
        }
        arch::cuda::DeviceAllocation<HelmDifferentialsSample> device_differentials;
        device_differentials.allocate(1);
        helm_differentials_kernel<<<1, 1, 0, stream>>>(owner.view(), point.rho, point.temperature,
                                                      device_differentials.get());
        cuda_check(cudaGetLastError(), "Helm differential launch");
        HelmDifferentialsSample device{};
        cuda_check(cudaMemcpyAsync(&device, device_differentials.get(), sizeof(device),
            cudaMemcpyDeviceToHost, stream), "copy Helm differentials");
        cuda_check(cudaStreamSynchronize(stream), "complete Helm differentials");
        for (const auto& d : {helm_differentials(host_view, point.rho, point.temperature), device}) {
            for (int i = 0; i < 10; ++i)
                close(d.values[i], point.derivatives[i], 2e-9,
                      "helm.independent_differential." + std::to_string(i));
            const auto& expected = point.derivatives;
            // A={1,4}, Z={1,2}; the fixed tangent (-0.4,+0.4)
            // has y'=-0.3, z'=-0.2, derived independently of the EOS adapters.
            for (int i = 0; i < 2; ++i) {
                const double a = i == 0 ? 1.0 : 0.25, z = i == 0 ? 1.0 : 0.5;
                close(d.energy_gradient[i], a * expected[2] + z * expected[3], 2e-9,
                      "helm.independent_energy_gradient");
                close(d.cv_gradient[i], a * expected[7] + z * expected[8], 2e-9,
                      "helm.independent_cv_gradient");
                close(d.hessian_action[i], a * (-0.3 * expected[4] - 0.2 * expected[5])
                    + z * (-0.3 * expected[5] - 0.2 * expected[6]), 2e-9,
                      "helm.independent_composition_hessian");
            }
            close(d.cv_gradient[2], expected[9], 2e-9, "helm.independent_cv_temperature");
            close(d.hessian_action[2], -0.3 * expected[7] - 0.2 * expected[8], 2e-9,
                  "helm.independent_composition_hessian_temperature");
        }
    }
    for (int i = 0; i < 9; ++i) assert_device_pointer(owner.view().f[i], "Helm f pointer");
    for (int i = 0; i < 4; ++i) assert_device_pointer(owner.view().ef_table[i], "Helm ef pointer");
    compare(run_device(owner.view(), Xi, 1.0e6, 1.0e8, stream),
            expected, "Helm");
    close(run_pressure(owner.view(), Xi, lower_rho, 1.0e3, stream),
          host_view.get_pressure_from_rho_T(lower_rho, 1.0e3, Xi.data()), 5.0e-10,
          "Helm exact lower bound");
    close(run_pressure(owner.view(), Xi, upper_rho, 1.0e13, stream),
          host_view.get_pressure_from_rho_T(upper_rho, 1.0e13, Xi.data()), 5.0e-10,
          "Helm exact upper bound");

    HelmFinalValues* device_values = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_values), sizeof(HelmFinalValues)),
               "cudaMalloc Helm finals");
    helm_final_kernel<<<1, 1, 0, stream>>>(owner.view(), device_values);
    cuda_check(cudaGetLastError(), "Helm final launch");
    HelmFinalValues values{};
    cuda_check(cudaMemcpyAsync(&values, device_values, sizeof(values),
                               cudaMemcpyDeviceToHost, stream), "copy Helm finals");
    cuda_check(cudaStreamSynchronize(stream), "Helm final synchronize");
    const int last = HelmEosHostView::imax * HelmEosHostView::jmax - 1;
    for (int i = 0; i < 9; ++i) {
        require(host_view.f[i][last] != 0.0, "Helm f final witness is zero");
        close(values.f[i], host_view.f[i][last], 0.0, "Helm f final element");
    }
    for (int i = 0; i < 4; ++i) {
        require(host_view.ef_table[i][last] != 0.0, "Helm ef final witness is zero");
        close(values.ef[i], host_view.ef_table[i][last], 0.0, "Helm ef final element");
    }
    cuda_check(cudaFree(device_values), "free Helm finals");

    const double *original_f = owner.view().f[0];
    const double *original_density_nodes = owner.view().density_nodes;
    auto moved = std::move(owner);
    require(owner.empty(), "moved-from Helm owner retained storage");
    compare(run_device(moved.view(), Xi, 1.0e6, 1.0e8, stream),
            expected, "Helm moved");
    arch::cuda::HelmEosDeviceOwner target(host, stream);
    const double *released_f = target.view().f[0];
    const double *released_density_nodes = target.view().density_nodes;
    const double *released_temperature_nodes = target.view().temperature_nodes;
    const double *released_helm_species = target.view().specs.A;
    target = std::move(moved);
    require(moved.empty(), "move-assigned Helm source retained storage");
    require(target.view().f[0] == original_f,
            "Helm move assignment changed allocation identity");
    require(target.view().density_nodes == original_density_nodes,
            "Helm move assignment changed grid allocation identity");
    assert_released_device_pointer(released_density_nodes, "Helm move assignment leaked density grid");
    assert_released_device_pointer(released_temperature_nodes, "Helm move assignment leaked temperature grid");
    assert_released_device_pointer(released_f,
                                   "Helm move assignment did not release target");
    assert_released_device_pointer(released_helm_species,
                                   "Helm move assignment did not release nested species");
    for (int index = 0; index < 9; ++index)
        assert_device_pointer(target.view().f[index], "Helm moved f pointer");
    for (int index = 0; index < 4; ++index)
        assert_device_pointer(target.view().ef_table[index], "Helm moved ef pointer");
    assert_device_pointer(target.view().specs.A, "Helm moved species pointer");
    compare(run_device(target.view(), Xi, 1.0e6, 1.0e8, stream),
            expected, "Helm move-assigned target");
    test_delayed_owner_destruction(
        [&] { return arch::cuda::HelmEosDeviceOwner(host, stream); }, Xi,
        1.0e6, 1.0e8, frozen.dp_dT, stream,
        "Helm immediate destruction did not synchronize consumer stream");
    for (int iteration = 0; iteration < 2; ++iteration) {
        arch::cuda::HelmEosDeviceOwner repeated(host, stream);
        assert_device_pointer(repeated.view().f[8], "Helm repeated final table pointer");
    }
}

void check_six_finals(const SixValues& device,
                      const std::array<std::vector<double>, 6>& host)
{
    for (int i = 0; i < 6; ++i)
        close(device.value[i], host[i].back(), 0.0, "tabular final element");
}

void test_tabular3(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 700.0);
    species.add_species("b", 4.0, 2.0, 1.6, 900.0);
    const std::vector<double> Xi{0.25, 0.75};
    const std::array<std::vector<double>, 6> tables{
        table3(1.0e14,1.0e12,2.0e12,3.0e12), energy3(),
        table3(2.0e7,1.0e5,2.0e5,3.0e5), table3(1.0e6,0.0,0.0,0.0),
        table3(7.0e6,1.0e4,2.0e4,3.0e4), table3(8.0e6,1.0e4,2.0e4,3.0e4)};
    const auto host = make_tab3(species, tables, true, true);
    eos_state_t frozen{};
    frozen.rho = 10.0; frozen.T = 1.0e8; frozen.Xi = Xi.data();
    host.evaluate_state(frozen);
    frozen_state(frozen, {0x42d87152d40e0000ULL, 0x42d6bcc41e911f80ULL,
                          0x412e848000000000ULL, 0x4173c9eb00000000ULL,
                          0x415afd2e00000000ULL, 0x415ecdbe00000000ULL,
                          0x0000000000000000ULL, 0x0000000000000000ULL,
                          0x0000000000000000ULL}, "tab3.present");
    const Probe table_expected = probe_leaf(host, Xi.data(), 10.0, 1.0e8);
    frozen_probe(table_expected, tab3_table_probe, "tab3.table.probe");
    const Probe iteration_expected = probe_leaf(host, Xi.data(), 10.0, 3.0e8);
    frozen_probe(iteration_expected, tab3_iteration_probe, "tab3.iteration.probe");
    frozen_bits(host.get_pressure_from_rho_T(1.0, 1.0e7, Xi.data()),
                0x42d7c2b358420000ULL, "tab3.lower_exact");
    frozen_value(host.get_pressure_from_rho_T(
                     std::pow(10.0, 2.0 - 1.0e-6), 1.0e8, Xi.data()),
                 0x42d8ab87f9817000ULL, 3.0e-16, "tab3.upper_margin");
    close(host.get_pressure_from_rho_T(1.0e3, 1.0e8, Xi.data()),
          ion_pressure_reference(1.0e3, 1.0e8, 7.0 / 16.0),
          2 * std::numeric_limits<double>::epsilon(), "tab3.analytic_fallback");
    frozen_bits(host.get_pressure_from_rho_T(0.0, 1.0e8, Xi.data()),
                0x0000000000000000ULL, "tab3.invalid");
    frozen_bits(host.get_temperature(frozen.rho, frozen.E, Xi.data()),
                0x4197d78400000000ULL, "tab3.recovered_T");
    require_exception_message(
        [&] { (void)eos_utils::get_isentropic_state_at_pressure_factor(
            host, 10.0, 1.0e8, Xi.data(), 1.001); },
        "Isentropic pressure initialization did not converge.",
        "tab3.isentrope.exception");
    require(Xi == std::vector<double>({0.25, 0.75}),
            "Tab3 isentrope changed composition");
    test_tabular_upload_descriptor_lengths<
        Tabular3DEOSHostView, arch::cuda::Tabular3DEOSDeviceOwner>(
            host, stream, "Tab3 malformed upload descriptor was accepted");
    arch::cuda::Tabular3DEOSDeviceOwner owner(host, stream);
    const auto view = owner.view();
    assert_device_pointer(view.table_P, "Tab3 P pointer");
    assert_device_pointer(view.table_E, "Tab3 E pointer");
    assert_device_pointer(view.table_cs, "Tab3 cs pointer");
    assert_device_pointer(view.table_cv, "Tab3 cv pointer");
    assert_device_pointer(view.table_dP_drho, "Tab3 dp_drho pointer");
    assert_device_pointer(view.table_dP_dT, "Tab3 dp_dT pointer");
    compare(run_device(view, Xi, 10.0, 1.0e8, stream),
            table_expected, "Tab3 table");
    compare(run_device(view, Xi, 10.0, 3.0e8, stream), iteration_expected,
            "Tab3 iteration identity");
    close(run_state_dp_dT(view, Xi, 10.0, 1.0e8, stream),
          std::bit_cast<double>(0x415ecdbe00000000ULL), 0.0,
          "Tab3 table dp_dT BASE oracle");
    close(run_pressure(view, Xi, 0.0, 1.0e8, stream), host.get_pressure_from_rho_T(0.0,1.0e8,Xi.data()),
          0.0, "Tab3 invalid branch");
    close(run_pressure(view, Xi, 1.0e3, 1.0e8, stream), host.get_pressure_from_rho_T(1.0e3,1.0e8,Xi.data()),
          2.0e-13, "Tab3 fallback branch");
    close(run_pressure(view, Xi, 1.0, 1.0e7, stream), host.get_pressure_from_rho_T(1.0,1.0e7,Xi.data()),
          2.0e-13, "Tab3 lower bound");
    close(run_pressure(view, Xi, pow(10.0,2.0-1.0e-6), 1.0e8, stream),
          host.get_pressure_from_rho_T(pow(10.0,2.0-1.0e-6),1.0e8,Xi.data()),
          2.0e-13, "Tab3 upper margin");

    SixValues* device_values = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_values), sizeof(SixValues)),
               "cudaMalloc Tab3 finals");
    tab3_final_kernel<<<1,1,0,stream>>>(view, device_values);
    SixValues values{};
    cuda_check(cudaMemcpyAsync(&values, device_values, sizeof(values),
                               cudaMemcpyDeviceToHost, stream), "copy Tab3 finals");
    cuda_check(cudaStreamSynchronize(stream), "Tab3 final synchronize");
    check_six_finals(values, tables);
    cuda_check(cudaFree(device_values), "free Tab3 finals");

    const auto host_null = make_tab3(species, tables, false, false);
    arch::cuda::Tabular3DEOSDeviceOwner null_owner(host_null, stream);
    require(null_owner.view().table_dP_drho == nullptr, "Tab3 optional dp_drho not null");
    require(null_owner.view().table_dP_dT == nullptr, "Tab3 optional dp_dT not null");
    const Probe null_expected = probe_leaf(host_null, Xi.data(), 10.0, 1.0e8);
    frozen_probe(null_expected, tab3_fd_probe, "tab3.finite_difference.probe");
    compare(run_device(null_owner.view(), Xi, 10.0, 1.0e8, stream),
            null_expected, "Tab3 finite difference");
    close(run_state_dp_dT(null_owner.view(), Xi, 10.0, 1.0e8, stream),
          std::bit_cast<double>(0x40c0f6f1e096bb99ULL), 0.0,
          "Tab3 finite-difference dp_dT BASE oracle");
    eos_state_t fallback{};
    fallback.rho = 1.0e3; fallback.T = 1.0e8; fallback.Xi = Xi.data();
    host.evaluate_state(fallback);
    close(fallback.dp_dT, ion_pressure_reference(1.0e3, 1.0, 7.0 / 16.0),
          2 * std::numeric_limits<double>::epsilon(), "tab3.analytic_fallback.dp_dT");
    close(run_state_dp_dT(view, Xi, 1.0e3, 1.0e8, stream), fallback.dp_dT,
          2.0e-16, "Tab3 analytic-fallback dp_dT BASE oracle");
    const double below = std::nextafter(1.0e-12, 0.0);
    const double exact = 1.0e-12;
    const double above = std::nextafter(
        1.0e-12, std::numeric_limits<double>::infinity());
    const double threshold_rho[3]{below, exact, above};
    for (int index = 0; index < 3; ++index) {
        const double authority = host.get_pressure_from_rho_T(
            threshold_rho[index], 1.0e8, Xi.data());
        const double expected_pressure = index < 2 ? 0.0 : ion_pressure_reference(
            threshold_rho[index], 1.0e8, 7.0 / 16.0);
        close(authority, expected_pressure, 2 * std::numeric_limits<double>::epsilon(),
              "tab3.analytic_threshold");
        close(run_pressure(view, Xi, threshold_rho[index], 1.0e8, stream),
              authority, 2.0e-16, "Tab3 1e-12 threshold");
    }

    const double *original_table_P = owner.view().table_P;
    auto moved = std::move(owner);
    require(owner.empty(), "moved-from Tab3 owner retained storage");
    compare(run_device(moved.view(), Xi, 10.0, 1.0e8, stream),
            table_expected, "Tab3 moved");
    arch::cuda::Tabular3DEOSDeviceOwner target(host, stream);
    const double *released_table_P = target.view().table_P;
    const double *released_species_A = target.view().specs.A;
    target = std::move(moved);
    require(moved.empty(), "move-assigned Tab3 source retained storage");
    require(target.view().table_P == original_table_P,
            "Tab3 move assignment changed allocation identity");
    assert_released_device_pointer(released_table_P,
                                   "Tab3 move assignment did not release target");
    assert_released_device_pointer(released_species_A,
                                   "Tab3 move assignment did not release nested species");
    assert_device_pointer(target.view().table_dP_dT, "Tab3 moved derivative pointer");
    assert_device_pointer(target.view().specs.Cv, "Tab3 moved species pointer");
    compare(run_device(target.view(), Xi, 10.0, 1.0e8, stream),
            table_expected, "Tab3 move-assigned target");
    test_delayed_owner_destruction(
        [&] { return arch::cuda::Tabular3DEOSDeviceOwner(host, stream); }, Xi,
        10.0, 1.0e8, frozen.dp_dT, stream,
        "Tab3 immediate destruction did not synchronize consumer stream");
    for (int iteration = 0; iteration < 2; ++iteration) {
        arch::cuda::Tabular3DEOSDeviceOwner repeated(host, stream);
        assert_device_pointer(repeated.view().table_cv, "Tab3 repeated table pointer");
    }
}

void test_tabular4(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species("q", 1.5, 0.75, 1.4, 1.0e6);
    const std::vector<double> Xi{1.0};
    const std::array<std::vector<double>, 6> tables{
        table4(2.0e14,1.0e12,2.0e12,3.0e12,4.0e12), energy4(),
        table4(3.0e7,1.0e5,2.0e5,3.0e5,4.0e5), table4(1.0e6,0.0,0.0,0.0,0.0),
        table4(9.0e6,1.0e4,2.0e4,3.0e4,4.0e4),
        table4(1.0e7,1.0e4,2.0e4,3.0e4,4.0e4)};
    const auto host = make_tab4(species, tables, true, true);
    eos_state_t frozen{};
    frozen.rho = 10.0; frozen.T = 1.0e8; frozen.Xi = Xi.data();
    host.evaluate_state(frozen);
    frozen_state(frozen, {0x42e7dfcdece40000ULL, 0x42d6bcc41e911580ULL,
                          0x412e848000000000ULL, 0x417d905c00000000ULL,
                          0x41615b5c00000000ULL, 0x416343a400000000ULL,
                          0x0000000000000000ULL, 0x0000000000000000ULL,
                          0x0000000000000000ULL}, "tab4.present");
    const Probe table_expected = probe_leaf(host, Xi.data(), 10.0, 1.0e8);
    frozen_probe(table_expected, tab4_table_probe, "tab4.table.probe");
    const Probe iteration_expected = probe_leaf(host, Xi.data(), 10.0, 3.0e8);
    frozen_probe(iteration_expected, tab4_iteration_probe, "tab4.iteration.probe");
    frozen_bits(host.get_pressure_from_rho_T(1.0, 1.0e7, Xi.data()),
                0x42e7887e2efe0000ULL, "tab4.lower_exact");
    frozen_value(host.get_pressure_from_rho_T(
                     std::pow(10.0, 2.0 - 1.0e-6), 1.0e8, Xi.data()),
                 0x42e7fce87f9db800ULL, 3.0e-16, "tab4.upper_margin");
    close(host.get_pressure_from_rho_T(1.0e3, 1.0e8, Xi.data()),
          ion_pressure_reference(1.0e3, 1.0e8, 2.0 / 3.0),
          2 * std::numeric_limits<double>::epsilon(), "tab4.analytic_fallback");
    frozen_bits(host.get_pressure_from_rho_T(0.0, 1.0e8, Xi.data()),
                0x0000000000000000ULL, "tab4.invalid");
    frozen_bits(host.get_temperature(frozen.rho, frozen.E, Xi.data()),
                0x4197d78400000000ULL, "tab4.recovered_T");
    require_exception_message(
        [&] { (void)eos_utils::get_isentropic_state_at_pressure_factor(
            host, 10.0, 1.0e8, Xi.data(), 1.001); },
        "Isentropic pressure initialization did not converge.",
        "tab4.isentrope.exception");
    require(Xi == std::vector<double>({1.0}), "Tab4 isentrope changed composition");
    test_tabular_upload_descriptor_lengths<
        Tabular4DEOSHostView, arch::cuda::Tabular4DEOSDeviceOwner>(
            host, stream, "Tab4 malformed upload descriptor was accepted");
    arch::cuda::Tabular4DEOSDeviceOwner owner(host, stream);
    const auto view = owner.view();
    assert_device_pointer(view.table_P, "Tab4 P pointer");
    assert_device_pointer(view.table_E, "Tab4 E pointer");
    assert_device_pointer(view.table_cs, "Tab4 cs pointer");
    assert_device_pointer(view.table_cv, "Tab4 cv pointer");
    assert_device_pointer(view.table_dP_drho, "Tab4 dp_drho pointer");
    assert_device_pointer(view.table_dP_dT, "Tab4 dp_dT pointer");
    compare(run_device(view, Xi, 10.0, 1.0e8, stream),
            table_expected, "Tab4 table");
    compare(run_device(view, Xi, 10.0, 3.0e8, stream), iteration_expected,
            "Tab4 iteration identity");
    close(run_state_dp_dT(view, Xi, 10.0, 1.0e8, stream),
          std::bit_cast<double>(0x416343a400000000ULL), 0.0,
          "Tab4 table dp_dT BASE oracle");
    close(run_pressure(view, Xi, 0.0, 1.0e8, stream), host.get_pressure_from_rho_T(0.0,1.0e8,Xi.data()),
          0.0, "Tab4 invalid branch");
    close(run_pressure(view, Xi, 1.0e3, 1.0e8, stream), host.get_pressure_from_rho_T(1.0e3,1.0e8,Xi.data()),
          2.0e-13, "Tab4 fallback branch");
    close(run_pressure(view, Xi, 1.0, 1.0e7, stream), host.get_pressure_from_rho_T(1.0,1.0e7,Xi.data()),
          2.0e-13, "Tab4 lower bound");
    close(run_pressure(view, Xi, pow(10.0,2.0-1.0e-6), 1.0e8, stream),
          host.get_pressure_from_rho_T(pow(10.0,2.0-1.0e-6),1.0e8,Xi.data()),
          2.0e-13, "Tab4 upper margin");

    SixValues* device_values = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_values), sizeof(SixValues)),
               "cudaMalloc Tab4 finals");
    tab4_final_kernel<<<1,1,0,stream>>>(view, device_values);
    SixValues values{};
    cuda_check(cudaMemcpyAsync(&values, device_values, sizeof(values),
                               cudaMemcpyDeviceToHost, stream), "copy Tab4 finals");
    cuda_check(cudaStreamSynchronize(stream), "Tab4 final synchronize");
    check_six_finals(values, tables);
    cuda_check(cudaFree(device_values), "free Tab4 finals");

    const auto host_null = make_tab4(species, tables, false, false);
    arch::cuda::Tabular4DEOSDeviceOwner null_owner(host_null, stream);
    require(null_owner.view().table_dP_drho == nullptr, "Tab4 optional dp_drho not null");
    require(null_owner.view().table_dP_dT == nullptr, "Tab4 optional dp_dT not null");
    const Probe null_expected = probe_leaf(host_null, Xi.data(), 10.0, 1.0e8);
    frozen_probe(null_expected, tab4_fd_probe, "tab4.finite_difference.probe");
    compare(run_device(null_owner.view(), Xi, 10.0, 1.0e8, stream),
            null_expected, "Tab4 finite difference");
    close(run_state_dp_dT(null_owner.view(), Xi, 10.0, 1.0e8, stream),
          std::bit_cast<double>(0x40c0f6f1e09d4952ULL), 2.3e-10,
          "Tab4 finite-difference dp_dT BASE oracle");
    eos_state_t fallback{};
    fallback.rho = 1.0e3; fallback.T = 1.0e8; fallback.Xi = Xi.data();
    host.evaluate_state(fallback);
    close(fallback.dp_dT, ion_pressure_reference(1.0e3, 1.0, 2.0 / 3.0),
          2 * std::numeric_limits<double>::epsilon(), "tab4.analytic_fallback.dp_dT");
    close(run_state_dp_dT(view, Xi, 1.0e3, 1.0e8, stream), fallback.dp_dT,
          2.0e-16, "Tab4 analytic-fallback dp_dT BASE oracle");
    const double below = std::nextafter(1.0e-12, 0.0);
    const double exact = 1.0e-12;
    const double above = std::nextafter(
        1.0e-12, std::numeric_limits<double>::infinity());
    const double threshold_rho[3]{below, exact, above};
    for (int index = 0; index < 3; ++index) {
        const double authority = host.get_pressure_from_rho_T(
            threshold_rho[index], 1.0e8, Xi.data());
        const double expected_pressure = index < 2 ? 0.0 : ion_pressure_reference(
            threshold_rho[index], 1.0e8, 2.0 / 3.0);
        close(authority, expected_pressure, 2 * std::numeric_limits<double>::epsilon(),
              "tab4.analytic_threshold");
        close(run_pressure(view, Xi, threshold_rho[index], 1.0e8, stream),
              authority, 2.0e-16, "Tab4 1e-12 threshold");
    }

    const double *original_table_P = owner.view().table_P;
    auto moved = std::move(owner);
    require(owner.empty(), "moved-from Tab4 owner retained storage");
    compare(run_device(moved.view(), Xi, 10.0, 1.0e8, stream),
            table_expected, "Tab4 moved");
    arch::cuda::Tabular4DEOSDeviceOwner target(host, stream);
    const double *released_table_P = target.view().table_P;
    const double *released_species_A = target.view().specs.A;
    target = std::move(moved);
    require(moved.empty(), "move-assigned Tab4 source retained storage");
    require(target.view().table_P == original_table_P,
            "Tab4 move assignment changed allocation identity");
    assert_released_device_pointer(released_table_P,
                                   "Tab4 move assignment did not release target");
    assert_released_device_pointer(released_species_A,
                                   "Tab4 move assignment did not release nested species");
    assert_device_pointer(target.view().table_dP_dT, "Tab4 moved derivative pointer");
    assert_device_pointer(target.view().specs.Cv, "Tab4 moved species pointer");
    compare(run_device(target.view(), Xi, 10.0, 1.0e8, stream),
            table_expected, "Tab4 move-assigned target");
    test_delayed_owner_destruction(
        [&] { return arch::cuda::Tabular4DEOSDeviceOwner(host, stream); }, Xi,
        10.0, 1.0e8, frozen.dp_dT, stream,
        "Tab4 immediate destruction did not synchronize consumer stream");
    for (int iteration = 0; iteration < 2; ++iteration) {
        arch::cuda::Tabular4DEOSDeviceOwner repeated(host, stream);
        assert_device_pointer(repeated.view().table_cv, "Tab4 repeated table pointer");
    }
}

} // namespace

int main()
{
    try {
        test_comparison_policy();
        test_type_contracts();
        cudaStream_t stream = nullptr;
        cuda_check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "create stream");
        test_species_upload_descriptor_lengths(stream);
        test_ideal_and_lifetime(stream);
        test_helm(stream);
        test_tabular3(stream);
        test_tabular4(stream);
        test_tabular_composition<false>(stream);
        test_tabular_composition<true>(stream);
        test_tabular_polynomials(stream);
        cuda_check(cudaStreamDestroy(stream), "destroy stream");
        std::cout << "eos_host_device_parity max_abs=" << max_abs_error
                  << " max_rel=" << max_rel_error << "\n";
        for (const auto &[field, statistics] : field_errors)
            std::cout << "field=" << field << " max_abs=" << statistics.first
                      << " max_rel=" << statistics.second << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "eos_host_device_parity: " << error.what() << "\n";
        return 1;
    }
}
