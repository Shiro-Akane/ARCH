// A real generated weak network with constant-cv and Helmholtz thermal controls.
// Both use the same trajectory driver/ODEs and actual table ownership.
// Select the package with -include and ARCH_TEST_NETWORK_TYPE, never an ID switch.
#include "cuda/microphysics/device_network_owner.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/linalg/DenseWrap.h"
#include "cuda/microphysics/helm_eos_device_owner.h"
#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <vector>

using Network = ARCH_TEST_NETWORK_TYPE;
static_assert(OdeMath::has_nonconservative_energy<Network>);
static_assert(BurnLimits::uses_compact_matrix(Network::ODE_NEQ));
struct ThermalControl {
    double cv;
    ARCH_HOST_DEVICE double get_eta(double, double, const double*) const { return 0.0; }
    ARCH_HOST_DEVICE double get_cv(double, double, const double*) const { return cv; }
    ARCH_HOST_DEVICE double get_eint_from_T(double, double t, const double*) const { return cv * t; }
};
struct Path {
    bool success = true;
    int attempts = 0, rejections = 0;
    double state[Network::ODE_NEQ]{};
    double closure = 0.0;
};
struct Result {
    Path paths[3];
    double source = 0.0;
    double gradient[Network::NUM_SPECIES + 1]{};
};
template<template<class, class, class> class Policy, class Eos>
ARCH_HOST_DEVICE Path integrate(Network network, double rho, double temperature,
    double interval, Eos eos, BurnConfigView cfg)
{
    Path result;
    double initial[Network::ODE_NEQ]{};
    for (int i = 0; i < Network::NUM_SPECIES; ++i) initial[i] = 1.0 / Network::NUM_SPECIES;
    initial[Network::NUM_SPECIES] = temperature;
    for (int i = 0; i < Network::ODE_NEQ; ++i) result.state[i] = initial[i];
    using Ode = Policy<Network, DenseMatrixData<Network::ODE_NEQ>, DenseLUSolver>;
    for (int part = 0; part < 8; ++part) {
        double dt = interval / 8;
        const auto report = Ode::integrate_report(result.state, rho, dt, eos, cfg, dt, network);
        result.success = result.success && report.success();
        result.attempts += report.attempted_substeps;
        result.rejections += report.rejected_substeps;
        if (!report.success()) break;
    }
    const double thermal = eos.get_eint_from_T(rho, result.state[Network::NUM_SPECIES], result.state)
                         - eos.get_eint_from_T(rho, temperature, initial);
    const double integrated = OdeMath::integrated_burn_energy<Network>(result.state, initial);
    result.closure = std::abs(thermal - integrated) / std::max(1.0, std::max(std::abs(thermal), std::abs(integrated)));
    result.success = result.success && result.attempts > 0 && std::isfinite(result.closure)
        && result.closure < 2.e-8 && result.state[Network::NONCONSERVATIVE_ENERGY_INDEX] < 0.0;
    return result;
}
template <class Eos>
ARCH_HOST_DEVICE Result evaluate(Network network, double rho, double temperature,
    double interval, Eos eos, BurnConfigView cfg)
{
    Result result;
    result.paths[0] = integrate<Solver_BE_NR>(network, rho, temperature, interval, eos, cfg);
    result.paths[1] = integrate<Solver_BD>(network, rho, temperature, interval, eos, cfg);
    result.paths[2] = integrate<Solver_ROS4>(network, rho, temperature, interval, eos, cfg);
    double state[Network::ODE_NEQ]{};
    for (int i = 0; i < Network::NUM_SPECIES; ++i) state[i] = 1.0 / Network::NUM_SPECIES;
    state[Network::NUM_SPECIES] = temperature;
    // Keep source-before-gradient: this order exposed optimized CUDA reuse of
    // the scalar accessor's discarded RHS scratch over this live input array.
    result.source = network.eval_nonconservative_energy(state, rho, 0.0);
    network.eval_nonconservative_gradient(state, rho, 0.0, result.gradient);
    for (int i = 0; i < Network::ODE_NEQ; ++i) {
        const double expected = i < Network::NUM_SPECIES ? 1.0 / Network::NUM_SPECIES
            : i == Network::NUM_SPECIES ? temperature : 0.0;
        result.paths[0].success = result.paths[0].success && state[i] == expected;
    }
    return result;
}
template <class Eos>
__global__ void evaluate_device(Result* result, Network network, double rho,
    double temperature, double interval, Eos eos, BurnConfigView cfg)
{
    *result = evaluate(network, rho, temperature, interval, eos, cfg);
}
template <class HostEos, class DeviceEos>
int compare_paths(double rho, double temperature, double interval,
                  HostEos host_eos, DeviceEos device_eos, BurnConfigView cfg)
{
    const auto host_network = Network::host_view();
    const auto host_storage = Network::host_table_storage();
    std::vector<double> borrowed(host_storage.data, host_storage.data + host_storage.size);
    arch::cuda::DeviceNetworkOwner owner({borrowed.data(), borrowed.size()}, nullptr);
    // Host input may be reused immediately after construction; the device
    // owner must neither borrow it nor publish an unfinished upload.
    std::fill(borrowed.begin(), borrowed.end(), std::numeric_limits<double>::quiet_NaN());
    const Network device_network{owner.view()};
    arch::cuda::DeviceAllocation<Result> device;
    device.allocate(1);
    evaluate_device<<<1, 1>>>(device.get(), device_network, rho, temperature, interval, device_eos, cfg);
    arch::cuda::check_cuda(cudaGetLastError(), "weak trajectory kernel");
    Result gpu;
    arch::cuda::check_cuda(cudaMemcpy(&gpu, device.get(), sizeof(Result), cudaMemcpyDeviceToHost), "weak trajectory result");
    const auto cpu = evaluate(host_network, rho, temperature, interval, host_eos, cfg);
    const auto close = [](double a, double b) {
        const bool matches = std::isfinite(a) && std::isfinite(b)
            && std::abs(a - b) <= 2.e-10 * std::max(1.0, std::abs(a));
        if (!matches) std::cerr << std::setprecision(17) << "parity mismatch CPU=" << a << " CUDA=" << b
                               << " relative=" << std::abs(a-b)/std::max(1.0, std::abs(a)) << '\n';
        return matches;
    };
    bool passed = close(cpu.source, gpu.source);
    for (int i = 0; i <= Network::NUM_SPECIES; ++i) {
        std::cout << "source_gradient," << i << ',' << cpu.gradient[i] << ',' << gpu.gradient[i] << '\n';
        passed = close(cpu.gradient[i], gpu.gradient[i]) && passed;
    }
    std::cout << std::setprecision(17) << "method,backend,attempts,rejections,closure,state...\n";
    for (int method = 0; method < 3; ++method) {
        for (int backend = 0; backend < 2; ++backend) {
            const auto& path = (backend == 0 ? cpu : gpu).paths[method];
            passed = passed && path.success;
            if (!path.success) std::cerr << "path did not complete method=" << method << " backend=" << backend << '\n';
            std::cout << method << ',' << backend << ',' << path.attempts << ',' << path.rejections << ',' << path.closure;
            for (double value : path.state) std::cout << ',' << value;
            std::cout << '\n';
        }
        for (int i = 0; i < Network::ODE_NEQ; ++i)
            passed = close(cpu.paths[method].state[i], gpu.paths[method].state[i]) && passed;
    }
    std::cout << (passed ? "WEAK_TRAJECTORY_PARITY_PASS" : "WEAK_TRAJECTORY_PARITY_FAIL") << '\n';
    return passed ? 0 : 1;
}

