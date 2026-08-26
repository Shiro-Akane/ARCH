#include "CudaBackend.h"

#include "amr/Block.h"
#include "amr/BoundaryPlan.h"
#include "cuda/diffusion/DiffusionSolver.cuh"
#include "cuda/hydro/Boundary.cuh"
#include "cuda/hydro/HydroIntegratorPolicies.cuh"
#include "cuda/microphysics/helm_eos_loader.h"
#include "cuda/microphysics/microphysics_api.h"
#include "grid/GridMetrics.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"
#include "physics/species/Species.h"

#include <cuda_runtime.h>

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace arch::cuda {
namespace {

[[noreturn]] void throw_cuda(cudaError_t error, const char* operation)
{
    throw std::runtime_error(
        std::string(operation) + ": " + cudaGetErrorString(error));
}

void check_cuda(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess) throw_cuda(error, operation);
}

template <class T>
class DeviceAllocation {
public:
    DeviceAllocation() = default;
    ~DeviceAllocation() { release(); }
    DeviceAllocation(const DeviceAllocation&) = delete;
    DeviceAllocation& operator=(const DeviceAllocation&) = delete;
    DeviceAllocation(DeviceAllocation&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)),
          count_(std::exchange(other.count_, 0))
    {
    }
    DeviceAllocation& operator=(DeviceAllocation&& other) noexcept
    {
        if (this != &other) {
            release();
            pointer_ = std::exchange(other.pointer_, nullptr);
            count_ = std::exchange(other.count_, 0);
        }
        return *this;
    }

    void allocate(std::size_t count)
    {
        if (pointer_ != nullptr || count == 0
            || count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::invalid_argument("invalid CUDA allocation extent");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&pointer_),
                              count * sizeof(T)),
                   "cudaMalloc");
        count_ = count;
    }

    T* get() const noexcept { return pointer_; }
    std::size_t size() const noexcept { return count_; }

private:
    void release() noexcept
    {
        if (pointer_ != nullptr) static_cast<void>(cudaFree(pointer_));
        pointer_ = nullptr;
        count_ = 0;
    }

    T* pointer_ = nullptr;
    std::size_t count_ = 0;
};

class StreamOwner {
public:
    StreamOwner() = default;
    ~StreamOwner()
    {
        if (stream_ != nullptr) {
            static_cast<void>(cudaSetDevice(device_ordinal_));
            static_cast<void>(cudaStreamSynchronize(stream_));
            static_cast<void>(cudaStreamDestroy(stream_));
        }
    }
    StreamOwner(const StreamOwner&) = delete;
    StreamOwner& operator=(const StreamOwner&) = delete;

    void create(int device_ordinal)
    {
        if (stream_ != nullptr)
            throw std::logic_error("CUDA stream already exists");
        check_cuda(cudaSetDevice(device_ordinal), "cudaSetDevice");
        cudaStream_t created = nullptr;
        check_cuda(cudaStreamCreate(&created), "cudaStreamCreate");
        stream_ = created;
        device_ordinal_ = device_ordinal;
    }

    cudaStream_t get() const noexcept { return stream_; }

private:
    cudaStream_t stream_ = nullptr;
    int device_ordinal_ = -1;
};

struct DeviceStateStorage {
    DeviceAllocation<double> rho;
    DeviceAllocation<double> mom_u;
    DeviceAllocation<double> mom_v;
    DeviceAllocation<double> mom_w;
    DeviceAllocation<double> eng;
    DeviceAllocation<double> enuc_rate;
    DeviceAllocation<double> species;
    int total_size = 0;
    int species_count = 0;

    void allocate(int total, int count)
    {
        if (total <= 0 || count < 0 || count > kMaxDeviceSpecies)
            throw std::invalid_argument("invalid device state shape");
        total_size = total;
        species_count = count;
        rho.allocate(total);
        mom_u.allocate(total);
        mom_v.allocate(total);
        mom_w.allocate(total);
        eng.allocate(total);
        enuc_rate.allocate(total);
        if (count > 0) {
            species.allocate(static_cast<std::size_t>(total)
                             * static_cast<std::size_t>(count));
        }
    }

    DeviceStateView view() const noexcept
    {
        return {rho.get(), mom_u.get(), mom_v.get(), mom_w.get(), eng.get(),
                enuc_rate.get(), species.get(), total_size, species_count};
    }
};

void validate_host_state_shape(
    const FluidState& state, int total, int species_count)
{
    const std::size_t size = static_cast<std::size_t>(total);
    if (state.block_total_size_ != total
        || state.GetNumSpecies() != species_count
        || state.rho.size() != size || state.mom_u.size() != size
        || state.mom_v.size() != size || state.mom_w.size() != size
        || state.eng.size() != size || state.enuc_rate.size() != size
        || state.mass_fractions.size()
            != size * static_cast<std::size_t>(species_count)) {
        throw std::invalid_argument("Host FluidState layout is inconsistent");
    }
}

std::size_t slot_index(state::StateSlot slot)
{
    const auto value = static_cast<std::uint8_t>(slot);
    if (value > static_cast<std::uint8_t>(state::StateSlot::Scratch))
        throw std::invalid_argument("invalid state slot");
    return value;
}

bool complete_token(state::CompletionToken token) noexcept
{
    return state::is_complete(token);
}

bool same_hydro_descriptor(
    const scheduler::StageDescriptor& left,
    const scheduler::StageDescriptor& right) noexcept
{
    return left.stage == right.stage
        && left.old_slot == right.old_slot
        && left.input_slot == right.input_slot
        && left.output_slot == right.output_slot
        && left.old_weight == right.old_weight
        && left.update_weight == right.update_weight
        && left.flux_register_weight == right.flux_register_weight
        && left.input_requires_ghost == right.input_requires_ghost
        && left.refresh_ghost_after == right.refresh_ghost_after;
}

