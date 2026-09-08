#include "RegridMigration.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace arch::cuda {
namespace {

ARCH_DEVICE inline amr::regrid_math::ConstStateView samples(DeviceStateView state)
{
    return {{state.rho, state.mom_u, state.mom_v, state.mom_w,
             state.eng, state.enuc_rate}, state.mass_fractions,
            static_cast<std::size_t>(state.total_size)};
}

__global__ void prolong_regrid_families(
    DeviceRegridBlock source, DeviceRegridBlock destination, int child_index,
    double density_floor, double min_eint, double* workspace, int* status,
    int family_count)
{
    const int family = blockIdx.x * blockDim.x + threadIdx.x;
    if (family >= family_count) return;
    const auto& src = source.grid;
    const auto& dst = destination.grid;
    const int nx = (dst.ie - dst.is) / 2;
    const int ny = dst.dim >= 2 ? (dst.je - dst.js) / 2 : 1;
    const int nz = dst.dim == 3 ? (dst.ke - dst.ks) / 2 : 1;
    const int i = family % nx;
    const int j = (family / nx) % ny;
    const int k = family / (nx * ny);
    const int ci = src.is + i + ((child_index & 1) ? nx : 0);
    const int cj = src.js + j + (dst.dim >= 2 && (child_index & 2) ? ny : 0);
    const int ck = src.ks + k + (dst.dim == 3 && (child_index & 4) ? nz : 0);
    amr::regrid_math::ProlongationGeometry geometry{};
    geometry.dimension = dst.dim;
    geometry.center = src.index(ci, cj, ck);
    geometry.neighbours[0] = src.index(ci - 1, cj, ck);
    geometry.neighbours[1] = src.index(ci + 1, cj, ck);
    geometry.neighbours[2] = dst.dim >= 2 ? src.index(ci, cj - 1, ck) : geometry.center;
    geometry.neighbours[3] = dst.dim >= 2 ? src.index(ci, cj + 1, ck) : geometry.center;
    geometry.neighbours[4] = dst.dim == 3 ? src.index(ci, cj, ck - 1) : geometry.center;
    geometry.neighbours[5] = dst.dim == 3 ? src.index(ci, cj, ck + 1) : geometry.center;
    geometry.coarse_volume = src.cell_volume[geometry.center];
    int targets[8]{};
    for (int child = 0; child < (1 << dst.dim); ++child) {
        const int fi = dst.is + 2 * i + (child & 1);
        const int fj = dst.js + 2 * j + (dst.dim >= 2 ? (child >> 1) & 1 : 0);
        const int fk = dst.ks + 2 * k + (dst.dim == 3 ? (child >> 2) & 1 : 0);
        targets[child] = dst.index(fi, fj, fk);
        geometry.fine_volumes[child] = dst.cell_volume[targets[child]];
    }
    const int species = destination.state.n_species;
    double* scratch = species > 0
        ? workspace + static_cast<std::size_t>(family)
            * amr::regrid_math::prolongation_workspace_per_species * species : nullptr;
    amr::regrid_math::ProlongationResult result{};
    const auto outcome = amr::regrid_math::prolong_family(
        samples(source.state), geometry, species, density_floor, min_eint,
        scratch, result);
    if (outcome != amr::regrid_math::Status::Ok) {
        atomicCAS(status, 0, static_cast<int>(outcome));
        return;
    }
    for (int child = 0; child < (1 << dst.dim); ++child) {
        destination.state.store(targets[child], result.fluid[child]);
        destination.state.enuc_rate[targets[child]] = result.enuc[child];
        for (int sp = 0; sp < species; ++sp)
            destination.state.set_species(sp, targets[child],
                result.rhoX[static_cast<std::size_t>(sp) * amr::regrid_math::maximum_children + child] / result.fluid[child].rho);
    }
}

__global__ void restrict_regrid_families(
    DeviceRegridChildren children, DeviceRegridBlock destination,
    double density_floor, double min_eint, double* workspace,
    int* status, int cell_count)
{
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell >= cell_count) return;
    const auto& dst = destination.grid;
    const int nx = dst.ie - dst.is;
    const int ny = dst.je - dst.js;
    const int nz = dst.ke - dst.ks;
    const int i = cell % nx;
    const int j = (cell / nx) % ny;
    const int k = cell / (nx * ny);
    const int child_index = (i >= nx / 2 ? 1 : 0)
        | ((dst.dim >= 2 && j >= ny / 2 ? 1 : 0) << 1)
        | ((dst.dim == 3 && k >= nz / 2 ? 1 : 0) << 2);
    const auto& source = children.blocks[child_index];
    const int ibase = 2 * (i % (nx / 2));
    const int jbase = dst.dim >= 2 ? 2 * (j % (ny / 2)) : 0;
    const int kbase = dst.dim == 3 ? 2 * (k % (nz / 2)) : 0;
    const int target = dst.index(dst.is + i, dst.js + j, dst.ks + k);
    amr::regrid_math::RestrictionGeometry geometry{};
    geometry.count = 1 << dst.dim;
    geometry.coarse_volume = dst.cell_volume[target];
    for (int child = 0; child < geometry.count; ++child) {
        const int fi = source.grid.is + ibase + (child & 1);
        const int fj = source.grid.js + jbase + (dst.dim >= 2 ? (child >> 1) & 1 : 0);
        const int fk = source.grid.ks + kbase + (dst.dim == 3 ? (child >> 2) & 1 : 0);
        geometry.source_cells[child] = source.grid.index(fi, fj, fk);
        geometry.volumes[child] = source.grid.cell_volume[geometry.source_cells[child]];
    }
    const int species = destination.state.n_species;
    double* scratch = species > 0
        ? workspace + static_cast<std::size_t>(cell) * species : nullptr;
    amr::regrid_math::RestrictionResult result{};
    const auto outcome = amr::regrid_math::restrict_family(
        samples(source.state), geometry, species, density_floor, min_eint,
        scratch, result);
    if (outcome != amr::regrid_math::Status::Ok) {
        atomicCAS(status, 0, static_cast<int>(outcome));
        return;
    }
    destination.state.store(target, result.fluid);
    destination.state.enuc_rate[target] = result.enuc;
    for (int sp = 0; sp < species; ++sp)
        destination.state.set_species(sp, target, result.fractions[sp]);
}

