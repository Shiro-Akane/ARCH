/**
 * @file GhostExchange.h
 * @brief Synchronizes same-level and coarse-fine AMR ghost cells.
 *
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <memory>
#include <vector>

#include "AmrTree.h"
#include "Block.h"

#include "../data/GlobalDefs.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace amr {

static constexpr int Align(int size, int alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

class GhostExchange {
private:
    std::vector<double> comm_buffer;
    int current_num_species = 0;
    int current_num_blocks = 0;
    int current_dim = 0;
    int num_vars = 0;

    int face_stride[6] = {0};
    size_t face_start_offset[6] = {0};
    size_t block_total_size = 0;

public:
    GhostExchange() = default;

    void Resize(int max_blocks) {
        // Buffer allocation is deferred until UpdateLayout supplies the species count.
    }

    size_t GetOffset(int active_idx, int face, int var_idx) const {
        return active_idx * block_total_size + face_start_offset[face] + var_idx * face_stride[face];
    }

    void UpdateLayout(int n_species, int dim) {
        num_vars = 5 + n_species;

        int ny = (dim >= 2) ? BLOCK_NY : 1;
        int nz = (dim == 3) ? BLOCK_NZ : 1;

        // Keep the exchange arena contiguous, but do not reserve slots for
        // faces that cannot exist in a lower-dimensional run.
        for (int f = 0; f < 6; ++f) face_stride[f] = 0;
        face_stride[0] = Align(MAX_NG * ny * nz, 16);
        face_stride[1] = Align(MAX_NG * ny * nz, 16);
        if (dim >= 2) {
            face_stride[2] = Align(BLOCK_NX * MAX_NG * nz, 16);
            face_stride[3] = Align(BLOCK_NX * MAX_NG * nz, 16);
        }
        if (dim == 3) {
            face_stride[4] = Align(BLOCK_NX * ny * MAX_NG, 16);
            face_stride[5] = Align(BLOCK_NX * ny * MAX_NG, 16);
        }

        block_total_size = 0;
        for (int f = 0; f < 6; ++f) {
            face_start_offset[f] = block_total_size;
            block_total_size += num_vars * face_stride[f];
        }
    }

    void ExecuteExchange(std::shared_ptr<MemoryPool> pool, std::shared_ptr<AmrTree> tree, int dim, FluidState Block::* state_ptr) {
        const auto& active_blocks = tree->GetActiveBlocks();
        if (active_blocks.empty()) return;

        int n_species = pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();

        if (n_species != current_num_species || active_blocks.size() != current_num_blocks || dim != current_dim) {
            current_num_species = n_species;
            current_num_blocks = active_blocks.size();
            current_dim = dim;
            UpdateLayout(n_species, dim);
            comm_buffer.assign(current_num_blocks * block_total_size, 0.0);
        }

        PackSameLevel(pool, active_blocks, dim, state_ptr);
        UnpackSameLevel(pool, active_blocks, dim, state_ptr);
        UpdateGhostFromCoarse(pool, active_blocks, dim, state_ptr);
        UpdateGhostFromFine(pool, active_blocks, dim, state_ptr);
    }

private:
    void PackSameLevel(std::shared_ptr<MemoryPool> pool, const std::vector<int>& active_blocks, int dim, FluidState Block::* state_ptr) {
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            Block& b = pool->GetBlock(active_blocks[i]);
            FluidState& b_state = b.*state_ptr;
            int active_idx = b.active_index;

            for (int f = 0; f < 6; ++f) {
                if (dim < 2 && (f == 2 || f == 3)) continue;
                if (dim < 3 && (f == 4 || f == 5)) continue;

                // Face buffers cover the active tangential extent. Source
                // coordinates therefore begin at the first active cell.
                int src_i = b.grid.Is();
                int src_j = (dim >= 2) ? b.grid.Js() : 0;
                int src_k = (dim == 3) ? b.grid.Ks() : 0;
                int nx = BLOCK_NX, ny = (dim >= 2) ? BLOCK_NY : 1, nz = (dim == 3) ? BLOCK_NZ : 1;

                if (f == 0) { src_i = b.grid.Is(); nx = MAX_NG; }
                else if (f == 1) { src_i = b.grid.Ie() - MAX_NG; nx = MAX_NG; }
                else if (f == 2) { src_j = b.grid.Js(); ny = MAX_NG; }
                else if (f == 3) { src_j = b.grid.Je() - MAX_NG; ny = MAX_NG; }
                else if (f == 4) { src_k = b.grid.Ks(); nz = MAX_NG; }
                else if (f == 5) { src_k = b.grid.Ke() - MAX_NG; nz = MAX_NG; }

                size_t off_rho = GetOffset(active_idx, f, 0);
                size_t off_u   = GetOffset(active_idx, f, 1);
                size_t off_v   = GetOffset(active_idx, f, 2);
                size_t off_w   = GetOffset(active_idx, f, 3);
                size_t off_e   = GetOffset(active_idx, f, 4);

                int cell_idx = 0;
                for (int k = 0; k < nz; ++k) {
                    for (int j = 0; j < ny; ++j) {
                        for (int ii = 0; ii < nx; ++ii) {
                            int src_idx = b.grid.GetIndex(src_i + ii, src_j + j, src_k + k);
                            comm_buffer[off_rho + cell_idx] = b_state.rho[src_idx];
                            comm_buffer[off_u + cell_idx]   = b_state.mom_u[src_idx];
                            comm_buffer[off_v + cell_idx]   = b_state.mom_v[src_idx];
                            comm_buffer[off_w + cell_idx]   = b_state.mom_w[src_idx];
                            comm_buffer[off_e + cell_idx]   = b_state.eng[src_idx];
                            for (int s = 0; s < current_num_species; ++s) {
                                comm_buffer[GetOffset(active_idx, f, 5 + s) + cell_idx] = b_state.X(s, src_idx);
                            }
                            cell_idx++;
                        }
                    }
                }
            }
        }
    }

    void UnpackSameLevel(std::shared_ptr<MemoryPool> pool, const std::vector<int>& active_blocks, int dim, FluidState Block::* state_ptr) {
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            Block& b = pool->GetBlock(active_blocks[i]);
            FluidState& b_state = b.*state_ptr;

            for (int f = 0; f < 6; ++f) {
                if (dim < 2 && (f == 2 || f == 3)) continue;
                if (dim < 3 && (f == 4 || f == 5)) continue;

                if (b.face_neighbors[f].count == 1 && b.face_neighbors[f].level_diff == 0) {
                    int neighbor_id = b.face_neighbors[f].ids[0];
                    if (neighbor_id < 0) continue; // Domain boundary

                    Block& nb = pool->GetBlock(neighbor_id);
                    int nb_idx = nb.active_index;

                    int opp_f = f ^ 1;

                    // Match PackSameLevel: the tangential dimensions are the
                    // active cells only, not their local ghost layers.
                    int dst_i = b.grid.Is();
                    int dst_j = (dim >= 2) ? b.grid.Js() : 0;
                    int dst_k = (dim == 3) ? b.grid.Ks() : 0;
                    int nx = BLOCK_NX, ny = (dim >= 2) ? BLOCK_NY : 1, nz = (dim == 3) ? BLOCK_NZ : 1;

                    if (f == 0) { dst_i = b.grid.Is() - MAX_NG; nx = MAX_NG; }
                    else if (f == 1) { dst_i = b.grid.Ie(); nx = MAX_NG; }
                    else if (f == 2) { dst_j = b.grid.Js() - MAX_NG; ny = MAX_NG; }
                    else if (f == 3) { dst_j = b.grid.Je(); ny = MAX_NG; }
                    else if (f == 4) { dst_k = b.grid.Ks() - MAX_NG; nz = MAX_NG; }
                    else if (f == 5) { dst_k = b.grid.Ke(); nz = MAX_NG; }

                    size_t off_rho = GetOffset(nb_idx, opp_f, 0);
                    size_t off_u   = GetOffset(nb_idx, opp_f, 1);
                    size_t off_v   = GetOffset(nb_idx, opp_f, 2);
                    size_t off_w   = GetOffset(nb_idx, opp_f, 3);
                    size_t off_e   = GetOffset(nb_idx, opp_f, 4);

                    int cell_idx = 0;
                    for (int k = 0; k < nz; ++k) {
                        for (int j = 0; j < ny; ++j) {
                            for (int ii = 0; ii < nx; ++ii) {
                                int dst_idx = b.grid.GetIndex(dst_i + ii, dst_j + j, dst_k + k);
                                b_state.rho[dst_idx]   = comm_buffer[off_rho + cell_idx];
                                b_state.mom_u[dst_idx] = comm_buffer[off_u + cell_idx];
                                b_state.mom_v[dst_idx] = comm_buffer[off_v + cell_idx];
                                b_state.mom_w[dst_idx] = comm_buffer[off_w + cell_idx];
                                b_state.eng[dst_idx]   = comm_buffer[off_e + cell_idx];
                                for (int s = 0; s < current_num_species; ++s) {
                                    b_state.X(s, dst_idx) = comm_buffer[GetOffset(nb_idx, opp_f, 5 + s) + cell_idx];
                                }
                                cell_idx++;
                            }
                        }
                    }
                }
            }
        }
    }

    void UpdateGhostFromCoarse(std::shared_ptr<MemoryPool> pool, const std::vector<int>& active_blocks, int dim, FluidState Block::* state_ptr) {
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            Block& b = pool->GetBlock(active_blocks[i]);
            for (int f = 0; f < 6; ++f) {
                if (dim < 2 && (f == 2 || f == 3)) continue;
                if (dim < 3 && (f == 4 || f == 5)) continue;

                if (b.face_neighbors[f].count == 1 && b.face_neighbors[f].level_diff == -1) {
                    int coarse_id = b.face_neighbors[f].ids[0];
                    if (coarse_id >= 0) {
                        const Block& coarse = pool->GetBlock(coarse_id);
                        InterpolateFaceFromCoarse(b, coarse, f, dim, state_ptr);
                    }
                }
            }
        }
    }

    void UpdateGhostFromFine(std::shared_ptr<MemoryPool> pool, const std::vector<int>& active_blocks, int dim, FluidState Block::* state_ptr) {
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            Block& b = pool->GetBlock(active_blocks[i]);
            for (int f = 0; f < 6; ++f) {
                if (dim < 2 && (f == 2 || f == 3)) continue;
                if (dim < 3 && (f == 4 || f == 5)) continue;

                if (b.face_neighbors[f].count > 0 && b.face_neighbors[f].level_diff == 1) {
                    for (int n = 0; n < b.face_neighbors[f].count; ++n) {
                        int fine_id = b.face_neighbors[f].ids[n];
                        if (fine_id < 0) continue;
                        const Block& fine = pool->GetBlock(fine_id);
                        AverageFaceFromFine(b, fine, f, dim, state_ptr);
                    }
                }
            }
        }
    }

        void InterpolateFaceFromCoarse(Block& fine, const Block& coarse, int face, int dim, FluidState Block::* state_ptr) {
        FluidState& f_state = fine.*state_ptr;
        const FluidState& c_state = coarse.*state_ptr;

        int n_sp = f_state.GetNumSpecies();

        int f_i_start = 0, f_j_start = 0, f_k_start = 0;
        int nx = BLOCK_NX, ny = (dim >= 2) ? BLOCK_NY : 1, nz = (dim == 3) ? BLOCK_NZ : 1;

        if (face == 0) { f_i_start = -MAX_NG; nx = MAX_NG; }
        else if (face == 1) { f_i_start = BLOCK_NX; nx = MAX_NG; }
        else if (face == 2) { f_j_start = -MAX_NG; ny = MAX_NG; }
        else if (face == 3) { f_j_start = BLOCK_NY; ny = MAX_NG; }
        else if (face == 4) { f_k_start = -MAX_NG; nz = MAX_NG; }
        else if (face == 5) { f_k_start = BLOCK_NZ; nz = MAX_NG; }

        int child_x = fine.logical_x1 % 2;
        int child_y = fine.logical_x2 % 2;
        int child_z = fine.logical_x3 % 2;

        int x_off = child_x * (BLOCK_NX / 2);
        int y_off = (dim >= 2) ? child_y * (BLOCK_NY / 2) : 0;
        int z_off = (dim == 3) ? child_z * (BLOCK_NZ / 2) : 0;

        if (face == 0) x_off = BLOCK_NX;
        if (face == 1) x_off = - (BLOCK_NX / 2);
        if (face == 2) y_off = BLOCK_NY;
        if (face == 3) y_off = - (BLOCK_NY / 2);
        if (face == 4) z_off = BLOCK_NZ;
        if (face == 5) z_off = - (BLOCK_NZ / 2);

        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    int f_i = f_i_start + i;
                    int f_j = f_j_start + j;
                    int f_k = f_k_start + k;

                    int c_i = x_off + (f_i >= 0 ? f_i / 2 : (f_i - 1) / 2);
                    int c_j = y_off + (f_j >= 0 ? f_j / 2 : (f_j - 1) / 2);
                    int c_k = z_off + (f_k >= 0 ? f_k / 2 : (f_k - 1) / 2);

                    int c_idx = coarse.grid.GetIndex(coarse.grid.Is() + c_i, coarse.grid.Js() + c_j, coarse.grid.Ks() + c_k);
                    int f_idx = fine.grid.GetIndex(fine.grid.Is() + f_i, fine.grid.Js() + f_j, fine.grid.Ks() + f_k);

                    f_state.rho[f_idx] = c_state.rho[c_idx];
                    f_state.mom_u[f_idx] = c_state.mom_u[c_idx];
                    f_state.mom_v[f_idx] = c_state.mom_v[c_idx];
                    f_state.mom_w[f_idx] = c_state.mom_w[c_idx];
                    f_state.eng[f_idx] = c_state.eng[c_idx];
                    for (int s = 0; s < n_sp; ++s) {
                        f_state.X(s, f_idx) = c_state.X(s, c_idx);
                    }
                }
            }
        }
    }

    void AverageFaceFromFine(Block& coarse, const Block& fine, int face, int dim, FluidState Block::* state_ptr) {
        FluidState& c_state = coarse.*state_ptr;
        const FluidState& f_state = fine.*state_ptr;
        int n_sp = c_state.GetNumSpecies();

        int child_x = fine.logical_x1 % 2;
        int child_y = fine.logical_x2 % 2;
        int child_z = fine.logical_x3 % 2;

        int x_off = child_x * (BLOCK_NX / 2);
        int y_off = (dim >= 2) ? child_y * (BLOCK_NY / 2) : 0;
        int z_off = (dim == 3) ? child_z * (BLOCK_NZ / 2) : 0;

        int c_i_start = 0, c_j_start = 0, c_k_start = 0;
        int nx = BLOCK_NX / 2, ny = (dim >= 2) ? BLOCK_NY / 2 : 1, nz = (dim == 3) ? BLOCK_NZ / 2 : 1;

        int f_i_start = 0, f_j_start = 0, f_k_start = 0;

        if (face == 0) { c_i_start = -MAX_NG; nx = MAX_NG; f_i_start = BLOCK_NX - 2 * MAX_NG; }
        else if (face == 1) { c_i_start = BLOCK_NX; nx = MAX_NG; f_i_start = 0; }
        else if (face == 2) { c_j_start = -MAX_NG; ny = MAX_NG; f_j_start = BLOCK_NY - 2 * MAX_NG; }
        else if (face == 3) { c_j_start = BLOCK_NY; ny = MAX_NG; f_j_start = 0; }
        else if (face == 4) { c_k_start = -MAX_NG; nz = MAX_NG; f_k_start = BLOCK_NZ - 2 * MAX_NG; }
        else if (face == 5) { c_k_start = BLOCK_NZ; nz = MAX_NG; f_k_start = 0; }

        for (int k = 0; k < nz; ++k) {
            for (int j = 0; j < ny; ++j) {
                for (int i = 0; i < nx; ++i) {
                    int c_i = c_i_start + i + ((face == 0 || face == 1) ? 0 : x_off);
                    int c_j = c_j_start + j + ((face == 2 || face == 3) ? 0 : y_off);
                    int c_k = c_k_start + k + ((face == 4 || face == 5) ? 0 : z_off);

                    int c_idx = coarse.grid.GetIndex(coarse.grid.Is() + c_i, coarse.grid.Js() + c_j, coarse.grid.Ks() + c_k);

                    int f_i_base = ((face == 0 || face == 1) ? f_i_start + i * 2 : (c_i - x_off) * 2);
                    int f_j_base = ((face == 2 || face == 3) ? f_j_start + j * 2 : (c_j - y_off) * 2);
                    int f_k_base = ((face == 4 || face == 5) ? f_k_start + k * 2 : (c_k - z_off) * 2);

                    double rho = 0, u = 0, v = 0, w = 0, eng = 0;
                    std::vector<double> X(n_sp, 0.0);
                    int count = 0;

                    for (int fk = 0; fk < (dim == 3 ? 2 : 1); ++fk) {
                        for (int fj = 0; fj < (dim >= 2 ? 2 : 1); ++fj) {
                            for (int fi = 0; fi < 2; ++fi) {
                                int f_idx = fine.grid.GetIndex(fine.grid.Is() + f_i_base + fi, fine.grid.Js() + f_j_base + fj, fine.grid.Ks() + f_k_base + fk);
                                rho += f_state.rho[f_idx];
                                u += f_state.mom_u[f_idx];
                                v += f_state.mom_v[f_idx];
                                w += f_state.mom_w[f_idx];
                                eng += f_state.eng[f_idx];
                                for (int s = 0; s < n_sp; ++s) X[s] += f_state.X(s, f_idx);
                                count++;
                            }
                        }
                    }

                    double inv = 1.0 / count;
                    c_state.rho[c_idx] = rho * inv;
                    c_state.mom_u[c_idx] = u * inv;
                    c_state.mom_v[c_idx] = v * inv;
                    c_state.mom_w[c_idx] = w * inv;
                    c_state.eng[c_idx] = eng * inv;
                    for (int s = 0; s < n_sp; ++s) c_state.X(s, c_idx) = X[s] * inv;
                }
            }
        }
    }
};

} // namespace amr
