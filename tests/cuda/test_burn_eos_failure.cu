/** Burn's original Tabular failure boundary must survive finite recovery.
 * This is a focused production-kernel test, not a second burn implementation.
 */
#include "cuda/runtime/burn/CudaBackendBurnImpl.cuh"
#include "cuda/microphysics/SparseBurnCells.cuh"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"
#include "../fixtures/SparseTransferNetwork.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class T> struct Buffer {
    T* data = nullptr;
    explicit Buffer(std::size_t count)
    { check(cudaMalloc(reinterpret_cast<void**>(&data), count * sizeof(T))); }
    ~Buffer() { cudaFree(data); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

enum class Fault { None, Preparation, OdeHeatCapacity, Energy };
template<class View> struct RecoveringTableEos : View {
    Fault fault = Fault::None;
    ARCH_INLINE void hidden_failure() const
    {
        // All query results visible to burn remain finite. Only binding the
        // inherited REAL Tabular internal hook can expose this bad Cv jet.
        View invalid = *this;
        invalid.free_energy_fields[tabular_eos::Fyy] = invalid.free_energy_fields[tabular_eos::Fy];
        if constexpr (requires { invalid.free_energy_state(1.0, 1.0, 0.5); })
            static_cast<void>(invalid.free_energy_state(1.0, 1.0, 0.5));
        else
            static_cast<void>(invalid.free_energy_state(1.0, 1.0, 14.0, 7.0));
    }
    ARCH_INLINE double get_temperature(double, double energy, const double*) const
    {
        if (fault == Fault::Preparation) hidden_failure();
        return energy;
    }
    ARCH_INLINE double get_cv(double, double, const double*) const
    {
        if (fault == Fault::OdeHeatCapacity) hidden_failure();
        return 1.0;
    }
    ARCH_INLINE double get_eint_from_T(double, double temperature, const double*) const
    {
        if (fault == Fault::Energy) hidden_failure();
        return temperature;
    }
    ARCH_INLINE double get_eta(double, double, const double*) const { return 0.0; }
};

template<class View, class Binding>
void run(View table, const char* name)
{
    using Network = SparseTransferNetwork<2>;
    using Mapping = arch::cuda::burn_detail::OdeType<Binding>;
    using Ode = arch::cuda::SparseOdePolicy<Network, Mapping::template solver>;
    using Batch = arch::cuda::SparseOdeBatchView<Network, Mapping::template solver>;
    constexpr int cells = 2, extent = Network::ODE_NEQ, nnz = extent * extent;
    // Two cells reuse one sparse lane, so every case also checks chunk reset.
    constexpr double dt = 1.0e-3;
    const std::array<double, 8 * cells> original{
        1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        100.0, 100.0, -7.0, -7.0, 0.8, 0.8, 0.2, 0.2};
    Buffer<double> flow(original.size());
    arch::cuda::DeviceStateView state{flow.data, flow.data + cells,
        flow.data + 2 * cells, flow.data + 3 * cells, flow.data + 4 * cells,
        flow.data + 5 * cells, flow.data + 6 * cells, cells, 2};
    arch::cuda::DeviceGridView grid{};
    grid.total_x = grid.total_size = cells; grid.total_y = grid.total_z = 1;
    grid.stride_y = grid.stride_z = cells;
    grid.ie = cells; grid.je = grid.ke = 1; grid.dim = 1;
    Buffer<arch::reduction::ReductionCandidate> candidates(cells);
    Buffer<int> statuses(cells), eos_status(1), requests(1), responses(1), rows(extent + 1), columns(nnz);
    Buffer<arch::cuda::BurnOdeMatrixWorkspaceFor<extent>> dense_workspace(cells);
    Buffer<arch::cuda::SparseBurnCellRecord> records(1);
    Buffer<typename Ode::Continuation> contexts(1);
    Buffer<double> values(nnz), jacobians(nnz), packed(extent), densities(1), intervals(1), solutions(extent);
    const int host_rows[]{0, 3, 6, 9}, host_columns[]{0, 1, 2, 0, 1, 2, 0, 1, 2};
    check(cudaMemcpy(rows.data, host_rows, sizeof(host_rows), cudaMemcpyHostToDevice));
    check(cudaMemcpy(columns.data, host_columns, sizeof(host_columns), cudaMemcpyHostToDevice));
    Batch batch{1, nnz, rows.data, columns.data, contexts.data, values.data,
        packed.data, densities.data, intervals.data, solutions.data, requests.data,
        responses.data, jacobians.data, eos_status.data};
    arch::cuda::SparseOdeBatchExecutor<Network, Mapping::template solver> executor(batch, nullptr);
    BurnConfig config{};
    config.use_burn = true; config.use_nse = false;
    config.nuclearDensMin = config.nuclearTempMin = 0.0;
    config.smallt = 1.0; config.smallx = 1.0e-30;
    config.odeconfig.rtol = 1.0e-4; config.odeconfig.atol = 1.0e-8;
    config.odeconfig.initial_dt_frac = 1.0;
    const auto cfg = make_burn_config_view(config);
    RecoveringTableEos<View> eos{};
    static_cast<View&>(eos) = table;
    std::array<double, original.size()> dense_valid{};
    auto reset = [&] {
        check(cudaMemcpy(flow.data, original.data(), sizeof(original), cudaMemcpyHostToDevice));
        // Production must clear its own latch even after a previous failure.
        check(cudaMemset(statuses.data, 0x7f, cells * sizeof(int)));
        check(cudaMemset(eos_status.data, 0x7f, sizeof(int)));
    };
    auto verify = [&](bool fail, bool dense) {
        std::array<double, original.size()> actual{};
        std::array<int, cells> actual_status{};
        check(cudaMemcpy(actual.data(), flow.data, sizeof(actual), cudaMemcpyDeviceToHost));
        check(cudaMemcpy(actual_status.data(), statuses.data, sizeof(actual_status), cudaMemcpyDeviceToHost));
        for (const int disposition : actual_status)
            require(disposition == static_cast<int>(fail
                ? DriverBurn::BurnCellDisposition::SolverFailed : DriverBurn::BurnCellDisposition::Ready),
                "Burn did not preserve the EOS failure disposition");
        if (fail) require(actual == original, "Failed EOS burn committed energy/species/ENUC");
        else {
            require(actual[6 * cells] < original[6 * cells]
                && actual[7 * cells] > original[7 * cells], "Valid control did not actually burn");
            for (double value : actual) require(std::isfinite(value), "Valid burn is nonfinite");
            if (dense) dense_valid = actual;
            else for (std::size_t i = 0; i < actual.size(); ++i)
                require(std::abs(actual[i] - dense_valid[i]) < 1.0e-12,
                        "EOS latch changed valid dense/sparse physics");
        }
    };
    for (bool dense : {true, false}) {
        // Valid -> failures -> valid verifies sticky failure and reuse, without
        // depending on a production table's iterative recovery by accident.
        for (Fault fault : {Fault::None, Fault::Preparation, Fault::OdeHeatCapacity,
                            Fault::Energy, Fault::None}) {
            reset(); eos.fault = fault;
            if (dense) {
                arch::cuda::burn_detail::burn_cells_kernel<Network, Binding><<<1, cells>>>(
                    state, grid, dense_workspace.data, candidates.data, statuses.data, dt, eos, cfg, Network{});
                check(cudaGetLastError());
            } else {
                arch::cuda::execute_sparse_burn_cells(executor, records.data, state, grid,
                    dt, eos, cfg, candidates.data, statuses.data);
                typename Ode::Continuation completed{};
                check(cudaMemcpy(&completed, contexts.data, sizeof(completed), cudaMemcpyDeviceToHost));
                require(completed.report.success() == (fault == Fault::None),
                        "Sparse EOS failure retained a previous successful ODE report");
                if (fault != Fault::None)
                    require(completed.report.status == BurnOdeStatus::EosFailure,
                            "Sparse EOS failure lacks an explicit failed ODE report");
            }
            verify(fault != Fault::None, dense);
        }
    }
    // Isolate failure *only* in the final energy handoff, after a real successful
    // ODE. Earlier EOS calls are valid, so pre-commit-only checks cannot pass.
    reset();
    for (int first = 0; first < cells; ++first) {
        eos.fault = Fault::None;
        arch::cuda::sparse_burn_detail::prepare_cells<Network, Mapping::template solver><<<1, 1>>>(
            batch, records.data, state, grid, first, 1, dt, eos, cfg);
        check(cudaGetLastError());
        executor.execute(1, eos, cfg);
        typename Ode::Continuation completed{};
        check(cudaMemcpy(&completed, contexts.data, sizeof(completed), cudaMemcpyDeviceToHost));
        require(completed.report.success(), "Handoff fixture never completed its real ODE");
        eos.fault = Fault::Energy;
        arch::cuda::sparse_burn_detail::commit_cells<Network, Mapping::template solver><<<1, 1>>>(
            batch, records.data, state, grid, first, 1, dt, eos, cfg, candidates.data, statuses.data);
        check(cudaGetLastError());
        check(cudaMemcpy(&completed, contexts.data, sizeof(completed), cudaMemcpyDeviceToHost));
        require(!completed.report.success() && completed.report.status == BurnOdeStatus::EosFailure,
                "Post-ODE EOS handoff failure retained a successful ODE report");
    }
    verify(true, false);
    // A caller cannot silently use a hook-bearing EOS without its global latch.
    Batch incomplete = batch; incomplete.eos_statuses = nullptr;
    arch::cuda::SparseOdeBatchExecutor<Network, Mapping::template solver> unchecked(incomplete, nullptr);
    bool rejected = false;
    try { unchecked.execute(1, eos, cfg); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Table EOS accepted a sparse pool without failure storage");
    std::cout << name << ": dense/sparse EOS failure no-commit and valid reuse passed\n";
}

template<class View> void test_table(View view, int knots, const char* name)
{
    const double jet[tabular_eos::FieldCount]{3.0, 2.0, 0.0, 0.0, 2.0, -3.0, 0.0, 0.0, 0.0};
    std::vector<double> fields(tabular_eos::FieldCount * knots);
    for (int field = 0; field < tabular_eos::FieldCount; ++field)
        std::fill_n(fields.data() + field * knots, knots, jet[field]);
    Buffer<double> device_fields(fields.size());
    check(cudaMemcpy(device_fields.data, fields.data(), fields.size() * sizeof(double), cudaMemcpyHostToDevice));
    view.uses_free_energy = true;
    for (int field = 0; field < tabular_eos::FieldCount; ++field)
        view.free_energy_fields[field] = device_fields.data + field * knots;
    run<View, arch::dispatch::CudaBeNrBinding>(view, name);
    run<View, arch::dispatch::CudaBdBinding>(view, name);
    run<View, arch::dispatch::CudaRos4Binding>(view, name);
}
} // namespace

int main()
{
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) return 77;
    if (probe != cudaSuccess) {
        std::cerr << cudaGetErrorString(probe) << '\n';
        return 1;
    }
    try {
        Tabular3DEOSView table3{};
        table3.n_rho = table3.n_T = table3.n_X = 2;
        table3.log_rho_max = table3.log_T_max = 1.0;
        table3.dlog_rho = table3.dlog_T = table3.dX = 1.0;
        table3.X_max = 1.0; table3.target_species_id = -1;
        test_table(table3, 8, "Tabular3");
        Tabular4DEOSView table4{};
        table4.n_rho = table4.n_T = table4.n_A = table4.n_Z = 2;
        table4.log_rho_max = table4.log_T_max = 1.0;
        table4.dlog_rho = table4.dlog_T = table4.dA = table4.dZ = 1.0;
        table4.A_min = 14.0; table4.A_max = 15.0;
        table4.Z_min = 7.0; table4.Z_max = 8.0;
        test_table(table4, 16, "Tabular4");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
