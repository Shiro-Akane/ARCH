/**
 * @file CudaBackendBoundary.cu
 * @brief Device boundary staging and shared resident Native EOS acceptance.
 *
 * Workflow:
 * 1. Validate requested donor/ghost offsets against the leased device slot.
 * 2. Gather only donor cells into reusable surface staging and transfer to Host.
 * 3. Host evaluates the shared callback/EOS; scatter validated returned ghosts.
 * 4. Upload packed diffusion-face controls consumed by the shared flux leaf.
 * 5. Lazily own the physical-surface flux observer named by the Host layout.
 * 6. Prepare one unpublished resident Native reflector layer with immutable
 *    ghost prefix readers and a separate bounded 11*N lane workspace.
 * 7. Check completed native logical cells through the shared EOS acceptance
 *    leaf; return compact diagnostics without field transfer or state writes.
 * 8. Inspect only actual staged coarse restricted-parent active interiors
 *    through the shared early closure leaf; that witness is failure-only.
 * 9. After an actual committed refusal, classify all active cells through the
 *    same completed EOS leaf; unrelated failures take precedence over retry.
 *
 * Epoch control capacity preparation authenticates the complete actual store,
 * preserves every live plane and reserves only unbound three-owner capacity.
 * It never evaluates callbacks/EOS or changes slot control bindings.
 *
 * Observer planes are sized by the Host-provided surface layout and bound to
 * all three stage slots; no state values cross this service. No boundary
 * formula is duplicated here. The reflector returns provisional surfaces only;
 * Runtime owns all layer joins, final scattering and completed acceptance.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "cuda/runtime/control/CudaBackendInternal.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "cuda/hydro/boundary/Boundary.cuh"
#include "cuda/hydro/policies/CheckedHydroEos.cuh"

namespace arch::cuda {
namespace {
/** Traverse active cells or disjoint logical-minus-active ghosts exactly once.
 * Workflow: completed mode launches active before ghosts on the same stream;
 * an active failure suppresses that patch's ghost queries. Error-only mode
 * traverses all active candidates, retains the original thermal target in bit
 * one and arbitrates sticky nonthermal evidence in bit two. Every lane keeps
 * its original sticky required-query latch; no mode changes EOS/state values.
 */
template<bool ClassifyActiveThermal, class Eos>
__global__ void validate_completed_native_eos(DeviceStateView input,
    DeviceGridView grid, state::Bounds bounds, Eos eos,
    SpeciesWorkspaceView workspace, int* failed,
    RzThermodynamics::AcceptanceDiagnostic* diagnostic,
    bool ghosts, int requested_index)
{
    // The preceding active launch has joined this stream before ghost work.
    // Atomic read also respects a failure first found by another ghost lane.
    if (ghosts && atomicAdd(failed, 0) != 0) return;
    __shared__ int required_status[kSpeciesKernelThreads];
    required_status[threadIdx.x] = 0;
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    const int lanes = blockDim.x * gridDim.x;
    detail::SpeciesLaneScratch<1> scratch(workspace, lane);
    double* fractions = scratch.array(0);
    const auto checked = make_checked_hydro_eos(eos, required_status + threadIdx.x);
    const auto geometry = make_grid_geometry_view(grid);
    const auto read = [input](int index) { return input.load(index); };
    const auto closure = [nx=grid.total_x](const auto& reader, int index,
        const auto& view, int i, const auto& limits) {
        const int support = i-1 < 0 ? 0 : (i-1 > nx-3 ? nx-3 : i-1);
        return RzThermodynamics::make_cell_supported(reader, index, view, i, support, limits);
    };
    const int active_x = grid.ie - grid.is, active_y = grid.je - grid.js;
    const int count = ghosts ? grid.total_x * grid.total_y : active_x * active_y;
    for (int logical = lane; logical < count; logical += lanes) {
        const int i = ghosts ? logical % grid.total_x : grid.is + logical % active_x;
        const int j = ghosts ? logical / grid.total_x : grid.js + logical / active_x;
        if (ghosts && i >= grid.is && i < grid.ie && j >= grid.js && j < grid.je)
            continue;
        const int index = grid.index(i, j, 0);
        for (int s = 0; s < input.n_species; ++s)
            fractions[s] = input.species(s, index);
        const auto result = RzThermodynamics::detail::check_patch_eos_cell(
            geometry, input.n_species, bounds, checked, i, j, index,
            closure, read, input.n_species > 0 ? fractions : nullptr);
        if (result.status != state::Status::valid) {
            if constexpr (ClassifyActiveThermal) {
                if (RzThermodynamics::is_retryable_thermal_failure(result)) {
                    if (index == requested_index) atomicOr(failed, 1);
                    continue; // A later active nonthermal failure must stay fatal.
                }
                if ((atomicOr(failed, 2) & 2) == 0) *diagnostic = result;
            } else if (atomicCAS(failed, 0, 1) == 0) *diagnostic = result;
            // Never clear a required-query failure for a later cell/node.
            break;
        }
    }
}

/** Failure-only early closure inspection of real coarse restricted parents.
 * Only actual active eligible cells are traversed: the shared leaf needs a real
 * three-active-rho support, so both active radial ends are excluded while every
 * active axial cell is inspected. Each lane loads the actual staged species and
 * reuses the original provisional/closure arithmetic. No EOS is queried, no
 * input is written and a clean traversal is neither EOS nor completion proof;
 * the first failing cell is copied out and no later cell clears it.
 */