scheduler::HydroMethod hydro_method(
    dispatch::TimeIntegratorId integrator)
{
    using dispatch::TimeIntegratorId;
    switch (integrator) {
    case TimeIntegratorId::Euler: return scheduler::HydroMethod::Euler;
    case TimeIntegratorId::Rk2: return scheduler::HydroMethod::RK2;
    case TimeIntegratorId::Rk3: return scheduler::HydroMethod::RK3;
    }
    throw std::invalid_argument("invalid resolved Hydro integrator");
}

bool same_rkl_descriptor(
    const scheduler::RklStageDescriptor& left,
    const scheduler::RklStageDescriptor& right) noexcept
{
    return left.stage == right.stage
        && left.state_n_slot == right.state_n_slot
        && left.previous_slot == right.previous_slot
        && left.older_slot == right.older_slot
        && left.output_slot == right.output_slot
        && left.reflux_before_publish == right.reflux_before_publish
        && left.refresh_ghost_after == right.refresh_ghost_after;
}

template <class Binding>
struct CudaBurnNetworkType;
template <>
struct CudaBurnNetworkType<dispatch::CudaAprox13Binding> {
    using type = NetAprox13;
};
template <>
struct CudaBurnNetworkType<dispatch::CudaAprox19Binding> {
    using type = NetAprox19;
};
template <>
struct CudaBurnNetworkType<dispatch::CudaAprox21Binding> {
    using type = NetAprox21;
};
template <>
struct CudaBurnNetworkType<dispatch::CudaIso7Binding> {
    using type = NetIso7;
};

struct BurnWorkspaceSizeVisitor {
    std::size_t bytes_per_cell = 0;

    template <class Registration>
    void operator()()
    {
        using Binding = typename dispatch::PolicyRegistration<
            Registration>::CudaBinding;
        if constexpr (!std::is_same_v<Binding, dispatch::AbsentBinding>
                      && !std::is_same_v<
                          Binding, dispatch::CudaNoNetworkBinding>) {
            using Network = typename CudaBurnNetworkType<Binding>::type;
            bytes_per_cell =
                sizeof(BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>);
        }
    }
};

std::size_t burn_workspace_bytes(
    const dispatch::ResolvedExecutionPlan& plan, std::size_t active_cells)
{
    if (plan.network == dispatch::NetworkId::None
        && plan.ode_solver == dispatch::OdeSolverId::None
        && plan.linear_solver == dispatch::LinearSolverId::None)
        return 0;
    if (plan.linear_solver != dispatch::LinearSolverId::DenseLu)
        throw std::invalid_argument(
            "CUDA burn requires its registered DenseLU route");
    BurnWorkspaceSizeVisitor visitor;
    const bool found = dispatch::visit_policy<dispatch::NetworkPolicies>(
        plan.network, visitor);
    if (!found || visitor.bytes_per_cell == 0)
        throw std::invalid_argument(
            "CUDA burn network has no device workspace binding");
    const std::size_t workspace_count =
        plan.ode_solver == dispatch::OdeSolverId::BeNr ? 1 : active_cells;
    if (workspace_count > std::numeric_limits<std::size_t>::max()
                              / visitor.bytes_per_cell)
        throw std::overflow_error("CUDA burn workspace extent overflow");
    return workspace_count * visitor.bytes_per_cell;
}

template <class Binding>
struct CudaBurnOdeType;
template <>
struct CudaBurnOdeType<dispatch::CudaBeNrBinding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_BE_NR<Network, Matrix, Linear>;
};
template <>
struct CudaBurnOdeType<dispatch::CudaBdBinding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_BD<Network, Matrix, Linear>;
};
template <>
struct CudaBurnOdeType<dispatch::CudaRos4Binding> {
    template <class Network, class Matrix, class Linear>
    using solver = Solver_ROS4<Network, Matrix, Linear>;
};

struct DeviceBurnSummary {
    double limiter = DriverBurn::INACTIVE_LIMITER_CANDIDATE;
    std::uint64_t failed_cells = 0;
    int status = 0;
};

template <class Network, class OdeBinding, class Eos>
__global__ void burn_cells_kernel(
    DeviceStateView state, DeviceGridView grid,
    BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>* workspaces,
    reduction::ReductionCandidate* candidates, int* statuses,
    double burn_dt, Eos eos, BurnConfigView config)
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    const int count = grid.active_cell_count();
    if (linear >= count) return;
    const int nx = grid.ie - grid.is;
    const int ny = grid.je - grid.js;
    const int i = grid.is + linear % nx;
    const int j = grid.js + (linear / nx) % ny;
    const int k = grid.ks + linear / (nx * ny);
    const int cell_index = grid.index(i, j, k);

    BurnPolicyCell cell{};
    cell.fluid = state.load(cell_index);
    for (int species = 0; species < Network::NUM_SPECIES; ++species)
        cell.state[species] = state.species(species, cell_index);
    cell.burn_dt = burn_dt;
    using Ode = CudaBurnOdeType<OdeBinding>;
    constexpr bool shared_workspace =
        std::is_same_v<OdeBinding, dispatch::CudaBeNrBinding>;
    execute_burn_policy_cell<Network, Ode::template solver>(
        cell, workspaces[shared_workspace ? 0 : linear], eos, config);

    if (cell.interior_effect.interior_written) {
        state.store(cell_index, cell.fluid);
        for (int species = 0; species < Network::NUM_SPECIES; ++species)
            state.mass_fractions[
                static_cast<std::size_t>(species) * state.total_size
                + cell_index] = cell.state[species];
        state.enuc_rate[cell_index] = cell.enuc_rate;
    }
    amr::CellLogicalKey key{};
    key.logical_i = i;
    key.logical_j = j;
    key.logical_k = k;
    key.component = 3;
    candidates[linear] = {cell.limiter_candidate, key, true};
    statuses[linear] = static_cast<int>(cell.disposition);
}

