// Selected real generated network: CPU KLU / production CUDA cuDSS owner.
// This is trajectory/provider evidence, not an independent reaction-data oracle
// or a full ARCH application/AMR qualification. Physical controls are explicit.
#include "GeneratedSparseBurnFactory.h"
#include "cuda/common/DeviceAllocation.h"
#include "driver/DriverBurnPolicy.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/linalg/SparseWrap.h"
#include "physics/eos/IdealGas.h"

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

using Network = ARCH_TEST_NETWORK_TYPE;
static_assert(!BurnLimits::uses_compact_matrix(Network::ODE_NEQ));
static_assert(ARCH_HAS_KLU && ARCH_HAS_CUDSS_PROVIDER);

namespace {
using namespace arch::cuda;
namespace dispatch = arch::dispatch;
void require(bool valid, const char* message)
{
    if (!valid) throw std::runtime_error(message);
}
double relative_error(double expected, double actual)
{
    require(std::isfinite(expected) && std::isfinite(actual), "Nonfinite sparse trajectory");
    return std::abs(expected - actual) / std::max(1.0, std::abs(expected));
}

template<template<class, class, class> class Solver>
void run(dispatch::OdeSolverId id, double rho, double temperature, double interval,
         int steps, const std::vector<double>& initial, IdealGasView host_eos,
         IdealGasView device_eos, BurnConfigView cfg,
         const std::array<int, 2>& storage_cells, int pool_cells)
{
    using Cpu = Solver<Network, SparseMatrixData<Network::ODE_NEQ>, SparseKLUSolver>;
    const auto network = make_host_burn_network<Network>();
    dispatch::ResolvedExecutionPlan plan{};
    plan.ode_solver = id;
    plan.linear_solver = dispatch::LinearSolverId::CuDss;
    auto owner = testing::make_generated_sparse_burn_owner(plan, device_eos, pool_cells, nullptr);
    const int capacity = owner->capacity();
    std::uint64_t cpu_attempts = 0, cpu_rejections = 0;
    double max_field_error = 0.0, max_limiter_error = 0.0, max_evolution = 0.0;
    // A changed storage generation and a partial final pool chunk must both
    // preserve the persistent provider. Neither is a substitute for AMR tests.
    for (int cells : storage_cells) {
        const int fields = 6 + Network::NUM_SPECIES;
        std::vector<double> reference(fields * cells, 0.0), actual(reference.size());
        for (int cell = 0; cell < cells; ++cell) {
            reference[cell] = rho * (1.0 + 0.05 * cell);
            reference[4 * cells + cell] = reference[cell]
                * host_eos.get_eint_from_T(reference[cell], temperature, initial.data());
            for (int k = 0; k < Network::NUM_SPECIES; ++k)
                reference[(6 + k) * cells + cell] = initial[k];
        }
        actual = reference;
        DeviceAllocation<double> flow;
        DeviceAllocation<arch::reduction::ReductionCandidate> candidates;
        DeviceAllocation<int> statuses;
        DeviceAllocation<DeviceBurnSummary> summary;
        flow.allocate(reference.size()); candidates.allocate(cells);
        statuses.allocate(cells); summary.allocate(1);
        check_cuda(cudaMemcpy(flow.get(), reference.data(), reference.size() * sizeof(double),
                              cudaMemcpyHostToDevice), "large network initial cells");
        DeviceStateView view{flow.get(), flow.get() + cells, flow.get() + 2 * cells,
            flow.get() + 3 * cells, flow.get() + 4 * cells, flow.get() + 5 * cells,
            flow.get() + 6 * cells, cells, Network::NUM_SPECIES};
        DeviceGridView grid{};
        grid.dim = 1; grid.total_x = grid.total_size = grid.ie = cells;
        grid.total_y = grid.total_z = grid.je = grid.ke = 1;
        grid.stride_y = grid.stride_z = cells;
        for (int step = 0; step < steps; ++step) {
            const auto cpu_start = std::chrono::steady_clock::now();
            std::vector<double> old_host_energy(cells), old_device_energy(cells);
            for (int cell = 0; cell < cells; ++cell) {
                old_host_energy[cell] = reference[4 * cells + cell];
                old_device_energy[cell] = actual[4 * cells + cell];
            }
            const double dt = interval / steps;
            const auto attempts_before = cpu_attempts, rejections_before = cpu_rejections;
            double limiter = DriverBurn::INACTIVE_LIMITER_CANDIDATE;
            for (int cell = 0; cell < cells; ++cell) {
                FluidVector fluid{reference[cell], 0.0, 0.0, 0.0, reference[4 * cells + cell]};
                std::vector<double> state(Network::ODE_NEQ, 0.0);
                for (int k = 0; k < Network::NUM_SPECIES; ++k)
                    state[k] = reference[(6 + k) * cells + cell];
                const auto prepared = DriverBurn::prepare_burn_cell(
                    fluid, state.data(), Network::NUM_SPECIES, host_eos, cfg);
                require(prepared.disposition == DriverBurn::BurnCellDisposition::Ready,
                        "CPU large-network preparation failed");
                double next_dt = dt;
                const auto report = Cpu::integrate_report(state.data(), fluid.rho,
                    dt, host_eos, cfg, next_dt, network);
                require(report.success(), "CPU KLU real-network trajectory failed");
                cpu_attempts += report.attempted_substeps;
                cpu_rejections += report.rejected_substeps;
                const auto handoff = DriverBurn::compute_burn_energy_handoff(
                    fluid, state.data(), Network::NUM_SPECIES, prepared.internal_energy,
                    prepared.kinetic_energy, dt, host_eos, cfg, report.energy_change);
                require(handoff.valid, "CPU KLU real-network energy handoff failed");
                DriverBurn::commit_burn_energy(fluid, handoff);
                reference[4 * cells + cell] = fluid.eng;
                reference[5 * cells + cell] = handoff.enuc_rate;
                limiter = DriverBurn::combine_burn_minimum(limiter, handoff.limiter_candidate);
                for (int k = 0; k < Network::NUM_SPECIES; ++k)
                    reference[(6 + k) * cells + cell] = state[k];
            }
            std::cout << "cpu_step," << static_cast<int>(id) << ',' << cells << ',' << step
                      << ',' << cpu_attempts - attempts_before << ',' << cpu_rejections - rejections_before
                      << ',' << std::chrono::duration<double>(std::chrono::steady_clock::now() - cpu_start).count() << '\n';
            const auto gpu_start = std::chrono::steady_clock::now();
            const auto counters = owner->execute(view, grid, dt, cfg,
                candidates.get(), statuses.get(), summary.get());
            check_cuda(cudaMemcpy(actual.data(), flow.get(), actual.size() * sizeof(double),
                                  cudaMemcpyDeviceToHost), "large network evolved cells");
            DeviceBurnSummary result;
            check_cuda(cudaMemcpy(&result, summary.get(), sizeof(result), cudaMemcpyDeviceToHost),
                       "large network summary");
            if (result.status != 0 || result.failed_cells != 0)
                std::cerr << "SPARSE_BURN_FAILURE ode=" << static_cast<int>(id)
                    << " cells=" << cells << " step=" << step << " status=" << result.status
                    << " failed_cells=" << result.failed_cells << " kernels=" << counters.kernels
                    << " synchronizations=" << counters.synchronizations << '\n';
            require(result.status == 0 && result.failed_cells == 0,
                    "GPU cuDSS real-network trajectory failed");
            require(counters.kernels > 0 && owner->capacity() == capacity,
                    "Large-network sparse owner did not preserve its bounded pool");
            std::cout << "gpu_step," << static_cast<int>(id) << ',' << cells << ',' << step
                      << ',' << counters.kernels << ',' << counters.synchronizations
                      << ',' << std::chrono::duration<double>(std::chrono::steady_clock::now() - gpu_start).count() << '\n';
            for (std::size_t index = 0; index < actual.size(); ++index) {
                const double error = relative_error(reference[index], actual[index]);
                max_field_error = std::max(max_field_error, error);
                if (error > 2.e-10) {
                    std::cerr << std::setprecision(17) << "ODE " << static_cast<int>(id)
                              << " step " << step << " cells " << cells << " field "
                              << index / cells << " CPU " << reference[index] << " GPU "
                              << actual[index] << " relative " << error << '\n';
                    const int cell = index % cells;
                    for (bool device : {false, true}) {
                        const auto& values = device ? actual : reference;
                        std::vector<double> composition(Network::NUM_SPECIES);
                        for (int k = 0; k < Network::NUM_SPECIES; ++k)
                            composition[k] = values[(6 + k) * cells + cell];
                        const double density = values[cell];
                        const double energy = values[4 * cells + cell] / density;
                        const double temperature = host_eos.get_temperature(density, energy, composition.data());
                        std::cerr << "THERMAL_DIAGNOSTIC " << (device ? "gpu_fields_host_eos" : "cpu")
                                  << " old_total=" << (device ? old_device_energy[cell] : old_host_energy[cell])
                                  << " new_total=" << values[4 * cells + cell]
                                  << " recovered_temperature=" << temperature
                                  << " cv=" << host_eos.get_cv(density, temperature, composition.data())
                                  << " sum_x=" << std::accumulate(composition.begin(), composition.end(), 0.0) << '\n';
                    }
                }
                require(error <= 2.e-10, "KLU/cuDSS generated cell-field parity failed");
            }
            max_limiter_error = std::max(max_limiter_error, relative_error(limiter, result.limiter));
            require(max_limiter_error <= 2.e-8, "KLU/cuDSS burn limiter parity failed");
            for (int cell = 0; cell < cells; ++cell) {
                double sum = 0.0;
                for (int k = 0; k < Network::NUM_SPECIES; ++k) {
                    const double x = actual[(6 + k) * cells + cell];
                    require(x >= -10.0 * cfg.smallx, "Negative evolved abundance");
                    sum += x;
                    max_evolution = std::max(max_evolution, std::abs(x - initial[k]));
                }
                require(std::abs(sum - 1.0) <= 2.e-10, "Generated abundance closure failed");
            }
        }
        std::cout << "state," << static_cast<int>(id) << ',' << cells;
        for (int k = 0; k < fields; ++k) std::cout << ',' << actual[k * cells];
        std::cout << '\n';
    }
    require(max_evolution > 64.0 * std::numeric_limits<double>::epsilon(),
            "Trajectory did not measurably evolve; it cannot qualify a burn route");
    std::cout << "metrics," << static_cast<int>(id) << ',' << cpu_attempts << ','
              << cpu_rejections << ',' << max_field_error << ',' << max_limiter_error
              << ',' << max_evolution << ',' << capacity << ',' << owner->workspace_bytes_per_lane() << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 8) throw std::invalid_argument(
            "usage: generated_sparse_burn rho temperature interval cv rtol steps "
            "[--ode name] [--storage-cells first second] [--pool-cells count] species=fraction ...");
        const double rho = std::stod(argv[1]), temperature = std::stod(argv[2]);
        const double interval = std::stod(argv[3]), cv = std::stod(argv[4]), rtol = std::stod(argv[5]);
        const int steps = std::stoi(argv[6]);
        for (double value : {rho, temperature, interval, cv, rtol})
            require(std::isfinite(value) && value > 0.0, "Invalid physical or tolerance control");
        require(steps > 0, "Trajectory needs a positive external step count");
        SpeciesManager species;
        Network::RegisterSpecies(species);
        std::vector<double> initial(Network::NUM_SPECIES, 0.0);
        std::vector<bool> assigned(Network::NUM_SPECIES, false);
        std::optional<dispatch::OdeSolverId> selected_ode;
        std::array<int, 2> storage_cells{2, 3};
        int pool_cells = 2;
        bool storage_selected = false, pool_selected = false;
        const auto positive_integer = [](const char* text) {
            std::size_t end = 0;
            const std::string value(text);
            const int result = std::stoi(value, &end);
            require(end == value.size() && result > 0, "Invalid positive cell count");
            return result;
        };
        for (int arg = 7; arg < argc; ++arg) {
            const std::string entry(argv[arg]);
            if (entry == "--storage-cells") {
                require(!storage_selected && arg + 2 < argc, "Missing or repeated storage selection");
                storage_cells[0] = positive_integer(argv[++arg]);
                storage_cells[1] = positive_integer(argv[++arg]);
                storage_selected = true;
                continue;
            }
            if (entry == "--pool-cells") {
                require(!pool_selected && arg + 1 < argc, "Missing or repeated pool selection");
                pool_cells = positive_integer(argv[++arg]);
                pool_selected = true;
                continue;
            }
            if (entry == "--ode") {
                require(!selected_ode && ++arg < argc, "Missing or repeated ODE selection");
                const auto parsed = dispatch::parse_registered_policy<dispatch::OdeSolverPolicies>(argv[arg]);
                require(parsed.ok && parsed.value != dispatch::OdeSolverId::None, "Unknown active ODE policy");
                selected_ode = parsed.value;
                continue;
            }
            const auto separator = entry.find('=');
            require(separator != std::string::npos, "Expected species=fraction");
            const int index = species.GetSpeciesID(entry.substr(0, separator));
            require(index >= 0 && !assigned[index], "Unknown or repeated species");
            initial[index] = std::stod(entry.substr(separator + 1));
            require(std::isfinite(initial[index]) && initial[index] >= 0.0, "Invalid mass fraction");
            assigned[index] = true;
        }
        require(std::abs(std::accumulate(initial.begin(), initial.end(), 0.0) - 1.0) <= 1.e-14,
                "Input mass fractions must sum to one");
        require(storage_cells[1] > storage_cells[0] && pool_cells <= storage_cells[0]
                && storage_cells[1] <= std::numeric_limits<int>::max() / (6 + Network::NUM_SPECIES),
                "Invalid or overflowing storage/pool extent");
        int devices = 0;
        const auto probe = cudaGetDeviceCount(&devices);
        if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
            || (probe == cudaSuccess && devices == 0)) return 77;
        check_cuda(probe, "large network device probe");
        std::vector<double> heat_capacity(Network::NUM_SPECIES, cv);
        DeviceAllocation<double> device_cv;
        device_cv.allocate(heat_capacity.size());
        check_cuda(cudaMemcpy(device_cv.get(), heat_capacity.data(), heat_capacity.size() * sizeof(double),
                              cudaMemcpyHostToDevice), "large network EOS coefficients");
        IdealGasView host_eos, device_eos;
        host_eos.species.Cv = heat_capacity.data();
        device_eos.species.Cv = device_cv.get();
        host_eos.species.count = device_eos.species.count = Network::NUM_SPECIES;
        BurnConfig config{};
        config.use_burn = true; config.use_nse = false;
        config.nuclearDensMin = config.nuclearTempMin = 0.0;
        config.smallx = 1.e-30; config.smallt = 1.0;
        config.odeconfig.rtol = rtol; config.odeconfig.atol = 1.e-14;
        config.odeconfig.max_substeps = 10000; config.odeconfig.initial_dt_frac = 1.0;
        const auto cfg = make_burn_config_view(config);
        std::cout << std::setprecision(17) << std::unitbuf << "controls," << Network::get_network_name()
                  << ',' << Network::ODE_NEQ << ',' << rho << ',' << temperature << ','
                  << interval << ',' << cv << ',' << rtol << ',' << steps
                  << ",selected_ode," << (selected_ode ? static_cast<int>(*selected_ode) : -1) << '\n';
        // Preserve the original transcript schema for the default matrix.
        if (storage_cells != std::array<int, 2>{2, 3} || pool_cells != 2)
            std::cout << "storage_controls," << storage_cells[0] << ','
                      << storage_cells[1] << ',' << pool_cells << '\n';
        if (!selected_ode || *selected_ode == dispatch::OdeSolverId::BeNr)
            run<Solver_BE_NR>(dispatch::OdeSolverId::BeNr, rho, temperature, interval, steps, initial, host_eos, device_eos, cfg, storage_cells, pool_cells);
        if (!selected_ode || *selected_ode == dispatch::OdeSolverId::Bd)
            run<Solver_BD>(dispatch::OdeSolverId::Bd, rho, temperature, interval, steps, initial, host_eos, device_eos, cfg, storage_cells, pool_cells);
        if (!selected_ode || *selected_ode == dispatch::OdeSolverId::Ros4)
            run<Solver_ROS4>(dispatch::OdeSolverId::Ros4, rho, temperature, interval, steps, initial, host_eos, device_eos, cfg, storage_cells, pool_cells);
        std::cout << "GENERATED_SPARSE_BURN_PARITY_PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