__global__ void inspect_native_restricted_interior_cells(DeviceStateView input,
    DeviceGridView grid, state::Bounds bounds, SpeciesWorkspaceView workspace,
    int* failed, RzThermodynamics::AcceptanceDiagnostic* diagnostic)
{
    const int eligible_x = grid.ie - grid.is - 2, eligible_y = grid.je - grid.js;
    if (eligible_x <= 0 || eligible_y <= 0) return;
    const int lane = blockIdx.x * blockDim.x + threadIdx.x;
    const int lanes = blockDim.x * gridDim.x;
    detail::SpeciesLaneScratch<1> scratch(workspace, lane);
    double* fractions = scratch.array(0);
    const auto geometry = make_grid_geometry_view(grid);
    const auto read = [input](int index) { return input.load(index); };
    const int count = eligible_x * eligible_y;
    for (int logical = lane; logical < count; logical += lanes) {
        const int i = grid.is + 1 + logical % eligible_x;
        const int j = grid.js + logical / eligible_x;
        const int index = grid.index(i, j, 0);
        for (int s = 0; s < input.n_species; ++s)
            fractions[s] = input.species(s, index);
        const auto result = RzThermodynamics::check_restricted_interior_cell(
            read, geometry, i, j, index,
            input.n_species > 0 ? fractions : nullptr, input.n_species, bounds);
        if (result.status != state::Status::valid) {
            if (atomicCAS(failed, 0, 1) == 0) *diagnostic = result;
            // The original early host loop stops at its first rejected cell.
            break;
        }
    }
}

/** Gather/scatter conserved, diagnostic and composition values in cell order. */
__global__ void boundary_slice(DeviceStateView state, const int* indices,
    double* values, int count, bool write) {
    const int n = blockIdx.x * blockDim.x + threadIdx.x;
    if (n >= count) return;
    const int cell = indices[n], stride = 6 + state.n_species;
    double* fields[6]{state.rho, state.mom_u, state.mom_v, state.mom_w, state.eng, state.enuc_rate};
    for (int f = 0; f < 6; ++f) {
        if (write) fields[f][cell] = values[n * stride + f];
        else values[n * stride + f] = fields[f][cell];
    }
    for (int s = 0; s < state.n_species; ++s) {
        if (write) state.set_species(s, cell, values[n * stride + 6 + s]);
        else values[n * stride + 6 + s] = state.species(s, cell);
    }
}
/** Check every offset before launching a kernel that can touch state memory. */
void validate_indices(const DeviceStateView& state, std::span<const int> indices, bool ghosts,
    const DeviceGridView& grid) {
    if (indices.empty() || indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Invalid user boundary slice size");
    for (const int cell : indices) {
        if (cell < 0 || cell >= state.total_size) throw std::out_of_range("Boundary cell outside device allocation");
        const int k = cell / grid.stride_z, rem = cell - k * grid.stride_z;
        const int j = rem / grid.stride_y, i = rem - j * grid.stride_y;
        const bool active = i >= grid.is && i < grid.ie && j >= grid.js && j < grid.je && k >= grid.ks && k < grid.ke;
        if (ghosts == active) throw std::invalid_argument("Boundary slice has the wrong interior/ghost role");
    }
}

/** Authenticate real logical cells; allocation padding is never a prefix cell.
 * Only ghosts may be replaced by an immutable preceding candidate prefix.
 */
void validate_native_ghost_offset(int cell, const DeviceStateView& input,
    const DeviceGridView& grid)
{
    if (cell < 0 || cell >= input.total_size)
        throw std::out_of_range("Native boundary ghost outside device allocation");
    const int k = cell / grid.stride_z;
    const int rem = cell - k * grid.stride_z;
    const int j = rem / grid.stride_y, i = rem - j * grid.stride_y;
    if (i >= grid.total_x || j >= grid.total_y || k >= grid.total_z
        || (i >= grid.is && i < grid.ie && j >= grid.js && j < grid.je
            && k >= grid.ks && k < grid.ke))
        throw std::invalid_argument("Native boundary prefix/target must name a logical ghost");
}

/** Bound contiguous Native scratch by useful work, device lanes and free memory.
 * Reserve at least a device warp for N>0; the grid-stride kernel reuses those
 * lanes for arbitrarily many surface requests. No transport workspace is lent.
 */
std::size_t native_reflecting_lanes(int species, std::size_t count, int device)
{
    cudaDeviceProp properties{};
    check_cuda(cudaGetDeviceProperties(&properties, device),
        "query Native reflecting workspace device");
    std::size_t available = 0, total = 0;
    check_cuda(cudaMemGetInfo(&available, &total),
        "query Native reflecting workspace budget");
    constexpr std::size_t arrays = 11, budget_divisor = 64;
    if (species < 1 || static_cast<std::size_t>(species)
            > std::numeric_limits<std::size_t>::max() / (arrays * sizeof(double))
        || properties.warpSize <= 0 || properties.warpSize > kSpeciesKernelThreads
        || properties.multiProcessorCount <= 0 || properties.maxThreadsPerMultiProcessor <= 0)
        throw std::invalid_argument("Invalid Native reflecting workspace extent/device");
    const std::size_t warp = static_cast<std::size_t>(properties.warpSize);
    const std::size_t bytes_per_lane = static_cast<std::size_t>(species) * arrays * sizeof(double);
    const std::size_t hardware = static_cast<std::size_t>(properties.multiProcessorCount)
        * properties.maxThreadsPerMultiProcessor;
    const std::size_t useful = ((count + warp - 1) / warp) * warp;
    const std::size_t cap = std::min({hardware, useful,
        static_cast<std::size_t>(std::numeric_limits<int>::max())});
    const std::size_t lanes = (std::min(cap, (available / budget_divisor) / bytes_per_lane) / warp) * warp;
    if (lanes == 0)
        throw std::runtime_error("Native reflecting workspace budget cannot hold one device warp");
    return lanes;
}

/** Physical outer-face plane cells owned by one face direction and side. */
int physical_face_cells(const DeviceGridView& grid, int face) {
    const int direction = face / 2;
    const int extent[3]{grid.ie - grid.is, grid.je - grid.js, grid.ke - grid.ks};
    return extent[(direction + 1) % 3] * extent[(direction + 2) % 3];
}
}

