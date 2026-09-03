#include "cuda/microphysics/helm_eos_loader.h"
#include "cuda/microphysics/microphysics_api.h"
#include "driver/DriverBurn.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "physics/eos/HelmEos.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
constexpr double kRho = 1.0e6;
constexpr double kDt = 1.0e-16;
int numerical_mismatches = 0;

struct RouteAuthority {
    const char* name;
    std::uint64_t state_hash;
    std::uint64_t dt_bits;
    std::uint64_t old_eint_bits;
    std::uint64_t new_eint_bits;
    int changed_fields;
    std::array<double, BurnLimits::MAX_ODE_NEQ> state_budgets;
    double old_eint_budget;
    double new_eint_budget;
};

template<class... Values>
constexpr std::array<double, BurnLimits::MAX_ODE_NEQ>
make_state_budgets(Values... values)
{
    static_assert(sizeof...(values) <= BurnLimits::MAX_ODE_NEQ);
    return {static_cast<double>(values)...};
}

constexpr RouteAuthority kAuthorities[]{
    {"aprox13.be_nr", 0x4943003f6ab3cb2dULL, 0x3ca9f0d3ef0faf29ULL, 0x4391413e4687cfdbULL, 0x4391413e4687d1d1ULL, 14,
        make_state_budgets(), 1.1e-15, 6.0e-15},
    {"aprox13.bd",    0x1a52639606be9948ULL, 0x3ca9f0d3ef0faf29ULL, 0x4391413e4687cfdbULL, 0x4391413e4687d1f4ULL, 14,
        make_state_budgets(), 1.1e-15, 1.7e-14},
    {"aprox13.ros4",  0x502d6fca059f57ffULL, 0x3ca9f0d3ef0faf29ULL, 0x4391413e4687cfdbULL, 0x4391413e4687d1f4ULL, 11,
        make_state_budgets(), 1.1e-15, 1.6e-14},
    {"aprox19.be_nr", 0xfeff3a8f8ded2dd1ULL, 0x3ca9f0d3ef0faf29ULL, 0x43944a49f2149211ULL, 0x43944a4b4b159ae4ULL, 20,
        make_state_budgets(
            1.20e-14, 1.33e-16, 1.96e-16, 2.56e-16, 3.21e-16,
            3.91e-16, 4.43e-16, 5.12e-16, 5.73e-16, 6.42e-16,
            7.03e-16, 7.81e-16, 8.33e-16, 8.85e-16, 9.72e-16,
            1.02e-15, 1.09e-15, 1.15e-15, 1.23e-15, 0.0),
        3.9e-15, 1.6e-14},
    {"aprox19.bd",    0x89a1b5f5417c267dULL, 0x3ca9f0d3ef0faf29ULL, 0x43944a49f2149211ULL, 0x43944a4b4b15e043ULL, 20,
        make_state_budgets(
            1.09e-14, 1.37e-16, 1.74e-16, 2.35e-16, 2.95e-16,
            3.47e-16, 4.17e-16, 4.69e-16, 5.21e-16, 5.90e-16,
            6.42e-16, 6.94e-16, 7.46e-16, 8.33e-16, 8.85e-16,
            9.37e-16, 9.89e-16, 1.04e-15, 1.11e-15, 0.0),
        3.9e-15, 2.4e-14},
    {"aprox19.ros4",  0xacdac9832a94cb34ULL, 0x3ca9f0d3ef0faf29ULL, 0x43944a49f2149211ULL, 0x43944a4b4b15e18dULL, 20,
        make_state_budgets(
            3.98e-15, 4.12e-17, 6.08e-17, 8.24e-17, 9.98e-17,
            1.22e-16, 1.48e-16, 1.65e-16, 1.83e-16, 2.00e-16,
            2.26e-16, 2.43e-16, 2.61e-16, 2.95e-16, 3.13e-16,
            3.30e-16, 3.47e-16, 3.65e-16, 3.82e-16, 0.0),
        3.9e-15, 3.5e-15},
    {"aprox21.be_nr", 0x5f371a72a6128b2bULL, 0x3ca9f0d3ef0faf29ULL, 0x4393dd63fe723809ULL, 0x4393dd6bcaa70f6bULL, 22,
        make_state_budgets(
            5.28e-14, 4.28e-16, 6.90e-16, 9.16e-16, 1.15e-15,
            1.38e-15, 1.61e-15, 1.84e-15, 2.07e-15, 2.30e-15,
            2.53e-15, 2.76e-15, 2.99e-15, 3.21e-15, 3.44e-15,
            3.67e-15, 3.89e-15, 4.13e-15, 4.36e-15, 4.60e-15,
            4.83e-15, 0.0),
        1.2e-15, 6.2e-14},
    {"aprox21.bd",    0x759fb3f8f7eea959ULL, 0x3ca9f0d3ef0faf29ULL, 0x4393dd63fe723809ULL, 0x4393dd6bcab3e05bULL, 22,
        make_state_budgets(
            5.26e-15, 1.89e-16, 6.51e-17, 8.68e-17, 1.09e-16,
            1.31e-16, 1.52e-16, 1.74e-16, 2.00e-16, 2.17e-16,
            2.35e-16, 2.61e-16, 2.87e-16, 3.04e-16, 3.30e-16,
            3.47e-16, 3.47e-16, 3.99e-16, 4.17e-16, 4.34e-16,
            4.52e-16, 0.0),
        1.2e-15, 1.7e-14},
    {"aprox21.ros4",  0x41e4d2caf71c8ef8ULL, 0x3ca9f0d3ef0faf29ULL, 0x4393dd63fe723809ULL, 0x4393dd6bcab4040fULL, 22,
        make_state_budgets(
            9.08e-14, 8.94e-16, 1.18e-15, 1.58e-15, 1.97e-15,
            2.37e-15, 2.76e-15, 3.16e-15, 3.55e-15, 3.94e-15,
            4.33e-15, 4.73e-15, 5.12e-15, 5.52e-15, 5.92e-15,
            6.32e-15, 6.74e-15, 7.10e-15, 7.48e-15, 7.88e-15,
            8.28e-15, 0.0),
        1.2e-15, 9.1e-14},
    {"iso7.be_nr",    0xbb6ba1b9dab1bffcULL, 0x3ca9f0d3ef0faf29ULL, 0x43919fb3f2f86bcaULL, 0x43919fb3f2f87d9cULL, 7,
        make_state_budgets(), 5.7e-15, 8.0e-15},
    {"iso7.bd",       0xd251d19740ab1a8aULL, 0x3ca9f0d3ef0faf29ULL, 0x43919fb3f2f86bcaULL, 0x43919fb3f2f87d6fULL, 7,
        make_state_budgets(), 5.7e-15, 1.5e-14},
    {"iso7.ros4",     0x27ef02d4d0bb4bffULL, 0x3ca9f0d3ef0faf29ULL, 0x43919fb3f2f86bcaULL, 0x43919fb3f2f87d6cULL, 7,
        make_state_budgets(), 5.7e-15, 1.55e-14},
};