bool valid_block(const DeviceRegridBlock& block)
{
    const auto& state = block.state;
    const auto& grid = block.grid;
    if (grid.dim < 1 || grid.dim > 3 || grid.total_size <= 0
        || state.total_size != grid.total_size || state.n_species < 0
        || state.n_species > std::numeric_limits<int>::max() / grid.total_size
        || !state.rho || !state.mom_u || !state.mom_v || !state.mom_w
        || !state.eng || !state.enuc_rate || !grid.cell_volume
        || (state.n_species > 0 && !state.mass_fractions)
        || grid.is < 0 || grid.ie > grid.total_x || grid.ie <= grid.is
        || grid.js < 0 || grid.je > grid.total_y || grid.je <= grid.js
        || grid.ks < 0 || grid.ke > grid.total_z || grid.ke <= grid.ks)
        return false;
    // Logical bounds alone do not prove the strided storage is addressable.
    // Use wide products before accepting externally supplied device views.
    if (grid.stride_y < grid.total_x
        || static_cast<std::int64_t>(grid.stride_z)
            < static_cast<std::int64_t>(grid.stride_y) * grid.total_y
        || static_cast<std::int64_t>(grid.total_z - 1) * grid.stride_z
             + static_cast<std::int64_t>(grid.total_y - 1) * grid.stride_y
             + grid.total_x > grid.total_size)
        return false;
    const int nx = grid.ie - grid.is;
    const int ny = grid.je - grid.js;
    const int nz = grid.ke - grid.ks;
    return nx % 2 == 0 && (grid.dim >= 2 ? ny % 2 == 0 : ny == 1)
        && (grid.dim == 3 ? nz % 2 == 0 : nz == 1)
        && static_cast<std::int64_t>(nx) * ny * nz <= std::numeric_limits<int>::max();
}