__global__ void reduce_burn_kernel(
    const reduction::ReductionCandidate* candidates, const int* statuses,
    int count, DeviceBurnSummary* result)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    const auto spec = reduction::minimum_spec(
        DriverBurn::INACTIVE_LIMITER_CANDIDATE);
    auto reduced = reduction::begin_reduction(spec);
    amr::CellLogicalKey seed_key{};
    seed_key.logical_i = -1;
    seed_key.component = 3;
    reduction::combine_candidate(
        spec, reduced,
        {DriverBurn::INACTIVE_LIMITER_CANDIDATE, seed_key, true});
    DeviceBurnSummary summary{};
    for (int cell = 0; cell < count; ++cell) {
        reduction::combine_candidate(spec, reduced, candidates[cell]);
        const auto disposition = static_cast<DriverBurn::BurnCellDisposition>(
            statuses[cell]);
        if (disposition == DriverBurn::BurnCellDisposition::InvalidComposition
            || disposition == DriverBurn::BurnCellDisposition::SolverFailed) {
            ++summary.failed_cells;
            if (summary.status == 0)
                summary.status = statuses[cell];
        }
    }
    const auto finalized = reduction::finalize_reduction(spec, reduced);
    if (finalized.status != reduction::ReductionStatus::Ok
        && summary.status == 0) {
        summary.status = -static_cast<int>(finalized.status) - 1;
    }
    summary.limiter = finalized.value;
    *result = summary;
}

template <class Network, class OdeBinding, class Eos>
cudaError_t launch_burn_route(
    DeviceStateView state, DeviceGridView grid,
    BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>* workspaces,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    BurnConfigView config, cudaStream_t stream)
{
    if (state.n_species != Network::NUM_SPECIES)
        return cudaErrorInvalidValue;
    const int count = grid.active_cell_count();
    constexpr int threads = 128;
    const int blocks = (count + threads - 1) / threads;
    burn_cells_kernel<Network, OdeBinding>
        <<<blocks, threads, 0, stream>>>(
            state, grid, workspaces, candidates, statuses, burn_dt, eos,
            config);
    cudaError_t error = cudaGetLastError();
    if (error == cudaSuccess) {
        reduce_burn_kernel<<<1, 1, 0, stream>>>(
            candidates, statuses, count, summary);
        error = cudaGetLastError();
    }
    return error;
}

template <class Eos>
struct BurnRouteContext {
    const dispatch::ResolvedExecutionPlan& plan;
    DeviceStateView state;
    DeviceGridView grid;
    std::byte* workspace_storage;
    reduction::ReductionCandidate* candidates;
    int* statuses;
    DeviceBurnSummary* summary;
    double burn_dt;
    Eos eos;
    BurnConfigView config;
    cudaStream_t stream;
    cudaError_t result = cudaErrorInvalidValue;
    bool invoked = false;
};

template <class Network, class Eos>
struct BurnOdeRouteVisitor {
    BurnRouteContext<Eos>& context;

    template <class OdeRegistration>
    void operator()()
    {
        using OdeBinding = typename dispatch::PolicyRegistration<
            OdeRegistration>::CudaBinding;
        if constexpr (!std::is_same_v<OdeBinding, dispatch::AbsentBinding>
                      && !std::is_same_v<
                          OdeBinding, dispatch::CudaNoOdeBinding>) {
            using Workspace = BurnOdeMatrixWorkspaceFor<Network::ODE_NEQ>;
            auto* workspaces = reinterpret_cast<Workspace*>(
                context.workspace_storage);
            context.result = launch_burn_route<Network, OdeBinding>(
                context.state, context.grid, workspaces,
                context.candidates, context.statuses, context.summary,
                context.burn_dt, context.eos, context.config,
                context.stream);
            context.invoked = true;
        }
    }
};

template <class Eos>
struct BurnNetworkRouteVisitor {
    BurnRouteContext<Eos>& context;

    template <class NetworkRegistration>
    void operator()()
    {
        using NetworkBinding = typename dispatch::PolicyRegistration<
            NetworkRegistration>::CudaBinding;
        if constexpr (!std::is_same_v<NetworkBinding, dispatch::AbsentBinding>
                      && !std::is_same_v<
                          NetworkBinding, dispatch::CudaNoNetworkBinding>) {
            using Network = typename CudaBurnNetworkType<
                NetworkBinding>::type;
            BurnOdeRouteVisitor<Network, Eos> visitor{context};
            const bool ode_found = dispatch::visit_policy<
                dispatch::OdeSolverPolicies>(context.plan.ode_solver, visitor);
            context.invoked = context.invoked && ode_found;
        }
    }
};

template <class Eos>
cudaError_t visit_cuda_burn_route(
    const dispatch::ResolvedExecutionPlan& plan, DeviceStateView state,
    DeviceGridView grid, std::byte* workspace_storage,
    reduction::ReductionCandidate* candidates, int* statuses,
    DeviceBurnSummary* summary, double burn_dt, Eos eos,
    BurnConfigView config, cudaStream_t stream)
{
    if (plan.linear_solver != dispatch::LinearSolverId::DenseLu
        || workspace_storage == nullptr || candidates == nullptr
        || statuses == nullptr || summary == nullptr)
        return cudaErrorInvalidValue;
    BurnRouteContext<Eos> context{
        plan, state, grid, workspace_storage, candidates, statuses, summary,
        burn_dt, eos, config, stream};
    BurnNetworkRouteVisitor<Eos> visitor{context};
    const bool network_found = dispatch::visit_policy<
        dispatch::NetworkPolicies>(plan.network, visitor);
    return network_found && context.invoked
        ? context.result : cudaErrorInvalidValue;
}