std::optional<backend::NativeEosFailure> CudaBackend::validate_completed_native_eos_batch(
    std::span<const backend::BackendStateAccess> accesses, const state::Bounds& bounds)
{
    return impl_->validate_native_acceptance_batch(accesses, bounds,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        }, Impl::NativeAcceptanceMode::Completed).failure;
}

/** Classify only the exact committed refusal; this issues no retry authority.
 * Workflow: shared preflight authenticates its target, the resident active scan
 * joins all patches, and nonthermal evidence wins before requested_seen.
 */
backend::NativeActiveThermalInspection CudaBackend::classify_completed_native_active_thermal(
    std::span<const backend::BackendStateAccess> accesses, const state::Bounds& bounds,
    const backend::NativeEosFailure& original_refusal)
{
    auto result = impl_->validate_native_acceptance_batch(accesses, bounds,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        }, Impl::NativeAcceptanceMode::ClassifyActiveThermal, &original_refusal);
    return {result.requested_failure, std::move(result.failure)};
}

std::optional<backend::NativeEosFailure> CudaBackend::validate_completed_native_eos_batch(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> accesses, const state::Bounds& bounds)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    return impl_->validate_native_acceptance_batch(accesses, bounds,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged boundary requires Current");
            return resolve_staged_block(staged, access);
        }, Impl::NativeAcceptanceMode::Completed).failure;
}

std::optional<backend::NativeEosFailure> CudaBackend::inspect_native_restricted_interiors(
    backend::BackendTopologyStoreTransaction& transaction,
    std::span<const backend::BackendStateAccess> accesses, const state::Bounds& bounds)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    return impl_->validate_native_acceptance_batch(accesses, bounds,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged restricted interior requires Current");
            return resolve_staged_block(staged, access);
        }, Impl::NativeAcceptanceMode::RestrictedInterior).failure;
}

/** One private execution owner for completed, early and error-only traversals.
 * Workflow: retain the original full-domain preflight, selected EOS, scratch and
 * compact single-join diagnostics. Completed checks active before ghosts; early
 * inspection invokes no EOS. Active classification preserves the original
 * target and reports any nonthermal failure before its requested thermal bit.
 * No mode writes a field or publishes completion/retry authority.
 */
