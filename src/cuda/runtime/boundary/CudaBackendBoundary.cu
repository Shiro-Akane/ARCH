/**
 * @file CudaBackendBoundary.cu
 * @brief Device boundary slices, scalar controls and physical-surface observers.
 *
 * Workflow: authenticate the active or staged store, gather or scatter selected
 * cells, and bind transient face controls to their physical state allocation.
 * Observer planes are shared by the three stage slots and follow the owner stream.
 */
#include "physics/boundary/BoundaryFlux.h"
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
    // shape. Stage values and null/non-null bindings follow each state owner.
    for (int face = 0; face < 6; ++face) {
        const auto& source = controls.faces[face];
        const std::size_t expected = static_cast<std::size_t>(physical_face_cells(block.grid, face))
            * static_cast<std::size_t>(4 + state.n_species);
        if (!source.empty() && source.size() != expected)
            throw std::invalid_argument("Diffusion boundary controls do not match the actual face shape");
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
