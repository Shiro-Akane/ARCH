// Test-only shadow diagnosis: same generated network / shared BD continuation.
// Alternate KLU policies below are NOT production fixes or qualification runs.
#include "driver/DriverBurnPolicy.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/linalg/SparseWrap.h"
#include "physics/eos/IdealGas.h"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/microphysics/CuDssSparseSolver.h"
#include "numerics/linalg/CsrMatrixView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Network = ARCH_TEST_NETWORK_TYPE;

struct ObservedKlu {
    static inline std::string mode;
    static inline int solves = 0, factors = 0;
    static inline long double worst = 0;

    template<int N, int M>
    static bool factorize(SparseMatrixData<N>& matrix, int (&pivots)[M]) {
        ++factors;
        if (mode == "fresh" && matrix.numeric != nullptr)
            klu_free_numeric(&matrix.numeric, &matrix.common);
        return SparseKLUSolver::factorize<N, M>(matrix, pivots);
    }

    template<int N>
    static std::array<double, N> residual(const SparseMatrixData<N>& matrix,
                                        const double* rhs, const double* x) {
        std::array<long double, N> product{}, scale{};
        for (int i = 0; i < N; ++i) scale[i] = std::abs(static_cast<long double>(rhs[i]));
        for (std::size_t k = 0; k < matrix.values.size(); ++k) {
            const auto term = static_cast<long double>(matrix.values[k]) * x[matrix.columns[k]];
            product[matrix.rows[k]] += term;
            scale[matrix.rows[k]] += std::abs(term);
        }
        std::array<double, N> result{};
        for (int i = 0; i < N; ++i) {
            const long double r = static_cast<long double>(rhs[i]) - product[i];
            result[i] = static_cast<double>(r);
            const long double relative = scale[i] == 0 ? std::abs(r) : std::abs(r) / scale[i];
            if (!std::isfinite(relative)) throw std::runtime_error("nonfinite diagnostic residual");
            worst = std::max(worst, relative);
        }
        return result;
    }

    template<int N, int M>
    static void solve_with_factors(const SparseMatrixData<N>& matrix,
                                   const int (&pivots)[M], double* rhs) {
        ++solves;
        std::array<double, N> original{};
        std::copy(rhs, rhs + N, original.begin());
        SparseKLUSolver::solve_with_factors<N, M>(matrix, pivots, rhs);
        auto correction = residual(matrix, original.data(), rhs);
        if (mode == "refine") {
            // Diagnostic only, using the original A/b and unchanged native KLU.
            // CPU long double residuals do not become shared production math.
            for (int iteration = 0; iteration < 2; ++iteration) {
                SparseKLUSolver::solve_with_factors<N, M>(matrix, pivots, correction.data());
                for (int i = 0; i < N; ++i) rhs[i] += correction[i];
                correction = residual(matrix, original.data(), rhs);
            }
        }
    }
};

