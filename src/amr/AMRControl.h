/**
 * @file AMRControl.h
 * @brief Bundles all AMR-related managers and data structures.
 */

/**
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <memory>
#include "AmrTree.h"
#include "../grid/GridMetrics.h"
#include "MemoryPool.h"
#include "GhostExchange.h"
#include "FluxRegister.h"

namespace amr {

/**
 * @brief Central controller for AMR, packing the MemoryPool, AmrTree,
 *        GhostExchange, and FluxRegister into a single manageable struct.
 */
struct AMRControl {
    std::shared_ptr<MemoryPool> pool;
    std::shared_ptr<AmrTree> tree;
    GhostExchange ghost_exchange;
    FluxRegister flux_register;


    void ApplyReflux(double dt, FluidState Block::* state_ptr = &Block::fluid_state) {
        // Iterate over all active blocks to find coarse ones that have registered fine fluxes
        const auto& active_blocks = tree->GetActiveBlocks();
        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            int block_id = active_blocks[i];
            Block& b = pool->GetBlock(block_id);
            FluidState& state = b.*state_ptr;
            int dim = tree->GetRootGridDim();
            int nx = BLOCK_NX, ny = (dim >= 2) ? BLOCK_NY : 1, nz = (dim == 3) ? BLOCK_NZ : 1;

            for (int f = 0; f < 6; ++f) {
                if (dim < 2 && (f == 2 || f == 3)) continue;
                if (dim < 3 && (f == 4 || f == 5)) continue;

                if (flux_register.HasData(block_id, f)) {
                    int c_dir = f / 2; // 0: x, 1: y, 2: z

                    // Iterate over face cells
                    int face_ny = (c_dir == 0) ? ny * nz : ((c_dir == 1) ? nx * nz : nx * ny);

                    for (int cell_idx = 0; cell_idx < face_ny; ++cell_idx) {
                        FluidVector delta_F = flux_register.GetSummedFlux(block_id, f, cell_idx);

                        // Wait, flux_register GetSummedFlux is sum(F_fine) - F_coarse.
                        // Or F_fine - F_coarse.
                        // We need to ADD this divergence correction to the coarse cell!
                        // The coarse cell index needs to be mapped back to 3D grid index!
                        int fi = 0, fj = 0, fk = 0;
                        if (c_dir == 0) {
                            fj = cell_idx % ny;
                            fk = cell_idx / ny;
                            fi = (f % 2 == 0) ? 0 : nx - 1;
                        } else if (c_dir == 1) {
                            fi = cell_idx % nx;
                            fk = cell_idx / nx;
                            fj = (f % 2 == 0) ? 0 : ny - 1;
                        } else if (c_dir == 2) {
                            fi = cell_idx % nx;
                            fj = cell_idx / nx;
                            fk = (f % 2 == 0) ? 0 : nz - 1;
                        }

                        const int cell_i = b.grid.Is() + fi;
                        const int cell_j = b.grid.Js() + fj;
                        const int cell_k = b.grid.Ks() + fk;
                        const int grid_idx = b.grid.GetIndex(cell_i, cell_j, cell_k);
                        const double area = GridMetrics::FaceArea(b.grid, c_dir, cell_i, cell_j, cell_k,
                                                                  (f % 2) == 1);
                        const double volume = GridMetrics::CellVolume(b.grid, cell_i, cell_j, cell_k);

                        // Sign convention: If f % 2 == 0 (left face), flux goes INTO the cell -> +
                        // If f % 2 == 1 (right face), flux goes OUT OF the cell -> -
                        // Wait, delta_F is (F_fine - F_coarse).
                        // The coarse update was U += dt/dx (F_L - F_R).
                        // So U += dt/dx * delta_F for Left face.
                        // U -= dt/dx * delta_F for Right face.

                        double sign = (f % 2 == 0) ? 1.0 : -1.0;

                        const double correction = sign * dt * area / volume;
                        const double rho_before = state.rho[grid_idx];
                        state.rho[grid_idx] += correction * delta_F.rho;
                        state.mom_u[grid_idx] += correction * delta_F.mom_u;
                        state.mom_v[grid_idx] += correction * delta_F.mom_v;
                        state.mom_w[grid_idx] += correction * delta_F.mom_w;
                        state.eng[grid_idx] += correction * delta_F.eng;

                        if (flux_register.GetNumSpecies() == state.GetNumSpecies()) {
                            for (int sp = 0; sp < state.GetNumSpecies(); ++sp) {
                                const double rhoX = rho_before * state.X(sp, grid_idx)
                                                  + correction * flux_register.GetSummedSpeciesFlux(block_id, f, cell_idx, sp);
                                state.X(sp, grid_idx) = rhoX / state.rho[grid_idx];
                            }
                        }
                    }
                }
            }
        }
    }

    AMRControl(int max_blocks, int dim) {
        pool = std::make_shared<MemoryPool>(max_blocks, dim);
        tree = std::make_shared<AmrTree>(pool);
        flux_register.Resize(max_blocks, dim);
    }
};

} // namespace amr
