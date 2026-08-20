/**
 * @file AmrTree.h
 * @brief Manages the hierarchy and active block list for AMR.
 */

/**
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "MemoryPool.h"
#include "Morton.h"

#include "../data/GlobalDefs.h"
#include "../grid/Grid.h"
#include "../physics/diagnostics/VelocityDiagnostics.h"
#include "../physics/species/Species.h"

namespace amr {

class AmrTree {
private:
    std::shared_ptr<MemoryPool> pool;
    std::vector<int> active_blocks; // List of active block IDs

    // Global Domain (root-level grid topology)
    Grid root_grid;
    double root_dx1, root_dx2, root_dx3;
    std::vector<int> refinement_species_indices;
    using ThermodynamicEvaluator = std::function<void(const FluidState&, std::vector<double>*, std::vector<double>*, std::vector<double>*)>;
    ThermodynamicEvaluator thermodynamic_evaluator;
    int deferred_initial_refinement_passes = 0;

public:
    AmrTree(std::shared_ptr<MemoryPool> memory_pool) : pool(memory_pool) {}

    int GetRootGridDim() const { return root_grid.dim; }
    /**
     * @brief Installs the EOS-backed pressure/temperature batch evaluator.
     *
     * A type-erased batch callback keeps regridding shared by ideal, tabular,
     * and Helmholtz closures.
     */
    void SetThermodynamicEvaluator(ThermodynamicEvaluator evaluator)
    {
        thermodynamic_evaluator = std::move(evaluator);
    }

    /** @brief Defers initial thermodynamic regridding until the EOS is live. */
    void DeferInitialRefinement(int passes)
    {
        deferred_initial_refinement_passes = std::max(0, passes);
    }

    /** @brief Returns and clears deferred initial AMR passes. */
    int ConsumeDeferredInitialRefinement()
    {
        const int passes = deferred_initial_refinement_passes;
        deferred_initial_refinement_passes = 0;
        return passes;
    }

    void ConfigureRefinementSpecies(const AmrConfig& config, const SpeciesManager& species)
    {
        refinement_species_indices.clear();
        if (!config.refine_on_species) return;
        if (config.refine_all_species) {
            for (int index = 0; index < species.count(); ++index) refinement_species_indices.push_back(index);
            return;
        }
        for (const std::string& name : config.refine_species_names) {
            const int index = species.GetSpeciesID(name);
            if (index < 0) throw std::invalid_argument("Unknown AMR species indicator: " + name);
            refinement_species_indices.push_back(index);
        }
    }

    /**
     * @brief Initialize the Level 0 root blocks.
     */
    void InitRootGrid(const SimConfig& config, int n_species) {
        if (config.amr.lrefinemin < 0 ||
            config.amr.lrefinemax < config.amr.lrefinemin ||
            config.amr.lrefinemax > kMaxRefinementLevel) {
            throw std::invalid_argument("AMR refinement levels must satisfy 0 <= lrefinemin <= lrefinemax <= 15.");
        }

        const uint64_t refinement_scale = uint64_t{1} << config.amr.lrefinemax;
        const auto coordinate_supported = [refinement_scale](int root_blocks) {
            return static_cast<uint64_t>(std::max(root_blocks, 1)) * refinement_scale - 1u
                   <= kMortonCoordinateMask;
        };
        if (!coordinate_supported(config.grid.nblockx1) ||
            !coordinate_supported(config.grid.nblockx2) ||
            !coordinate_supported(config.grid.nblockx3)) {
            throw std::invalid_argument("AMR root-block extent exceeds the 20-bit Morton coordinate range at lrefinemax.");
        }

        // Populate root grid from user config
        root_grid = Grid(amr::MAX_NG,
                         config.grid.x1_min, config.grid.x1_max,
                         config.grid.x2_min, config.grid.x2_max,
                         config.grid.x3_min, config.grid.x3_max,
                         config.grid.nblockx1, config.grid.nblockx2, config.grid.nblockx3);
        root_grid.dim = config.grid.dim;
        root_grid.geometry = config.grid.geometry;

        // Root-level cell spacing
        double total_x1_len = config.grid.x1_max - config.grid.x1_min;
        double total_x2_len = config.grid.x2_max - config.grid.x2_min;
        double total_x3_len = config.grid.x3_max - config.grid.x3_min;

        root_dx1 = total_x1_len / (root_grid.nblockx1 * BLOCK_NX);
        root_dx2 = (root_grid.nblockx2 > 0) ? total_x2_len / (root_grid.nblockx2 * BLOCK_NY) : 0.0;
        root_dx3 = (root_grid.nblockx3 > 0) ? total_x3_len / (root_grid.nblockx3 * BLOCK_NZ) : 0.0;

        active_blocks.clear();

        int loop_k = std::max(1, root_grid.nblockx3);
        int loop_j = std::max(1, root_grid.nblockx2);
        int loop_i = std::max(1, root_grid.nblockx1);

        for (int k = 0; k < loop_k; ++k) {
            for (int j = 0; j < loop_j; ++j) {
                for (int i = 0; i < loop_i; ++i) {
                    int id = pool->AllocateBlock();
                    Block& b = pool->GetBlock(id);

                    b.level = 0;
                    b.logical_x1 = i;
                    b.logical_x2 = j;
                    b.logical_x3 = k;
                    b.morton_code = encodeMorton(0, i, j, k);
                    b.InitGeometry(root_grid, root_dx1, root_dx2, root_dx3);
                    b.fluid_state.InitSpecies(n_species);
                    b.state_next.InitSpecies(n_species);
                    b.state_scratch.InitSpecies(n_species);

                    active_blocks.push_back(id);
                }
            }
        }

        // Sort blocks by morton code (Z-curve)
        SortActiveBlocks();

        // The first initial-refinement pass also relies on face_neighbors in
        // RippleCheck(). Populate the root-level cache during initialization so the
        // 2:1 balance constraint is enforced from the very first pass.
        UpdateNeighbors(config);
    }

    /**
     * @brief Rebuild active AMR leaves from checkpoint topology records.
     *
     * The hierarchy retains leaf blocks only.  Level and logical coordinates
     * are sufficient to recreate geometry, Morton order, and neighbour caches.
     */
    void LoadLeafGrid(const SimConfig& config, int n_species,
                      const std::vector<int>& levels,
                      const std::vector<uint32_t>& logical_x1,
                      const std::vector<uint32_t>& logical_x2,
                      const std::vector<uint32_t>& logical_x3)
    {
        const size_t count = levels.size();
        if (count == 0 || logical_x1.size() != count || logical_x2.size() != count ||
            logical_x3.size() != count) {
            throw std::invalid_argument("Checkpoint AMR leaf metadata is inconsistent.");
        }

        InitRootGrid(config, n_species);
        for (const int root_id : active_blocks) pool->FreeBlock(root_id);
        active_blocks.clear();

        for (size_t index = 0; index < count; ++index) {
            if (levels[index] < 0 || levels[index] > config.amr.lrefinemax)
                throw std::invalid_argument("Checkpoint leaf level is outside the configured AMR range.");
            const int id = pool->AllocateBlock();
            Block& block = pool->GetBlock(id);
            block.level = levels[index];
            block.logical_x1 = logical_x1[index];
            block.logical_x2 = logical_x2[index];
            block.logical_x3 = logical_x3[index];
            block.morton_code = encodeMorton(block.level, block.logical_x1,
                                             block.logical_x2, block.logical_x3);
            block.InitGeometry(root_grid, root_dx1, root_dx2, root_dx3);
            block.fluid_state.InitSpecies(n_species);
            block.state_next.InitSpecies(n_species);
            block.state_scratch.InitSpecies(n_species);
            active_blocks.push_back(id);
        }
        SortActiveBlocks();
        UpdateNeighbors(config);
    }

    void SortActiveBlocks() {
        std::sort(active_blocks.begin(), active_blocks.end(),
            [this](int a, int b) {
                return pool->GetBlock(a).morton_code < pool->GetBlock(b).morton_code;
            });

        // Update active_index for $O(1)$ buffer addressing
        for (size_t i = 0; i < active_blocks.size(); ++i) {
            pool->GetBlock(active_blocks[i]).active_index = i;
        }
    }

    const std::vector<int>& GetActiveBlocks() const {
        return active_blocks;
    }

    /**
     * @brief Finds a block ID given its level and logical coordinates using binary search.
     * @return Block ID if found, -1 otherwise.
     */
    int FindBlock(int level, uint32_t x, uint32_t y, uint32_t z) const {
        uint64_t target_code = encodeMorton(level, x, y, z);

        auto it = std::lower_bound(active_blocks.begin(), active_blocks.end(), target_code,
            [this](int block_id, uint64_t code) {
                return pool->GetBlock(block_id).morton_code < code;
            });

        if (it != active_blocks.end() && pool->GetBlock(*it).morton_code == target_code) {
            return *it;
        }
        return -1;
    }

    /**
     * @brief Updates neighbor cache for all active blocks.
     * Searches for adjacent blocks in all 6 directions using binary search.
     */

    /**
     * @brief Evaluates all requested AMR indicators with a dimensionless Lohner estimator.
     *
     * Each selected scalar produces an error in [0, 1] from directional second
     * differences.  The block uses the maximum error over cells, directions,
     * and variables; thresholds consequently remain dimensionless and common
     * to DENS, species, EOS fields, and derivative diagnostics.
     */
    void EvaluateRefinement(const SimConfig& config) {
        constexpr double kLohnerEpsilon = 1.0e-2;
        const bool needs_pressure = config.amr.refine_on_p || config.amr.refine_on_entropy;
        const bool needs_temperature = config.amr.refine_on_temp;
        const bool needs_gamma1 = config.amr.refine_on_entropy;
        const bool needs_thermodynamics = needs_pressure || needs_temperature || needs_gamma1;

        if (config.amr.refine_on_jeans) {
            throw std::runtime_error(
                "AMR JENS requires a self-gravity potential solver; self gravity is not implemented in this build.");
        }
        if (needs_thermodynamics && !thermodynamic_evaluator) {
            throw std::runtime_error(
                "EOS-backed AMR indicators require the thermodynamic evaluator before regridding.");
        }

        for (const int block_id : active_blocks) {
            Block& block = pool->GetBlock(block_id);
            const Grid& grid = block.grid;
            const FluidState& state = block.fluid_state;
            const int total_size = grid.GetTotalSize();
            block.refine_flag = 0;

            const auto max_loehner_error = [&](const std::vector<double>& values) {
                if (static_cast<int>(values.size()) != total_size) {
                    throw std::runtime_error("AMR indicator buffer does not match the compact block layout.");
                }
                double maximum = 0.0;
                const auto directional_error = [&](int index_minus, int index, int index_plus) {
                    const double qm = values[index_minus];
                    const double q0 = values[index];
                    const double qp = values[index_plus];
                    if (!std::isfinite(qm) || !std::isfinite(q0) || !std::isfinite(qp)) {
                        throw std::runtime_error("Non-finite AMR indicator value encountered.");
                    }
                    const double numerator = std::abs(qp - 2.0 * q0 + qm);
                    const double denominator = std::abs(qp - q0) + std::abs(q0 - qm) +
                        kLohnerEpsilon * (std::abs(qp) + 2.0 * std::abs(q0) + std::abs(qm)) +
                        std::numeric_limits<double>::min();
                    return std::min(1.0, numerator / denominator);
                };

                for (int k = grid.Ks(); k < grid.Ke(); ++k) {
                    for (int j = grid.Js(); j < grid.Je(); ++j) {
                        for (int i = grid.Is(); i < grid.Ie(); ++i) {
                            const int index = grid.GetIndex(i, j, k);
                            maximum = std::max(maximum, directional_error(
                                grid.GetIndex(i - 1, j, k), index, grid.GetIndex(i + 1, j, k)));
                            if (grid.dim >= 2) {
                                maximum = std::max(maximum, directional_error(
                                    grid.GetIndex(i, j - 1, k), index, grid.GetIndex(i, j + 1, k)));
                            }
                            if (grid.dim == 3) {
                                maximum = std::max(maximum, directional_error(
                                    grid.GetIndex(i, j, k - 1), index, grid.GetIndex(i, j, k + 1)));
                            }
                        }
                    }
                }
                return maximum;
            };

            const auto primitive_velocity = [&](const std::vector<double>& momentum) {
                std::vector<double> values(total_size, 0.0);
                for (int index = 0; index < total_size; ++index) {
                    const double rho = state.rho[index];
                    values[index] = rho > config.numerics.sml_rho ? momentum[index] / rho : 0.0;
                }
                return values;
            };

            std::vector<double> pressure;
            std::vector<double> temperature;
            std::vector<double> gamma1;
            if (needs_thermodynamics) {
                thermodynamic_evaluator(state, needs_pressure ? &pressure : nullptr,
                                        needs_temperature ? &temperature : nullptr,
                                        needs_gamma1 ? &gamma1 : nullptr);
                if (needs_pressure && static_cast<int>(pressure.size()) != total_size) {
                    throw std::runtime_error("EOS pressure evaluator returned an invalid AMR buffer.");
                }
                if (needs_temperature && static_cast<int>(temperature.size()) != total_size) {
                    throw std::runtime_error("EOS temperature evaluator returned an invalid AMR buffer.");
                }
                if (needs_gamma1 && static_cast<int>(gamma1.size()) != total_size) {
                    throw std::runtime_error("EOS Gamma1 evaluator returned an invalid AMR buffer.");
                }
            }

            double block_error = 0.0;
            const auto include_indicator = [&](const std::vector<double>& values) {
                block_error = std::max(block_error, max_loehner_error(values));
            };

            if (config.amr.refine_on_rho) include_indicator(state.rho);
            if (config.amr.refine_on_p) include_indicator(pressure);
            if (config.amr.refine_on_temp) include_indicator(temperature);
            if (config.amr.refine_on_eng) include_indicator(state.eng);

            std::vector<double> velocity_x;
            std::vector<double> velocity_y;
            std::vector<double> velocity_z;
            const auto need_velocity = [&] {
                return config.amr.refine_on_velx || config.amr.refine_on_vely ||
                    config.amr.refine_on_velz || config.amr.refine_on_vorticity ||
                    config.amr.refine_on_div_v;
            };
            if (need_velocity()) {
                velocity_x = primitive_velocity(state.mom_u);
                if (config.amr.refine_on_velx) include_indicator(velocity_x);
                if (grid.dim >= 2 || config.amr.refine_on_vorticity) {
                    velocity_y = primitive_velocity(state.mom_v);
                    if (config.amr.refine_on_vely) include_indicator(velocity_y);
                }
                if (grid.dim == 3 || config.amr.refine_on_vorticity) {
                    velocity_z = primitive_velocity(state.mom_w);
                    if (config.amr.refine_on_velz) include_indicator(velocity_z);
                }
            }

            if (config.amr.refine_on_vely && grid.dim < 2) {
                throw std::invalid_argument("VELY AMR indicator requires at least two spatial dimensions.");
            }
            if (config.amr.refine_on_velz && grid.dim < 3) {
                throw std::invalid_argument("VELZ AMR indicator requires three spatial dimensions.");
            }

            if (config.amr.refine_on_entropy) {
                std::vector<double> entropy(total_size, 0.0);
                const auto valid_gamma1 = [&](int index) {
                    return std::isfinite(pressure[index]) && pressure[index] > 0.0 &&
                        std::isfinite(gamma1[index]) && gamma1[index] > 0.0;
                };
                // A thermodynamic failure in a physical cell is fatal. Ghost cells,
                // however, may sit outside an EOS table or across a coordinate-axis
                // reflection. Extend the nearest physical scalar there; this is a
                // boundary completion for the estimator, not a Gamma1 fallback.
                for (int k = grid.Ks(); k < grid.Ke(); ++k) {
                    for (int j = grid.Js(); j < grid.Je(); ++j) {
                        for (int i = grid.Is(); i < grid.Ie(); ++i) {
                            const int index = grid.GetIndex(i, j, k);
                            if (!valid_gamma1(index)) {
                                throw std::runtime_error(
                                    "ENTR requires positive physical pressure and local EOS Gamma1 (rho*c_s^2/p).");
                            }
                        }
                    }
                }
                for (int k = 0; k < grid.GetTotalZ(); ++k) {
                    for (int j = 0; j < grid.GetTotalY(); ++j) {
                        for (int i = 0; i < grid.GetTotalX(); ++i) {
                            const int index = grid.GetIndex(i, j, k);
                            const int source_i = std::clamp(i, grid.Is(), grid.Ie() - 1);
                            const int source_j = std::clamp(j, grid.Js(), grid.Je() - 1);
                            const int source_k = std::clamp(k, grid.Ks(), grid.Ke() - 1);
                            const int source_index = valid_gamma1(index)
                                ? index : grid.GetIndex(source_i, source_j, source_k);
                            entropy[index] = pressure[source_index] /
                                std::pow(std::max(state.rho[source_index], config.numerics.sml_rho), gamma1[source_index]);
                        }
                    }
                }
                include_indicator(entropy);
            }

            if (config.amr.refine_on_enuc) {
                include_indicator(state.enuc_rate);
            }

            if (config.amr.refine_on_species) {
                for (const int species : refinement_species_indices) {
                    std::vector<double> mass_fraction(total_size, 0.0);
                    for (int index = 0; index < total_size; ++index) {
                        mass_fraction[index] = state.X(species, index);
                    }
                    include_indicator(mass_fraction);
                }
            }

            if (config.amr.refine_on_vorticity || config.amr.refine_on_div_v) {
                if (velocity_y.empty()) velocity_y = primitive_velocity(state.mom_v);
                if (velocity_z.empty()) velocity_z = primitive_velocity(state.mom_w);
                std::vector<double> vorticity(total_size, 0.0);
                std::vector<double> divergence(total_size, 0.0);
                for (int k = grid.Ks() - (grid.dim == 3 ? 1 : 0); k <= grid.Ke() - (grid.dim == 3 ? 0 : 1); ++k) {
                    for (int j = grid.Js() - (grid.dim >= 2 ? 1 : 0); j <= grid.Je() - (grid.dim >= 2 ? 0 : 1); ++j) {
                        for (int i = grid.Is() - 1; i <= grid.Ie(); ++i) {
                            const int index = grid.GetIndex(i, j, k);
                            const VelocityDiagnostics::Values diagnostic =
                                VelocityDiagnostics::evaluate(grid, velocity_x, velocity_y, velocity_z, i, j, k);
                            divergence[index] = diagnostic.divergence;
                            vorticity[index] = diagnostic.vorticity;
                        }
                    }
                }
                if (config.amr.refine_on_vorticity) include_indicator(vorticity);
                if (config.amr.refine_on_div_v) include_indicator(divergence);
            }

            if (block_error > config.amr.refine_threshold && block.level < config.amr.lrefinemax) {
                block.refine_flag = 1;
            } else if (block_error < config.amr.derefine_threshold && block.level > config.amr.lrefinemin) {
                block.refine_flag = -1;
            }
        }
    }
    void RippleCheck() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (int block_id : active_blocks) {
                Block& b = pool->GetBlock(block_id);
                // Coarsening is kept at its current level until its group is
                // verified below not to violate the 2:1 balance constraint.
                int future_L = b.level + (b.refine_flag == 1 ? 1 : 0);

                // Check all neighbors
                for (int f = 0; f < 6; ++f) {
                    if (b.face_neighbors[f].count == 0) continue;
                    for (int n = 0; n < b.face_neighbors[f].count; ++n) {
                        int n_id = b.face_neighbors[f].ids[n];
                        Block& nb = pool->GetBlock(n_id);
                        // Merging nb would lower it one more level.  If that
                        // would leave a gap larger than one level, keep the
                        // sibling group intact instead of refining the domain.
                        if (nb.refine_flag == -1 && future_L > nb.level) {
                            nb.refine_flag = 0;
                            changed = true;
                        }
                        int nb_future_L = nb.level + (nb.refine_flag == 1 ? 1 : 0);

                        // Refine the neighbor when the proposed levels would differ by more than one.
                        if (future_L > nb_future_L + 1) {
                            if (nb.refine_flag != 1) {
                                nb.refine_flag = 1; // Enforce the 2:1 balance constraint.
                                changed = true;
                            }
                        }
                    }
                }
            }
        }
    }

    bool Regrid(const SimConfig& config) {
        EvaluateRefinement(config);
        RippleCheck();

        bool changed = false;
        std::vector<int> new_active_blocks;
        std::vector<int> blocks_to_free;

        for (size_t i = 0; i < active_blocks.size(); ++i) {
            int b_id = active_blocks[i];
            Block& b = pool->GetBlock(b_id);

            if (b.refine_flag == 1) {
                changed = true;
                // Split
                int num_children = 1 << root_grid.dim;
                for (int c = 0; c < num_children; ++c) {
                    int c_id = pool->AllocateBlock();
                    Block& child = pool->GetBlock(c_id);
                    child.level = b.level + 1;
                    child.logical_x1 = (b.logical_x1 << 1) + ((c & 1) ? 1 : 0);
                    child.logical_x2 = (b.logical_x2 << 1) + ((root_grid.dim >= 2 && (c & 2)) ? 1 : 0);
                    child.logical_x3 = (b.logical_x3 << 1) + ((root_grid.dim == 3 && (c & 4)) ? 1 : 0);
                    child.morton_code = encodeMorton(child.level, child.logical_x1, child.logical_x2, child.logical_x3);
                    child.InitGeometry(root_grid, root_dx1, root_dx2, root_dx3);

                    int n_species = b.fluid_state.GetNumSpecies();
                    child.fluid_state.InitSpecies(n_species);
                    child.state_next.InitSpecies(n_species);
                    child.state_scratch.InitSpecies(n_species);

                    child.InterpolateFromCoarse(b, c, root_grid.dim);

                    new_active_blocks.push_back(c_id);
                }
                blocks_to_free.push_back(b_id);
            }
            else if (b.refine_flag == -1) {
                // Only the lowest-coordinate child initiates a sibling-group merge.
                int num_children = 1 << root_grid.dim;
                bool is_first = ((b.logical_x1 & 1) == 0) && ((root_grid.dim < 2) || ((b.logical_x2 & 1) == 0)) && ((root_grid.dim < 3) || ((b.logical_x3 & 1) == 0));

                bool can_merge = is_first;
                std::vector<int> siblings;
                if (can_merge) {
                    siblings.push_back(b_id);
                    for (int c = 1; c < num_children; ++c) {
                        int nx = b.logical_x1 + ((c & 1) ? 1 : 0);
                        int ny = b.logical_x2 + ((root_grid.dim >= 2 && (c & 2)) ? 1 : 0);
                        int nz = b.logical_x3 + ((root_grid.dim == 3 && (c & 4)) ? 1 : 0);
                        int sib_id = FindBlock(b.level, nx, ny, nz);
                        if (sib_id == -1 || pool->GetBlock(sib_id).refine_flag != -1) {
                            can_merge = false;
                            break;
                        }
                        siblings.push_back(sib_id);
                    }
                }

                if (can_merge) {
                    changed = true;
                    int c_id = pool->AllocateBlock();
                    Block& parent = pool->GetBlock(c_id);
                    parent.level = b.level - 1;
                    parent.logical_x1 = b.logical_x1 >> 1;
                    parent.logical_x2 = b.logical_x2 >> 1;
                    parent.logical_x3 = b.logical_x3 >> 1;
                    parent.morton_code = encodeMorton(parent.level, parent.logical_x1, parent.logical_x2, parent.logical_x3);
                    parent.InitGeometry(root_grid, root_dx1, root_dx2, root_dx3);

                    int n_species = b.fluid_state.GetNumSpecies();
                    parent.fluid_state.InitSpecies(n_species);
                    parent.state_next.InitSpecies(n_species);
                    parent.state_scratch.InitSpecies(n_species);

                    const Block* child_ptrs[8];
                    for(int i=0; i<8; i++) child_ptrs[i] = nullptr;
                    for (int i=0; i<num_children; i++) child_ptrs[i] = &pool->GetBlock(siblings[i]);

                    parent.AverageToCoarse(child_ptrs, root_grid.dim);

                    new_active_blocks.push_back(c_id);
                    for (int sib : siblings) {
                        blocks_to_free.push_back(sib);
                        pool->GetBlock(sib).refine_flag = 2; // Mark as handled and deleted
                    }
                } else {
                    new_active_blocks.push_back(b_id); // Keep it since it couldn't merge
                }
            } else if (b.refine_flag != 2) {
                new_active_blocks.push_back(b_id);
            }
        }

        for (int id : blocks_to_free) {
            pool->FreeBlock(id);
        }

        active_blocks = new_active_blocks;
        SortActiveBlocks();
        UpdateNeighbors(config);

        return changed;
    }

    void UpdateNeighbors(const SimConfig& config) {
        const std::array<int, 3> root_extent = {
            std::max(1, root_grid.nblockx1),
            std::max(1, root_grid.nblockx2),
            std::max(1, root_grid.nblockx3)
        };
        const std::array<bool, 3> periodic = {
            config.grid.x1l_boundary_type == "periodic" && config.grid.x1r_boundary_type == "periodic",
            root_grid.dim >= 2 && config.grid.x2l_boundary_type == "periodic" && config.grid.x2r_boundary_type == "periodic",
            root_grid.dim == 3 && config.grid.x3l_boundary_type == "periodic" && config.grid.x3r_boundary_type == "periodic"
        };

        for (int block_id : active_blocks) {
            Block& b = pool->GetBlock(block_id);
            int offsets[6][3] = {
                {-1, 0, 0}, {1, 0, 0},
                {0, -1, 0}, {0, 1, 0},
                {0, 0, -1}, {0, 0, 1}
            };

            for (int f = 0; f < 6; ++f) {
                b.face_neighbors[f].count = 0;
                b.face_neighbors[f].level_diff = 0;
                b.grid.amr_coarse_fine_face[f] = false;

                if (root_grid.dim < 2 && (f == 2 || f == 3)) continue;
                if (root_grid.dim < 3 && (f == 4 || f == 5)) continue;

                int nx = static_cast<int>(b.logical_x1) + offsets[f][0];
                int ny = static_cast<int>(b.logical_x2) + offsets[f][1];
                int nz = static_cast<int>(b.logical_x3) + offsets[f][2];

                // Convert a face coordinate at this level to the equivalent
                // periodic coordinate before every same/coarse/fine lookup.
                // Without this, each edge patch applies a local boundary
                // condition instead of exchanging with the patch across the
                // global periodic seam, breaking flux conservation.
                const int extent_x = root_extent[0] << b.level;
                const int extent_y = root_extent[1] << b.level;
                const int extent_z = root_extent[2] << b.level;
                auto wrap_coordinate = [](int value, int extent, bool is_periodic, int& wrapped) {
                    if (value >= 0 && value < extent) {
                        wrapped = value;
                        return true;
                    }
                    if (!is_periodic) return false;
                    wrapped = value % extent;
                    if (wrapped < 0) wrapped += extent;
                    return true;
                };
                if (!wrap_coordinate(nx, extent_x, periodic[0], nx) ||
                    !wrap_coordinate(ny, extent_y, periodic[1], ny) ||
                    !wrap_coordinate(nz, extent_z, periodic[2], nz)) continue;

                // 1. Try same level
                int n_id = FindBlock(b.level, nx, ny, nz);
                if (n_id != -1) {
                    b.face_neighbors[f].ids[0] = n_id;
                    b.face_neighbors[f].count = 1;
                    b.face_neighbors[f].level_diff = 0;
                    continue;
                }

                // 2. Try coarse level
                int cx = nx >> 1;
                int cy = ny >> 1;
                int cz = nz >> 1;
                n_id = FindBlock(b.level - 1, cx, cy, cz);
                if (n_id != -1) {
                    b.face_neighbors[f].ids[0] = n_id;
                    b.face_neighbors[f].count = 1;
                    b.face_neighbors[f].level_diff = -1;
                    b.grid.amr_coarse_fine_face[f] = true;
                    continue;
                }

                // 3. Try fine level
                int fx = nx << 1;
                int fy = ny << 1;
                int fz = nz << 1;

                // A face can have up to 4 fine neighbors
                int f_count = 0;
                int dx_start = (f == 0) ? 1 : 0;
                int dy_start = (f == 2) ? 1 : 0;
                int dz_start = (f == 4) ? 1 : 0;

                int len_x = (f == 0 || f == 1) ? 1 : 2;
                int len_y = (f == 2 || f == 3) ? 1 : (root_grid.dim >= 2 ? 2 : 1);
                int len_z = (f == 4 || f == 5) ? 1 : (root_grid.dim == 3 ? 2 : 1);

                for (int dz = dz_start; dz < dz_start + len_z; ++dz) {
                    for (int dy = dy_start; dy < dy_start + len_y; ++dy) {
                        for (int dx = dx_start; dx < dx_start + len_x; ++dx) {
                            n_id = FindBlock(b.level + 1, fx + dx, fy + dy, fz + dz);
                            if (n_id != -1) {
                                b.face_neighbors[f].ids[f_count++] = n_id;
                            }
                        }
                    }
                }
                if (f_count > 0) {
                    b.face_neighbors[f].count = f_count;
                    b.face_neighbors[f].level_diff = 1;
                    b.grid.amr_coarse_fine_face[f] = true;
                }
            }
        }
    }
};

} // namespace amr