CudaBackend::Impl::NativeAcceptanceResult CudaBackend::Impl::validate_native_acceptance_batch(
    std::span<const backend::BackendStateAccess> accesses, const state::Bounds& bounds,
    const BlockResolver& resolve, NativeAcceptanceMode mode,
    const backend::NativeEosFailure* original_refusal)
{
    const auto slot = accesses.empty() ? state::StateSlot::Current : accesses.front().slot;
    if (!state::valid_bounds(bounds) || bounds.density != launch.density_floor
        || bounds.internal_min != launch.minimum_internal_energy
        || bounds.internal_max != launch.maximum_internal_energy)
        throw std::invalid_argument("Native EOS acceptance requires the frozen backend bounds");
    const int species = species_view.count;
    // Inspect the actual selected EOS/count before any launch, including a
    // monostate owner. No caller EOS enum or duplicate dispatch table is used.
    visit_eos(eos, [&](const auto& eos) {
        const int count = [&] {
            if constexpr (requires { eos.species.count; }) return eos.species.count;
            else return eos.specs.count;
        }();
        if (count != species)
            throw std::invalid_argument("Native EOS acceptance species owner mismatch");
    });
    if (!valid_species_workspace(species_workspace, species, 1))
        throw std::invalid_argument("Invalid Native EOS acceptance species workspace");
    struct ResidentPatch { DeviceStateView state; DeviceGridView grid; };
    std::vector<ResidentPatch> patches;
    patches.reserve(accesses.size());
    std::vector<const CudaBlockRuntime*> owners;
    owners.reserve(accesses.size());
    // Preflight every later entry before a kernel can inspect the first patch.
    for (const auto access : accesses) {
        if (access.slot != slot)
            throw std::invalid_argument("Mixed Native EOS acceptance slots");
        // The existing Host-compiled store resolver checks handle, epoch,
        // storage and slot. Compare its unique owners after resolving once;
        // NVCC 12.3 can leave implicit C++20 handle equality undefined.
        auto& block = resolve(access);
        const auto view = block.require_access(access);
        const auto& grid = block.grid;
        if (!valid_hydro_view(view) || !valid_hydro_grid(grid)
            || grid.semantics != GridMetrics::GeometrySemantics::AxisymmetricRz
            || grid.ng < 1 || grid.total_x < 3
            || view.total_size != grid.total_size || view.n_species != species)
            throw std::invalid_argument("Native EOS acceptance requires a complete resident logical patch");
        patches.push_back({view, grid});
        owners.push_back(&block);
    }
    std::size_t requested_patch = accesses.size();
    if (mode == NativeAcceptanceMode::ClassifyActiveThermal) {
        if (!original_refusal || original_refusal->access.slot != slot
            || !RzThermodynamics::is_retryable_thermal_failure(original_refusal->diagnostic))
            throw std::invalid_argument("Native active classification requires its original thermal refusal");
        // Resolve the original four-field access through the existing Host store
        // owner. Compare resolved addresses, never NVCC implicit handle equality.
        const auto* requested_owner = &resolve(original_refusal->access);
        const auto found = std::find(owners.begin(), owners.end(), requested_owner);
        if (found == owners.end())
            throw std::invalid_argument("Native thermal refusal is outside the actual batch");
        requested_patch = static_cast<std::size_t>(found - owners.begin());
        const auto& grid = patches[requested_patch].grid;
        const auto& target = original_refusal->diagnostic;
        if (target.i < grid.is || target.i >= grid.ie || target.j < grid.js || target.j >= grid.je
            || target.index != grid.index(target.i, target.j, 0))
            throw std::invalid_argument("Native thermal refusal must retain its original active cell");
    }
    std::sort(owners.begin(), owners.end(), std::less<>{});
    if (std::adjacent_find(owners.begin(), owners.end()) != owners.end())
        throw std::invalid_argument("Duplicate Native EOS acceptance block");
    if (accesses.empty()) return {};
    select_device();
    auto& scratch = hydro_batch;
    scratch.ensure_capacity(accesses.size());
    auto& device_diagnostics = native_eos_diagnostics;
    device_diagnostics.reserve(accesses.size());
    std::vector<RzThermodynamics::AcceptanceDiagnostic> diagnostics(accesses.size());
    static_assert(std::is_trivially_copyable_v<RzThermodynamics::AcceptanceDiagnostic>);
    // Host destinations and reusable device owners outlive the guard on every
    // enqueue/launch/download exception. Calls on this backend remain serial.
    CudaQuiescenceGuard work_guard{*this};
    check_cuda(cudaMemsetAsync(scratch.status->get(), 0, accesses.size() * sizeof(int),
        stream.get()), "clear Native EOS acceptance status");
    check_cuda(cudaMemsetAsync(device_diagnostics.get(), 0,
        accesses.size() * sizeof(RzThermodynamics::AcceptanceDiagnostic), stream.get()),
        "clear Native EOS acceptance diagnostics");
    if (mode == NativeAcceptanceMode::RestrictedInterior) {
        // Failure-only early inspection performs no thermodynamic EOS query;
        // shared preflight still validates the selected EOS owner/species count.
        const int threads = detail::species_launch_threads(species_workspace);
        for (std::size_t index = 0; index < patches.size(); ++index) {
            const auto& patch = patches[index];
            const int cells = patch.grid.total_x * patch.grid.total_y;
            inspect_native_restricted_interior_cells<<<detail::species_launch_blocks(cells,
                species_workspace), threads, 0, stream.get()>>>(
                    patch.state, patch.grid, bounds, species_workspace,
                    scratch.status->get() + index, device_diagnostics.get() + index);
            check_cuda(cudaGetLastError(), "inspect staged Native restricted interiors");
            ++runtime_counters.kernel_count;
        }
    } else visit_eos(eos, [&](const auto& eos) {
        const int threads = detail::species_launch_threads(species_workspace);
        for (std::size_t index = 0; index < patches.size(); ++index) {
            const auto& patch = patches[index];
            const int cells = patch.grid.total_x * patch.grid.total_y;
            if (mode == NativeAcceptanceMode::ClassifyActiveThermal) {
                const int requested_index = index == requested_patch
                    ? original_refusal->diagnostic.index : -1;
                validate_completed_native_eos<true><<<detail::species_launch_blocks(cells,
                    species_workspace), threads, 0, stream.get()>>>(
                        patch.state, patch.grid, bounds, eos, species_workspace,
                        scratch.status->get() + index, device_diagnostics.get() + index,
                        false, requested_index);
                check_cuda(cudaGetLastError(), "classify resident Native active thermal failures");
                ++runtime_counters.kernel_count;
            } else {
                validate_completed_native_eos<false><<<detail::species_launch_blocks(cells,
                    species_workspace), threads, 0, stream.get()>>>(
                        patch.state, patch.grid, bounds, eos, species_workspace,
                        scratch.status->get() + index, device_diagnostics.get() + index,
                        false, -1);
                check_cuda(cudaGetLastError(), "validate resident Native active EOS cells");
                ++runtime_counters.kernel_count;
                // Same-stream launch: active refusal skips this patch's ghost
                // EOS; success checks every remaining logical cell once.
                validate_completed_native_eos<false><<<detail::species_launch_blocks(cells,
                    species_workspace), threads, 0, stream.get()>>>(
                        patch.state, patch.grid, bounds, eos, species_workspace,
                        scratch.status->get() + index, device_diagnostics.get() + index,
                        true, -1);
                check_cuda(cudaGetLastError(), "validate resident Native ghost EOS cells");
                ++runtime_counters.kernel_count;
            }
        }
    });
    check_cuda(cudaMemcpyAsync(scratch.host_status.data(), scratch.status->get(),
        accesses.size() * sizeof(int), cudaMemcpyDeviceToHost, stream.get()),
        "download Native EOS acceptance status");
    check_cuda(cudaMemcpyAsync(diagnostics.data(), device_diagnostics.get(),
        accesses.size() * sizeof(RzThermodynamics::AcceptanceDiagnostic),
        cudaMemcpyDeviceToHost, stream.get()), "download Native EOS acceptance diagnostics");
    checked_quiesce("synchronize CUDA backend");
    work_guard.completed = true;
    runtime_counters.bytes_d2h += accesses.size()
        * (sizeof(int) + sizeof(RzThermodynamics::AcceptanceDiagnostic));
    if (mode == NativeAcceptanceMode::ClassifyActiveThermal) {
        // Inspect every patch's fatal bit before permitting the requested target.
        // A thermal target in an earlier patch cannot hide a later fatal cell.
        for (std::size_t index = 0; index < accesses.size(); ++index)
            if ((scratch.host_status[index] & 2) != 0)
                return {backend::NativeEosFailure{accesses[index], diagnostics[index]}, false};
        return {{}, (scratch.host_status[requested_patch] & 1) != 0};
    }
    for (std::size_t index = 0; index < accesses.size(); ++index)
        if (scratch.host_status[index] != 0)
            return {backend::NativeEosFailure{accesses[index], diagnostics[index]}, false};
    return {};
}

/** Consume actual resident storage and the selected backend EOS for one layer.
 * All leases, requests and complete ghost prefix shapes are preflighted before
 * any device touch. Every sibling reads the same input/prefix; candidate buffers
 * are disjoint and only a successful joined layer returns surface values.
 */
