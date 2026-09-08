/** Real device RHS/Jacobian/ODE continuation -> host cuDSS API -> device resume. */
#include "cuda/microphysics/SparseOdeBatch.cuh"
#include "cuda/microphysics/SparseBurnCells.cuh"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/linalg/CsrPattern.h"
#include "numerics/linalg/DenseWrap.h"
#include "../fixtures/SparseTransferNetwork.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
constexpr bool request_protocol_roundtrip()
{
    using namespace arch::cuda::sparse_burn_detail;
    for (auto request : {OdeLinearRequest::Complete, OdeLinearRequest::FactorizeAndSolve,
                         OdeLinearRequest::Factorize, OdeLinearRequest::SolveWithFactors}) {
        for (bool rejected : {false, true}) {
            const int message = encode_request(request, rejected);
            if (message == invalid_structure_request || decode_request(message) != request
                || rejected_previous_response(message) != rejected) return false;
        }
    }
    return !rejected_previous_response(invalid_structure_request);
}
static_assert(request_protocol_roundtrip());

void check(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
void require(bool passed, const char* message)
{
    if (!passed) throw std::runtime_error(message);
}
struct Stream
{
    cudaStream_t value{};
    Stream() { check(cudaStreamCreate(&value)); }
    ~Stream() { cudaStreamDestroy(value); }
};
template <class T> struct Buffer
{
    T* value = nullptr;
    explicit Buffer(std::size_t count)
    {
        check(cudaMalloc(reinterpret_cast<void**>(&value), count * sizeof(T)));
    }
    ~Buffer() { cudaFree(value); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

template <int Species, template <class, class, class> class Solver, bool SingularRetry = false>
void run(const char* method)
{
    using Network = std::conditional_t<SingularRetry,
        SparseSingularRetryNetwork<Species>, SparseTransferNetwork<Species>>;
    using Ode = arch::cuda::SparseOdePolicy<Network, Solver>;
    // Dense is a test-only independent linear oracle for manufactured matrices
    // (including the intentionally singular first-retry fixture). Production
    // DenseLU selection remains <=31 total ODE equations.
    using Cpu = Solver<Network, DenseMatrixData<Network::ODE_NEQ>, DenseLUSolver>;
    constexpr int extent = Network::ODE_NEQ, count = 2;
    Stream stream;
    BurnConfig config{};
    config.nuclearDensMin = 0.0; config.nuclearTempMin = 0.0;
    config.smallt = 1.0; config.smallx = 1.0e-30;
    config.odeconfig.rtol = 1.0e-4; config.odeconfig.atol = 1.0e-8;
    config.odeconfig.initial_dt_frac = 1.0;
    const auto cfg = make_burn_config_view(config);
    const SparseTransferEos eos;
    std::vector<double> initial(count * extent), cpu;
    const double densities[count]{1.0, 2.0};
    const double intervals[count]{SingularRetry ? 1.0 / 1024.0 : 1.0e-3,
                                  SingularRetry ? 1.0 / 1024.0 : 2.0e-3};
    for (int lane = 0; lane < count; ++lane) {
        double sum = 0.0;
        for (int species = 0; species < Species; ++species) {
            const double value = 1.0 + 0.01 * species + 0.2 * lane;
            initial[lane * extent + species] = value; sum += value;
        }
        for (int species = 0; species < Species; ++species) initial[lane * extent + species] /= sum;
        if constexpr (SingularRetry) {
            static_assert(Species > 2);
            initial[lane * extent] = 1.0e-4 * (lane + 1.0);
            initial[lane * extent + 1] = 0.5;
            for (int species = 2; species < Species; ++species)
                initial[lane * extent + species] = (0.5 - initial[lane * extent]) / (Species - 2);
        }
        initial[lane * extent + Species] = 100.0 + lane;
    }
    cpu = initial;
    BurnOdeReport reference[count];
    double dt[count];
    for (int lane = 0; lane < count; ++lane) {
        dt[lane] = intervals[lane];
        reference[lane] = Cpu::integrate_report(cpu.data() + lane * extent,
            densities[lane], intervals[lane], eos, cfg, dt[lane]);
        require(reference[lane].success(), "CPU numerical oracle failed");
        if constexpr (SingularRetry)
            require(reference[lane].rejected_substeps > 0,
                    "Singular first matrix did not exercise CPU linear-failure retry");
    }
    arch::linalg::CsrPatternBuilder builder(extent);
    Network::eval_jacobian(initial.data(), 1.0, 0.0, builder, nullptr);
    OdeMath::include_burn_coupling<Network>(builder);
    const auto pattern = builder.finish();
    const int nnz = static_cast<int>(pattern.column_indices.size());
    if constexpr (SingularRetry) {
        std::vector<double> coefficients(nnz), rhs(extent);
        auto first_matrix = pattern.template view<extent>(coefficients.data());
        Network::eval_jacobian(initial.data(), 1.0, 0.0, first_matrix, nullptr);
        first_matrix.form_shifted_identity(-intervals[0]);
        double enuc = 0.0;
        Network::eval_rhs(initial.data(), 1.0, 0.0, rhs.data(), enuc);
        for (int column = 1; column <= extent; ++column)
            require(first_matrix(1, column) == 0.0,
                    "Singular-retry fixture's first matrix row is not exactly zero");
        require(intervals[0] * rhs[0] != 0.0,
                "Singular-retry fixture's first matrix has a compatible zero RHS");
    }
    Buffer<int> rows(pattern.row_offsets.size()), columns(nnz), requests(count), responses(count);
    Buffer<typename Ode::Continuation> contexts(count);
    Buffer<double> values(count * nnz), jacobians(count * nnz), states(count * extent), rho(count), interval(count), solutions(count * extent);
    check(cudaMemcpyAsync(rows.value, pattern.row_offsets.data(), pattern.row_offsets.size() * sizeof(int), cudaMemcpyHostToDevice, stream.value));
    check(cudaMemcpyAsync(columns.value, pattern.column_indices.data(), nnz * sizeof(int), cudaMemcpyHostToDevice, stream.value));
    check(cudaMemcpyAsync(rho.value, densities, sizeof(densities), cudaMemcpyHostToDevice, stream.value));
    check(cudaMemcpyAsync(interval.value, intervals, sizeof(intervals), cudaMemcpyHostToDevice, stream.value));
    arch::cuda::SparseOdeBatchView<Network, Solver> view{
        count, nnz, rows.value, columns.value, contexts.value, values.value,
        states.value, rho.value, interval.value, solutions.value, requests.value, responses.value,
        jacobians.value};
    arch::cuda::SparseOdeBatchExecutor<Network, Solver> executor(view, stream.value);
    for (int repeat = 0; repeat < 2; ++repeat) {
        check(cudaMemcpyAsync(states.value, initial.data(), initial.size() * sizeof(double), cudaMemcpyHostToDevice, stream.value));
        executor.execute(count, eos, cfg);
        std::vector<double> result(initial.size());
        std::vector<typename Ode::Continuation> reports(count);
        check(cudaMemcpyAsync(result.data(), states.value, result.size() * sizeof(double), cudaMemcpyDeviceToHost, stream.value));
        check(cudaMemcpyAsync(reports.data(), contexts.value, reports.size() * sizeof(typename Ode::Continuation), cudaMemcpyDeviceToHost, stream.value));
        check(cudaStreamSynchronize(stream.value));
        double max_error = 0.0;
        for (std::size_t i = 0; i < result.size(); ++i) {
            require(std::isfinite(result[i]), "GPU continuation produced nonfinite state");
            max_error = std::max(max_error, std::abs(result[i] - cpu[i]));
        }
        if (!(max_error < 1.0e-12)) {
            std::cerr << std::setprecision(17) << method << " N=" << extent
                      << " singular=" << SingularRetry << " maximum error=" << max_error << '\n';
            for (int lane = 0; lane < count; ++lane)
                std::cerr << "lane " << lane << " first state " << cpu[lane * extent]
                          << ' ' << result[lane * extent] << " attempts "
                          << reference[lane].attempted_substeps << ' '
                          << reports[lane].report.attempted_substeps << " rejects "
                          << reference[lane].rejected_substeps << ' '
                          << reports[lane].report.rejected_substeps << '\n';
        }
        require(max_error < 1.0e-12, "GPU sparse continuation differs from CPU shared ODE");
        for (int lane = 0; lane < count; ++lane) {
            require(reports[lane].report.status == reference[lane].status
                && reports[lane].report.attempted_substeps == reference[lane].attempted_substeps
                && reports[lane].report.rejected_substeps == reference[lane].rejected_substeps,
                "GPU sparse continuation changed ODE status or retries");
            require(std::abs(reports[lane].dt_recommended - dt[lane]) <= 1.0e-10 * dt[lane],
                "GPU sparse continuation changed adaptive dt recommendation");
            if constexpr (SingularRetry) {
                require(reports[lane].report.rejected_substeps > 0,
                        "GPU accepted a singular/perturbed first linear solve");
                require(result[lane * extent] > initial[lane * extent]
                    && result[lane * extent + 1] < initial[lane * extent + 1],
                    "GPU sparse continuation did not resume after its singular solve");
            } else {
                require(result[lane * extent] < initial[lane * extent]
                    && result[lane * extent + Species - 1] > initial[lane * extent + Species - 1],
                    "GPU sparse continuation did not advance the conservative transfer");
            }
        }
    }

    // Exercise the production packing/DriverBurn handoff around the same pool.
    // Five cells force three chunks through a two-lane pool, including its tail.
    constexpr int cells = 5;
    Buffer<double> flow((6 + Species) * cells);
    Buffer<arch::cuda::SparseBurnCellRecord> records(count);
    Buffer<arch::reduction::ReductionCandidate> candidates(cells);
    Buffer<int> statuses(cells);
    std::vector<double> packed_flow((6 + Species) * cells, 0.0);
    for (int cell = 0; cell < cells; ++cell) {
        const int source = cell % count;
        const double density = cell == 3 ? 0.5 : densities[source];
        const double temperature = cell == 4 ? 25.0 : initial[source * extent + Species];
        const double momentum = 0.05 * density;
        packed_flow[cell] = density;
        packed_flow[cells + cell] = momentum;
        packed_flow[4 * cells + cell] = density * temperature + 0.5 * momentum * momentum / density;
        for (int species = 0; species < Species; ++species)
            packed_flow[(6 + species) * cells + cell] = initial[source * extent + species];
    }
    packed_flow[6 * cells + 2] = -0.1; // Invalid composition must remain untouched.
    auto cell_config = config;
    cell_config.use_burn = true;
    cell_config.nuclearDensMin = 0.75;
    cell_config.nuclearTempMin = 50.0;
    const auto cell_cfg = make_burn_config_view(cell_config);
    arch::cuda::DeviceStateView flow_view{
        flow.value, flow.value + cells, flow.value + 2 * cells,
        flow.value + 3 * cells, flow.value + 4 * cells, flow.value + 5 * cells,
        flow.value + 6 * cells, cells, Species};
    arch::cuda::DeviceGridView grid{};
    grid.dim = 1; grid.is = grid.js = grid.ks = 0;
    grid.ie = cells; grid.je = grid.ke = 1;
    grid.total_x = grid.total_size = grid.stride_y = grid.stride_z = cells;
    grid.total_y = grid.total_z = 1;
    std::vector<double> expected_flow = packed_flow;
    for (int cell = 0; cell < 2; ++cell) {
        std::vector<double> ode_state(extent);
        for (int species = 0; species < Species; ++species)
            ode_state[species] = packed_flow[(6 + species) * cells + cell];
        FluidVector fluid{packed_flow[cell], packed_flow[cells + cell], 0.0, 0.0, packed_flow[4 * cells + cell]};
        const auto prepared = DriverBurn::prepare_burn_cell(fluid, ode_state.data(), Species, eos, cell_cfg);
        double next_dt = intervals[0];
        const auto result = Cpu::integrate_report(ode_state.data(), fluid.rho, intervals[0], eos, cell_cfg, next_dt);
        require(result.success(), "CPU packed burn reference failed");
        const auto handoff = DriverBurn::compute_burn_energy_handoff(fluid, ode_state.data(), Species,
            prepared.internal_energy, prepared.kinetic_energy, intervals[0], eos, cell_cfg, result.energy_change);
        require(handoff.valid, "CPU packed source-energy handoff failed");
        DriverBurn::commit_burn_energy(fluid, handoff);
        expected_flow[4 * cells + cell] = fluid.eng;
        expected_flow[5 * cells + cell] = handoff.enuc_rate;
        for (int species = 0; species < Species; ++species)
            expected_flow[(6 + species) * cells + cell] = ode_state[species];
    }
    check(cudaMemcpyAsync(flow.value, packed_flow.data(), packed_flow.size() * sizeof(double), cudaMemcpyHostToDevice, stream.value));
    arch::cuda::execute_sparse_burn_cells(executor, records.value, flow_view, grid,
        intervals[0], eos, cell_cfg, candidates.value, statuses.value);
    std::vector<double> result_flow(packed_flow.size());
    std::vector<int> cell_status(cells);
    check(cudaMemcpyAsync(result_flow.data(), flow.value, result_flow.size() * sizeof(double), cudaMemcpyDeviceToHost, stream.value));
    check(cudaMemcpyAsync(cell_status.data(), statuses.value, cell_status.size() * sizeof(int), cudaMemcpyDeviceToHost, stream.value));
    check(cudaStreamSynchronize(stream.value));
    for (std::size_t index = 0; index < result_flow.size(); ++index)
        require(std::isfinite(result_flow[index]) && std::abs(result_flow[index] - expected_flow[index]) < 1.0e-12,
                "Sparse production packing/energy handoff differs from shared CPU DriverBurn");
    require(cell_status[0] == static_cast<int>(DriverBurn::BurnCellDisposition::Ready)
        && cell_status[1] == static_cast<int>(DriverBurn::BurnCellDisposition::Ready)
        && cell_status[2] == static_cast<int>(DriverBurn::BurnCellDisposition::InvalidComposition)
        && cell_status[3] == static_cast<int>(DriverBurn::BurnCellDisposition::BelowDensity)
        && cell_status[4] == static_cast<int>(DriverBurn::BurnCellDisposition::BelowTemperature),
        "Sparse production activation/composition gates differ from CPU");

    // Force ODE termination before a single accepted substep. No fluid/species
    // bytes may be committed for a failed solve, even after successful pool reuse.
    cell_config.odeconfig.max_substeps = 0;
    check(cudaMemcpyAsync(flow.value, packed_flow.data(), packed_flow.size() * sizeof(double), cudaMemcpyHostToDevice, stream.value));
    arch::cuda::execute_sparse_burn_cells(executor, records.value, flow_view, grid,
        intervals[0], eos, make_burn_config_view(cell_config), candidates.value, statuses.value);
    check(cudaMemcpyAsync(result_flow.data(), flow.value, result_flow.size() * sizeof(double), cudaMemcpyDeviceToHost, stream.value));
    check(cudaMemcpyAsync(cell_status.data(), statuses.value, cell_status.size() * sizeof(int), cudaMemcpyDeviceToHost, stream.value));
    check(cudaStreamSynchronize(stream.value));
    require(result_flow == packed_flow, "Failed sparse burn committed state or diagnostics");
    require(cell_status[0] == static_cast<int>(DriverBurn::BurnCellDisposition::SolverFailed)
        && cell_status[1] == static_cast<int>(DriverBurn::BurnCellDisposition::SolverFailed),
        "Failed sparse burn did not report per-cell failure");
    std::cout << "GPU shared " << method << " + cuDSS: N=" << extent << ", nnz=" << nnz
              << ", lanes=" << count << " passed (pool reuse/chunks/DriverBurn/failure no-commit)\n";
}
template <int Species> void run_methods()
{
    run<Species, Solver_BE_NR>("BE_NR");
    run<Species, Solver_BD>("BD");
    run<Species, Solver_ROS4>("ROS4");
}
} // namespace

int main()
{
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) return 77;
    if (probe != cudaSuccess) {
        std::cerr << "CUDA device probe failed: " << cudaGetErrorString(probe) << '\n';
        return 1;
    }
    try {
        run_methods<31>(); run_methods<150>(); run_methods<200>();
        run<31, Solver_BE_NR, true>("BE_NR singular-factor/residual retry");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