template <class Function>
decltype(auto) visit_eos(
    std::variant<std::monostate, IdealGasView, HelmEosView,
                 Tabular3DEOSView, Tabular4DEOSView>& eos,
    Function&& function)
{
    return std::visit(
        [&]<class Eos>(Eos& value) -> decltype(auto) {
            if constexpr (std::is_same_v<Eos, std::monostate>)
                throw std::logic_error("CUDA EOS owner is unavailable");
            else
                return function(value);
        },
        eos);
}

constexpr dispatch::EosId host_eos_id(const IdealGas&) noexcept
{
    return dispatch::EosId::Ideal;
}

constexpr dispatch::EosId host_eos_id(const HelmEos&) noexcept
{
    return dispatch::EosId::Helmholtz;
}

constexpr dispatch::EosId host_eos_id(
    const Tabular3DEOSHostView&) noexcept
{
    return dispatch::EosId::Tabular3D;
}

constexpr dispatch::EosId host_eos_id(
    const Tabular4DEOSHostView&) noexcept
{
    return dispatch::EosId::Tabular4D;
}

constexpr bool same_block_handle(
    amr::BlockHandle left, amr::BlockHandle right) noexcept
{
    return left.uid.value == right.uid.value
        && left.epoch.value == right.epoch.value;
}

constexpr bool same_storage_generation(
    backend::StorageGeneration left,
    backend::StorageGeneration right) noexcept
{
    return left.value == right.value;
}

} // namespace

struct CudaBackend::Impl {
    int device_ordinal;
    amr::BlockHandle handle;
    backend::StorageGeneration generation;
    CudaLaunchConfig launch;
    StreamOwner stream;
    std::array<DeviceStateStorage, 3> state_storage;
    std::array<DeviceStateView, 3> slots{};
    DeviceStateStorage face_flux;
    DeviceStateStorage hydro_delta;
    DeviceStateStorage diffusion_delta;
    DeviceStateStorage diffusion_initial_delta;
    DeviceAllocation<double> cfl_candidates;
    DeviceAllocation<double> cfl_result;
    DeviceAllocation<double> diffusion_dt_candidates;
    DeviceAllocation<double> diffusion_dt_result;
    DeviceAllocation<int> diffusion_status;
    DeviceAllocation<std::byte> burn_workspace_storage;
    DeviceAllocation<reduction::ReductionCandidate> burn_candidates;
    DeviceAllocation<int> burn_statuses;
    DeviceAllocation<DeviceBurnSummary> burn_summary;
    DeviceAllocation<double> cell_volume;
    std::array<DeviceAllocation<double>, 3> face_area_lower;
    std::array<DeviceAllocation<double>, 3> face_area_upper;
    DeviceGridView grid{};
    DeviceCompiledBoundaryPlan boundary;
    DeviceAllocation<DeviceBoundaryTransfer> boundary_transfers;
    std::unique_ptr<DeviceSpeciesOwner> species_owner;
    std::unique_ptr<HelmEosDeviceOwner> helm_owner;
    std::unique_ptr<Tabular3DEOSDeviceOwner> tabular3_owner;
    std::unique_ptr<Tabular4DEOSDeviceOwner> tabular4_owner;
    SpeciesPODView species_view{};
    std::variant<std::monostate, IdealGasView, HelmEosView,
                 Tabular3DEOSView, Tabular4DEOSView> eos;
    backend::BackendCounters runtime_counters{};
    std::vector<backend::BackendTraceRecord> runtime_trace;

