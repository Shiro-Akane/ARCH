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
 * 6. Check completed native logical cells through the shared EOS acceptance
 *    leaf; return compact diagnostics without field transfer or state writes.
 *
 * Observer planes are sized by the Host-provided surface layout and bound to
 * all three stage slots; no state values cross this service. No boundary
 * formula is duplicated here. Built-in BCs never call this service.
 */
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

#include "cuda/runtime/control/CudaBackendInternal.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "cuda/hydro/policies/CheckedHydroEos.cuh"

namespace arch::cuda {
namespace {
/** Traverse logical cells only; stride padding is neither a cell nor support.
 * Every lane owns a sticky required-query latch in shared memory. The patch's
 * compact status arbitrates only the diagnostic writer, never EOS queries.
 */
template<class Eos>
__global__ void validate_completed_native_eos(DeviceStateView input,
    DeviceGridView grid, state::Bounds bounds, Eos eos,
    SpeciesWorkspaceView workspace, int* failed,
    RzThermodynamics::AcceptanceDiagnostic* diagnostic)
{
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
    const int count = grid.total_x * grid.total_y;
    for (int logical = lane; logical < count; logical += lanes) {
        const int i = logical % grid.total_x, j = logical / grid.total_x;
        const int index = grid.index(i, j, 0);
        for (int s = 0; s < input.n_species; ++s)
            fractions[s] = input.species(s, index);
        const auto result = RzThermodynamics::detail::check_patch_eos_cell(
            geometry, input.n_species, bounds, checked, i, j, index,
            closure, read, input.n_species > 0 ? fractions : nullptr);
        if (result.status != state::Status::valid) {
            if (atomicCAS(failed, 0, 1) == 0) *diagnostic = result;
            // Never clear a required-query failure for a later cell/node.
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
    const auto slot = accesses.empty() ? state::StateSlot::Current : accesses.front().slot;
    if (!state::valid_bounds(bounds) || bounds.density != impl_->launch.density_floor
        || bounds.internal_min != impl_->launch.minimum_internal_energy
        || bounds.internal_max != impl_->launch.maximum_internal_energy)
        throw std::invalid_argument("Native EOS acceptance requires the frozen backend bounds");
    const int species = impl_->species_view.count;
    // Inspect the actual selected EOS/count before any launch, including a
    // monostate owner. No caller EOS enum or duplicate dispatch table is used.
    visit_eos(impl_->eos, [&](const auto& eos) {
        const int count = [&] {
            if constexpr (requires { eos.species.count; }) return eos.species.count;
            else return eos.specs.count;
        }();
        if (count != species)
            throw std::invalid_argument("Native EOS acceptance species owner mismatch");
    });
    if (!valid_species_workspace(impl_->species_workspace, species, 1))
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
        auto& block = impl_->require_block(access);
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
    std::sort(owners.begin(), owners.end(), std::less<>{});
    if (std::adjacent_find(owners.begin(), owners.end()) != owners.end())
        throw std::invalid_argument("Duplicate Native EOS acceptance block");
    if (accesses.empty()) return std::nullopt;
    impl_->select_device();
    auto& scratch = impl_->hydro_batch;
    scratch.ensure_capacity(accesses.size());
    auto& device_diagnostics = impl_->native_eos_diagnostics;
    device_diagnostics.reserve(accesses.size());
    std::vector<RzThermodynamics::AcceptanceDiagnostic> diagnostics(accesses.size());
    static_assert(std::is_trivially_copyable_v<RzThermodynamics::AcceptanceDiagnostic>);
    // Host destinations and reusable device owners outlive the guard on every
    // enqueue/launch/download exception. Calls on this backend remain serial.
    CudaQuiescenceGuard work_guard{*impl_};
    check_cuda(cudaMemsetAsync(scratch.status->get(), 0, accesses.size() * sizeof(int),
        impl_->stream.get()), "clear Native EOS acceptance status");
    check_cuda(cudaMemsetAsync(device_diagnostics.get(), 0,
        accesses.size() * sizeof(RzThermodynamics::AcceptanceDiagnostic), impl_->stream.get()),
        "clear Native EOS acceptance diagnostics");
    visit_eos(impl_->eos, [&](const auto& eos) {
        const int threads = detail::species_launch_threads(impl_->species_workspace);
        for (std::size_t index = 0; index < patches.size(); ++index) {
            const auto& patch = patches[index];
            const int cells = patch.grid.total_x * patch.grid.total_y;
            validate_completed_native_eos<<<detail::species_launch_blocks(cells,
                impl_->species_workspace), threads, 0, impl_->stream.get()>>>(
                    patch.state, patch.grid, bounds, eos, impl_->species_workspace,
                    scratch.status->get() + index, device_diagnostics.get() + index);
            check_cuda(cudaGetLastError(), "validate resident Native EOS patch");
            ++impl_->runtime_counters.kernel_count;
        }
    });
    check_cuda(cudaMemcpyAsync(scratch.host_status.data(), scratch.status->get(),
        accesses.size() * sizeof(int), cudaMemcpyDeviceToHost, impl_->stream.get()),
        "download Native EOS acceptance status");
    check_cuda(cudaMemcpyAsync(diagnostics.data(), device_diagnostics.get(),
        accesses.size() * sizeof(RzThermodynamics::AcceptanceDiagnostic),
        cudaMemcpyDeviceToHost, impl_->stream.get()), "download Native EOS acceptance diagnostics");
    quiesce();
    work_guard.completed = true;
    impl_->runtime_counters.bytes_d2h += accesses.size()
        * (sizeof(int) + sizeof(RzThermodynamics::AcceptanceDiagnostic));
    for (std::size_t index = 0; index < accesses.size(); ++index)
        if (scratch.host_status[index] != 0)
            return backend::NativeEosFailure{accesses[index], diagnostics[index]};
    return std::nullopt;
}

backend::BoundaryCells CudaBackend::read_boundary_cells(backend::BackendStateAccess access,
    std::span<const int> indices, state::StateRegion region) {
    auto& block = impl_->require_block(access);
    const auto state = block.require_access(access);
    if(region!=state::StateRegion::Interior && region!=state::StateRegion::Ghost)
        throw std::invalid_argument("Unknown boundary snapshot region");
    validate_indices(state, indices, region==state::StateRegion::Ghost, block.grid);
    impl_->select_device();
    const std::size_t stride = 6 + state.n_species;
    std::vector<double> packed(indices.size() * stride);
    block.user_boundary_indices.reserve(indices.size());
    block.user_boundary_values.reserve(packed.size());
    check_cuda(cudaMemcpyAsync(block.user_boundary_indices.get(), indices.data(), indices.size_bytes(),
        cudaMemcpyHostToDevice, impl_->stream.get()), "upload user boundary donors");
    boundary_slice<<<(indices.size() + 127) / 128, 128, 0, impl_->stream.get()>>>(state,
        block.user_boundary_indices.get(), block.user_boundary_values.get(), static_cast<int>(indices.size()), false);
    check_cuda(cudaGetLastError(), "gather user boundary donors");
    check_cuda(cudaMemcpyAsync(packed.data(), block.user_boundary_values.get(), packed.size() * sizeof(double),
        cudaMemcpyDeviceToHost, impl_->stream.get()), "download boundary slice");
    quiesce();
    impl_->runtime_counters.bytes_h2d += indices.size_bytes();
    impl_->runtime_counters.bytes_d2h += packed.size() * sizeof(double);
    ++impl_->runtime_counters.kernel_count;
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
    const backend::BoundaryCells& values, const boundary::DiffusionBoundaryStorage& controls) {
    auto& block = impl_->require_block(access);
    auto state = block.require_access(access);
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
    impl_->select_device();
    block.user_boundary_indices.reserve(destinations.size()); block.user_boundary_values.reserve(packed.size());
    check_cuda(cudaMemcpyAsync(block.user_boundary_indices.get(), destinations.data(), destinations.size() * sizeof(int),
        cudaMemcpyHostToDevice, impl_->stream.get()), "upload user ghost offsets");
    check_cuda(cudaMemcpyAsync(block.user_boundary_values.get(), packed.data(), packed.size() * sizeof(double),
        cudaMemcpyHostToDevice, impl_->stream.get()), "upload user ghost values");
    boundary_slice<<<(destinations.size() + 127) / 128, 128, 0, impl_->stream.get()>>>(state,
        block.user_boundary_indices.get(), block.user_boundary_values.get(), static_cast<int>(destinations.size()), true);
    check_cuda(cudaGetLastError(), "scatter user ghost slice");
    std::size_t bytes = 0;
    auto& slot = block.slots[slot_index(access.slot)];
    // Slot views rotate, allocations do not. Controls follow the physical
    // state owner, so filling Yprev cannot overwrite cached Y0 controls.
    std::size_t owner=block.state_storage.size();
    for(std::size_t candidate=0;candidate<block.state_storage.size();++candidate)
        if(block.state_storage[candidate].rho.get()==state.rho) owner=candidate;
    if(owner==block.state_storage.size()) throw std::logic_error("User ghost slice has no state allocation owner");
    for (int face = 0; face < 6; ++face) {
        const auto& source = controls.faces[face];
        auto& destination = block.user_boundary_controls[owner][face];
        if (source.empty()) { slot.diffusion_boundary.faces[face] = nullptr; continue; }
        destination.reserve(source.size());
        check_cuda(cudaMemcpyAsync(destination.get(), source.data(), source.size() * sizeof(source[0]),
            cudaMemcpyHostToDevice, impl_->stream.get()), "upload diffusion boundary controls");
        slot.diffusion_boundary.faces[face] = destination.get();
        bytes += source.size() * sizeof(source[0]);
    }
    quiesce();
    impl_->runtime_counters.bytes_h2d += destinations.size() * sizeof(int) + packed.size() * sizeof(double) + bytes;
    ++impl_->runtime_counters.kernel_count;
}

void CudaBackend::configure_boundary_flux_capture(
    std::span<const backend::BoundaryFluxPlanes> layout, double weight,
    double initial_weight, bool save_initial) {
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