int main(int argc, char** argv)
{
    try {
        const bool helm = argc > 1 && std::string(argv[1]) == "--helm";
        if (argc != 5 && argc != 6)
            throw std::invalid_argument("usage: weak_trajectory rho temperature interval cv [rtol] | --helm rho temperature interval [rtol]");
        const int start = helm ? 2 : 1;
        const double rho = std::stod(argv[start]), temperature = std::stod(argv[start + 1]);
        const double interval = std::stod(argv[start + 2]), cv = helm ? 1.0 : std::stod(argv[4]);
        for (double value : {rho, temperature, interval, cv})
            if (!std::isfinite(value) || value <= 0.0) throw std::invalid_argument("positive finite input required");
        int devices = 0;
        arch::cuda::check_cuda(cudaGetDeviceCount(&devices), "weak trajectory GPU probe");
        if (!devices) return 77;
        BurnConfig settings;
        settings.use_nse = false; settings.smallt = 1.0; settings.smallx = 1.e-30;
        settings.nuclearTempMin = settings.nuclearDensMin = 0.0;
        settings.odeconfig.rtol = argc == 6 ? std::stod(argv[5]) : 1.e-7;
        if (!std::isfinite(settings.odeconfig.rtol) || settings.odeconfig.rtol <= 0.0)
            throw std::invalid_argument("positive finite rtol required");
        settings.odeconfig.atol = 1.e-14;
        settings.odeconfig.initial_dt_frac = 1.0; settings.odeconfig.max_substeps = 100000;
        const auto cfg = make_burn_config_view(settings);
        std::cout << std::setprecision(17);
        if (helm) {
            SpeciesManager species;
            Network::RegisterSpecies(species);
            HelmEos eos(std::string(ARCH_SOURCE_DIR) + "/EOS_toolkit/tables/helmholtz/helm_table.dat", &species);
            arch::cuda::HelmEosDeviceOwner owner(eos, nullptr);
            std::array<double, Network::NUM_SPECIES> fractions;
            fractions.fill(1.0 / Network::NUM_SPECIES);
            const double energy_scale = eos.get_eint_from_T(rho, temperature, fractions.data());
            std::cout << "controls_helm," << rho << ',' << temperature << ',' << interval
                      << ',' << energy_scale << ',' << settings.odeconfig.rtol << '\n';
            return compare_paths(rho, temperature, interval, eos.get_view(), owner.view(), cfg);
        }
        std::cout << "controls," << rho << ',' << temperature << ',' << interval
                  << ',' << cv << ',' << settings.odeconfig.rtol << '\n';
        return compare_paths(rho, temperature, interval, ThermalControl{cv}, ThermalControl{cv}, cfg);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