    Impl(const amr::Block& block, amr::BlockHandle requested_handle,
         backend::StorageGeneration requested_generation, int device,
         const CudaLaunchConfig& launch_config,
         const SpeciesManager& species,
         const boundary::BoundaryPlan& logical_boundary)
        : device_ordinal(device), handle(requested_handle),
          generation(requested_generation), launch(launch_config)
    {
        if (!amr::is_valid(handle) || !backend::is_valid(generation))
            throw std::invalid_argument("invalid backend identity");
        if (device_ordinal < 0)
            throw std::invalid_argument("negative CUDA device ordinal");
        if (block.grid.geometry != "cartesian")
            throw std::invalid_argument("CUDA E3 requires Cartesian geometry");
        const int total = block.grid.GetTotalSize();
        const int count = block.fluid_state.GetNumSpecies();
        if (count != species.count() || count > kMaxDeviceSpecies)
            throw std::invalid_argument("CUDA species registry mismatch");
        validate_host_state_shape(block.fluid_state, total, count);
        validate_host_state_shape(block.state_next, total, count);
        validate_host_state_shape(block.state_scratch, total, count);

        stream.create(device_ordinal);

        for (auto& storage : state_storage) storage.allocate(total, count);
        for (std::size_t slot = 0; slot < slots.size(); ++slot)
            slots[slot] = state_storage[slot].view();
        face_flux.allocate(total, count);
        hydro_delta.allocate(total, count);
        diffusion_delta.allocate(total, count);
        diffusion_initial_delta.allocate(total, count);

        grid = make_device_grid_view(block.grid);
        if (!valid_hydro_grid(grid))
            throw std::invalid_argument("invalid CUDA grid layout");
        const int active = grid.active_cell_count();
        if (active <= 0)
            throw std::invalid_argument("CUDA block has no active cells");
        cfl_candidates.allocate(active);
        cfl_result.allocate(1);
        diffusion_dt_candidates.allocate(active);
        diffusion_dt_result.allocate(1);
        diffusion_status.allocate(1);
        if (launch.burn.use_burn) {
            burn_workspace_storage.allocate(burn_workspace_bytes(
                launch.plan, static_cast<std::size_t>(active)));
            burn_candidates.allocate(active);
            burn_statuses.allocate(active);
            burn_summary.allocate(1);
        }

        cell_volume.allocate(total);
        for (int axis = 0; axis < 3; ++axis) {
            face_area_lower[axis].allocate(total);
            face_area_upper[axis].allocate(total);
        }
        std::vector<double> volumes(total, 0.0);
        std::array<std::vector<double>, 3> lower{
            std::vector<double>(total, 0.0),
            std::vector<double>(total, 0.0),
            std::vector<double>(total, 0.0)};
        std::array<std::vector<double>, 3> upper{
            std::vector<double>(total, 0.0),
            std::vector<double>(total, 0.0),
            std::vector<double>(total, 0.0)};
        for (int k = block.grid.Ks(); k < block.grid.Ke(); ++k) {
            for (int j = block.grid.Js(); j < block.grid.Je(); ++j) {
                for (int i = block.grid.Is(); i < block.grid.Ie(); ++i) {
                    const int cell = block.grid.GetIndex(i, j, k);
                    volumes[cell] = GridMetrics::CellVolume(block.grid, i, j, k);
                    for (int axis = 0; axis < block.grid.dim; ++axis) {
                        lower[axis][cell] = GridMetrics::FaceArea(
                            block.grid, axis, i, j, k, false);
                        upper[axis][cell] = GridMetrics::FaceArea(
                            block.grid, axis, i, j, k, true);
                    }
                }
            }
        }
        const std::size_t metric_bytes =
            static_cast<std::size_t>(total) * sizeof(double);
        check_cuda(cudaMemcpyAsync(
                       cell_volume.get(), volumes.data(), metric_bytes,
                       cudaMemcpyHostToDevice, stream.get()),
                   "upload cell volume");
        grid.cell_volume = cell_volume.get();
        for (int axis = 0; axis < 3; ++axis) {
            check_cuda(cudaMemcpyAsync(
                           face_area_lower[axis].get(), lower[axis].data(),
                           metric_bytes, cudaMemcpyHostToDevice, stream.get()),
                       "upload lower face area");
            check_cuda(cudaMemcpyAsync(
                           face_area_upper[axis].get(), upper[axis].data(),
                           metric_bytes, cudaMemcpyHostToDevice, stream.get()),
                       "upload upper face area");
            grid.face_area_lower[axis] = face_area_lower[axis].get();
            grid.face_area_upper[axis] = face_area_upper[axis].get();
        }

        boundary = compile_boundary_plan(logical_boundary, grid);
        boundary_transfers.allocate(boundary.transfers.size());
        check_cuda(cudaMemcpyAsync(
                       boundary_transfers.get(), boundary.transfers.data(),
                       boundary.transfers.size()
                           * sizeof(DeviceBoundaryTransfer),
                       cudaMemcpyHostToDevice, stream.get()),
                   "upload boundary transfers");
        check_cuda(cudaStreamSynchronize(stream.get()),
                   "synchronize immutable construction uploads");
        ++runtime_counters.stream_sync_count;
    }

    ~Impl()
    {
        static_cast<void>(cudaSetDevice(device_ordinal));
        static_cast<void>(cudaStreamSynchronize(stream.get()));
    }

    void initialize_eos(const IdealGas& host, const SpeciesManager& species)
    {
        species_owner = std::make_unique<DeviceSpeciesOwner>(
            species, stream.get());
        species_view = species_owner->view();
        eos = species_owner->ideal_gas_view(host.global_gamma);
        check_cuda(cudaStreamSynchronize(stream.get()), "upload Ideal EOS");
        ++runtime_counters.stream_sync_count;
    }

    void initialize_eos(const HelmEos& host, const SpeciesManager&)
    {
        helm_owner = std::make_unique<HelmEosDeviceOwner>(host, stream.get());
        const HelmEosView view = helm_owner->view();
        species_view = view.specs;
        eos = view;
        check_cuda(cudaStreamSynchronize(stream.get()), "upload Helm EOS");
        ++runtime_counters.stream_sync_count;
    }

    void initialize_eos(
        const Tabular3DEOSHostView& host, const SpeciesManager&)
    {
        tabular3_owner = std::make_unique<Tabular3DEOSDeviceOwner>(
            host, stream.get());
        const Tabular3DEOSView view = tabular3_owner->view();
        species_view = view.specs;
        eos = view;
        check_cuda(cudaStreamSynchronize(stream.get()), "upload Tabular3 EOS");
        ++runtime_counters.stream_sync_count;
    }

    void initialize_eos(
        const Tabular4DEOSHostView& host, const SpeciesManager&)
    {
        tabular4_owner = std::make_unique<Tabular4DEOSDeviceOwner>(
            host, stream.get());
        const Tabular4DEOSView view = tabular4_owner->view();
        species_view = view.specs;
        eos = view;
        check_cuda(cudaStreamSynchronize(stream.get()), "upload Tabular4 EOS");
        ++runtime_counters.stream_sync_count;
    }

    DeviceStateView require_access(backend::BackendStateAccess access) const
    {
        if (!same_block_handle(access.block, handle)
            || !same_storage_generation(access.storage, generation))
            throw std::invalid_argument("stale CUDA backend access");
        return slots[slot_index(access.slot)];
    }

    template <class Function>
    void for_each_region_segment(state::StateRegion region, Function&& function)
    {
        if (region != state::StateRegion::Interior
            && region != state::StateRegion::Ghost)
            throw std::invalid_argument("invalid state region");
        for (int k = 0; k < grid.total_z; ++k) {
            for (int j = 0; j < grid.total_y; ++j) {
                const bool active_row =
                    k >= grid.ks && k < grid.ke
                    && j >= grid.js && j < grid.je;
                if (region == state::StateRegion::Interior) {
                    if (active_row)
                        function(grid.index(grid.is, j, k), grid.ie - grid.is);
                } else if (!active_row) {
                    function(grid.index(0, j, k), grid.total_x);
                } else {
                    if (grid.is > 0) function(grid.index(0, j, k), grid.is);
                    if (grid.ie < grid.total_x) {
                        function(grid.index(grid.ie, j, k),
                                 grid.total_x - grid.ie);
                    }
                }
            }
        }
    }

