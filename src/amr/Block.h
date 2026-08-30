/**
 * @file Block.h
 * @brief Definition of a single AMR Block.
 */

/**
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "Morton.h"

#include "../data/FluidState.h"
#include "../grid/Grid.h"
#include "../grid/GridMetrics.h"

namespace amr {

/**
 * @brief Represents a single AMR block with its local fluid state.
 */
struct Block {
    int id;                 ///< Unique ID of this block (its index in the memory pool)
    uint64_t morton_code;   ///< 64-bit Morton code
    int level;              ///< Refinement level (0 is root)
    int active_index = -1;  ///< Index in AmrTree active_blocks array

    // Discrete Coordinates at this level
    uint32_t logical_x1;
    uint32_t logical_x2;
    uint32_t logical_x3;

    // Grid topology object (used by physics modules)
    Grid grid;

    // Data payload
    FluidState fluid_state;
    FluidState state_next;
    FluidState state_scratch;

    // Tree Topology (Hierarchy)
    int parent_id = -1;
    int children_id[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    // Neighbor Cache (Face neighbors: -X, +X, -Y, +Y, -Z, +Z)
    // Under 2:1 refinement, a face has one coarse or same-level neighbor,
    // or at most four fine neighbors in three dimensions. The fixed capacity
    // avoids per-face allocation while covering every supported dimension.
    struct FaceNeighbors {
        int count = 0;
        int level_diff = 0; // -1: coarse, 0: same, 1: fine
        int ids[4] = {-1, -1, -1, -1};
    };
    FaceNeighbors face_neighbors[6];

    // Status flags
    bool active = false;      ///< Whether this block is currently active in the simulation
    int refine_flag = 0;      ///< 1: refine, -1: coarsen, 0: keep

    Block() = default;

    void Reset() {
        // Clear hierarchy and state while retaining allocated storage.
        id = -1;
        morton_code = 0;
        level = 0;
        active_index = -1;
        logical_x1 = 0;
        logical_x2 = 0;
        logical_x3 = 0;
        parent_id = -1;
        for (int i = 0; i < 8; ++i) children_id[i] = -1;
        for (int i = 0; i < 6; ++i) {
            face_neighbors[i].count = 0;
            face_neighbors[i].level_diff = 0;
            for (int j = 0; j < 4; ++j) face_neighbors[i].ids[j] = -1;
        }
        active = true;
        refine_flag = 0;

        fluid_state.Reset();
        state_next.Reset();
        state_scratch.Reset();
    }

    /**
     * @brief Interpolate data from a coarse parent block using MinMod limited linear gradients.
     * @param coarse The parent block.
     * @param child_idx The relative child index (0 to 7 in 3D).
     * @param dim Dimensionality (1, 2, or 3).
     */
    void InterpolateFromCoarse(const Block& coarse, int child_idx, int dim,
                               double density_floor,
                               double min_specific_internal_energy);

    /**
     * @brief Volume average data from child blocks to this coarse block.
     * @param children Array of pointers to the 8 children blocks.
     * @param dim Dimensionality (1, 2, or 3).
     */
    void AverageToCoarse(const Block* children[], int dim,
                         double density_floor,
                         double min_specific_internal_energy);


    /**
     * @brief Initialize block geometry based on root domain and its location.
     * @param root_grid  Global domain root-level Grid (carries x_min, nblockx, dim, geometry)
     * @param root_dx1/2/3  Root-level cell spacing (computed by AmrTree)
     */
    void InitGeometry(const Grid& root_grid,
                      double root_dx1, double root_dx2, double root_dx3)
    {
        // Calculate cell sizes at this level
        double factor = 1.0 / (1 << level);
        double dx1 = root_dx1 * factor;
        double dx2 = root_dx2 * factor;
        double dx3 = root_dx3 * factor;

        // Bounding box
        double x1_min = root_grid.x1_min + logical_x1 * BLOCK_NX * dx1;
        double x1_max = x1_min + BLOCK_NX * dx1;

        double x2_min = root_grid.x2_min + logical_x2 * BLOCK_NY * dx2;
        double x2_max = x2_min + BLOCK_NY * dx2;

        double x3_min = root_grid.x3_min + logical_x3 * BLOCK_NZ * dx3;
        double x3_max = x3_min + BLOCK_NZ * dx3;

        // Initialize local grid topology wrapper
        grid = Grid(MAX_NG,
                    x1_min, x1_max,
                    x2_min, x2_max,
                    x3_min, x3_max,
                    root_grid.nblockx1, root_grid.nblockx2, root_grid.nblockx3);
        grid.geometry = root_grid.geometry;
        grid.dim = root_grid.dim;
        grid.InitializeTopology();
    }
};



inline double minmod(double a, double b) {
    if (a * b > 0) {
        return (a > 0) ? std::min(a, b) : std::max(a, b);
    }
    return 0.0;
}

inline bool is_admissible_conserved_state(
    const FluidVector& state, double density_floor,
    double min_specific_internal_energy)
{
    if (!std::isfinite(state.rho) || !std::isfinite(state.mom_u) ||
        !std::isfinite(state.mom_v) || !std::isfinite(state.mom_w) ||
        !std::isfinite(state.eng) || state.rho < density_floor) {
        return false;
    }
    const double kinetic = 0.5 *
        (state.mom_u * state.mom_u + state.mom_v * state.mom_v +
         state.mom_w * state.mom_w) / state.rho;
    return std::isfinite(kinetic) &&
           state.eng - kinetic >=
               state.rho * min_specific_internal_energy;
}

inline double composition_simplex_tolerance(int species_count)
{
    return 64.0 * std::numeric_limits<double>::epsilon() *
           static_cast<double>(std::max(1, species_count));
}

inline FluidVector blend_conserved_state(
    const FluidVector& average, const FluidVector& candidate, double theta)
{
    if (theta <= 0.0) return average;
    if (theta >= 1.0) return candidate;
    return FluidVector(
        average.rho + theta * (candidate.rho - average.rho),
        average.mom_u + theta * (candidate.mom_u - average.mom_u),
        average.mom_v + theta * (candidate.mom_v - average.mom_v),
        average.mom_w + theta * (candidate.mom_w - average.mom_w),
        average.eng + theta * (candidate.eng - average.eng));
}

inline void Block::InterpolateFromCoarse(
    const Block& coarse, int child_idx, int dim, double density_floor,
    double min_specific_internal_energy) {
    const int x_offset = (child_idx & 1) ? amr::BLOCK_NX / 2 : 0;
    const int y_offset = (dim >= 2 && (child_idx & 2)) ? amr::BLOCK_NY / 2 : 0;
    const int z_offset = (dim == 3 && (child_idx & 4)) ? amr::BLOCK_NZ / 2 : 0;
    const int coarse_nx = amr::BLOCK_NX / 2;
    const int coarse_ny = (dim >= 2) ? amr::BLOCK_NY / 2 : 1;
    const int coarse_nz = (dim == 3) ? amr::BLOCK_NZ / 2 : 1;
    const int fine_y = (dim >= 2) ? 2 : 1;
    const int fine_z = (dim == 3) ? 2 : 1;
    const int fine_count = 2 * fine_y * fine_z;
    const int n_sp = fluid_state.GetNumSpecies();

    // Reconstruct each parent cell into its 2^dim children, then shift every
    // reconstruction by a constant so its physical-volume average is exactly
    // the parent conserved value.  This retains the limited second-order
    // profile while making refinement conservative in all supported metrics.
    for (int kc_local = 0; kc_local < coarse_nz; ++kc_local) {
        for (int jc_local = 0; jc_local < coarse_ny; ++jc_local) {
            for (int ic_local = 0; ic_local < coarse_nx; ++ic_local) {
                const int ic = coarse.grid.Is() + x_offset + ic_local;
                const int jc = coarse.grid.Js() + y_offset + jc_local;
                const int kc = coarse.grid.Ks() + z_offset + kc_local;
                const int c_idx = coarse.grid.GetIndex(ic, jc, kc);
                const int cx_m = coarse.grid.GetIndex(ic - 1, jc, kc);
                const int cx_p = coarse.grid.GetIndex(ic + 1, jc, kc);
                const int cy_m = (dim >= 2) ? coarse.grid.GetIndex(ic, jc - 1, kc) : c_idx;
                const int cy_p = (dim >= 2) ? coarse.grid.GetIndex(ic, jc + 1, kc) : c_idx;
                const int cz_m = (dim == 3) ? coarse.grid.GetIndex(ic, jc, kc - 1) : c_idx;
                const int cz_p = (dim == 3) ? coarse.grid.GetIndex(ic, jc, kc + 1) : c_idx;
                const double coarse_volume = GridMetrics::CellVolume(coarse.grid, ic, jc, kc);

                std::vector<FluidVector> fine_values(fine_count);
                std::vector<std::vector<double>> fine_rhoX(n_sp, std::vector<double>(fine_count));
                std::vector<int> fine_indices(fine_count);
                std::vector<double> fine_volumes(fine_count);

                int q = 0;
                for (int fk = 0; fk < fine_z; ++fk) {
                    for (int fj = 0; fj < fine_y; ++fj) {
                        for (int fi = 0; fi < 2; ++fi, ++q) {
                            const int fine_i = 2 * ic_local + fi;
                            const int fine_j = 2 * jc_local + fj;
                            const int fine_k = 2 * kc_local + fk;
                            const int f_i = grid.Is() + fine_i;
                            const int f_j = grid.Js() + fine_j;
                            const int f_k = grid.Ks() + fine_k;
                            fine_indices[q] = grid.GetIndex(f_i, f_j, f_k);
                            fine_volumes[q] = GridMetrics::CellVolume(grid, f_i, f_j, f_k);

                            const double dx_sign = (fi == 0) ? -0.25 : 0.25;
                            const double dy_sign = (fj == 0) ? -0.25 : 0.25;
                            const double dz_sign = (fk == 0) ? -0.25 : 0.25;
                            auto reconstruct = [&](auto getter) {
                                const double qc = getter(c_idx);
                                const double dq_x = minmod(qc - getter(cx_m), getter(cx_p) - qc);
                                const double dq_y = (dim >= 2) ? minmod(qc - getter(cy_m), getter(cy_p) - qc) : 0.0;
                                const double dq_z = (dim == 3) ? minmod(qc - getter(cz_m), getter(cz_p) - qc) : 0.0;
                                return qc + dx_sign * dq_x + dy_sign * dq_y + dz_sign * dq_z;
                            };

                            fine_values[q] = FluidVector(
                                reconstruct([&](int idx) { return coarse.fluid_state.rho[idx]; }),
                                reconstruct([&](int idx) { return coarse.fluid_state.mom_u[idx]; }),
                                reconstruct([&](int idx) { return coarse.fluid_state.mom_v[idx]; }),
                                reconstruct([&](int idx) { return coarse.fluid_state.mom_w[idx]; }),
                                reconstruct([&](int idx) { return coarse.fluid_state.eng[idx]; }));
                            for (int sp = 0; sp < n_sp; ++sp) {
                                fine_rhoX[sp][q] = reconstruct([&](int idx) {
                                    return coarse.fluid_state.rho[idx] * coarse.fluid_state.X(sp, idx);
                                });
                            }
                        }
                    }
                }

                auto correct_component = [&](double coarse_value, auto component) {
                    double integral = 0.0;
                    for (int cell = 0; cell < fine_count; ++cell)
                        integral += component(fine_values[cell]) * fine_volumes[cell];
                    const double shift = coarse_value - integral / coarse_volume;
                    for (int cell = 0; cell < fine_count; ++cell)
                        component(fine_values[cell]) += shift;
                };
                correct_component(coarse.fluid_state.rho[c_idx], [](FluidVector& value) -> double& { return value.rho; });
                correct_component(coarse.fluid_state.mom_u[c_idx], [](FluidVector& value) -> double& { return value.mom_u; });
                correct_component(coarse.fluid_state.mom_v[c_idx], [](FluidVector& value) -> double& { return value.mom_v; });
                correct_component(coarse.fluid_state.mom_w[c_idx], [](FluidVector& value) -> double& { return value.mom_w; });
                correct_component(coarse.fluid_state.eng[c_idx], [](FluidVector& value) -> double& { return value.eng; });

                // Euler-admissible states form a convex set. Scale every
                // reconstructed conserved-variable deviation by one common
                // factor so density and internal energy remain admissible while
                // the physical-volume average stays exactly equal to the parent.
                const FluidVector parent_state = coarse.fluid_state.get(c_idx);
                if (!is_admissible_conserved_state(
                        parent_state, density_floor,
                        min_specific_internal_energy)) {
                    throw std::runtime_error(
                        "AMR prolongation requires an admissible parent fluid state.");
                }
                double fluid_theta = 1.0;
                for (const FluidVector& candidate : fine_values) {
                    if (candidate.rho < density_floor) {
                        fluid_theta = std::min(
                            fluid_theta,
                            (parent_state.rho - density_floor) /
                                (parent_state.rho - candidate.rho));
                    }
                }
                fluid_theta = std::clamp(fluid_theta, 0.0, 1.0);
                for (const FluidVector& candidate : fine_values) {
                    if (is_admissible_conserved_state(
                            blend_conserved_state(
                                parent_state, candidate, fluid_theta),
                            density_floor,
                            min_specific_internal_energy)) {
                        continue;
                    }
                    double lower = 0.0;
                    double upper = fluid_theta;
                    for (int iteration = 0; iteration < 64; ++iteration) {
                        const double midpoint = 0.5 * (lower + upper);
                        if (is_admissible_conserved_state(
                                blend_conserved_state(
                                    parent_state, candidate, midpoint),
                                density_floor,
                                min_specific_internal_energy)) {
                            lower = midpoint;
                        } else {
                            upper = midpoint;
                        }
                    }
                    fluid_theta = lower;
                }
                if (fluid_theta < 1.0) {
                    fluid_theta *= 1.0 - 64.0 *
                        std::numeric_limits<double>::epsilon();
                }
                for (FluidVector& candidate : fine_values) {
                    candidate = blend_conserved_state(
                        parent_state, candidate, fluid_theta);
                    if (!is_admissible_conserved_state(
                            candidate, density_floor,
                            min_specific_internal_energy)) {
                        throw std::runtime_error(
                            "AMR prolongation produced an inadmissible fine-cell fluid state.");
                    }
                }

                // A valid species state is a simplex. Independent limited
                // slopes for rho and rhoX preserve their parent integrals but
                // do not, by themselves, preserve sum(rhoX)=rho in every fine
                // cell. Use an exactly normalized parent composition for the
                // species row targets; any adjustment is limited to roundoff.
                std::vector<double> parent_X(n_sp, 0.0);
                int closure_species = -1;
                double parent_sum = 0.0;
                const double simplex_tolerance =
                    composition_simplex_tolerance(n_sp);
                for (int sp = 0; sp < n_sp; ++sp) {
                    const double value = coarse.fluid_state.X(sp, c_idx);
                    if (!std::isfinite(value) ||
                        value < -simplex_tolerance) {
                        throw std::runtime_error(
                            "AMR prolongation requires finite, non-negative parent mass fractions.");
                    }
                    parent_X[sp] = std::max(0.0, value);
                    parent_sum += parent_X[sp];
                    if (closure_species < 0 || parent_X[sp] > parent_X[closure_species])
                        closure_species = sp;
                }
                if (n_sp > 0) {
                    if (!std::isfinite(parent_sum) ||
                        std::abs(parent_sum - 1.0) > simplex_tolerance) {
                        throw std::runtime_error(
                            "AMR prolongation requires parent mass fractions normalized to one.");
                    }
                    parent_X[closure_species] += 1.0 - parent_sum;
                    if (parent_X[closure_species] < 0.0) {
                        throw std::runtime_error(
                            "AMR prolongation could not close the parent composition simplex.");
                    }
                }

                for (int sp = 0; sp < n_sp; ++sp) {
                    double integral = 0.0;
                    for (int cell = 0; cell < fine_count; ++cell)
                        integral += fine_rhoX[sp][cell] * fine_volumes[cell];
                    const double coarse_rhoX = coarse.fluid_state.rho[c_idx] * parent_X[sp];
                    const double shift = coarse_rhoX - integral / coarse_volume;
                    for (int cell = 0; cell < fine_count; ++cell)
                        fine_rhoX[sp][cell] += shift;
                }

                if (n_sp > 0) {
                    // Project the independently reconstructed rhoX fields onto
                    // both required margins: each species retains its parent
                    // physical-volume integral, while every fine cell sums to
                    // its already-conservative rho. The correction weights are
                    // constant parent fractions, so their volume integral is
                    // zero. A common positivity limiter scales only the
                    // zero-margin deviations and therefore preserves both
                    // constraints.
                    std::vector<double> column_excess(fine_count, 0.0);
                    std::vector<std::vector<double>> deviation(
                        n_sp, std::vector<double>(fine_count, 0.0));
                    for (int cell = 0; cell < fine_count; ++cell) {
                        double sum_rhoX = 0.0;
                        for (int sp = 0; sp < n_sp; ++sp)
                            sum_rhoX += fine_rhoX[sp][cell];
                        column_excess[cell] = sum_rhoX - fine_values[cell].rho;
                    }

                    double theta = 1.0;
                    for (int sp = 0; sp < n_sp; ++sp) {
                        for (int cell = 0; cell < fine_count; ++cell) {
                            const double baseline =
                                parent_X[sp] * fine_values[cell].rho;
                            deviation[sp][cell] = fine_rhoX[sp][cell] - baseline
                                - parent_X[sp] * column_excess[cell];
                            if (deviation[sp][cell] < 0.0) {
                                theta = std::min(
                                    theta, baseline / -deviation[sp][cell]);
                            }
                        }
                    }
                    theta = std::clamp(theta, 0.0, 1.0);
                    if (theta < 1.0) {
                        theta *= 1.0 - 32.0 *
                            std::numeric_limits<double>::epsilon();
                    }
                    for (int cell = 0; cell < fine_count; ++cell) {
                        double sum_rhoX = 0.0;
                        for (int sp = 0; sp < n_sp; ++sp) {
                            const double baseline =
                                parent_X[sp] * fine_values[cell].rho;
                            fine_rhoX[sp][cell] =
                                baseline + theta * deviation[sp][cell];
                            if (!std::isfinite(fine_rhoX[sp][cell]) ||
                                fine_rhoX[sp][cell] < 0.0) {
                                throw std::runtime_error(
                                    "AMR composition projection produced an invalid mass fraction.");
                            }
                            sum_rhoX += fine_rhoX[sp][cell];
                        }
                        // Absorb the final floating-point closure residual into
                        // the largest parent species. This is at roundoff scale
                        // and leaves its conserved integral unchanged to the
                        // same tolerance as the fluid prolongation.
                        fine_rhoX[closure_species][cell] +=
                            fine_values[cell].rho - sum_rhoX;
                        if (!std::isfinite(fine_rhoX[closure_species][cell]) ||
                            fine_rhoX[closure_species][cell] < 0.0) {
                            throw std::runtime_error(
                                "AMR composition projection produced a negative mass fraction.");
                        }
                    }
                }

                for (int cell = 0; cell < fine_count; ++cell) {
                    fluid_state.set(fine_indices[cell], fine_values[cell]);
                    for (int sp = 0; sp < n_sp; ++sp)
                        fluid_state.X(sp, fine_indices[cell]) = fine_rhoX[sp][cell] / fine_values[cell].rho;
                }
            }
        }
    }
}

inline void Block::AverageToCoarse(
    const Block* children[], int dim, double density_floor,
    double min_specific_internal_energy) {
    const int nz = (dim == 3) ? amr::BLOCK_NZ : 1;
    const int ny = (dim >= 2) ? amr::BLOCK_NY : 1;
    const int nx = amr::BLOCK_NX;
    const int n_sp = fluid_state.GetNumSpecies();
    const int fine_y = (dim >= 2) ? 2 : 1;
    const int fine_z = (dim == 3) ? 2 : 1;

    for (int kc = 0; kc < nz; ++kc) {
        for (int jc = 0; jc < ny; ++jc) {
            for (int ic = 0; ic < nx; ++ic) {
                const int child_x = ic >= nx / 2 ? 1 : 0;
                const int child_y = (dim >= 2 && jc >= ny / 2) ? 1 : 0;
                const int child_z = (dim == 3 && kc >= nz / 2) ? 1 : 0;
                const int child_idx = child_x | (child_y << 1) | (child_z << 2);
                const Block* child = children[child_idx];
                const int i_base = (ic % (nx / 2)) * 2;
                const int j_base = (dim >= 2) ? (jc % (ny / 2)) * 2 : 0;
                const int k_base = (dim == 3) ? (kc % (nz / 2)) * 2 : 0;
                const int c_i = grid.Is() + ic;
                const int c_j = grid.Js() + jc;
                const int c_k = grid.Ks() + kc;
                const int c_idx = grid.GetIndex(c_i, c_j, c_k);
                const double coarse_volume = GridMetrics::CellVolume(grid, c_i, c_j, c_k);

                FluidVector integral{};
                std::vector<double> rhoX_integral(n_sp, 0.0);
                for (int fk = 0; fk < fine_z; ++fk) {
                    for (int fj = 0; fj < fine_y; ++fj) {
                        for (int fi = 0; fi < 2; ++fi) {
                            const int f_i = child->grid.Is() + i_base + fi;
                            const int f_j = child->grid.Js() + j_base + fj;
                            const int f_k = child->grid.Ks() + k_base + fk;
                            const int f_idx = child->grid.GetIndex(f_i, f_j, f_k);
                            const double volume = GridMetrics::CellVolume(child->grid, f_i, f_j, f_k);
                            integral = integral + child->fluid_state.get(f_idx) * volume;
                            for (int sp = 0; sp < n_sp; ++sp)
                                rhoX_integral[sp] += child->fluid_state.rho[f_idx]
                                                   * child->fluid_state.X(sp, f_idx) * volume;
                        }
                    }
                }

                const FluidVector averaged = integral * (1.0 / coarse_volume);
                if (!is_admissible_conserved_state(
                        averaged, density_floor,
                        min_specific_internal_energy)) {
                    throw std::runtime_error(
                        "AMR restriction produced an inadmissible coarse-cell fluid state.");
                }
                fluid_state.set(c_idx, averaged);
                double total_rhoX_integral = 0.0;
                for (int sp = 0; sp < n_sp; ++sp) {
                    if (!std::isfinite(rhoX_integral[sp]) ||
                        rhoX_integral[sp] < 0.0) {
                        throw std::runtime_error(
                            "AMR restriction produced an invalid species integral.");
                    }
                    total_rhoX_integral += rhoX_integral[sp];
                    fluid_state.X(sp, c_idx) = rhoX_integral[sp] / (averaged.rho * coarse_volume);
                }
                if (n_sp > 0) {
                    const double expected_rho_integral =
                        averaged.rho * coarse_volume;
                    const double scale = std::max(
                        std::abs(expected_rho_integral),
                        std::numeric_limits<double>::min());
                    if (!std::isfinite(total_rhoX_integral) ||
                        std::abs(total_rhoX_integral - expected_rho_integral) >
                            composition_simplex_tolerance(n_sp) * scale) {
                        throw std::runtime_error(
                            "AMR restriction requires species mass to close to density.");
                    }
                }
            }
        }
    }
}

} // namespace amr