backend::BoundaryCells CudaBackend::prepare_native_reflecting_layer(
    backend::BackendStateAccess access,
    std::span<const boundary::native_rz_math::Request> requests,
    const state::Bounds& bounds, std::span<const int> prefix_indices,
    const backend::BoundaryCells* prefix)
{
    return impl_->prepare_native_reflecting_layer(access, requests, bounds, prefix_indices, prefix,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
}

backend::BoundaryCells CudaBackend::prepare_native_reflecting_layer(
    backend::BackendTopologyStoreTransaction& transaction,
    backend::BackendStateAccess access,
    std::span<const boundary::native_rz_math::Request> requests,
    const state::Bounds& bounds, std::span<const int> prefix_indices,
    const backend::BoundaryCells* prefix)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    return impl_->prepare_native_reflecting_layer(access, requests, bounds, prefix_indices, prefix,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged boundary requires Current");
            return resolve_staged_block(staged, access);
        });
}

backend::BoundaryCells CudaBackend::Impl::prepare_native_reflecting_layer(
    backend::BackendStateAccess access,
    std::span<const boundary::native_rz_math::Request> requests,
    const state::Bounds& bounds, std::span<const int> prefix_indices,
    const backend::BoundaryCells* prefix,
    const BlockResolver& resolve)
{
    namespace math = boundary::native_rz_math;
    auto& block = resolve(access);
    const auto input = block.require_access(access);
    const auto& grid = block.grid;
    const int species = species_view.count;
    if (!valid_hydro_view(input) || !valid_hydro_grid(grid)
        || grid.semantics != GridMetrics::GeometrySemantics::AxisymmetricRz
        || grid.ng < 1 || input.total_size != grid.total_size
        || input.n_species != species || species_count != species)
        throw std::invalid_argument("Native reflector requires an actual complete resident Native patch");
    if (!state::valid_bounds(bounds) || bounds.density != launch.density_floor
        || bounds.internal_min != launch.minimum_internal_energy
        || bounds.internal_max != launch.maximum_internal_energy)
        throw std::invalid_argument("Native reflector requires the frozen backend bounds");
    visit_eos(eos, [&](const auto& eos) {
        const int count = [&] {
            if constexpr (requires { eos.species.count; }) return eos.species.count;
            else return eos.specs.count;
        }();
        if (count != species)
            throw std::invalid_argument("Native reflector selected EOS species owner mismatch");
    });
    const std::size_t maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (requests.empty() || requests.size() > maximum || prefix_indices.size() > maximum
        || (species > 0 && (requests.size() > std::numeric_limits<std::size_t>::max()
            / static_cast<std::size_t>(species)
            || prefix_indices.size() > std::numeric_limits<std::size_t>::max()
                / static_cast<std::size_t>(species))))
        throw std::invalid_argument("Invalid Native reflector surface size");
    const math::Context context{make_grid_geometry_view(grid), grid.total_x,
        grid.total_y, grid.is, grid.ie, grid.js, grid.je};
    std::vector<int> destinations;
    destinations.reserve(requests.size());
    for (const auto& request : requests) {
        math::CellSupport source{}, target{};
        int support_begin = 0;
        if (request.axis != requests.front().axis
            || math::validate_request(context, request) != math::Status::valid
            || math::support(context, request.source, source) != math::Status::valid
            || math::support(context, request.destination, target) != math::Status::valid
            || math::source_support_begin(context, request.source[0], support_begin) != math::Status::valid)
            throw std::invalid_argument("Invalid Native reflector request/support or mixed axis layer");
        const int destination = grid.index(request.destination[0], request.destination[1]);
        validate_native_ghost_offset(destination, input, grid);
        // The sole support selector bounds three actual radial observations;
        // require their flattened offsets before the first kernel can read U.
        for (int n = 0; n < 3; ++n) {
            const int index = grid.index(support_begin + n, request.source[1]);
            if (support_begin + n < 0 || support_begin + n >= grid.total_x
                || index < 0 || index >= input.total_size)
                throw std::invalid_argument("Native reflector support lies outside logical storage");
        }
        destinations.push_back(destination);
    }
    std::sort(destinations.begin(), destinations.end());
    if (std::adjacent_find(destinations.begin(), destinations.end()) != destinations.end())
        throw std::invalid_argument("Duplicate Native reflector destination");
    if ((!prefix && !prefix_indices.empty())
        || (prefix && (prefix->species_count != static_cast<std::size_t>(species)
            || prefix->conserved.size() != prefix_indices.size()
            || prefix->enuc.size() != prefix_indices.size()
            || prefix->composition.size() != prefix_indices.size() * static_cast<std::size_t>(species))))
        throw std::invalid_argument("Mismatched Native reflector prefix shape");
    for (std::size_t n = 0; n < prefix_indices.size(); ++n) {
        if (n > 0 && prefix_indices[n - 1] >= prefix_indices[n])
            throw std::invalid_argument("Native reflector prefix offsets must be sorted and unique");
        validate_native_ghost_offset(prefix_indices[n], input, grid);
        const auto& u = prefix->conserved[n];
        if (!std::isfinite(u.rho) || !std::isfinite(u.mom_u) || !std::isfinite(u.mom_v)
            || !std::isfinite(u.mom_w) || !std::isfinite(u.eng) || !std::isfinite(prefix->enuc[n]))
            throw std::invalid_argument("Nonfinite Native reflector prefix fields");
        for (int s = 0; s < species; ++s)
            if (!std::isfinite(prefix->composition[n * static_cast<std::size_t>(species) + s]))
                throw std::invalid_argument("Nonfinite Native reflector prefix composition");
    }

    select_device();
    // Allocation growth/reuse may release prior capacity: establish the owner's
    // stream witness before touching any reusable request/prefix/candidate row.
    checked_quiesce("quiesce before Native reflector scratch reuse");
    auto& scratch = native_reflecting;
    scratch.requests.reserve(requests.size());
    scratch.conserved.reserve(requests.size());
    scratch.enuc.reserve(requests.size());
    scratch.fractions.reserve(requests.size() * static_cast<std::size_t>(species));
    scratch.failed.reserve(1);
    scratch.prefix_indices.reserve(prefix_indices.size());
    scratch.prefix_conserved.reserve(prefix_indices.size());
    scratch.prefix_enuc.reserve(prefix_indices.size());
    scratch.prefix_fractions.reserve(prefix_indices.size() * static_cast<std::size_t>(species));
    if (species > 0 && (!scratch.workspace.values
        || !valid_species_workspace(scratch.workspace, species, 11))) {
        const auto lanes = native_reflecting_lanes(species, requests.size(), device_ordinal);
        scratch.workspace_storage.reserve(lanes * static_cast<std::size_t>(species) * 11);
        scratch.workspace = {scratch.workspace_storage.get(), scratch.workspace_storage.size(),
            static_cast<int>(lanes), species, 11};
    }
    const NativeBoundaryPrefixView device_prefix{scratch.prefix_indices.get(),
        scratch.prefix_conserved.get(), scratch.prefix_fractions.get(), scratch.prefix_enuc.get(),
        static_cast<int>(prefix_indices.size()), species};
    backend::BoundaryCells result;
    result.species_count = species;
    result.conserved.resize(requests.size());
    result.enuc.resize(requests.size());
    result.composition.resize(requests.size() * static_cast<std::size_t>(species));
    int failed = 0;
    // All asynchronous Host sources/destinations and reusable device owners
    // outlive this guard on upload, launch, status or candidate-copy exceptions.
    CudaQuiescenceGuard work_guard{*this};
    enqueue_cuda_metadata_upload(scratch.requests.get(), requests.data(), requests.size_bytes(),
        stream.get(), runtime_counters, "upload Native reflector requests");
    if (!prefix_indices.empty()) {
        enqueue_cuda_metadata_upload(scratch.prefix_indices.get(), prefix_indices.data(),
            prefix_indices.size_bytes(), stream.get(), runtime_counters,
            "upload Native reflector prefix offsets");
        enqueue_cuda_metadata_upload(scratch.prefix_conserved.get(), prefix->conserved.data(),
            prefix->conserved.size() * sizeof(FluidVector), stream.get(), runtime_counters,
            "upload Native reflector prefix conserved");
        enqueue_cuda_metadata_upload(scratch.prefix_enuc.get(), prefix->enuc.data(),
            prefix->enuc.size() * sizeof(double), stream.get(), runtime_counters,
            "upload Native reflector prefix ENUC");
        if (species > 0)
            enqueue_cuda_metadata_upload(scratch.prefix_fractions.get(), prefix->composition.data(),
                prefix->composition.size() * sizeof(double), stream.get(), runtime_counters,
                "upload Native reflector prefix composition");
    }
    check_cuda(cudaMemsetAsync(scratch.failed.get(), 0, sizeof(int), stream.get()),
        "clear Native reflector layer status");
    visit_eos(eos, [&](const auto& eos) {
        check_cuda(launch_native_reflecting_candidates(input, grid, scratch.requests.get(),
            static_cast<int>(requests.size()), bounds, eos, scratch.workspace,
            scratch.conserved.get(), scratch.fractions.get(), scratch.failed.get(),
            stream.get(), device_prefix, scratch.enuc.get()), "prepare Native reflector layer");
        ++runtime_counters.kernel_count;
    });
    check_cuda(cudaMemcpyAsync(&failed, scratch.failed.get(), sizeof(int),
        cudaMemcpyDeviceToHost, stream.get()), "download Native reflector layer status");
    runtime_counters.bytes_d2h += sizeof(int);
    checked_quiesce("synchronize CUDA backend");
    if (failed != 0)
        throw std::runtime_error("Native reflecting layer failed shared math/EOS acceptance: "
            + std::to_string(failed));
    check_cuda(cudaMemcpyAsync(result.conserved.data(), scratch.conserved.get(),
        result.conserved.size() * sizeof(FluidVector), cudaMemcpyDeviceToHost, stream.get()),
        "download Native reflector conserved candidates");
    runtime_counters.bytes_d2h += result.conserved.size() * sizeof(FluidVector);
    check_cuda(cudaMemcpyAsync(result.enuc.data(), scratch.enuc.get(), result.enuc.size() * sizeof(double),
        cudaMemcpyDeviceToHost, stream.get()), "download Native reflector inherited ENUC");
    runtime_counters.bytes_d2h += result.enuc.size() * sizeof(double);
    if (species > 0) {
        check_cuda(cudaMemcpyAsync(result.composition.data(), scratch.fractions.get(),
            result.composition.size() * sizeof(double), cudaMemcpyDeviceToHost, stream.get()),
            "download Native reflector composition candidates");
        runtime_counters.bytes_d2h += result.composition.size() * sizeof(double);
    }
    checked_quiesce("synchronize CUDA backend");
    work_guard.completed = true;
    return result;
}