bool same_layout(const DeviceRegridBlock& source, const DeviceRegridBlock& destination)
{
    return valid_block(source) && valid_block(destination)
        && source.state.n_species == destination.state.n_species
        && source.grid.dim == destination.grid.dim
        && source.grid.ie - source.grid.is == destination.grid.ie - destination.grid.is
        && source.grid.je - source.grid.js == destination.grid.je - destination.grid.js
        && source.grid.ke - source.grid.ks == destination.grid.ke - destination.grid.ks;
}

bool valid_workspace(int cells, int species, int per_species,
                     double* workspace, std::size_t supplied)
{
    const auto count = static_cast<std::size_t>(cells);
    const auto width = static_cast<std::size_t>(species) * per_species;
    return width == 0 || (workspace != nullptr
        && count <= std::numeric_limits<std::size_t>::max() / width
        && supplied >= count * width);
}

bool valid_floors(double density_floor, double min_eint)
{
    return std::isfinite(density_floor) && density_floor > 0.0
        && std::isfinite(min_eint) && min_eint >= 0.0;
}

} // namespace

cudaError_t launch_cuda_regrid_prolongation(
    DeviceRegridBlock source, DeviceRegridBlock destination, int child_index,
    double density_floor, double min_eint, double* workspace,
    std::size_t workspace_scalars, int* status, cudaStream_t stream)
{
    if (!same_layout(source, destination) || !valid_floors(density_floor, min_eint)
        || !status || child_index < 0 || child_index >= (1 << destination.grid.dim))
        return cudaErrorInvalidValue;
    const auto& grid = source.grid;
    // The full reconstruction needs one coarse ghost sample on each axis.
    if (grid.is == 0 || grid.ie == grid.total_x
        || (grid.dim >= 2 && (grid.js == 0 || grid.je == grid.total_y))
        || (grid.dim == 3 && (grid.ks == 0 || grid.ke == grid.total_z)))
        return cudaErrorInvalidValue;
    const int families = destination.grid.active_cell_count() / (1 << grid.dim);
    if (!valid_workspace(families, destination.state.n_species,
            amr::regrid_math::prolongation_workspace_per_species, workspace, workspace_scalars))
        return cudaErrorInvalidValue;
    constexpr int threads = 128;
    prolong_regrid_families<<<(static_cast<unsigned>(families) + threads - 1) / threads, threads, 0, stream>>>(
        source, destination, child_index, density_floor, min_eint, workspace, status, families);
    return cudaGetLastError();
}

cudaError_t launch_cuda_regrid_restriction(
    const DeviceRegridChildren& children, DeviceRegridBlock destination,
    double density_floor, double min_eint, double* workspace,
    std::size_t workspace_scalars, int* status, cudaStream_t stream)
{
    if (!valid_block(destination) || !valid_floors(density_floor, min_eint) || !status)
        return cudaErrorInvalidValue;
    for (int child = 0; child < (1 << destination.grid.dim); ++child)
        if (!same_layout(children.blocks[child], destination)) return cudaErrorInvalidValue;
    const int cells = destination.grid.active_cell_count();
    if (!valid_workspace(cells, destination.state.n_species,
            amr::regrid_math::restriction_workspace_per_species, workspace, workspace_scalars))
        return cudaErrorInvalidValue;
    constexpr int threads = 128;
    restrict_regrid_families<<<(static_cast<unsigned>(cells) + threads - 1) / threads, threads, 0, stream>>>(
        children, destination, density_floor, min_eint, workspace, status, cells);
    return cudaGetLastError();
}

} // namespace arch::cuda
