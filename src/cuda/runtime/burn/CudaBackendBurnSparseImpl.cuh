/**
 * @file CudaBackendBurnSparseImpl.cuh
 * @brief Typed ownership and execution of a bounded sparse burn pool.
 *
 * Each network/EOS instantiation owns its device workspaces, weak-table owner
 * and sparse executor. It borrows the containing runtime's EOS storage and stream;
 * destruction quiesces work before retiring dependent buffers and factor state.
 * Cell preparation, ODE continuation and energy handoff use the shared authorities.
 */
#pragma once

#include "cuda/runtime/burn/CudaBackendBurnSparse.h"
#include "cuda/runtime/burn/CudaBackendBurnReduction.cuh"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/runtime/burn/CudaBurnOdeTypes.h"
#include "cuda/microphysics/SparseBurnCells.cuh"
#include "cuda/microphysics/device_network_owner.h"
#include "numerics/linalg/CsrPattern.h"

namespace arch::cuda::burn_detail {
template <class Network, template <class, class, class> class Solver, class Eos>
class TypedSparseBurnOwner final : public CudaSparseBurnOwner
{
    using Ode = SparseOdePolicy<Network, Solver>;
    using Context = typename Ode::Continuation;
    using Executor = SparseOdeBatchExecutor<Network, Solver>;
public:
    TypedSparseBurnOwner(Eos eos, int max_cells, cudaStream_t stream)
        : eos_(eos), stream_(stream)
    {
        if (max_cells <= 0) throw std::invalid_argument("Sparse burn pool requires active cells");
        check_cuda(cudaGetDevice(&device_), "sparse burn owner device");
        struct ConstructionGuard {
            int device;
            cudaStream_t stream;
            bool completed = false;
            ~ConstructionGuard() {
                if (!completed) set_device_and_quiesce_or_terminate(device, stream);
            }
        } construction{device_, stream_};
        Network network{};
        if constexpr (requires { Network::host_table_storage(); }) {
            network_owner_ = std::make_unique<DeviceNetworkOwner>(Network::host_table_storage(), stream_);
            construction_counters_.bytes_h2d += network_owner_->bytes();
            construction_counters_.synchronizations += network_owner_->synchronization_count();
            ++construction_counters_.immutable_owner_constructions;
            network = Network{network_owner_->view()};
            if (!network.valid()) throw std::invalid_argument("Sparse network table binding is invalid");
        }
        arch::linalg::CsrPatternBuilder builder(Network::ODE_NEQ);
        if constexpr (requires { Network::enumerate_jacobian_structure(builder); }) {
            Network::enumerate_jacobian_structure(builder);
        } else {
            // Compact built-ins can safely use an overcomplete symbolic pattern.
            // Large custom networks must supply declared structure: probing a
            // numeric Jacobian would incorrectly drop state-dependent zeros.
            static_assert(Network::NUM_SPECIES <= BurnLimits::MAX_SPECIES,
                          "Large sparse networks must enumerate symbolic Jacobian structure");
            for (int row = 1; row <= Network::NUM_SPECIES; ++row)
                for (int column = 1; column <= Network::NUM_SPECIES; ++column)
                    builder.set(row, column, 0.0);
        }
        OdeMath::include_burn_coupling<Network>(builder);
        pattern_ = builder.finish();
        const std::size_t nnz = pattern_.column_indices.size();
        lane_bytes_ = sizeof(Context) + sizeof(SparseBurnCellRecord) + 3 * sizeof(int)
            + sizeof(double) * (2 * static_cast<std::size_t>(Network::ODE_NEQ) + 2
                + nnz * (Ode::USES_JACOBIAN_WORKSPACE ? 2 : 1));
        int warp = 0;
        check_cuda(cudaDeviceGetAttribute(&warp, cudaDevAttrWarpSize, device_),
                   "sparse burn pool device warp width");
        std::size_t available = 0, total = 0;
        check_cuda(cudaMemGetInfo(&available, &total), "sparse burn pool memory budget");
        const std::size_t fitting = available / lane_bytes_;
        // A hardware-sized active group bounds ODE storage and is shared across
        // ALL blocks and AMR generations. The executor retains only ONE cuDSS
        // factor set; its analysis checks predicted peak memory before factoring.
        // This is not a claim that cuDSS's opaque factor memory equals lane_bytes_.
        capacity_ = static_cast<int>(std::min({static_cast<std::size_t>(max_cells),
            static_cast<std::size_t>(std::max(warp, 0)), fitting}));
        if (capacity_ <= 0) throw std::runtime_error("Insufficient device memory for one sparse burn lane");
        const auto lanes = static_cast<std::size_t>(capacity_);
        rows_.allocate(pattern_.row_offsets.size());
        columns_.allocate(nnz);
        contexts_.allocate(lanes);
        records_.allocate(lanes);
        coefficients_.allocate(lanes * nnz);
        if constexpr (Ode::USES_JACOBIAN_WORKSPACE) jacobians_.allocate(lanes * nnz);
        states_.allocate(lanes * Network::ODE_NEQ);
        solutions_.allocate(lanes * Network::ODE_NEQ);
        densities_.allocate(lanes);
        intervals_.allocate(lanes);
        requests_.allocate(lanes);
        responses_.allocate(lanes);
        eos_statuses_.allocate(lanes);
        check_cuda(cudaMemcpyAsync(rows_.get(), pattern_.row_offsets.data(),
            pattern_.row_offsets.size() * sizeof(int), cudaMemcpyHostToDevice, stream_),
            "sparse burn row structure upload");
        construction_counters_.bytes_h2d += pattern_.row_offsets.size() * sizeof(int);
        check_cuda(cudaMemcpyAsync(columns_.get(), pattern_.column_indices.data(),
            nnz * sizeof(int), cudaMemcpyHostToDevice, stream_), "sparse burn column structure upload");
        construction_counters_.bytes_h2d += nnz * sizeof(int);
        SparseOdeBatchView<Network, Solver> batch{capacity_, static_cast<int>(nnz),
            rows_.get(), columns_.get(), contexts_.get(), coefficients_.get(),
            states_.get(), densities_.get(), intervals_.get(), solutions_.get(),
            requests_.get(), responses_.get(), jacobians_.get(), eos_statuses_.get(), network};
        executor_ = std::make_unique<Executor>(batch, stream_);
        construction.completed = true;
    }