backend::BoundaryCells CudaBackend::read_boundary_cells(backend::BackendStateAccess access,
    std::span<const int> indices, state::StateRegion region)
{
    return impl_->read_boundary_cells(access, indices, region,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
}

backend::BoundaryCells CudaBackend::read_boundary_cells(
    backend::BackendTopologyStoreTransaction& transaction,
    backend::BackendStateAccess access,
    std::span<const int> indices, state::StateRegion region)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    return impl_->read_boundary_cells(access, indices, region,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged boundary requires Current");
            return resolve_staged_block(staged, access);
        });
}

backend::BoundaryCells CudaBackend::Impl::read_boundary_cells(backend::BackendStateAccess access,
    std::span<const int> indices, state::StateRegion region,
    const BlockResolver& resolve)
{
    auto& block = resolve(access);
    const auto state = block.require_access(access);
    if(region!=state::StateRegion::Interior && region!=state::StateRegion::Ghost)
        throw std::invalid_argument("Unknown boundary snapshot region");
    validate_indices(state, indices, region==state::StateRegion::Ghost, block.grid);
    select_device();
    const std::size_t stride = 6 + state.n_species;
    std::vector<double> packed(indices.size() * stride);
    block.user_boundary_indices.reserve(indices.size());
    block.user_boundary_values.reserve(packed.size());
    check_cuda(cudaMemcpyAsync(block.user_boundary_indices.get(), indices.data(), indices.size_bytes(),
        cudaMemcpyHostToDevice, stream.get()), "upload user boundary donors");
    boundary_slice<<<(indices.size() + 127) / 128, 128, 0, stream.get()>>>(state,
        block.user_boundary_indices.get(), block.user_boundary_values.get(), static_cast<int>(indices.size()), false);
    check_cuda(cudaGetLastError(), "gather user boundary donors");
    check_cuda(cudaMemcpyAsync(packed.data(), block.user_boundary_values.get(), packed.size() * sizeof(double),
        cudaMemcpyDeviceToHost, stream.get()), "download boundary slice");
    checked_quiesce("synchronize CUDA backend");
    runtime_counters.bytes_h2d += indices.size_bytes();
    runtime_counters.bytes_d2h += packed.size() * sizeof(double);
    ++runtime_counters.kernel_count;
    backend::BoundaryCells result;
    result.species_count = state.n_species;
    result.conserved.resize(indices.size()); result.enuc.resize(indices.size());
    result.composition.resize(indices.size() * state.n_species);
    for (std::size_t n = 0; n < indices.size(); ++n) {
        const auto* p = packed.data() + n * stride;
        result.conserved[n] = {p[0], p[1], p[2], p[3], p[4]}; result.enuc[n] = p[5];
        if (state.n_species) std::copy_n(p + 6, state.n_species, result.composition.data() + n * state.n_species);
    }
    return result;
}