    std::uint64_t copy_host_device_region(
        DeviceStateView device, const backend::HostStateTransferView& host,
        state::StateRegion region, cudaMemcpyKind direction)
    {
        backend::validate_host_state_transfer_view(host);
        if (host.cell_count != static_cast<std::size_t>(grid.total_size)
            || host.species_count != static_cast<std::size_t>(device.n_species))
            throw std::invalid_argument("Host/device transfer shape mismatch");
        std::uint64_t copied = 0;
        const std::array<double*, 6> device_fields{
            device.rho, device.mom_u, device.mom_v, device.mom_w,
            device.eng, device.enuc_rate};
        const std::array<double*, 6> host_fields{
            host.rho, host.mom_u, host.mom_v, host.mom_w,
            host.eng, host.enuc_rate};
        for_each_region_segment(region, [&](int offset, int count) {
            const std::size_t bytes =
                static_cast<std::size_t>(count) * sizeof(double);
            for (std::size_t field = 0; field < device_fields.size(); ++field) {
                void* destination = direction == cudaMemcpyHostToDevice
                    ? static_cast<void*>(device_fields[field] + offset)
                    : static_cast<void*>(host_fields[field] + offset);
                const void* source = direction == cudaMemcpyHostToDevice
                    ? static_cast<const void*>(host_fields[field] + offset)
                    : static_cast<const void*>(device_fields[field] + offset);
                check_cuda(cudaMemcpyAsync(
                               destination, source, bytes, direction,
                               stream.get()),
                           "enqueue state transfer");
                copied += bytes;
            }
            for (int species = 0; species < device.n_species; ++species) {
                double* device_pointer = device.mass_fractions
                    + static_cast<std::size_t>(species) * device.total_size
                    + offset;
                double* host_pointer = host.species
                    + static_cast<std::size_t>(species) * host.species_stride
                    + offset;
                void* destination = direction == cudaMemcpyHostToDevice
                    ? static_cast<void*>(device_pointer)
                    : static_cast<void*>(host_pointer);
                const void* source = direction == cudaMemcpyHostToDevice
                    ? static_cast<const void*>(host_pointer)
                    : static_cast<const void*>(device_pointer);
                check_cuda(cudaMemcpyAsync(
                               destination, source, bytes, direction,
                               stream.get()),
                           "enqueue species transfer");
                copied += bytes;
            }
        });
        return copied;
    }
};

CudaBackend::CudaBackend(std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation))
{
    if (!impl_) throw std::invalid_argument("CUDA backend implementation is null");
}

CudaBackend::~CudaBackend() = default;

state::ExecutionSide CudaBackend::side() const noexcept
{
    return state::ExecutionSide::Device;
}

amr::BlockHandle CudaBackend::block_handle() const noexcept
{
    return impl_->handle;
}

backend::StorageGeneration CudaBackend::storage_generation() const noexcept
{
    return impl_->generation;
}

double CudaBackend::compute_hydro_dt(
    backend::BackendStateAccess current, double cfl)
{
    const DeviceStateView state = impl_->require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("Hydro dt requires Current");
    const CudaHydroWorkspaceView workspace{
        impl_->face_flux.view(), impl_->hydro_delta.view(),
        impl_->cfl_candidates.get(), impl_->cfl_result.get()};
    cudaError_t launch_error = cudaSuccess;
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch_error = launch_compute_hydro_dt(
            state, impl_->grid, eos, cfl, workspace, impl_->stream.get());
    });
    check_cuda(launch_error, "launch Hydro dt");
    double result = 0.0;
    check_cuda(cudaMemcpyAsync(
                   &result, impl_->cfl_result.get(), sizeof(double),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download Hydro dt");
    quiesce();
    impl_->runtime_counters.kernel_count += 2;
    impl_->runtime_counters.bytes_d2h += sizeof(double);
    return result;
}

state::CompletionToken CudaBackend::execute_hydro_stage(
    backend::BackendStateAccess current,
    const scheduler::StageDescriptor& descriptor,
    double dt, state::CompletionToken expected)
{
    static_cast<void>(impl_->require_access(current));
    const scheduler::HydroPlan plan = scheduler::make_hydro_plan(
        hydro_method(impl_->launch.plan.time_integrator));
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !same_hydro_descriptor(
            descriptor, plan.stages[descriptor.stage - 1]))
        throw std::invalid_argument("invalid Hydro stage contract");
    const DeviceStateView old_state = impl_->slots[slot_index(descriptor.old_slot)];
    const DeviceStateView input = impl_->slots[slot_index(descriptor.input_slot)];
    const DeviceStateView output = impl_->slots[slot_index(descriptor.output_slot)];
    cudaError_t launch_error = cudaErrorInvalidValue;
    int kernels = 0;
    const bool route = visit_cuda_hydro_route(
        impl_->launch.plan,
        [&]<class Reconstruction, class Flux> {
            visit_eos(impl_->eos, [&](const auto& eos) {
                launch_error = launch_bounded_hydro_stage<Reconstruction, Flux>(
                    old_state, input, output, impl_->hydro_delta.view(),
                    impl_->face_flux.view(), impl_->grid, eos,
                    impl_->launch.entropy_fix_coefficient,
                    impl_->launch.density_floor,
                    impl_->launch.minimum_internal_energy,
                    impl_->launch.maximum_internal_energy,
                    descriptor, dt, impl_->stream.get(), kernels);
            });
        });
    if (!route) throw std::logic_error("CUDA Hydro route is unavailable");
    check_cuda(launch_error, "launch Hydro stage");
    quiesce();
    impl_->runtime_counters.kernel_count +=
        static_cast<std::uint64_t>(kernels);
    return expected;
}