    ~TypedSparseBurnOwner() override
    {
        set_device_and_quiesce_or_terminate(device_, stream_);
        // Executor/factors are declared last and are destroyed before their
        // buffers. The containing backend stream and immutable EOS outlive us.
    }

    std::size_t workspace_bytes_per_lane() const override { return lane_bytes_; }
    int capacity() const override { return capacity_; }
    SparseBurnLaunchCounters construction_counters() const override { return construction_counters_; }

    SparseBurnLaunchCounters execute(
        DeviceStateView state, DeviceGridView grid, double dt, BurnConfigView config,
        reduction::ReductionCandidate* candidates, int* statuses, DeviceBurnSummary* summary) override
    {
        if (summary == nullptr) throw std::invalid_argument("Sparse burn summary is missing");
        const auto before_kernels = executor_->kernel_count();
        const auto before_d2h = executor_->bytes_d2h();
        const auto before_h2d = executor_->bytes_h2d();
        const auto before_sync = executor_->synchronization_count();
        execute_sparse_burn_cells(*executor_, records_.get(), state, grid, dt, eos_, config, candidates, statuses);
        reduce_burn_kernel<Eos><<<1, 1, 0, stream_>>>(
            candidates, statuses, grid.active_cell_count(), summary);
        check_cuda(cudaGetLastError(), "sparse burn summary reduction");
        const int cells = grid.active_cell_count();
        const auto chunks = static_cast<std::uint64_t>(
            cells > 0 ? (cells - 1) / capacity_ + 1 : 0);
        return {executor_->kernel_count() - before_kernels + 2 * chunks + 1,
                executor_->bytes_d2h() - before_d2h, executor_->bytes_h2d() - before_h2d,
                executor_->synchronization_count() - before_sync};
    }

private:
    Eos eos_;
    cudaStream_t stream_;
    int device_ = -1, capacity_ = 0;
    std::size_t lane_bytes_ = 0;
    SparseBurnLaunchCounters construction_counters_{};
    arch::linalg::CsrPattern pattern_;
    std::unique_ptr<DeviceNetworkOwner> network_owner_;
    DeviceAllocation<int> rows_, columns_, requests_, responses_, eos_statuses_;
    DeviceAllocation<Context> contexts_;
    DeviceAllocation<SparseBurnCellRecord> records_;
    DeviceAllocation<double> coefficients_, jacobians_, states_, solutions_, densities_, intervals_;
    std::unique_ptr<Executor> executor_;
};

template <class Network, class Eos>
struct SparseOdeOwnerVisitor
{
    Eos eos;
    int max_cells;
    cudaStream_t stream;
    std::unique_ptr<CudaSparseBurnOwner> owner;
    template <class Registration> void operator()()
    {
        using Binding = typename dispatch::PolicyRegistration<Registration>::CudaBinding;
        if constexpr (!std::is_same_v<Binding, dispatch::AbsentBinding>
                      && !std::is_same_v<Binding, dispatch::CudaNoOdeBinding>) {
            using Mapping = OdeType<Binding>;
            owner = std::make_unique<TypedSparseBurnOwner<Network, Mapping::template solver, Eos>>(
                eos, max_cells, stream);
        }
    }
};
} // namespace arch::cuda::burn_detail

namespace arch::cuda {
template <class Network, class Eos>
std::unique_ptr<CudaSparseBurnOwner> make_sparse_burn_owner_for_network(
    const dispatch::ResolvedExecutionPlan& plan, Eos eos, int max_cells, cudaStream_t stream)
{
    if (plan.linear_solver != dispatch::LinearSolverId::CuDss)
        throw std::invalid_argument("Sparse CUDA burn owner requires an explicit resolved cuDSS policy");
    burn_detail::SparseOdeOwnerVisitor<Network, Eos> visitor{eos, max_cells, stream, {}};
    if (!dispatch::visit_policy<dispatch::OdeSolverPolicies>(plan.ode_solver, visitor) || !visitor.owner)
        throw std::invalid_argument("Sparse CUDA burn owner has no registered ODE binding");
    return std::move(visitor.owner);
}
} // namespace arch::cuda
