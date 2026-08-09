/**
 * @file AMRFluxRegistering.h
 * @brief Shared coarse-fine flux registration for conservative AMR operators.
 */

/**
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <vector>

#include "../grid/GridMetrics.h"
#include "AMRControl.h"

namespace amr {

/**
 * Register flux densities from one block face in the common flux register.
 *
 * Flux solvers store a face flux at the index of the cell on its right in the
 * sweep direction.  This routine is the single owner of that convention for
 * hydro and diffusion, including child-to-parent tangential index mapping.
 */
inline void RegisterCoarseFineFluxes(AMRControl& amr_ctrl, int block_id,
                                     const Grid& grid, int dir,
                                     const std::vector<FluidVector>& flux_buffer,
                                     const std::vector<double>& species_flux_buffer,
                                     int num_species, double flux_weight)
{
    const Block& block = amr_ctrl.pool->GetBlock(block_id);
    const int nx = BLOCK_NX;
    const int ny = (grid.dim >= 2) ? BLOCK_NY : 1;
    const int nz = (grid.dim == 3) ? BLOCK_NZ : 1;
    const int total_size = grid.GetTotalSize();

    const auto coarse_face_area = [](const Block& coarse, int normal_dir,
                                     int coarse_face, int face_cell_idx) {
        const int coarse_ny = (coarse.grid.dim >= 2) ? BLOCK_NY : 1;
        int i = coarse.grid.Is();
        int j = coarse.grid.Js();
        int k = coarse.grid.Ks();
        if (normal_dir == 0) {
            j += face_cell_idx % coarse_ny;
            k += face_cell_idx / coarse_ny;
            i += (coarse_face % 2 == 0) ? 0 : BLOCK_NX - 1;
        } else if (normal_dir == 1) {
            i += face_cell_idx % BLOCK_NX;
            k += face_cell_idx / BLOCK_NX;
            j += (coarse_face % 2 == 0) ? 0 : BLOCK_NY - 1;
        } else {
            i += face_cell_idx % BLOCK_NX;
            j += face_cell_idx / BLOCK_NX;
            k += (coarse_face % 2 == 0) ? 0 : BLOCK_NZ - 1;
        }
        return GridMetrics::FaceArea(coarse.grid, normal_dir, i, j, k,
                                     (coarse_face % 2) == 1);
    };

    for (int side = 0; side < 2; ++side) {
        const int face = 2 * dir + side;
        const auto& neighbours = block.face_neighbors[face];
        if (neighbours.count == 0 || neighbours.level_diff == 0) continue;

        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    if ((dir == 0 && i != (side == 0 ? 0 : nx - 1)) ||
                        (dir == 1 && j != (side == 0 ? 0 : ny - 1)) ||
                        (dir == 2 && k != (side == 0 ? 0 : nz - 1))) continue;

                    const int flux_idx = grid.GetIndex(
                        grid.Is() + i + ((dir == 0 && side == 1) ? 1 : 0),
                        grid.Js() + j + ((dir == 1 && side == 1) ? 1 : 0),
                        grid.Ks() + k + ((dir == 2 && side == 1) ? 1 : 0));
                    const double fine_area = GridMetrics::FaceArea(
                        grid, dir, grid.Is() + i, grid.Js() + j, grid.Ks() + k, side == 1);

                    int coarse_cell_idx = 0;
                    if (neighbours.level_diff == -1) {
                        const int coarse_id = neighbours.ids[0];
                        if (coarse_id < 0) continue;

                        const int child_x = block.logical_x1 & 1;
                        const int child_y = block.logical_x2 & 1;
                        const int child_z = block.logical_x3 & 1;
                        const int c_i = child_x * (nx / 2) + i / 2;
                        const int c_j = (grid.dim >= 2) ? child_y * (ny / 2) + j / 2 : 0;
                        const int c_k = (grid.dim == 3) ? child_z * (nz / 2) + k / 2 : 0;

                        if (dir == 0) coarse_cell_idx = c_k * ny + c_j;
                        else if (dir == 1) coarse_cell_idx = c_k * nx + c_i;
                        else coarse_cell_idx = c_j * nx + c_i;

                        const int coarse_face = 2 * dir + (side == 0 ? 1 : 0);
                        const Block& coarse = amr_ctrl.pool->GetBlock(coarse_id);
                        const double area = coarse_face_area(coarse, dir, coarse_face, coarse_cell_idx);
                        if (area <= 0.0) continue;
                        const double area_weight = flux_weight * fine_area / area;
                        amr_ctrl.flux_register.AddFineFlux(coarse_id, coarse_face,
                                                           coarse_cell_idx, flux_buffer[flux_idx],
                                                           area_weight);
                        for (int species = 0; species < num_species; ++species) {
                            amr_ctrl.flux_register.AddFineSpeciesFlux(
                                coarse_id, coarse_face, coarse_cell_idx, species,
                                species_flux_buffer[species * total_size + flux_idx],
                                area_weight);
                        }
                    } else if (neighbours.level_diff == 1) {
                        if (dir == 0) coarse_cell_idx = k * ny + j;
                        else if (dir == 1) coarse_cell_idx = k * nx + i;
                        else coarse_cell_idx = j * nx + i;

                        amr_ctrl.flux_register.AddCoarseFlux(block_id, face,
                                                             coarse_cell_idx, flux_buffer[flux_idx],
                                                             flux_weight);
                        for (int species = 0; species < num_species; ++species) {
                            amr_ctrl.flux_register.AddCoarseSpeciesFlux(
                                block_id, face, coarse_cell_idx, species,
                                species_flux_buffer[species * total_size + flux_idx],
                                flux_weight);
                        }
                    }
                }
            }
        }
    }
}

} // namespace amr