// Host network/BD arithmetic with the unchanged GPU linear provider isolates
// provider rounding from device RHS/Jacobian/continuation arithmetic. Numerical
// upload/download here is diagnostic only; production remains device-resident.
BurnOdeReport host_bd_with_cudss(double* state, double rho, double dt,
                               IdealGasView eos, BurnConfigView cfg, const Network& network) {
    using namespace arch::cuda;
    constexpr int n = Network::ODE_NEQ;
    using Ode = Solver_BD<Network, SparseMatrixData<n>, void>;
    auto context = std::make_unique<Ode::Continuation>();
    SparseMatrixData<n> jacobian, matrix;
    std::vector<int> offsets, columns, slots;
    std::vector<double> values, solution(n);
    DeviceAllocation<int> device_offsets, device_columns;
    DeviceAllocation<double> device_values, device_rhs, device_solution;
    // Declared after buffers: destroy/fence provider before releasing its views.
    std::unique_ptr<CuDssSparseSolver> provider;
    std::uint64_t token = 0;
    int residual_rejections = 0, refinement_solves = 0;
    Ode::begin(*context, state, rho, dt, cfg, dt, network);
    for (;;) {
        const auto request = Ode::advance(*context, jacobian, matrix, state, eos, cfg);
        if (request == OdeLinearRequest::Complete) break;
        bool success = true;
        if (request == OdeLinearRequest::Factorize) {
            ++ObservedKlu::factors;
            if (!provider) {
                offsets.resize(n + 1);
                for (int row = 0; row < n; ++row) {
                    for (int column = 0; column < n; ++column) {
                        const int slot = matrix.slots[static_cast<std::size_t>(column) * n + row];
                        if (slot >= 0) { columns.push_back(column); slots.push_back(slot); }
                    }
                    offsets[row + 1] = static_cast<int>(columns.size());
                }
                values.resize(slots.size());
                device_offsets.allocate(offsets.size()); device_columns.allocate(columns.size());
                device_values.allocate(values.size()); device_rhs.allocate(n); device_solution.allocate(n);
                check_cuda(cudaMemcpy(device_offsets.get(), offsets.data(), offsets.size() * sizeof(int),
                                      cudaMemcpyHostToDevice), "shadow CSR offsets");
                check_cuda(cudaMemcpy(device_columns.get(), columns.data(), columns.size() * sizeof(int),
                                      cudaMemcpyHostToDevice), "shadow CSR columns");
                provider = std::make_unique<CuDssSparseSolver>(n, static_cast<int>(values.size()),
                    device_offsets.get(), device_columns.get(), device_values.get(),
                    device_rhs.get(), device_solution.get(), nullptr);
            }
            if (slots.size() != matrix.values.size()) throw std::runtime_error("shadow pattern changed");
            for (std::size_t k = 0; k < slots.size(); ++k) values[k] = matrix.values[slots[k]];
            check_cuda(cudaMemcpy(device_values.get(), values.data(), values.size() * sizeof(double),
                                  cudaMemcpyHostToDevice), "shadow matrix");
            provider->factorize(device_values.get(), ++token).require_success();
        } else if (request == OdeLinearRequest::SolveWithFactors) {
            ++ObservedKlu::solves;
            check_cuda(cudaMemcpy(device_rhs.get(), context->b, n * sizeof(double),
                                  cudaMemcpyHostToDevice), "shadow RHS");
            provider->solve(device_rhs.get(), device_solution.get(), token).require_success();
            check_cuda(cudaMemcpy(solution.data(), device_solution.get(), n * sizeof(double),
                                  cudaMemcpyDeviceToHost), "shadow solution");
            const CsrMatrixView<n> original{offsets.data(), columns.data(), values.data(),
                                          static_cast<int>(values.size()), true};
            auto correction = ObservedKlu::residual(matrix, context->b, solution.data());
            success = original.solution_accurate(context->b, solution.data());
            if (ObservedKlu::mode == "cudss_refine") {
                for (int retry = 0; !success && retry < 2; ++retry) {
                    check_cuda(cudaMemcpy(device_rhs.get(), correction.data(), n * sizeof(double),
                                          cudaMemcpyHostToDevice), "shadow residual RHS");
                    provider->solve(device_rhs.get(), device_solution.get(), token).require_success();
                    check_cuda(cudaMemcpy(correction.data(), device_solution.get(), n * sizeof(double),
                                          cudaMemcpyDeviceToHost), "shadow correction");
                    for (int i = 0; i < n; ++i) solution[i] += correction[i];
                    ++refinement_solves;
                    correction = ObservedKlu::residual(matrix, context->b, solution.data());
                    success = original.solution_accurate(context->b, solution.data());
                }
            }
            if (success) std::copy(solution.begin(), solution.end(), context->b);
            else { ++residual_rejections; provider->invalidate(); }
        } else throw std::runtime_error("unexpected BD linear request");
        Ode::complete_linear_solve(*context, success);
    }
    std::cout << "shadow_residual_rejections," << rho << ',' << residual_rejections
              << ",refinement_solves," << refinement_solves << '\n';
    return context->report;
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("usage: diagnose_sparse_bd observe|fresh|refine|cudss|cudss_refine");
        ObservedKlu::mode = argv[1];
        if (ObservedKlu::mode != "observe" && ObservedKlu::mode != "fresh"
            && ObservedKlu::mode != "refine" && ObservedKlu::mode != "cudss"
            && ObservedKlu::mode != "cudss_refine")
            throw std::invalid_argument("unknown diagnostic mode");
        SpeciesManager species;
        Network::RegisterSpecies(species);
        std::vector<double> initial(Network::NUM_SPECIES, 0.0), cv(Network::NUM_SPECIES, 1e8);
        initial.at(species.GetSpeciesID("c12")) = 0.5;
        initial.at(species.GetSpeciesID("o16")) = 0.5;
        IdealGasView eos;
        eos.species.Cv = cv.data(); eos.species.count = Network::NUM_SPECIES;
        BurnConfig config{};
        config.use_burn = true; config.use_nse = false;
        config.nuclearDensMin = config.nuclearTempMin = 0.0;
        config.smallx = 1e-30; config.smallt = 1.0;
        config.odeconfig.rtol = 1e-7; config.odeconfig.atol = 1e-14;
        config.odeconfig.max_substeps = 10000; config.odeconfig.initial_dt_frac = 1.0;
        const auto cfg = make_burn_config_view(config);
        const auto network = make_host_burn_network<Network>();
        using Cpu = Solver_BD<Network, SparseMatrixData<Network::ODE_NEQ>, ObservedKlu>;
        std::cout << std::setprecision(17) << "scope,test-only-not-qualification\n";
        for (int cell = 0; cell < 3; ++cell) {
            ObservedKlu::solves = ObservedKlu::factors = 0; ObservedKlu::worst = 0;
            const double rho = 1e7 * (1.0 + 0.05 * cell), dt = 1e-10 / 4;
            FluidVector fluid{rho, 0, 0, 0, rho * eos.get_eint_from_T(rho, 3e9, initial.data())};
            std::vector<double> state(Network::ODE_NEQ, 0.0);
            std::copy(initial.begin(), initial.end(), state.begin());
            const auto prepared = DriverBurn::prepare_burn_cell(
                fluid, state.data(), Network::NUM_SPECIES, eos, cfg);
            if (prepared.disposition != DriverBurn::BurnCellDisposition::Ready)
                throw std::runtime_error("diagnostic preparation failed");
            double next_dt = dt;
            const auto report = ObservedKlu::mode.starts_with("cudss")
                ? host_bd_with_cudss(state.data(), rho, dt, eos, cfg, network)
                : Cpu::integrate_report(state.data(), rho, dt, eos, cfg, next_dt, network);
            if (!report.success()) throw std::runtime_error("diagnostic BD failed");
            const auto handoff = DriverBurn::compute_burn_energy_handoff(fluid, state.data(),
                Network::NUM_SPECIES, prepared.internal_energy, prepared.kinetic_energy,
                dt, eos, cfg, report.energy_change);
            if (!handoff.valid) throw std::runtime_error("diagnostic handoff failed");
            std::cout << "cell," << Network::get_network_name() << ',' << ObservedKlu::mode
                << ',' << cell << ',' << rho << ',' << handoff.enuc_rate << ',' << report.energy_change
                << ',' << report.attempted_substeps << ',' << report.rejected_substeps
                << ',' << ObservedKlu::factors << ',' << ObservedKlu::solves
                << ',' << ObservedKlu::worst << '\n';
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
