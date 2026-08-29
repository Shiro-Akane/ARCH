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
#include <map>
#include <span>
#include <vector>

#include "AmrTree.h"
#include "Block.h"
#include "ExchangePlan.h"

#include "../data/GlobalDefs.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace amr {

class GhostExchange {
public:
    GhostExchange() = default;

    SameLevelExchangePlan BuildSameLevelPlan(
        const std::shared_ptr<MemoryPool>& pool,
        const std::shared_ptr<AmrTree>& tree, int dim,
        std::span<const BlockHandle> handles = {}) const
    {
        const auto& active_blocks = tree->GetActiveBlocks();
        if (active_blocks.empty())
            throw std::invalid_argument(
                "same-level exchange requires active blocks");
        if (!handles.empty() && handles.size() != active_blocks.size())
            throw std::invalid_argument(
                "same-level exchange handle count mismatch");

        std::map<int, std::size_t> active_index;
        std::vector<SameLevelTopologyEntry> topology(active_blocks.size());
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            if (!active_index.emplace(active_blocks[index], index).second)
                throw std::invalid_argument(
                    "same-level active pool index is duplicated");
            const Block& block = pool->GetBlock(active_blocks[index]);
            topology[index].logical = {
                dim, block.level, block.logical_x1,
                block.logical_x2, block.logical_x3};
            topology[index].handle = handles.empty()
                ? BlockHandle{} : handles[index];
        }
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            const Block& block = pool->GetBlock(active_blocks[index]);
            for (int face = 0; face < 2 * dim; ++face) {
                const Block::FaceNeighbors& neighbors =
                    block.face_neighbors[face];
                if (neighbors.count == 0) continue;
                if (neighbors.level_diff != 0) continue;
                if (neighbors.count != 1 || neighbors.ids[0] < 0)
                    throw std::invalid_argument(
                        "same-level face has invalid neighbor cardinality");
                const auto found = active_index.find(neighbors.ids[0]);
                if (found == active_index.end())
                    throw std::invalid_argument(
                        "same-level neighbor is not active");
                topology[index].neighbors[static_cast<std::size_t>(face)] =
                    topology[found->second].logical;
            }
        }
        const TopologyEpoch epoch = handles.empty()
            ? TopologyEpoch{} : handles.front().epoch;
        return make_same_level_exchange_plan(
            topology, dim,
            {BLOCK_NX, dim >= 2 ? BLOCK_NY : 1,
             dim == 3 ? BLOCK_NZ : 1},
            MAX_NG, epoch);
    }

    void ExecuteExchange(
        std::shared_ptr<MemoryPool> pool, std::shared_ptr<AmrTree> tree,
        int dim, FluidState Block::* state_ptr,
        std::span<const BlockHandle> handles = {})
    {
        const auto& active_blocks = tree->GetActiveBlocks();
        if (active_blocks.empty()) return;

        int n_species = pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
        const SameLevelExchangePlan plan = BuildSameLevelPlan(
            pool, tree, dim, handles);
        std::vector<HostExchangeBlockView> views;
        views.reserve(active_blocks.size());
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            Block& block = pool->GetBlock(active_blocks[index]);
            FluidState& state = block.*state_ptr;
            if (state.GetNumSpecies() != n_species)
                throw std::invalid_argument(
                    "same-level blocks have inconsistent species counts");
            HostExchangeBlockView view{};
            view.logical = {
                dim, block.level, block.logical_x1,
                block.logical_x2, block.logical_x3};
            view.handle = handles.empty() ? BlockHandle{} : handles[index];
            view.layout = {
                dim,
                {block.grid.Is(), block.grid.Js(), block.grid.Ks()},
                {block.grid.GetTotalX(), block.grid.GetTotalY(),
                 block.grid.GetTotalZ()},
                {1, block.grid.stride_y, block.grid.stride_z},
                block.grid.GetTotalSize()};
            view.conserved = {
                state.rho.data(), state.mom_u.data(), state.mom_v.data(),
                state.mom_w.data(), state.eng.data(), state.enuc_rate.data()};
            view.species = n_species == 0
                ? nullptr : state.mass_fractions.data();
            view.species_count = n_species;
            view.species_stride = block.grid.GetTotalSize();
            views.push_back(view);
        }
        const auto compiled = compile_host_exchange_plan(plan, views);
        execute_host_exchange_plan(compiled, views);
        UpdateGhostFromCoarse(pool, active_blocks, dim, state_ptr);
        UpdateGhostFromFine(pool, active_blocks, dim, state_ptr);
    }

private:
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
