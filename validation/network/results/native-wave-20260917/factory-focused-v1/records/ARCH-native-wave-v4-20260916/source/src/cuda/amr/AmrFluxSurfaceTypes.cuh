/**
 * @file AmrFluxSurfaceTypes.cuh
 * @brief Borrowed CUDA views of compact AMR flux surfaces.
 *
 * Views address conserved and species flux arrays owned by the runtime's
 * block/flux resources. They carry layout information but allocate no storage;
 * the owning store and topology generation must outlive every queued use.
 */

#pragma once

#include "cuda/common/CudaCommon.cuh"

#include <array>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace arch::cuda {

enum class AmrFluxSource : int {
    StageScratch = 0,
    InitialSurface = 1,
};

/** Five conserved fluxes plus species fluxes; ENUC is intentionally absent. */
struct DeviceAmrFluxSurfaceView {
    double* rho = nullptr;
    double* mom_u = nullptr;
    double* mom_v = nullptr;
    double* mom_w = nullptr;
    double* eng = nullptr;
    double* species = nullptr;
    int cell_count = 0;
    int species_count = 0;

    ARCH_INLINE double species_flux(int component, int cell) const
    {
        return species[component * cell_count + cell];
    }

    ARCH_INLINE void set_species_flux(
        int component, int cell, double value) const
    {
        species[component * cell_count + cell] = value;
    }
};

/**
 * Descriptor array uploaded once per topology/slot binding.  Each non-null
 * face view has only O(face cells) storage, never a full block volume.
 */
struct DeviceAmrFluxBlockView {
    DeviceStateView state{};
    DeviceStateView stage_flux{};
    DeviceGridView grid{};
    DeviceAmrFluxSurfaceView registers[6]{};
    DeviceAmrFluxSurfaceView initial_flux[6]{};
};

inline constexpr std::size_t amr_flux_surface_scalar_count(
    int face_cells, int species_count) noexcept
{
    return face_cells > 0 && species_count >= 0
        ? static_cast<std::size_t>(face_cells)
            * static_cast<std::size_t>(5 + species_count)
        : 0;
}

inline DeviceAmrFluxSurfaceView make_amr_flux_surface_view(
    double* contiguous, int face_cells, int species_count) noexcept
{
    DeviceAmrFluxSurfaceView result{};
    if (contiguous == nullptr || face_cells <= 0 || species_count < 0)
        return result;
    result.rho = contiguous;
    result.mom_u = result.rho + face_cells;
    result.mom_v = result.mom_u + face_cells;
    result.mom_w = result.mom_v + face_cells;
    result.eng = result.mom_w + face_cells;
    result.species = species_count == 0 ? nullptr : result.eng + face_cells;
    result.cell_count = face_cells;
    result.species_count = species_count;
    return result;
}

inline bool valid_amr_flux_surface(
    const DeviceAmrFluxSurfaceView& view) noexcept
{
    return view.cell_count > 0 && view.species_count >= 0
        && view.rho != nullptr && view.mom_u != nullptr
        && view.mom_v != nullptr && view.mom_w != nullptr
        && view.eng != nullptr
        && (view.species_count == 0 || view.species != nullptr)
        && (view.species_count == 0
            || view.cell_count
                <= std::numeric_limits<int>::max() / view.species_count);
}

static_assert(std::is_standard_layout_v<DeviceAmrFluxSurfaceView>);
static_assert(std::is_trivially_copyable_v<DeviceAmrFluxSurfaceView>);
static_assert(std::is_standard_layout_v<DeviceAmrFluxBlockView>);
static_assert(std::is_trivially_copyable_v<DeviceAmrFluxBlockView>);

} // namespace arch::cuda