state::CompletionToken CudaBackend::execute_physical_boundary(
    backend::BackendStateAccess access, state::StateVersion version,
    state::CompletionToken expected)
{
    const DeviceStateView selected = impl_->require_access(access);
    if (!state::is_valid(version) || !complete_token(expected))
        throw std::invalid_argument("invalid boundary completion contract");
    check_cuda(launch_boundary_plan(
                   selected, impl_->boundary_transfers.get(),
                   impl_->boundary, impl_->stream.get()),
               "launch boundary plan");
    quiesce();
    for (const auto& phase : impl_->boundary.phases)
        if (phase.count > 0) ++impl_->runtime_counters.kernel_count;
    return expected;
}

void CudaBackend::rotate_slots(
    backend::BackendStateAccess current, state::SlotRotation rotation)
{
    static_cast<void>(impl_->require_access(current));
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("rotation requires Current access");
    const std::array<std::size_t, 3> source{
        slot_index(rotation.current_from), slot_index(rotation.next_from),
        slot_index(rotation.scratch_from)};
    if (source[0] == source[1] || source[0] == source[2]
        || source[1] == source[2])
        throw std::invalid_argument("slot rotation is not a permutation");
    const auto old = impl_->slots;
    for (std::size_t destination = 0; destination < 3; ++destination)
        impl_->slots[destination] = old[source[destination]];
}

