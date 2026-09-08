/** Production registry -> typed sparse owner -> shared network/ODE/DriverBurn.
 * This is a host C++ executable, not a second instantiation of CUDA kernels.
 * The fixed iso7 fixture exercises a short active trajectory. Custom networks
 * exercise registry/owner construction here; their physical states and full
 * trajectories are supplied explicitly by the network validation runners.
 */
#include "cuda/microphysics/device_species_owner.h"
#include "cuda/microphysics/CuDssSparseSolver.h"
#include "cuda/runtime/burn/CudaBackendBurnSparse.h"
#include "driver/DriverBurn.h"
#include "numerics/burnsolver/Networks.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/linalg/CsrPattern.h"

#include <cuda_runtime_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
namespace dispatch = arch::dispatch;
constexpr int kCells = 5;
constexpr int kMaximumLanes = 2; // Deliberately force full chunks and a tail.
constexpr double kDt = 1.0e-16;

void check(cudaError_t result)
{
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
void require(bool passed, const std::string& message)
{
    if (!passed) throw std::runtime_error(message);
}
struct Stream {
    cudaStream_t value{};
    Stream() { check(cudaStreamCreate(&value)); }
    ~Stream() { cudaStreamDestroy(value); }
};
template<class T> struct Buffer {
    T* value = nullptr;
    explicit Buffer(std::size_t count)
    {
        check(cudaMalloc(reinterpret_cast<void**>(&value), count * sizeof(T)));
    }
    ~Buffer() { cudaFree(value); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

// An epsilon-scaled contract for this short, smooth fixture, not a tolerance
// inferred from observed GPU results. Dense policy parity and independent
// burn-accuracy checks retain their separate scientific budgets.
double roundoff_budget(double expected)
{
    return 128.0 * std::numeric_limits<double>::epsilon()
        * std::max(1.0, std::abs(expected));
}
void require_close(double actual, double expected, double budget,
                   const std::string& field)
{
    require(std::isfinite(actual) && std::isfinite(expected)
        && std::abs(actual - expected) <= budget, field + " differs from CPU oracle");
}

BurnConfig make_config()
{
    BurnConfig config{};
    config.use_burn = true;
    config.use_nse = false;
    config.nuclearDensMin = 1.0;
    config.nuclearTempMin = 1.0e8;
    config.smallt = 1.0e5;
    config.smallx = 1.0e-30;
    config.enucDtFactor = 0.5;
    config.odeconfig.rtol = 1.0e-4;
    config.odeconfig.atol = 1.0e-8;
    config.odeconfig.max_newton_iter = 50;
    config.odeconfig.max_substeps = 100;
    config.odeconfig.initial_dt_frac = 1.0;
    return config;
}

// Failure-only isolation, never a substitute for the production factory result.
// All ODE/network/EOS work comes from the shared continuation on the CPU; only
// this diagnostic transports its matrix/RHS to the actual cuDSS provider.
template<class Network>
void diagnose_bd_linear_path(const std::vector<double>& initial, int cell,
                             const IdealGasView& eos, const BurnConfigView& config,
                             const std::string& name)
{
    constexpr int extent = Network::ODE_NEQ;
    using Matrix = CsrMatrixView<extent>;
    using Ode = Solver_BD<Network, Matrix, void>;
    std::vector<double> state(extent);
    for (int species = 0; species < Network::NUM_SPECIES; ++species)
        state[species] = initial[(6 + species) * kCells + cell];
    FluidVector fluid{initial[cell], initial[kCells + cell], initial[2 * kCells + cell],
        initial[3 * kCells + cell], initial[4 * kCells + cell]};
    const auto prepared = DriverBurn::prepare_burn_cell(
        fluid, state.data(), Network::NUM_SPECIES, eos, config);
    require(prepared.disposition == DriverBurn::BurnCellDisposition::Ready,
            "BD linear isolation fixture is inactive");
    arch::linalg::CsrPatternBuilder builder(extent);
    if constexpr (requires { Network::enumerate_jacobian_structure(builder); })
        Network::enumerate_jacobian_structure(builder);
    else
        for (int row = 1; row <= Network::NUM_SPECIES; ++row)
            for (int column = 1; column <= Network::NUM_SPECIES; ++column)
                builder.set(row, column, 0.0);
    OdeMath::include_burn_coupling<Network>(builder);
    const auto pattern = builder.finish();
    std::vector<double> values(pattern.column_indices.size()), jacobian_values(values.size()), solved(extent);
    auto matrix = pattern.template view<extent>(values.data());
    auto jacobian = pattern.template view<extent>(jacobian_values.data());
    Stream stream;
    Buffer<int> rows(pattern.row_offsets.size()), columns(pattern.column_indices.size());
    Buffer<double> device_values(values.size()), rhs(extent), solution(extent);
    // Synchronous diagnostic transfers keep borrowed Host storage lifetime
    // unambiguous even if an API call subsequently throws.
    check(cudaMemcpy(rows.value, pattern.row_offsets.data(), pattern.row_offsets.size() * sizeof(int), cudaMemcpyHostToDevice));
    check(cudaMemcpy(columns.value, pattern.column_indices.data(), pattern.column_indices.size() * sizeof(int), cudaMemcpyHostToDevice));
    arch::cuda::CuDssSparseSolver provider(extent, static_cast<int>(values.size()),
        rows.value, columns.value, device_values.value, rhs.value, solution.value, stream.value);
    typename Ode::Continuation context;
    Ode::begin(context, state.data(), fluid.rho, kDt, config, kDt);
    std::uint64_t token = 0;
    int sequence = 0;
    std::cerr << "BD_LINEAR_ISOLATION_BEGIN " << name << " cell=" << cell
              << " species=" << Network::NUM_SPECIES << '\n';
    for (;;) {
        const auto request = Ode::advance(context, jacobian, matrix, state.data(), eos, config);
        if (request == OdeLinearRequest::Complete) {
            std::cerr << "BD_LINEAR_ISOLATION_END status=" << static_cast<int>(context.report.status)
                      << " success=" << context.report.success() << " attempts=" << context.report.attempted_substeps
                      << " rejected=" << context.report.rejected_substeps << " requests=" << sequence << '\n';
            return;
        }
        ++sequence;
        arch::cuda::CuDssResult result;
        if (request == OdeLinearRequest::Factorize) {
            check(cudaMemcpy(device_values.value, values.data(), values.size() * sizeof(double), cudaMemcpyHostToDevice));
            result = provider.factorize(device_values.value, ++token);
        } else {
            require(request == OdeLinearRequest::SolveWithFactors, "Unexpected BD linear request");
            check(cudaMemcpy(rhs.value, context.b, extent * sizeof(double), cudaMemcpyHostToDevice));
            result = provider.solve(rhs.value, solution.value, token);
            if (result.success())
                check(cudaMemcpy(solved.data(), solution.value, extent * sizeof(double), cudaMemcpyDeviceToHost));
        }
        const bool solve = request == OdeLinearRequest::SolveWithFactors;
        const bool accurate = result.success() && (!solve || matrix.solution_accurate(context.b, solved.data()));
        if (sequence <= 12 || !accurate)
            std::cerr << "  request=" << sequence << " kind=" << static_cast<int>(request)
                      << " phase=" << static_cast<int>(context.phase) << " H=" << context.H
                      << " h=" << context.h << " k=" << context.k << " stage=" << context.stage
                      << " j=" << context.j << " token=" << token
                      << " cuda=" << result.cuda_status << " library=" << result.library_status
                      << " info=" << result.device_info << " residual_pass=" << accurate << '\n';
        if (!accurate) {
            if (solve && result.success()) {
                // Independent TEST-ONLY dense solve of the very same matrix
                // determines whether CPU success also meets the residual gate.
                DenseMatrixData<extent> dense;
                std::array<double, extent> dense_solution{};
                for (int row = 1; row <= extent; ++row) {
                    dense_solution[row - 1] = context.b[row - 1];
                    for (int column = 1; column <= extent; ++column)
                        dense.set(row, column, matrix(row, column));
                }
                const bool dense_success = DenseLUSolver::template solve<extent, extent>(dense, dense_solution.data());
                int worst_row = -1;
                double worst_ratio = -1.0;
                for (int row = 0; row < extent; ++row) {
                    double product = 0.0, scale = std::abs(context.b[row]);
                    for (int slot = pattern.row_offsets[row]; slot < pattern.row_offsets[row + 1]; ++slot) {
                        const double term = values[slot] * solved[pattern.column_indices[slot]];
                        product += term; scale += std::abs(term);
                    }
                    const double residual = std::abs(product - context.b[row]);
                    const double ratio = scale == 0.0 ? residual : residual / scale;
                    if (ratio > worst_ratio) { worst_ratio = ratio; worst_row = row; }
                }
                std::cerr << "  dense_success=" << dense_success
                          << " dense_residual_pass=" << matrix.solution_accurate(context.b, dense_solution.data())
                          << " worst_row=" << worst_row << " backward_error=" << worst_ratio << '\n';
                for (int row = 0; row < extent; ++row)
                    std::cerr << "  vector " << row << std::hexfloat << " rhs=" << context.b[row]
                              << " cudss=" << solved[row] << " dense=" << dense_solution[row]
                              << std::defaultfloat << '\n';
            }
            // Hexadecimal payload permits exact reproduction, not rounded
            // decimal reconstruction of a cancellation-sensitive failed solve.
            for (int row = 0; row < extent; ++row)
                for (int slot = pattern.row_offsets[row]; slot < pattern.row_offsets[row + 1]; ++slot)
                    std::cerr << "  csr " << row << ' ' << pattern.column_indices[slot] << ' '
                              << std::hexfloat << values[slot] << std::defaultfloat << '\n';
            std::cerr << "BD_LINEAR_ISOLATION_REJECT (production failure remains a test failure)\n";
            return;
        }
        if (solve) std::copy(solved.begin(), solved.end(), context.b);
        Ode::complete_linear_solve(context, true);
    }
}

template<class Network, template<class, class, class> class Solver>
void run_route(dispatch::NetworkId network_id, dispatch::OdeSolverId ode_id,
               const std::string& name, bool check_trajectory = true)
{
    constexpr int species = Network::NUM_SPECIES;
    using Cpu = Solver<Network, DenseMatrixData<Network::ODE_NEQ>, DenseLUSolver>;
    Stream stream;
    SpeciesManager manager;
    Network::RegisterSpecies(manager);
    require(manager.count() == species, name + ": network registration extent mismatch");
    // Timmes RegisterSpecies intentionally leaves Cv=0 for Helmholtz use. Give
    // this explicitly ideal-gas TEST fixture a positive caloric property while
    // preserving actual network names, A/Z, ordering and both EOS owners' input.
    for (auto& entry : manager.species_list) entry.Cv_ref = 1.0e8;
    IdealGas host_eos_owner(manager);
    const IdealGasView cpu_eos = host_eos_owner.get_view();
    arch::cuda::DeviceSpeciesOwner device_species(manager, stream.value);
    const auto gpu_eos = device_species.ideal_gas_view(cpu_eos.global_gamma);
    auto config = make_config();
    const auto cfg = make_burn_config_view(config);
    dispatch::ResolvedExecutionPlan plan{};
    plan.eos = dispatch::EosId::Ideal;
    plan.network = network_id;
    plan.ode_solver = ode_id;
    plan.linear_solver = dispatch::LinearSolverId::CuDss;

    auto invalid_plan = plan;
    invalid_plan.linear_solver = dispatch::LinearSolverId::DenseLu;
    bool rejected = false;
    try {
        auto wrong = arch::cuda::make_cuda_sparse_burn_owner(
            invalid_plan, gpu_eos, kMaximumLanes, stream.value);
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, name + ": sparse factory silently accepted DenseLU");

    auto owner = arch::cuda::make_cuda_sparse_burn_owner(
        plan, gpu_eos, kMaximumLanes, stream.value);
    require(owner && owner->capacity() > 0 && owner->capacity() <= kMaximumLanes
        && owner->capacity() < kCells && owner->workspace_bytes_per_lane() > 0,
        name + ": factory did not provide a bounded typed workspace");
    if (!check_trajectory) {
        // A registry binding cannot imply that an arbitrary rho/T/composition
        // lies inside this user's rate tables or produces an active burn.
        // Real custom trajectories, provider reuse and energy checks belong to
        // run_sparse_validation.py / run_weak_validation.py with explicit data.
        std::cout << "Production sparse binding " << name << ": species=" << species
                  << ", capacity=" << owner->capacity()
                  << " passed (typed owner, bounded workspace, Dense rejection; "
                     "trajectory requires explicit validation inputs)\n";
        return;
    }

    std::vector<double> initial((6 + species) * kCells, 0.0);
    std::vector<double> expected = initial;
    std::array<double, kCells> old_energy{}, new_energy{};
    std::array<BurnOdeReport, kCells> cpu_reports{};
    std::array<double, kCells> cpu_next_dt{}, cpu_signal{};
    for (int cell = 0; cell < kCells; ++cell) {
        std::vector<double> state(Network::ODE_NEQ);
        double sum = 0.0;
        for (int k = 0; k < species; ++k) {
            state[k] = (k + 1.0) * (1.0 + 1.0e-4 * ((k + cell) % 3));
            sum += state[k];
        }
        for (int k = 0; k < species; ++k) {
            state[k] /= sum;
            initial[(6 + k) * kCells + cell] = state[k];
        }
        const double rho = 1.0e6 * (1.0 + 0.01 * cell);
        const double temperature = 2.0e9 + 1.0e6 * cell;
        const double momentum = rho * 1.0e4;
        const double kinetic = 0.5 * momentum * momentum / rho;
        FluidVector fluid{rho, momentum, 0.0, 0.0,
            rho * cpu_eos.get_eint_from_T(rho, temperature, state.data()) + kinetic};
        initial[cell] = fluid.rho;
        initial[kCells + cell] = fluid.mom_u;
        initial[4 * kCells + cell] = fluid.eng;
        const auto prepared = DriverBurn::prepare_burn_cell(
            fluid, state.data(), species, cpu_eos, cfg);
        require(prepared.disposition == DriverBurn::BurnCellDisposition::Ready,
                name + ": fixture is not an active burn cell");
        old_energy[cell] = prepared.internal_energy;
        double next_dt = kDt;
        const auto report = Cpu::integrate_report(state.data(), rho, kDt,
            cpu_eos, cfg, next_dt);
        cpu_reports[cell] = report;
        cpu_next_dt[cell] = next_dt;
        require(report.success(), name + ": CPU numerical oracle failed");
        const auto handoff = DriverBurn::compute_burn_energy_handoff(
            fluid, state.data(), species, prepared.internal_energy,
            prepared.kinetic_energy, kDt, cpu_eos, cfg, report.energy_change);
        require(handoff.valid, name + ": CPU source-energy handoff failed");
        DriverBurn::commit_burn_energy(fluid, handoff);
        new_energy[cell] = handoff.new_internal_energy;
        expected[cell] = fluid.rho;
        expected[kCells + cell] = fluid.mom_u;
        expected[4 * kCells + cell] = fluid.eng;
        expected[5 * kCells + cell] = handoff.enuc_rate;
        double signal = 0.0;
        for (int k = 0; k < species; ++k) {
            expected[(6 + k) * kCells + cell] = state[k];
            signal = std::max(signal, std::abs(state[k]
                - initial[(6 + k) * kCells + cell]));
        }
        // No-op rejection is meaningful only if the prescribed fixture burns
        // well above roundoff; an inert custom network must supply a new fixture,
        // never silently pass as evidence for its production integration.
        require(signal > 8.0 * roundoff_budget(1.0),
                name + ": fixture burn signal is insufficient to reject a no-op");
        cpu_signal[cell] = signal;
    }

    Buffer<double> flow(initial.size());
    Buffer<arch::reduction::ReductionCandidate> candidates(kCells);
    Buffer<int> statuses(kCells);
    Buffer<arch::cuda::DeviceBurnSummary> summary(1);
    arch::cuda::DeviceStateView view{flow.value, flow.value + kCells,
        flow.value + 2 * kCells, flow.value + 3 * kCells, flow.value + 4 * kCells,
        flow.value + 5 * kCells, flow.value + 6 * kCells, kCells, species};
    arch::cuda::DeviceGridView grid{};
    grid.dim = 1;
    grid.ie = grid.total_x = grid.total_size = grid.stride_y = grid.stride_z = kCells;
    grid.je = grid.ke = grid.total_y = grid.total_z = 1;
    std::vector<double> actual(initial.size());
    std::array<int, kCells> cell_status{};
    arch::cuda::DeviceBurnSummary result{};
    for (int repeat = 0; repeat < 2; ++repeat) {
        check(cudaMemcpyAsync(flow.value, initial.data(), initial.size() * sizeof(double),
            cudaMemcpyHostToDevice, stream.value));
        const auto counters = owner->execute(view, grid, kDt, cfg,
            candidates.value, statuses.value, summary.value);
        check(cudaMemcpyAsync(actual.data(), flow.value, actual.size() * sizeof(double),
            cudaMemcpyDeviceToHost, stream.value));
        check(cudaMemcpyAsync(cell_status.data(), statuses.value, sizeof(cell_status),
            cudaMemcpyDeviceToHost, stream.value));
        check(cudaMemcpyAsync(&result, summary.value, sizeof(result),
            cudaMemcpyDeviceToHost, stream.value));
        check(cudaStreamSynchronize(stream.value));
        if (result.status != 0 || result.failed_cells != 0) {
            std::cerr << name << ": repeat=" << repeat << " summary_status=" << result.status
                      << " failed_cells=" << result.failed_cells << " limiter=" << result.limiter
                      << " capacity=" << owner->capacity() << " kernels=" << counters.kernels
                      << " syncs=" << counters.synchronizations
                      << " bytes_h2d=" << counters.bytes_h2d << " bytes_d2h=" << counters.bytes_d2h << '\n';
            for (int cell = 0; cell < kCells; ++cell) {
                double signal = 0.0;
                for (int species_index = 0; species_index < species; ++species_index)
                    signal = std::max(signal, std::abs(actual[(6 + species_index) * kCells + cell]
                        - initial[(6 + species_index) * kCells + cell]));
                std::cerr << "  cell=" << cell << " lane=" << cell % owner->capacity()
                          << " disposition=" << cell_status[cell] << " rho=" << initial[cell]
                          << " cpu_status=" << static_cast<int>(cpu_reports[cell].status)
                          << " cpu_attempts=" << cpu_reports[cell].attempted_substeps
                          << " cpu_rejections=" << cpu_reports[cell].rejected_substeps
                          << " cpu_next_dt=" << cpu_next_dt[cell]
                          << " cpu_signal=" << cpu_signal[cell] << " gpu_signal=" << signal
                          << " gpu_delta_energy=" << actual[4 * kCells + cell] - initial[4 * kCells + cell]
                          << " gpu_enuc=" << actual[5 * kCells + cell] << '\n';
            }
            if constexpr (std::is_same_v<Cpu,
                Solver_BD<Network, DenseMatrixData<Network::ODE_NEQ>, DenseLUSolver>>) {
                const auto failed = std::find(cell_status.begin(), cell_status.end(),
                    static_cast<int>(DriverBurn::BurnCellDisposition::SolverFailed));
                if (failed != cell_status.end())
                    diagnose_bd_linear_path<Network>(initial,
                        static_cast<int>(failed - cell_status.begin()), cpu_eos, cfg, name);
            }
        }
        require(result.status == 0 && result.failed_cells == 0,
                name + ": production summary reports a failed cell");
        require(counters.kernels > 0 && counters.bytes_d2h > 0
            && counters.bytes_h2d > 0 && counters.synchronizations > 0,
            name + ": sparse continuation activity was not reported");
        for (int cell = 0; cell < kCells; ++cell) {
            require(cell_status[cell] == static_cast<int>(DriverBurn::BurnCellDisposition::Ready),
                    name + ": factory skipped or failed an active cell");
            double composition_sum = 0.0, signal = 0.0;
            for (int component = 0; component < 6 + species; ++component) {
                const auto index = component * kCells + cell;
                // ENUC is a subtraction of two energies, then divided by dt.
                // Propagate the energy roundoff bound instead of demanding a
                // spuriously tight relative tolerance on cancellation's result.
                const double budget = component == 5
                    ? (roundoff_budget(old_energy[cell]) + roundoff_budget(new_energy[cell])) / kDt
                    : roundoff_budget(expected[index]);
                require_close(actual[index], expected[index], budget,
                    name + ": cell " + std::to_string(cell) + " field " + std::to_string(component));
                if (component <= 3)
                    require(actual[index] == initial[index],
                            name + ": burn changed density or momentum");
                if (component >= 6) {
                    composition_sum += actual[index];
                    signal = std::max(signal, std::abs(actual[index] - initial[index]));
                }
            }
            require_close(composition_sum, 1.0, species * roundoff_budget(1.0),
                          name + ": mass fraction normalization");
            require(signal > 8.0 * roundoff_budget(1.0), name + ": GPU produced a no-op");
        }
        require(std::isfinite(result.limiter) && result.limiter > 0.0,
                name + ": invalid global burn limiter");
    }

    // A zero accepted-step budget must fail every active cell without committing
    // species, energy or ENUC, including after successful owner/factor reuse.
    config.odeconfig.max_substeps = 0;
    check(cudaMemcpyAsync(flow.value, initial.data(), initial.size() * sizeof(double),
        cudaMemcpyHostToDevice, stream.value));
    owner->execute(view, grid, kDt, make_burn_config_view(config),
        candidates.value, statuses.value, summary.value);
    check(cudaMemcpyAsync(actual.data(), flow.value, actual.size() * sizeof(double),
        cudaMemcpyDeviceToHost, stream.value));
    check(cudaMemcpyAsync(cell_status.data(), statuses.value, sizeof(cell_status),
        cudaMemcpyDeviceToHost, stream.value));
    check(cudaMemcpyAsync(&result, summary.value, sizeof(result),
        cudaMemcpyDeviceToHost, stream.value));
    check(cudaStreamSynchronize(stream.value));
    require(actual == initial && result.failed_cells == kCells,
            name + ": failed production burn committed state or lost failures");
    for (const int status : cell_status)
        require(status == static_cast<int>(DriverBurn::BurnCellDisposition::SolverFailed),
                name + ": missing per-cell failure disposition");
    std::cout << "Production sparse factory " << name << ": species=" << species
              << ", cells=" << kCells << ", capacity=" << owner->capacity()
              << " passed (active burn, CPU parity, pool reuse, no-commit, Dense rejection)\n";
}

struct CustomNetworkVisitor {
    int& routes;
    template<class Registration> void operator()()
    {
        using RegistrationData = dispatch::PolicyRegistration<Registration>;
        using Binding = typename RegistrationData::CudaBinding;
        constexpr bool builtin = RegistrationData::id == dispatch::NetworkId::None
            || RegistrationData::id == dispatch::NetworkId::Iso7
            || RegistrationData::id == dispatch::NetworkId::Aprox13
            || RegistrationData::id == dispatch::NetworkId::Aprox19
            || RegistrationData::id == dispatch::NetworkId::Aprox21;
        if constexpr (!builtin && !std::is_same_v<Binding, dispatch::AbsentBinding>) {
            using Network = typename CpuNetworkType<typename RegistrationData::CpuBinding>::type;
            run_route<Network, Solver_BE_NR>(RegistrationData::id, dispatch::OdeSolverId::BeNr,
                std::string(RegistrationData::names[0]) + ".be_nr", false);
            run_route<Network, Solver_BD>(RegistrationData::id, dispatch::OdeSolverId::Bd,
                std::string(RegistrationData::names[0]) + ".bd", false);
            run_route<Network, Solver_ROS4>(RegistrationData::id, dispatch::OdeSolverId::Ros4,
                std::string(RegistrationData::names[0]) + ".ros4", false);
            ++routes;
        }
    }
};
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
        run_route<NetIso7, Solver_BE_NR>(dispatch::NetworkId::Iso7, dispatch::OdeSolverId::BeNr, "iso7.be_nr");
        run_route<NetIso7, Solver_BD>(dispatch::NetworkId::Iso7, dispatch::OdeSolverId::Bd, "iso7.bd");
        run_route<NetIso7, Solver_ROS4>(dispatch::NetworkId::Iso7, dispatch::OdeSolverId::Ros4, "iso7.ros4");
        int custom_routes = 0;
        CustomNetworkVisitor visitor{custom_routes};
        constexpr auto networks = dispatch::make_policy_descriptors<dispatch::NetworkPolicies>();
        for (const auto& descriptor : networks)
            require(dispatch::visit_policy<dispatch::NetworkPolicies>(descriptor.id, visitor),
                    "Registered network could not be visited");
        std::cout << "Coverage: builtin Iso7 all 3 ODEs; " << custom_routes
                  << " configured CUDA custom networks with all 3 ODEs (Ideal EOS factory smoke).\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