void CudaBackend::write_boundary_cells(backend::BackendStateAccess access, std::span<const int> indices,
    const backend::BoundaryCells& values, const boundary::DiffusionBoundaryStorage& controls)
{
    impl_->write_boundary_cells(access, indices, values, controls,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            return impl_->require_block(access);
        });
}

void CudaBackend::write_boundary_cells(
    backend::BackendTopologyStoreTransaction& transaction,
    backend::BackendStateAccess access, std::span<const int> indices,
    const backend::BoundaryCells& values, const boundary::DiffusionBoundaryStorage& controls)
{
    auto& staged = require_staged_boundary_transaction(transaction);
    impl_->write_boundary_cells(access, indices, values, controls,
        [&](backend::BackendStateAccess access) -> CudaBlockRuntime& {
            if (access.slot != state::StateSlot::Current)
                throw std::invalid_argument("staged boundary requires Current");
            return resolve_staged_block(staged, access);
        });
}

void CudaBackend::Impl::write_boundary_cells(backend::BackendStateAccess access, std::span<const int> indices,
    const backend::BoundaryCells& values, const boundary::DiffusionBoundaryStorage& controls,
    const BlockResolver& resolve)
{
    auto& block = resolve(access);
    auto state = block.require_access(access);
    std::size_t owner = block.state_storage.size();
    for (std::size_t candidate = 0; candidate < block.state_storage.size(); ++candidate)
        if (block.state_storage[candidate].rho.get() == state.rho) owner = candidate;
    if (owner == block.state_storage.size())
        throw std::logic_error("User ghost slice has no state allocation owner");
    // Preflight every actual control plane before any ghost transfer or write.
    // Non-null DiffusionBoundaryView records use a fixed actual physical-face
    // shape. A macro savepoint pins the original allocation while stage values
    // and null/non-null bindings may change within its existing capacity.
    for (int face = 0; face < 6; ++face) {
        const auto& source = controls.faces[face];
        const auto& destination = block.user_boundary_controls[owner][face];
        const std::size_t expected = static_cast<std::size_t>(physical_face_cells(block.grid, face))
            * static_cast<std::size_t>(4 + state.n_species);
        if (!source.empty() && source.size() != expected)
            throw std::invalid_argument("Diffusion boundary controls do not match the actual face shape");
        if (macro_state.active && source.size() > destination.size())
            throw std::logic_error("CUDA macro savepoint forbids scalar-control allocation growth");
    }
    validate_indices(state, indices, true, block.grid);
    if (values.species_count != static_cast<std::size_t>(state.n_species) || values.conserved.size() != indices.size()
        || values.enuc.size() != indices.size() || values.composition.size() != indices.size() * values.species_count)
        throw std::invalid_argument("Mismatched user boundary result shape");
    // Corners have a deterministic final writer. Collapse duplicate offsets on
    // the Host so the device scatter has no write races.
    std::map<int, std::size_t> final;
    for (std::size_t n = 0; n < indices.size(); ++n) final[indices[n]] = n;
    std::vector<int> destinations; std::vector<double> packed;
    for (const auto& [cell, n] : final) {
        destinations.push_back(cell); const auto& u = values.conserved[n];
        packed.insert(packed.end(), {u.rho, u.mom_u, u.mom_v, u.mom_w, u.eng, values.enuc[n]});
        packed.insert(packed.end(), values.composition.begin() + n * state.n_species,
            values.composition.begin() + (n + 1) * state.n_species);
    }
    select_device();
    block.user_boundary_indices.reserve(destinations.size()); block.user_boundary_values.reserve(packed.size());
    check_cuda(cudaMemcpyAsync(block.user_boundary_indices.get(), destinations.data(), destinations.size() * sizeof(int),
        cudaMemcpyHostToDevice, stream.get()), "upload user ghost offsets");
    check_cuda(cudaMemcpyAsync(block.user_boundary_values.get(), packed.data(), packed.size() * sizeof(double),
        cudaMemcpyHostToDevice, stream.get()), "upload user ghost values");
    boundary_slice<<<(destinations.size() + 127) / 128, 128, 0, stream.get()>>>(state,
        block.user_boundary_indices.get(), block.user_boundary_values.get(), static_cast<int>(destinations.size()), true);
    check_cuda(cudaGetLastError(), "scatter user ghost slice");
    std::size_t bytes = 0;
    auto& slot = block.slots[slot_index(access.slot)];
    // Slot views rotate, allocations do not. Controls follow the physical
    // state owner, so filling Yprev cannot overwrite cached Y0 controls.
    for (int face = 0; face < 6; ++face) {
        const auto& source = controls.faces[face];
        auto& destination = block.user_boundary_controls[owner][face];
        if (source.empty()) { slot.diffusion_boundary.faces[face] = nullptr; continue; }
        destination.reserve(source.size());
        check_cuda(cudaMemcpyAsync(destination.get(), source.data(), source.size() * sizeof(source[0]),
            cudaMemcpyHostToDevice, stream.get()), "upload diffusion boundary controls");
        slot.diffusion_boundary.faces[face] = destination.get();
        bytes += source.size() * sizeof(source[0]);
    }
    checked_quiesce("synchronize CUDA backend");
    runtime_counters.bytes_h2d += destinations.size() * sizeof(int) + packed.size() * sizeof(double) + bytes;
    ++runtime_counters.kernel_count;
}