double CudaBackend::compute_diffusion_dt(
    backend::BackendStateAccess current)
{
    const DeviceStateView selected = impl_->require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("diffusion dt requires Current");
    const DiffusionWorkspaceView workspace{
        impl_->face_flux.view(), impl_->diffusion_dt_candidates.get(),
        impl_->diffusion_dt_result.get(), impl_->diffusion_status.get()};
    DiffusionLaunchResult result{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        result = launch_raw_diffusion_dt(
            selected, eos, impl_->species_view, impl_->grid,
            impl_->launch.diffusion, workspace, impl_->stream.get());
    });
    check_cuda(result.error, "launch diffusion dt");
    int status = 0;
    double dt = 0.0;
    check_cuda(cudaMemcpyAsync(
                   &status, impl_->diffusion_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion status");
    check_cuda(cudaMemcpyAsync(
                   &dt, impl_->diffusion_dt_result.get(), sizeof(double),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion dt");
    quiesce();
    impl_->runtime_counters.kernel_count += result.kernels_launched;
    impl_->runtime_counters.bytes_d2h += sizeof(int) + sizeof(double);
    if (status != 0) throw std::runtime_error("diffusion dt candidate failed");
    return dt;
}

void CudaBackend::copy_state_slot(
    backend::BackendStateAccess source,
    backend::BackendStateAccess destination)
{
    const DeviceStateView from = impl_->require_access(source);
    const DeviceStateView to = impl_->require_access(destination);
    if (source.slot == destination.slot)
        throw std::invalid_argument("state copy aliases one logical slot");
    check_cuda(copy_diffusion_slot(from, to, impl_->stream.get()),
               "copy state slot");
    quiesce();
}

state::CompletionToken CudaBackend::execute_diffusion_stage(
    backend::BackendStateAccess current, const scheduler::RklPlan& plan,
    const scheduler::RklStageDescriptor& descriptor,
    double dt, double dt_fe, state::CompletionToken expected)
{
    static_cast<void>(impl_->require_access(current));
    const bool frozen_rkl1 = impl_->launch.plan.diffusion_integrator
        == dispatch::DiffusionIntegratorId::Rkl1;
    const bool frozen_rkl2 = impl_->launch.plan.diffusion_integrator
        == dispatch::DiffusionIntegratorId::Rkl2;
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || !impl_->launch.diffusion.use_diffusion
        || (!frozen_rkl1 && !frozen_rkl2)
        || plan.second_order != frozen_rkl2
        || descriptor.stage <= 0
        || descriptor.stage > static_cast<int>(plan.stages.size())
        || !same_rkl_descriptor(
            descriptor, plan.stages[descriptor.stage - 1])
        || !(dt > 0.0) || !(dt_fe > 0.0))
        throw std::invalid_argument("invalid diffusion stage contract");
    const DeviceStateView state_n =
        impl_->slots[slot_index(descriptor.state_n_slot)];
    const DeviceStateView previous =
        impl_->slots[slot_index(descriptor.previous_slot)];
    const DeviceStateView older =
        impl_->slots[slot_index(descriptor.older_slot)];
    const DeviceStateView output =
        impl_->slots[slot_index(descriptor.output_slot)];
    const DiffusionWorkspaceView workspace{
        impl_->face_flux.view(), impl_->diffusion_dt_candidates.get(),
        impl_->diffusion_dt_result.get(), impl_->diffusion_status.get()};
    DiffusionLaunchResult operation{};
    visit_eos(impl_->eos, [&](const auto& eos) {
        operation = launch_bounded_diffusion_operator(
            descriptor.stage == 1 ? state_n : previous,
            descriptor.stage == 1 && plan.second_order
                ? impl_->diffusion_initial_delta.view()
                : impl_->diffusion_delta.view(),
            eos, impl_->species_view, impl_->grid, impl_->launch.diffusion,
            workspace, impl_->stream.get());
    });
    check_cuda(operation.error, "launch diffusion operator");
    DiffusionLaunchResult update{};
    const DiffFunction::RKLOrder order = plan.second_order
        ? DiffFunction::RKLOrder::Second : DiffFunction::RKLOrder::First;
    const auto coefficients = DiffFunction::get_rkl_coeffs(
        order, descriptor.stage, static_cast<int>(plan.stages.size()));
    if (descriptor.stage == 1) {
        update = launch_bounded_first_rkl_stage(
            state_n,
            plan.second_order ? impl_->diffusion_initial_delta.view()
                              : impl_->diffusion_delta.view(),
            output, impl_->grid, impl_->launch.diffusion,
            coefficients.tilde_mu * dt, impl_->stream.get());
    } else {
        update = launch_bounded_recursive_rkl_stage(
            state_n, previous, older, impl_->diffusion_delta.view(),
            plan.second_order ? impl_->diffusion_initial_delta.view()
                              : impl_->diffusion_delta.view(),
            output, impl_->grid, impl_->launch.diffusion, coefficients,
            plan.second_order, dt, false, impl_->stream.get());
    }
    check_cuda(update.error, "launch diffusion stage");
    int status = 0;
    check_cuda(cudaMemcpyAsync(
                   &status, impl_->diffusion_status.get(), sizeof(int),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download diffusion stage status");
    quiesce();
    impl_->runtime_counters.kernel_count +=
        operation.kernels_launched + update.kernels_launched;
    impl_->runtime_counters.bytes_d2h += sizeof(int);
    if (status != 0) throw std::runtime_error("diffusion stage failed");
    return expected;
}

backend::BurnExecutionResult CudaBackend::execute_burn(
    backend::BackendStateAccess current, double dt,
    state::CompletionToken expected)
{
    static_cast<void>(impl_->require_access(current));
    if (current.slot != state::StateSlot::Current || !complete_token(expected)
        || !(dt > 0.0))
        throw std::invalid_argument("invalid burn contract");
    if (!impl_->launch.burn.use_burn) {
        check_cuda(cudaMemsetAsync(
                       impl_->slots[slot_index(state::StateSlot::Current)].enuc_rate,
                       0,
                       static_cast<std::size_t>(impl_->grid.total_size)
                           * sizeof(double),
                       impl_->stream.get()),
                   "clear disabled burn diagnostic");
        quiesce();
        return {DriverBurn::INACTIVE_LIMITER_CANDIDATE, 0, 0, expected};
    }
    DeviceStateView selected =
        impl_->slots[slot_index(state::StateSlot::Current)];
    check_cuda(cudaMemsetAsync(
                   selected.enuc_rate, 0,
                   static_cast<std::size_t>(impl_->grid.total_size)
                       * sizeof(double),
                   impl_->stream.get()),
               "clear burn diagnostic");
    cudaError_t launch_error = cudaErrorInvalidValue;
    visit_eos(impl_->eos, [&](const auto& eos) {
        launch_error = visit_cuda_burn_route(
            impl_->launch.plan, selected, impl_->grid,
            impl_->burn_workspace_storage.get(), impl_->burn_candidates.get(),
            impl_->burn_statuses.get(), impl_->burn_summary.get(), dt, eos,
            impl_->launch.burn, impl_->stream.get());
    });
    check_cuda(launch_error, "launch burn route");
    DeviceBurnSummary summary{};
    check_cuda(cudaMemcpyAsync(
                   &summary, impl_->burn_summary.get(), sizeof(summary),
                   cudaMemcpyDeviceToHost, impl_->stream.get()),
               "download burn summary");
    quiesce();
    impl_->runtime_counters.kernel_count += 2;
    impl_->runtime_counters.bytes_d2h += sizeof(summary);
    return {summary.limiter, summary.failed_cells, summary.status, expected};
}

void CudaBackend::enqueue_materialize_host_current(
    backend::BackendStateAccess current, state::StateRegion region,
    backend::HostStateTransferView host)
{
    const DeviceStateView selected = impl_->require_access(current);
    if (current.slot != state::StateSlot::Current)
        throw std::invalid_argument("materialization requires Current");
    impl_->runtime_counters.bytes_d2h += impl_->copy_host_device_region(
        selected, host, region, cudaMemcpyDeviceToHost);
}

void CudaBackend::enqueue_upload_slot(
    backend::BackendStateAccess access, state::StateRegion region,
    backend::HostStateTransferView host)
{
    const DeviceStateView selected = impl_->require_access(access);
    impl_->runtime_counters.bytes_h2d += impl_->copy_host_device_region(
        selected, host, region, cudaMemcpyHostToDevice);
}

void CudaBackend::quiesce()
{
    check_cuda(cudaStreamSynchronize(impl_->stream.get()),
               "synchronize CUDA backend");
    ++impl_->runtime_counters.stream_sync_count;
}

backend::BackendCounters CudaBackend::counters() const noexcept
{
    ++impl_->runtime_counters.getter_count;
    return impl_->runtime_counters;
}

void CudaBackend::append_trace(backend::BackendTraceRecord record)
{
    if (!same_block_handle(record.block, impl_->handle)
        || !same_storage_generation(record.storage, impl_->generation))
        throw std::invalid_argument("trace targets stale CUDA storage");
    impl_->runtime_trace.push_back(record);
}

std::span<const backend::BackendTraceRecord>
CudaBackend::trace_snapshot() const noexcept
{
    ++impl_->runtime_counters.getter_count;
    return impl_->runtime_trace;
}

template <class Eos>
std::unique_ptr<CudaBackend> make_cuda_backend_impl(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const Eos& eos)
{
    if (launch.plan.eos != host_eos_id(eos))
        throw std::invalid_argument(
            "resolved EOS does not match the CUDA factory owner");
    auto implementation = std::make_unique<CudaBackend::Impl>(
        block, handle, storage, device_ordinal, launch, species, boundary);
    implementation->initialize_eos(eos, species);
    return std::make_unique<CudaBackend>(std::move(implementation));
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const IdealGas& eos)
{
    return make_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const HelmEos& eos)
{
    return make_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary,
    const Tabular3DEOSHostView& eos)
{
    return make_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary,
    const Tabular4DEOSHostView& eos)
{
    return make_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

} // namespace arch::cuda