void cuda_check(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": "
                                 + cudaGetErrorString(error));
}

std::uint64_t state_hash(const double* values, int count)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (int i = 0; i < count; ++i) {
        hash ^= std::bit_cast<std::uint64_t>(values[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void require_close(double actual, double expected, double relative,
                   const std::string& label)
{
    if (std::isnan(expected)) {
        if (!std::isnan(actual)) {
            std::cerr << label << " NaN mismatch\n";
            ++numerical_mismatches;
        }
        return;
    }
    if (std::isinf(expected) || std::isinf(actual)) {
        if (std::bit_cast<std::uint64_t>(actual)
            != std::bit_cast<std::uint64_t>(expected))
        {
            std::cerr << label << " infinity mismatch\n";
            ++numerical_mismatches;
        }
        return;
    }
    if (expected == 0.0 && actual == 0.0) {
        if (std::signbit(actual) != std::signbit(expected))
        {
            std::cerr << label << " signed-zero mismatch\n";
            ++numerical_mismatches;
        }
        return;
    }
    const double error = std::abs(actual - expected)
        / std::max(1.0, std::abs(expected));
    if (error > relative) {
        std::cerr << label << " expected=" << std::hexfloat << expected
                  << " actual=" << actual << std::defaultfloat
                  << " relative=" << error << '\n';
        ++numerical_mismatches;
    }
}

BurnConfig make_config()
{
    BurnConfig config{};
    config.use_burn = true;
    config.use_nse = false;
    config.nuclearTempMin = 1.0e8;
    config.nuclearDensMin = 1.0;
    config.smallt = 1.0e5;
    config.smallx = 1.0e-30;
    config.enucDtFactor = 0.5;
    config.odeconfig.rtol = 1.0e-4;
    config.odeconfig.atol = 1.0e-8;
    config.odeconfig.max_newton_iter = 50;
    config.odeconfig.max_substeps = 100;
    config.odeconfig.initial_dt_frac = 1.0;
    config.odeconfig.dt_safe_factor = 0.9;
    config.odeconfig.dt_fac_min = 0.1;
    config.odeconfig.dt_fac_max = 2.0;
    return config;
}

template<class Net>
std::array<double, BurnLimits::MAX_ODE_NEQ> make_state(int variant)
{
    std::array<double, BurnLimits::MAX_ODE_NEQ> state{};
    const double denominator =
        static_cast<double>(Net::NUM_SPECIES * (Net::NUM_SPECIES + 1) / 2);
    double sum = 0.0;
    std::uint32_t seed = 0x9e3779b9U ^ static_cast<std::uint32_t>(variant + 1);
    for (int i = 0; i < Net::NUM_SPECIES; ++i) {
        seed = 1664525U * seed + 1013904223U;
        const double perturbation = 1.0
            + 1.0e-5 * static_cast<double>(static_cast<int>(seed % 9U) - 4);
        state[i] = static_cast<double>(i + 1) / denominator * perturbation;
        sum += state[i];
    }
    for (int i = 0; i < Net::NUM_SPECIES; ++i) state[i] /= sum;
    state[Net::ODE_NEQ - 1] = 2.0e9 + 1.0e6 * variant;
    return state;
}

template<class Net, template<class, class, class> class Solver>
__global__ void policy_kernel(arch::cuda::BurnPolicyCell* result,
                              arch::cuda::BurnOdeMatrixWorkspace* workspace,
                              HelmEosView eos, BurnConfigView config)
{
    if (threadIdx.x == 0 && blockIdx.x == 0)
        arch::cuda::execute_ode_policy<Net, Solver>(
            *result, *workspace, eos, config);
}

template<class Net, template<class, class, class> class Solver>
void check_route(const RouteAuthority& authority, int variant,
                 const HelmEos& eos, HelmEosView device_eos,
                 cudaStream_t stream)
{
    BurnConfig config = make_config();
    auto initial = make_state<Net>(variant);
    auto host = initial;
    double host_dt = kDt;
    const double host_old_eint = eos.get_eint_from_T(
        kRho, host[Net::ODE_NEQ - 1], host.data());
    const bool host_success =
        Solver<Net, DenseMatrixData<Net::ODE_NEQ>, DenseLUSolver>::integrate(
        host.data(), kRho, kDt, eos, config, host_dt);
    const double host_new_eint = eos.get_eint_from_T(
        kRho, host[Net::ODE_NEQ - 1], host.data());
    if (!host_success)
        throw std::runtime_error(std::string(authority.name)
                                 + " host reference integration failed");
    int changed = 0;
    for (int i = 0; i < Net::ODE_NEQ; ++i)
        changed += std::bit_cast<std::uint64_t>(host[i])
                != std::bit_cast<std::uint64_t>(initial[i]);
    if (changed != authority.changed_fields || changed == 0)
        throw std::runtime_error(std::string(authority.name)
                                 + " is not a discriminating route fixture");

    arch::cuda::BurnPolicyCell result{};
    result.fluid.rho = kRho;
    std::copy(initial.begin(), initial.end(), result.state);
    result.burn_dt = kDt;
    arch::cuda::BurnPolicyCell* device = nullptr;
    arch::cuda::BurnOdeMatrixWorkspace* device_workspace = nullptr;
    cuda_check(cudaMalloc(&device, sizeof(result)), "cudaMalloc route result");
    cuda_check(cudaMalloc(&device_workspace, sizeof(*device_workspace)),
               "cudaMalloc route workspace");
    if (!arch::cuda::burn_ode_workspace_preflight(
            device_workspace, 1, 1))
        throw std::runtime_error("route workspace preflight failed");
    cudaPointerAttributes workspace_attributes{};
    cuda_check(cudaPointerGetAttributes(&workspace_attributes, device_workspace),
               "inspect route workspace allocation");
    if (workspace_attributes.type != cudaMemoryTypeDevice)
        throw std::runtime_error("route workspace is not device-global storage");
    cuda_check(cudaMemcpyAsync(device, &result, sizeof(result),
                               cudaMemcpyHostToDevice, stream), "upload route result");
    policy_kernel<Net, Solver><<<1, 1, 0, stream>>>(
        device, device_workspace, device_eos, make_burn_config_view(config));
    cuda_check(cudaGetLastError(), "launch route kernel");
    cuda_check(cudaMemcpyAsync(&result, device, sizeof(result),
                               cudaMemcpyDeviceToHost, stream), "download route result");
    cuda_check(cudaStreamSynchronize(stream), "synchronize route kernel");
    cuda_check(cudaFree(device_workspace), "cudaFree route workspace");
    cuda_check(cudaFree(device), "cudaFree route result");

    if (!result.ode.success() || result.ode.status != BurnOdeStatus::OdeSuccess
        || result.ode.attempted_substeps != 1 || result.ode.nse_attempts != 0
        || result.ode.nse_failures != 0)
        throw std::runtime_error(std::string(authority.name)
                                 + " status/retry identity mismatch");
    for (int i = 0; i < Net::ODE_NEQ; ++i)
        require_close(result.state[i], host[i], authority.state_budgets[i],
                      std::string(authority.name) + ".state." + std::to_string(i));
    require_close(result.dt_recommended, host_dt, 0.0,
                  std::string(authority.name) + ".dt");
    require_close(result.eint_old, host_old_eint, authority.old_eint_budget,
                  std::string(authority.name) + ".eint_old");
    require_close(result.eint_new, host_new_eint, authority.new_eint_budget,
                  std::string(authority.name) + ".eint_new");
}

template<class Net>
void check_network(int offset, int variant, const HelmEos& eos,
                   HelmEosView device_eos, cudaStream_t stream)
{
    check_route<Net, Solver_BE_NR>(kAuthorities[offset], variant, eos, device_eos, stream);
    check_route<Net, Solver_BD>(kAuthorities[offset + 1], variant, eos, device_eos, stream);
    check_route<Net, Solver_ROS4>(kAuthorities[offset + 2], variant, eos, device_eos, stream);
}

struct LeafEos {
    ARCH_INLINE double get_temperature(double, double eint, const double* x) const
    { return eint / (10.0 + x[0]); }
    ARCH_INLINE double get_eint_from_T(double, double temperature, const double* x) const
    { return temperature * (10.0 + x[0]); }
};

struct LeafNet { static constexpr int NUM_SPECIES = 2; static constexpr int ODE_NEQ = 3; };

struct StatusEos {
    ARCH_INLINE double get_eint_from_T(double, double temperature, const double*) const
    { return temperature; }
    ARCH_INLINE double get_cv(double, double, const double*) const { return 1.0; }
    ARCH_INLINE double get_eta(double, double, const double*) const { return 0.0; }
};

struct NseRejectNet {
    static constexpr int NUM_SPECIES = 1;
    static constexpr int ODE_NEQ = 2;
    static constexpr bool SUPPORTS_NSE = true;
    static constexpr double ENERGY_CONVERSION = 1.0;
    ARCH_INLINE static constexpr double aion(int) { return 1.0; }
    ARCH_INLINE static constexpr double zion(int) { return 0.5; }
    ARCH_INLINE static constexpr double binding_energy(int) { return 0.0; }
    ARCH_INLINE static constexpr double spin_weight(int) { return 0.0; }
    ARCH_INLINE static constexpr double energy_weight(int) { return 0.0; }
    ARCH_INLINE static void eval_rhs(
        const double*, double, double, double* rhs, double& enuc)
    {
        rhs[0] = 0.0;
        enuc = 0.0;
    }
    template<class Matrix>
    ARCH_INLINE static void eval_jacobian(
        const double*, double, double, Matrix&, double* denuc)
    { denuc[0] = 0.0; }
    ARCH_INLINE static void eval_temperature_derivative(
        const double*, double, double, double* rhs, double& denuc)
    {
        rhs[0] = 0.0;
        denuc = 0.0;
    }
};

struct RejectingNet : NseRejectNet {
    ARCH_INLINE static void eval_rhs(
        const double*, double, double, double* rhs, double& enuc)
    {
        rhs[0] = std::numeric_limits<double>::quiet_NaN();
        enuc = 0.0;
    }
};

struct NseAcceptNet : NseRejectNet {
    ARCH_INLINE static constexpr double spin_weight(int) { return 1.0; }
};

template<class, class MatrixType, class>
struct RhoSensitiveSolver {
    template<class Eos>
    ARCH_INLINE static BurnOdeReport integrate_report(
        double* state, double rho, double, const Eos&,
        const BurnConfigView&, OdeMatrixWorkspace<MatrixType>& workspace,
        double& dt_rec)
    {
        state[0] -= 0.125;
        state[1] += 0.125;
        state[2] *= rho < 3.0 ? 1.25 : 0.75;
        dt_rec = rho < 3.0 ? 0.125 : 0.25;
        workspace.jacobian.data[0][0] = rho;
        workspace.system.data[0][0] = dt_rec;
        BurnOdeReport report{};
        report.status = BurnOdeStatus::OdeSuccess;
        report.attempted_substeps = 1;
        report.dt_recommended = dt_rec;
        return report;
    }
};

__global__ void driver_leaf_kernel(
    arch::cuda::BurnPolicyCell* cells,
    arch::cuda::BurnOdeMatrixWorkspace* workspaces,
    int count, bool alias_workspaces, BurnConfigView config)
{
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell < count)
        arch::cuda::execute_burn_policy_cell<LeafNet, RhoSensitiveSolver>(
            cells[cell], workspaces[alias_workspaces ? 0 : cell],
            LeafEos{}, config);
}

arch::cuda::BurnPolicyCell leaf_cell(double rho, double temperature,
                                     double mx, double my, double mz)
{
    arch::cuda::BurnPolicyCell cell{};
    cell.fluid = {rho, mx, my, mz, 0.0};
    cell.state[0] = 0.75;
    cell.state[1] = 0.25;
    const double kinetic = 0.5 * (mx * mx + my * my + mz * mz) / rho;
    cell.fluid.eng = rho * temperature * 10.75 + kinetic;
    cell.burn_dt = 2.0;
    cell.enuc_rate = 1234.5;
    return cell;
}

void check_driver_leaves(cudaStream_t stream, bool alias_workspaces = false)
{
    BurnConfig config = make_config();
    config.nuclearTempMin = 1.0;
    config.nuclearDensMin = 1.0;
    config.smallx = 1.0e-20;
    constexpr int count = 9;
    std::array<arch::cuda::BurnPolicyCell, count> cells{
        leaf_cell(0.5, 2.0, 0.0, 0.0, 0.0),
        leaf_cell(1.5, 0.5, 0.3, -0.2, 0.1),
        leaf_cell(2.0, 2.0, 1.5, -0.5, 0.25),
        leaf_cell(4.0, 2.0, -2.0, 0.75, -0.5),
        leaf_cell(2.0, 2.0, 0.0, 0.0, 0.0),
        leaf_cell(2.0, 2.0, 0.0, 0.0, 0.0),
        leaf_cell(2.0, 2.0, 0.0, 0.0, 0.0),
        leaf_cell(2.0, 2.0, 0.0, 0.0, 0.0),
        leaf_cell(2.0, 2.0, 0.0, 0.0, 0.0)};
    cells[4].state[0] = 0.4;
    cells[4].state[1] = 0.5;
    cells[5].state[0] = std::nextafter(-10.0 * config.smallx,
                                      -std::numeric_limits<double>::infinity());
    cells[5].state[1] = 1.0 - cells[5].state[0];
    cells[6].state[0] = -10.0 * config.smallx;
    cells[6].state[1] = 1.0 - cells[6].state[0];
    cells[7].state[0] = 0.5;
    cells[7].state[1] = (1.0 + 1.0e-6) - cells[7].state[0];
    cells[8].state[0] = 0.5;
    cells[8].state[1] = std::nextafter(
        1.0 + 1.0e-6, std::numeric_limits<double>::infinity())
        - cells[8].state[0];

    arch::cuda::BurnPolicyCell* device = nullptr;
    arch::cuda::BurnOdeMatrixWorkspace* device_workspaces = nullptr;
    cuda_check(cudaMalloc(&device, sizeof(cells)), "cudaMalloc driver leaves");
    cuda_check(cudaMalloc(&device_workspaces,
                          count * sizeof(*device_workspaces)),
               "cudaMalloc driver leaf workspaces");
    cuda_check(cudaMemsetAsync(device_workspaces, 0,
                               count * sizeof(*device_workspaces), stream),
               "clear driver leaf workspaces");
    cuda_check(cudaMemcpyAsync(device, cells.data(), sizeof(cells),
                               cudaMemcpyHostToDevice, stream), "upload driver leaves");
    driver_leaf_kernel<<<1, 32, 0, stream>>>(
        device, device_workspaces, count, alias_workspaces,
        make_burn_config_view(config));
    cuda_check(cudaGetLastError(), "launch driver leaf kernel");
    std::array<arch::cuda::BurnOdeMatrixWorkspace, count> workspaces{};
    cuda_check(cudaMemcpyAsync(cells.data(), device, sizeof(cells),
                               cudaMemcpyDeviceToHost, stream), "download driver leaves");
    cuda_check(cudaMemcpyAsync(workspaces.data(), device_workspaces,
                               sizeof(workspaces), cudaMemcpyDeviceToHost, stream),
               "download driver leaf workspaces");
    cuda_check(cudaStreamSynchronize(stream), "synchronize driver leaves");
    cuda_check(cudaFree(device_workspaces), "cudaFree driver leaf workspaces");
    cuda_check(cudaFree(device), "cudaFree driver leaves");

    using D = DriverBurn::BurnCellDisposition;
    const D expected[]{D::BelowDensity, D::BelowTemperature, D::Ready, D::Ready,
                       D::InvalidComposition, D::InvalidComposition, D::Ready,
                       D::Ready, D::InvalidComposition};
    for (int i = 0; i < count; ++i) {
        if (cells[i].disposition != expected[i])
            throw std::runtime_error("DriverBurn disposition mismatch");
        if ((i == 2 || i == 3 || i == 6 || i == 7)
            != cells[i].interior_effect.interior_written)
            throw std::runtime_error("burn interior-written seam mismatch");
        if (i != 2 && i != 3 && i != 6 && i != 7
            && cells[i].enuc_rate != 0.0)
            throw std::runtime_error("skipped/invalid ENUC was not cleared");
        const bool active = i == 2 || i == 3 || i == 6 || i == 7;
        const double workspace_rho = workspaces[i].jacobian.data[0][0];
        if ((active && workspace_rho != cells[i].fluid.rho)
            || (!active && workspace_rho != 0.0))
            throw std::runtime_error("caller workspace binding/alias contract drifted");
    }
    require_close(cells[2].fluid.eng, std::bit_cast<double>(0x404ae20000000000ULL), 0.0,
                  "positive handoff energy");
    require_close(cells[2].enuc_rate, std::bit_cast<double>(0x4004400000000000ULL), 0.0,
                  "positive signed ENUC");
    require_close(cells[3].fluid.eng, std::bit_cast<double>(0x4050168000000000ULL), 0.0,
                  "negative handoff energy");
    require_close(cells[3].enuc_rate, std::bit_cast<double>(0xc006400000000000ULL), 0.0,
                  "negative signed ENUC");
    require_close(std::min(cells[2].limiter_candidate, cells[3].limiter_candidate),
                  std::bit_cast<double>(0x4006ebdd7baf75efULL), 0.0,
                  "global limiter candidate");

    config.use_burn = false;
    for (auto& cell : cells) {
        cell.enuc_rate = 99.0;
        cell.interior_effect.interior_written = true;
    }
    cuda_check(cudaMalloc(&device, sizeof(cells)), "cudaMalloc disabled leaves");
    cuda_check(cudaMalloc(&device_workspaces,
                          count * sizeof(*device_workspaces)),
               "cudaMalloc disabled leaf workspaces");
    cuda_check(cudaMemcpyAsync(device, cells.data(), sizeof(cells),
                               cudaMemcpyHostToDevice, stream), "upload disabled leaves");
    driver_leaf_kernel<<<1, 32, 0, stream>>>(
        device, device_workspaces, count, false, make_burn_config_view(config));
    cuda_check(cudaGetLastError(), "launch disabled driver leaves");
    cuda_check(cudaMemcpyAsync(cells.data(), device, sizeof(cells),
                               cudaMemcpyDeviceToHost, stream), "download disabled leaves");
    cuda_check(cudaStreamSynchronize(stream), "sync disabled driver leaves");
    cuda_check(cudaFree(device_workspaces), "cudaFree disabled leaf workspaces");
    cuda_check(cudaFree(device), "cudaFree disabled leaves");
    for (const auto& cell : cells)
        if (cell.enuc_rate != 0.0 || cell.interior_effect.interior_written)
            throw std::runtime_error("whole-stage disabled ENUC clear drifted");
}

struct HandoffEos {
    ARCH_INLINE double get_eint_from_T(double, double temperature,
                                       const double*) const
    { return temperature; }
};

__global__ void handoff_threshold_kernel(
    DriverBurn::BurnEnergyHandoff* output, BurnConfigView config)
{
    if (threadIdx.x != 0 || blockIdx.x != 0) return;
    FluidVector fluid{1.0, 0.0, 0.0, 0.0, 0.0};
    double state[BurnLimits::MAX_ODE_NEQ]{};
    state[0] = 1.0;
    state[1] = 5.0e-21;
    output[0] = DriverBurn::compute_burn_energy_handoff(
        fluid, state, 1, 4.0e-21, 0.0, 1.0,
        HandoffEos{}, config);
    state[1] = 1.0;
    output[1] = DriverBurn::compute_burn_energy_handoff(
        fluid, state, 1, 0.0, 0.0, 1.0e31,
        HandoffEos{}, config);
}

void check_handoff_thresholds(cudaStream_t stream)
{
    BurnConfig config = make_config();
    std::array<DriverBurn::BurnEnergyHandoff, 2> values{};
    DriverBurn::BurnEnergyHandoff* device = nullptr;
    cuda_check(cudaMalloc(&device, sizeof(values)), "cudaMalloc handoff thresholds");
    handoff_threshold_kernel<<<1, 1, 0, stream>>>(
        device, make_burn_config_view(config));
    cuda_check(cudaGetLastError(), "launch handoff thresholds");
    cuda_check(cudaMemcpyAsync(values.data(), device, sizeof(values),
                               cudaMemcpyDeviceToHost, stream), "download handoff thresholds");
    cuda_check(cudaStreamSynchronize(stream), "sync handoff thresholds");
    cuda_check(cudaFree(device), "cudaFree handoff thresholds");

    const double frozen_delta = std::abs(5.0e-21 - 4.0e-21);
    const double expected_floor_limiter = config.enucDtFactor
        / ((frozen_delta / 1.0) / 1.0e-20);
    require_close(values[0].limiter_candidate, expected_floor_limiter, 0.0,
                  "ENUC denominator floor");
    if (values[1].limiter_candidate
        != DriverBurn::INACTIVE_LIMITER_CANDIDATE)
        throw std::runtime_error("ENUC source threshold drifted");
}

template<template<class, class, class> class Solver>
__global__ void status_kernel(arch::cuda::BurnPolicyCell* cells,
                              arch::cuda::BurnOdeMatrixWorkspace* workspaces,
                              BurnConfigView config)
{
    if (threadIdx.x != 0 || blockIdx.x != 0) return;

    config.use_nse = true;
    config.nseTempThreshold = 1.0;
    config.nseDensThreshold = 1.0;
    config.nuclearTempMin = 1.0;
    config.nuclearDensMin = 1.0;
    config.smallt = 0.5;
    config.odeconfig.initial_dt_frac = 1.0;
    arch::cuda::execute_ode_policy<NseRejectNet, Solver>(
        cells[0], workspaces[0], StatusEos{}, config);

    config.use_nse = false;
    config.odeconfig.max_substeps = 0;
    arch::cuda::execute_ode_policy<NseRejectNet, Solver>(
        cells[1], workspaces[1], StatusEos{}, config);

    config.odeconfig.max_substeps = 10;
    arch::cuda::execute_ode_policy<RejectingNet, Solver>(
        cells[2], workspaces[2], StatusEos{}, config);

    config.use_nse = true;
    arch::cuda::execute_ode_policy<NseAcceptNet, Solver>(
        cells[3], workspaces[3], StatusEos{}, config);

    arch::cuda::execute_ode_policy<RejectingNet, Solver>(
        cells[4], workspaces[4], StatusEos{}, config);
}

template<template<class, class, class> class Solver,
         std::uint64_t ExpectedRetryNseAttempts>
void check_status_contract(const char* solver_name, cudaStream_t stream)
{
    BurnConfig config = make_config();
    std::array<arch::cuda::BurnPolicyCell, 5> cells{};
    for (auto& cell : cells) {
        cell.fluid.rho = 2.0;
        cell.state[0] = 1.0;
        cell.state[1] = 2.0;
        cell.burn_dt = kDt;
    }
    cells[2].burn_dt = 1.0e-20;
    cells[4].burn_dt = 1.0e-20;

    arch::cuda::BurnPolicyCell* device = nullptr;
    arch::cuda::BurnOdeMatrixWorkspace* device_workspaces = nullptr;
    cuda_check(cudaMalloc(&device, sizeof(cells)), "cudaMalloc status contract");
    cuda_check(cudaMalloc(&device_workspaces,
                          cells.size() * sizeof(*device_workspaces)),
               "cudaMalloc status workspaces");
    cuda_check(cudaMemcpyAsync(device, cells.data(), sizeof(cells),
                               cudaMemcpyHostToDevice, stream), "upload status contract");
    status_kernel<Solver><<<1, 1, 0, stream>>>(
        device, device_workspaces, make_burn_config_view(config));
    cuda_check(cudaGetLastError(), "launch status contract");
    cuda_check(cudaMemcpyAsync(cells.data(), device, sizeof(cells),
                               cudaMemcpyDeviceToHost, stream), "download status contract");
    cuda_check(cudaStreamSynchronize(stream), "sync status contract");
    cuda_check(cudaFree(device_workspaces), "cudaFree status workspaces");
    cuda_check(cudaFree(device), "cudaFree status contract");

    if (cells[0].ode.status != BurnOdeStatus::OdeSuccess
        || cells[0].ode.nse_attempts != 1 || cells[0].ode.nse_failures != 1
        || cells[0].ode.attempted_substeps != 1
        || cells[0].ode.rejected_substeps != 0 || !cells[0].ode.success())
        throw std::runtime_error(std::string(solver_name)
                                 + " failed-NSE continuation status drifted");
    if (cells[1].ode.status != BurnOdeStatus::MaxSubsteps
        || cells[1].ode.attempted_substeps != 1
        || cells[1].ode.rejected_substeps != 0
        || cells[1].ode.nse_attempts != 0 || cells[1].ode.nse_failures != 0
        || cells[1].ode.success())
        throw std::runtime_error(std::string(solver_name)
                                 + " max-substeps status drifted");
    if (cells[2].ode.status != BurnOdeStatus::Stalled
        || cells[2].ode.attempted_substeps != 4
        || cells[2].ode.rejected_substeps != 4
        || cells[2].ode.nse_attempts != 0 || cells[2].ode.nse_failures != 0
        || cells[2].ode.success())
        throw std::runtime_error(std::string(solver_name)
                                 + " stall/retry status drifted");
    if (cells[3].ode.status != BurnOdeStatus::NseSuccess
        || cells[3].ode.nse_attempts != 1 || cells[3].ode.nse_failures != 0
        || cells[3].ode.attempted_substeps != 0
        || cells[3].ode.rejected_substeps != 0 || !cells[3].ode.success())
        throw std::runtime_error(std::string(solver_name)
                                 + " NSE-success status drifted");
    if (cells[4].ode.status != BurnOdeStatus::Stalled
        || cells[4].ode.attempted_substeps != 4
        || cells[4].ode.rejected_substeps != 4
        || cells[4].ode.nse_attempts != ExpectedRetryNseAttempts
        || cells[4].ode.nse_failures != ExpectedRetryNseAttempts
        || cells[4].ode.success())
        throw std::runtime_error(std::string(solver_name)
                                 + " failed-NSE retry/reset status drifted");

    constexpr std::uint64_t expected_report_dt_bits[]{
        0x3ca9f0d3ef0faf29ULL,
        0x3c9cd2b297d889bcULL,
        0x3bc79ca10c924223ULL,
        0x3c9cd2b297d889bcULL,
        0x3bc79ca10c924223ULL,
    };
    constexpr std::uint64_t expected_input_dt_bits[]{
        0x3c9cd2b297d889bcULL,
        0x3c9cd2b297d889bcULL,
        0x3bc79ca10c924223ULL,
        0x3c9cd2b297d889bcULL,
        0x3bc79ca10c924223ULL,
    };
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (std::bit_cast<std::uint64_t>(cells[i].ode.dt_recommended)
                != expected_report_dt_bits[i]
            || std::bit_cast<std::uint64_t>(cells[i].dt_recommended)
                != expected_report_dt_bits[i]
            || std::bit_cast<std::uint64_t>(cells[i].burn_dt)
                != expected_input_dt_bits[i])
            throw std::runtime_error(std::string(solver_name)
                                     + " status/dt contract drifted at cell "
                                     + std::to_string(i));
    }
}

__global__ void helper_threshold_kernel(double* output)
{
    if (threadIdx.x == 0 && blockIdx.x == 0) {
        const double below = std::nextafter(1.0e-10, 0.0);
        output[0] = OdeMath::pi_uses_small_error_branch(below) ? 1.0 : 0.0;
        output[1] = OdeMath::pi_uses_small_error_branch(1.0e-10) ? 1.0 : 0.0;
        output[2] = OdeMath::burn_cv_floor(std::nextafter(1.0e-10, 0.0));
        output[3] = OdeMath::burn_cv_floor(1.0e-10);
    }
}

void check_helper_thresholds(cudaStream_t stream)
{
    std::array<double, 4> values{};
    double* device = nullptr;
    cuda_check(cudaMalloc(&device, sizeof(values)), "cudaMalloc helper thresholds");
    helper_threshold_kernel<<<1, 1, 0, stream>>>(device);
    cuda_check(cudaGetLastError(), "launch helper thresholds");
    cuda_check(cudaMemcpyAsync(values.data(), device, sizeof(values),
                               cudaMemcpyDeviceToHost, stream), "download helper thresholds");
    cuda_check(cudaStreamSynchronize(stream), "sync helper thresholds");
    cuda_check(cudaFree(device), "cudaFree helper thresholds");
    if (values[0] != 1.0 || values[1] != 0.0
        || values[2] != 1.0e-10 || values[3] != 1.0e-10)
        throw std::runtime_error("PI/Cv exact threshold contract drifted");
}

void check_workspace_contract()
{
    alignas(arch::cuda::BurnOdeMatrixWorkspace)
        std::array<std::byte,
                   sizeof(arch::cuda::BurnOdeMatrixWorkspace) * 2 + 1> storage{};
    auto* aligned = reinterpret_cast<arch::cuda::BurnOdeMatrixWorkspace*>(
        storage.data());
    auto* misaligned = reinterpret_cast<arch::cuda::BurnOdeMatrixWorkspace*>(
        storage.data() + 1);
    if (!arch::cuda::burn_ode_workspace_preflight(aligned, 2, 2)
        || !arch::cuda::burn_ode_workspace_preflight<
            BurnLimits::MAX_ODE_NEQ>(nullptr, 0, 0)
        || arch::cuda::burn_ode_workspace_preflight<
            BurnLimits::MAX_ODE_NEQ>(nullptr, 1, 1)
        || arch::cuda::burn_ode_workspace_preflight(aligned, 1, 2)
        || arch::cuda::burn_ode_workspace_preflight(misaligned, 2, 2))
        throw std::runtime_error("burn ODE workspace preflight contract drifted");
}

template<class Net, template<class, class, class> class Solver>
void run_route(const RouteAuthority& authority, int variant,
               cudaStream_t stream)
{
    SpeciesManager species;
    Net::RegisterSpecies(species);
    const std::string path = std::string(ARCH_SOURCE_DIR)
        + "/EOS_toolkit/tables/helmholtz/helm_table.dat";
    HelmEos eos(path, &species);
    arch::cuda::HelmEosDeviceOwner owner(eos, stream);
    check_route<Net, Solver>(authority, variant, eos, owner.view(), stream);
    std::cout << authority.name << " passed\n";
}

template<class Action>
void run_in_fresh_context(Action action)
{
    cudaStream_t stream = nullptr;
    cuda_check(cudaStreamCreate(&stream), "cudaStreamCreate");
    try {
        action(stream);
        cuda_check(cudaStreamDestroy(stream), "cudaStreamDestroy");
        stream = nullptr;
        cuda_check(cudaDeviceReset(), "cudaDeviceReset");
    } catch (...) {
        if (stream != nullptr) cudaStreamDestroy(stream);
        cudaDeviceReset();
        throw;
    }
}
} // namespace

