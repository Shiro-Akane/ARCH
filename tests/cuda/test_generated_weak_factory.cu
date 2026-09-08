// Selected-package production launch/owner contract. This does not replace an
// independent scientific trajectory or the complete application/AMR tests.
#include "cuda/runtime/burn/CudaBackendBurnImpl.cuh"
#if ARCH_HAS_CUDSS_PROVIDER
#include "cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh"
#endif
#include <array>
#include <iomanip>
#include <iostream>
#include <vector>

using Network = ARCH_TEST_NETWORK_TYPE;
static_assert(OdeMath::has_nonconservative_energy<Network>);
static_assert(BurnLimits::uses_compact_matrix(Network::ODE_NEQ));
namespace {
using namespace arch::cuda;
namespace dispatch = arch::dispatch;
struct ThermalControl {
    double cv;
    ARCH_HOST_DEVICE double get_eta(double, double, const double*) const { return 0.0; }
    ARCH_HOST_DEVICE double get_cv(double, double, const double*) const { return cv; }
    ARCH_HOST_DEVICE double get_eint_from_T(double, double t, const double*) const { return cv * t; }
    ARCH_HOST_DEVICE double get_temperature(double, double e, const double*) const { return e / cv; }
};
void require(bool valid, const char* message)
{
    if (!valid) throw std::runtime_error(message);
}
bool close(double a, double b)
{
    return std::isfinite(a) && std::isfinite(b)
        && std::abs(a - b) <= 2.e-10 * std::max(1.0, std::abs(a));
}
void verify_storage_contract()
{
    const auto host = Network::host_table_storage();
    cudaStream_t stream = nullptr;
    check_cuda(cudaStreamCreate(&stream), "weak owner test stream");
    try {
        DeviceNetworkOwner owner(host, stream);
        require(owner.bytes() == host.size * sizeof(double) && owner.synchronization_count() == 1,
                "Weak owner omitted its completed immutable upload/fence accounting");
        require(owner.bound_to(host, stream) && !owner.bound_to(host, nullptr)
            && !owner.bound_to({host.data, host.size - 1}, stream),
            "Weak owner failed its stream/extent identity contract");
        const double invalid = std::numeric_limits<double>::quiet_NaN();
        for (auto input : {arch::network::WeakTableStorageView{},
                           arch::network::WeakTableStorageView{&invalid, 1},
                           arch::network::WeakTableStorageView{host.data,
                               std::numeric_limits<std::size_t>::max()}}) {
            bool rejected = false;
            try { DeviceNetworkOwner bad(input, stream); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Weak owner accepted empty/nonfinite/overflowing data");
        }
    } catch (...) {
        require_cuda_success_or_terminate(cudaStreamDestroy(stream));
        throw;
    }
    check_cuda(cudaStreamDestroy(stream), "weak owner test stream retirement");
}
template<template<class, class, class> class Solver>
void verify_route(dispatch::OdeSolverId id, double rho, double temperature,
    double interval, ThermalControl eos, BurnConfigView cfg)
{
    dispatch::ResolvedExecutionPlan plan{};
    plan.ode_solver = id;
    plan.linear_solver = dispatch::LinearSolverId::DenseLu;
    std::unique_ptr<DeviceNetworkOwner> immutable;
    const double* table_address = nullptr;
#if ARCH_HAS_CUDSS_PROVIDER
    auto sparse_plan = plan;
    sparse_plan.linear_solver = dispatch::LinearSolverId::CuDss;
    auto sparse = make_sparse_burn_owner_for_network<Network>(sparse_plan, eos, 2, nullptr);
    const auto capacity = sparse->capacity();
    const auto construction = sparse->construction_counters();
    require(construction.immutable_owner_constructions == 1 && construction.synchronizations == 1
        && construction.bytes_h2d > Network::host_table_storage().size * sizeof(double)
        && construction.bytes_d2h == 0 && construction.kernels == 0,
        "Sparse owner construction omitted or duplicated table/pattern uploads");
#endif
    // New cell buffers, and more cells than the sparse capacity, stand in for
    // the launch boundary after regridding. Owners persist outside this loop.
    for (const int cells : {2, 3}) {
        constexpr int fields = 6 + Network::NUM_SPECIES;
        std::vector<double> original(fields * cells), expected(original.size());
        DeviceAllocation<double> flow;
        flow.allocate(original.size());
        DeviceStateView state{flow.get(), flow.get() + cells, flow.get() + 2 * cells,
            flow.get() + 3 * cells, flow.get() + 4 * cells, flow.get() + 5 * cells,
            flow.get() + 6 * cells, cells, Network::NUM_SPECIES};
        for (int cell = 0; cell < cells; ++cell) {
            BurnPolicyCell host{};
            host.fluid.rho = rho;
            host.fluid.eng = rho * eos.cv * temperature;
            host.burn_dt = interval;
            original[cell] = rho;
            original[4 * cells + cell] = host.fluid.eng;
            for (int k = 0; k < Network::NUM_SPECIES; ++k)
                original[(6 + k) * cells + cell] = host.state[k] = 1.0 / Network::NUM_SPECIES;
            BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ> workspace{};
            execute_burn_policy_cell<Network, Solver>(host, workspace, eos, cfg, Network::host_view());
            require(host.ode.success() && host.interior_effect.interior_written,
                "Host weak cell control did not complete");
            expected[cell] = host.fluid.rho;
            expected[4 * cells + cell] = host.fluid.eng;
            expected[5 * cells + cell] = host.enuc_rate;
            for (int k = 0; k < Network::NUM_SPECIES; ++k)
                expected[(6 + k) * cells + cell] = host.state[k];
            require(host.enuc_rate < 0.0, "Weak control did not cool");
        }
        DeviceGridView grid{};
        grid.total_x = grid.total_size = grid.ie = cells;
        grid.total_y = grid.total_z = grid.je = grid.ke = 1;
        grid.stride_y = grid.stride_z = cells;
        grid.dim = 1;
        DeviceAllocation<BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>> workspaces;
        DeviceAllocation<arch::reduction::ReductionCandidate> candidates;
        DeviceAllocation<int> statuses;
        DeviceAllocation<DeviceBurnSummary> summary;
        workspaces.allocate(cells); candidates.allocate(cells);
        statuses.allocate(cells); summary.allocate(1);
        const auto dense = [&](CudaBurnArguments args) {
            return burn_detail::visit_ode_route<Network>(plan, state, grid,
                reinterpret_cast<std::byte*>(workspaces.get()), candidates.get(), statuses.get(),
                summary.get(), interval, eos, args, nullptr);
        };
        check_cuda(cudaMemcpy(flow.get(), original.data(), original.size() * sizeof(double),
            cudaMemcpyHostToDevice), "weak initial cells");
        require(dense(cfg) == cudaErrorInvalidValue, "Weak launch accepted a missing owner slot");
        std::vector<double> foreign(Network::host_table_storage().data,
            Network::host_table_storage().data + Network::TABLE_VALUE_COUNT);
        auto mismatched = std::make_unique<DeviceNetworkOwner>(
            arch::network::WeakTableStorageView{foreign.data(), foreign.size()}, nullptr);
        require(dense({cfg, &mismatched}) == cudaErrorInvalidValue,
            "Weak launch accepted an unrelated table owner");
        std::vector<double> actual(original.size());
        check_cuda(cudaMemcpy(actual.data(), flow.get(), actual.size() * sizeof(double),
            cudaMemcpyDeviceToHost), "rejected weak cells");
        require(actual == original, "Rejected owner launch mutated cell state");
        const auto verify = [&](const char* provider) {
            check_cuda(cudaMemcpy(actual.data(), flow.get(), actual.size() * sizeof(double),
                cudaMemcpyDeviceToHost), "weak final cells");
            DeviceBurnSummary result;
            check_cuda(cudaMemcpy(&result, summary.get(), sizeof(result), cudaMemcpyDeviceToHost),
                "weak summary");
            require(result.status == 0 && result.failed_cells == 0, "Weak launch reports a failed cell");
            for (std::size_t i = 0; i < actual.size(); ++i) {
                if (!close(expected[i], actual[i]))
                    std::cerr << std::setprecision(17) << "ODE " << static_cast<int>(id)
                              << " provider " << provider << " cells " << cells
                              << " field " << i / cells << ": " << expected[i] << " vs " << actual[i]
                              << " relative " << std::abs(expected[i] - actual[i])
                                  / std::max(1.0, std::abs(expected[i])) << '\n';
                require(close(expected[i], actual[i]), "Weak launch changed shared cell physics");
            }
        };
        check_cuda(dense({cfg, &immutable}), "weak dense factory launch");
        require(immutable && immutable->bound_to(Network::host_table_storage(), nullptr),
            "Dense owner did not retain the selected package binding");
        if (table_address) require(table_address == immutable->view().data,
            "Dense owner reuploaded immutable data after a grid-storage change");
        table_address = immutable->view().data;
        verify("DenseLU");
#if ARCH_HAS_CUDSS_PROVIDER
        check_cuda(cudaMemcpy(flow.get(), original.data(), original.size() * sizeof(double),
            cudaMemcpyHostToDevice), "weak sparse initial cells");
        const auto counters = sparse->execute(state, grid, interval, cfg,
            candidates.get(), statuses.get(), summary.get());
        require(counters.kernels > 0 && sparse->capacity() == capacity,
            "Sparse owner was not reused across a grid-storage change");
        verify("cuDSS");
#endif
    }
    std::cout << "weak factory ODE " << static_cast<int>(id) << ": owner reuse and cell handoff PASS\n";
}
} // namespace
int main(int argc, char** argv)
{
    try {
        if (argc != 5) throw std::invalid_argument("usage: weak_factory rho temperature interval cv");
        const double rho = std::stod(argv[1]), temperature = std::stod(argv[2]);
        const double interval = std::stod(argv[3]), cv = std::stod(argv[4]);
        for (double value : {rho, temperature, interval, cv})
            require(std::isfinite(value) && value > 0.0, "Weak factory controls must be finite and positive");
        int devices = 0;
        if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) return 77;
        verify_storage_contract();
        BurnConfig config{};
        config.use_burn = true; config.use_nse = false;
        config.nuclearDensMin = config.nuclearTempMin = 0.0;
        config.smallx = 1.e-30; config.smallt = 1.0;
        config.odeconfig.rtol = 1.e-7; config.odeconfig.atol = 1.e-14;
        config.odeconfig.initial_dt_frac = 1.0; config.odeconfig.max_substeps = 10000;
        const auto cfg = make_burn_config_view(config);
        verify_route<Solver_BE_NR>(dispatch::OdeSolverId::BeNr, rho, temperature, interval, {cv}, cfg);
        verify_route<Solver_BD>(dispatch::OdeSolverId::Bd, rho, temperature, interval, {cv}, cfg);
        verify_route<Solver_ROS4>(dispatch::OdeSolverId::Ros4, rho, temperature, interval, {cv}, cfg);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