void CudaBackend::configure_boundary_flux_capture(
    std::span<const backend::BoundaryFluxPlanes> layout, double weight,
    double initial_weight, bool save_initial) {
    // A resident savepoint allows stage weights and values to change, but its
    // fixed observer layout must have been configured before macro entry.
    // Validate the whole batch before any weight, shape, allocation or memset.
    if (impl_->macro_state.active) {
        for (const backend::BoundaryFluxPlanes& planes : layout) {
            CudaBlockRuntime* found = impl_->find_block(planes.block);
            if (found == nullptr)
                throw std::invalid_argument("boundary flux capture names an unknown CUDA block");
            const auto& observer = found->boundary_flux_observer;
            for (int face = 0; face < 6; ++face) {
                const std::size_t elements = planes.stage[face].size();
                if (observer.active_elements[face] != elements
                    || observer.owned[face] != (elements != 0)
                    || observer.stage[face].size() < elements
                    || observer.initial[face].size() < elements)
                    throw std::logic_error("CUDA macro savepoint forbids observer layout changes");
            }
        }
    }
    impl_->select_device();
    for (const backend::BoundaryFluxPlanes& planes : layout) {
        CudaBlockRuntime* found = impl_->find_block(planes.block);
        if (found == nullptr)
            throw std::invalid_argument(
                "boundary flux capture names an unknown CUDA block");
        auto& observer = found->boundary_flux_observer;
        const int fields = 6 + found->state_storage[0].species_count;
        for (int face = 0; face < 6; ++face) {
            const std::size_t cells = planes.stage[face].size();
            observer.active_elements[face] = cells;
            if (cells == 0) { observer.owned[face] = false; continue; }
            if (cells % static_cast<std::size_t>(fields) != 0
                || cells / static_cast<std::size_t>(fields)
                    != static_cast<std::size_t>(physical_face_cells(found->grid, face)))
                throw std::invalid_argument(
                    "boundary flux capture plane does not match the owned face");
            // Grow only: the layout is a shape, never a placeholder payload.
            observer.stage[face].reserve(cells);
            observer.initial[face].reserve(cells);
            observer.owned[face] = true;
            // Zero surfaces before the next operator; an inactive transport
            // face contributes zero rather than retaining a prior stage.
            check_cuda(cudaMemsetAsync(observer.stage[face].get(),0,cells*sizeof(double),
                impl_->stream.get()), "clear boundary stage observer");
            if(save_initial) check_cuda(cudaMemsetAsync(observer.initial[face].get(),0,
                cells*sizeof(double),impl_->stream.get()), "clear boundary initial observer");
        }
        observer.weight = weight;
        observer.initial_weight = initial_weight;
        observer.save_initial = save_initial;
        // Refresh every cached slot binding so the next execute uses the
        // current weights; only metadata changes, never state values.
        const auto view = observer.view();
        for (std::size_t slot = 0; slot < found->slots.size(); ++slot) {
            found->state_storage[slot].capture = view;
            found->slots[slot].capture = view;
        }
    }
}

std::vector<backend::BoundaryFluxPlanes>
CudaBackend::download_boundary_flux_capture() {
    impl_->select_device();
    std::vector<backend::BoundaryFluxPlanes> result;
    // Arena-slot order is stable, so block order is reproducible on Host.
    std::size_t bytes = 0;
    for (auto& entry : impl_->active_resources) {
        CudaBlockRuntime& runtime = *entry.second;
        const auto& observer = runtime.boundary_flux_observer;
        backend::BoundaryFluxPlanes planes;
        planes.block = runtime.handle;
        for (int face = 0; face < 6; ++face) {
            const std::size_t cells = observer.owned[face]
                ? observer.active_elements[face] : 0;
            if (cells == 0) continue;
            planes.stage[face].resize(cells);
            check_cuda(cudaMemcpyAsync(planes.stage[face].data(),
                observer.stage[face].get(), cells * sizeof(double),
                cudaMemcpyDeviceToHost, impl_->stream.get()),
                "download physical surface flux observer");
            bytes += cells * sizeof(double);
        }
        // The initial planes stay device-resident: Host integrates the
        // weighted stage value, so only owned stage planes cross.
        result.push_back(std::move(planes));
    }
    if (bytes != 0) quiesce();
    impl_->runtime_counters.bytes_d2h += bytes;
    return result;
}
} // namespace arch::cuda