int main(int argc, char** argv)
{
    static_assert(std::is_standard_layout_v<BurnConfigView>);
    static_assert(std::is_trivially_copyable_v<BurnConfigView>);
    static_assert(std::is_standard_layout_v<BurnOdeReport>);
    static_assert(std::is_trivially_copyable_v<BurnOdeReport>);
    static_assert(std::is_standard_layout_v<arch::cuda::BurnPolicyCell>);
    static_assert(std::is_trivially_copyable_v<arch::cuda::BurnPolicyCell>);
    static_assert(std::is_standard_layout_v<arch::cuda::BurnOdeMatrixWorkspace>);
    static_assert(std::is_trivially_copyable_v<arch::cuda::BurnOdeMatrixWorkspace>);
    static_assert(sizeof(arch::cuda::BurnOdeMatrixWorkspace)
                  == 2 * sizeof(DenseMatrixData<BurnLimits::MAX_ODE_NEQ>));
    static_assert(alignof(arch::cuda::BurnOdeMatrixWorkspace)
                  == alignof(DenseMatrixData<BurnLimits::MAX_ODE_NEQ>));
    static_assert(offsetof(arch::cuda::BurnOdeMatrixWorkspace, system)
                  == sizeof(DenseMatrixData<BurnLimits::MAX_ODE_NEQ>));
    try {
        if (argc != 2)
            throw std::runtime_error("usage: arch_cuda_burn_policy_parity ROUTE");
        const std::string route = argv[1];
        if (route == "helpers") {
            run_in_fresh_context([](cudaStream_t stream) {
                check_workspace_contract();
                check_driver_leaves(stream);
                check_handoff_thresholds(stream);
                check_helper_thresholds(stream);
            });
        }
        else if (route == "status.be_nr") {
            run_in_fresh_context([](cudaStream_t stream) {
                check_status_contract<Solver_BE_NR, 1>("be_nr", stream);
            });
        }
        else if (route == "status.bd") {
            run_in_fresh_context([](cudaStream_t stream) {
                check_status_contract<Solver_BD, 4>("bd", stream);
            });
        }
        else if (route == "status.ros4") {
            run_in_fresh_context([](cudaStream_t stream) {
                check_status_contract<Solver_ROS4, 4>("ros4", stream);
            });
        }
        else if (route == "workspace_alias") {
            run_in_fresh_context([](cudaStream_t stream) {
                check_driver_leaves(stream, true);
            });
        }
#define RUN_ROUTE(NET, SOLVER, INDEX, VARIANT)                              \
        else if (route == kAuthorities[INDEX].name) {                       \
            run_in_fresh_context([](cudaStream_t stream) {                 \
                run_route<NET, SOLVER>(kAuthorities[INDEX], VARIANT,       \
                                       stream);                            \
            });                                                            \
        }
        RUN_ROUTE(NetAprox13, Solver_BE_NR, 0, 0)
        RUN_ROUTE(NetAprox13, Solver_BD,    1, 0)
        RUN_ROUTE(NetAprox13, Solver_ROS4,  2, 0)
        RUN_ROUTE(NetAprox19, Solver_BE_NR, 3, 1)
        RUN_ROUTE(NetAprox19, Solver_BD,    4, 1)
        RUN_ROUTE(NetAprox19, Solver_ROS4,  5, 1)
        RUN_ROUTE(NetAprox21, Solver_BE_NR, 6, 2)
        RUN_ROUTE(NetAprox21, Solver_BD,    7, 2)
        RUN_ROUTE(NetAprox21, Solver_ROS4,  8, 2)
        RUN_ROUTE(NetIso7,    Solver_BE_NR, 9, 3)
        RUN_ROUTE(NetIso7,    Solver_BD,   10, 3)
        RUN_ROUTE(NetIso7,    Solver_ROS4, 11, 3)
        else {
            throw std::runtime_error("unknown burn policy parity route: " + route);
        }
#undef RUN_ROUTE
        if (numerical_mismatches != 0)
            throw std::runtime_error("burn policy numerical mismatches: "
                                     + std::to_string(numerical_mismatches));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
