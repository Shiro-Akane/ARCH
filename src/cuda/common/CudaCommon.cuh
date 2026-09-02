#pragma once

#include <cuda_runtime.h>

#include <cstdint>
#include <limits>
#include <type_traits>

#include "../../data/FluidState.h"
#include "../../grid/Grid.h"

namespace arch::cuda
{
inline constexpr int kMaxDeviceSpecies = 30;

namespace detail
{
inline int hydro_launch_blocks(int count, int threads)
{
    return (count - 1) / threads + 1;
}
} // namespace detail

enum class DeviceGeometry : int
{
    Cartesian = 0,
    Cylindrical = 1,
    Spherical = 2,
};

struct DeviceStateView
{
    double* rho;
    double* mom_u;
    double* mom_v;
    double* mom_w;
    double* eng;
    double* enuc_rate;
    double* mass_fractions;
    int total_size;
    int n_species;

    ARCH_INLINE FluidVector load(int cell) const
    {
        return {rho[cell], mom_u[cell], mom_v[cell], mom_w[cell], eng[cell]};
    }

    ARCH_INLINE void store(int cell, const FluidVector& value) const
    {
        rho[cell] = value.rho;
        mom_u[cell] = value.mom_u;
        mom_v[cell] = value.mom_v;
        mom_w[cell] = value.mom_w;
        eng[cell] = value.eng;
    }

    ARCH_INLINE double species(int species_index, int cell) const
    {
        return mass_fractions[species_index * total_size + cell];
    }

    ARCH_INLINE void set_species(
        int species_index, int cell, double value) const
    {
        mass_fractions[species_index * total_size + cell] = value;
    }
};

struct DeviceGridView
{
    int dim;
    int ng;
    int stride_y;
    int stride_z;
    int total_size;
    int total_x;
    int total_y;
    int total_z;
    int is;
    int ie;
    int js;
    int je;
    int ks;
    int ke;
    int geometry;
    std::uint8_t amr_coarse_fine_face[6]{};
    double dx1;
    double dx2;
    double dx3;
    double x1_min;
    double x2_min;
    double x3_min;
    double x1_max;
    double x2_max;
    double x3_max;
    const double* cell_volume;
    const double* face_area_lower[3];
    const double* face_area_upper[3];

    ARCH_INLINE int index(int i, int j = 0, int k = 0) const
    {
        return k * stride_z + j * stride_y + i;
    }

    ARCH_INLINE int stride(int direction) const
    {
        return direction == 0 ? 1 : (direction == 1 ? stride_y : stride_z);
    }

    ARCH_INLINE int active_cell_count() const
    {
        return (ie - is) * (je - js) * (ke - ks);
    }

    ARCH_INLINE int active_cell(int linear) const
    {
        const int ni = ie - is;
        const int nj = je - js;
        const int i = is + linear % ni;
        linear /= ni;
        const int j = js + linear % nj;
        const int k = ks + linear / nj;
        return index(i, j, k);
    }
};

struct CudaHydroWorkspaceView
{
    DeviceStateView flux;
    DeviceStateView delta;
    double* cfl_candidates;
    double* cfl_result;
    int* cfl_status;
};

inline bool valid_hydro_view(const DeviceStateView& view)
{
    if (!(view.total_size > 0
        && view.n_species >= 0
        && view.n_species <= kMaxDeviceSpecies
        && view.rho != nullptr
        && view.mom_u != nullptr
        && view.mom_v != nullptr
        && view.mom_w != nullptr
        && view.eng != nullptr
        && view.enuc_rate != nullptr
        && (view.n_species == 0 || view.mass_fractions != nullptr)))
        return false;
    return view.n_species == 0
        || view.total_size
            <= std::numeric_limits<int>::max() / view.n_species;
}

inline bool valid_hydro_grid(const DeviceGridView& grid)
{
    if (grid.dim < 1 || grid.dim > 3 || grid.ng < 0
        || grid.stride_y < grid.total_x || grid.stride_z <= 0
        || grid.total_size <= 0 || grid.total_x <= 0
        || grid.total_y <= 0 || grid.total_z <= 0
        || grid.is < 0 || grid.is >= grid.ie || grid.ie > grid.total_x
        || grid.js < 0 || grid.js >= grid.je || grid.je > grid.total_y
        || grid.ks < 0 || grid.ks >= grid.ke || grid.ke > grid.total_z)
        return false;
    for (const std::uint8_t flag : grid.amr_coarse_fine_face)
        if (flag > 1) return false;
    if (grid.total_y > std::numeric_limits<int>::max() / grid.stride_y)
        return false;
    const int minimum_stride_z = grid.total_y * grid.stride_y;
    if (grid.stride_z < minimum_stride_z)
        return false;

    int remaining = grid.total_size - 1;
    if (grid.total_z - 1 > remaining / grid.stride_z)
        return false;
    remaining -= (grid.total_z - 1) * grid.stride_z;
    if (grid.total_y - 1 > remaining / grid.stride_y)
        return false;
    remaining -= (grid.total_y - 1) * grid.stride_y;
    return grid.total_x - 1 <= remaining;
}

static_assert(std::is_standard_layout_v<DeviceStateView>);
static_assert(std::is_trivially_copyable_v<DeviceStateView>);
static_assert(std::is_standard_layout_v<DeviceGridView>);
static_assert(std::is_trivially_copyable_v<DeviceGridView>);
static_assert(std::is_standard_layout_v<CudaHydroWorkspaceView>);
static_assert(std::is_trivially_copyable_v<CudaHydroWorkspaceView>);

inline DeviceGridView make_device_grid_view(const Grid& grid)
{
    int geometry = static_cast<int>(DeviceGeometry::Cartesian);
    if (grid.geometry == "cylindrical")
        geometry = static_cast<int>(DeviceGeometry::Cylindrical);
    else if (grid.geometry == "spherical")
        geometry = static_cast<int>(DeviceGeometry::Spherical);

    DeviceGridView view{};
    view.dim = grid.dim;
    view.ng = grid.ng;
    view.stride_y = grid.stride_y;
    view.stride_z = grid.stride_z;
    view.total_size = grid.GetTotalSize();
    view.total_x = grid.GetTotalX();
    view.total_y = grid.GetTotalY();
    view.total_z = grid.GetTotalZ();
    view.is = grid.Is();
    view.ie = grid.Ie();
    view.js = grid.Js();
    view.je = grid.Je();
    view.ks = grid.Ks();
    view.ke = grid.Ke();
    view.geometry = geometry;
    for (int face = 0; face < 6; ++face)
        view.amr_coarse_fine_face[face] = static_cast<std::uint8_t>(
            grid.amr_coarse_fine_face[face]);
    view.dx1 = grid.dx1;
    view.dx2 = grid.dx2;
    view.dx3 = grid.dx3;
    view.x1_min = grid.x1_min;
    view.x2_min = grid.x2_min;
    view.x3_min = grid.x3_min;
    view.x1_max = grid.x1_max;
    view.x2_max = grid.x2_max;
    view.x3_max = grid.x3_max;
    return view;
}
} // namespace arch::cuda
