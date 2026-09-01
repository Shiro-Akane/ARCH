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
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "AmrTransferPlans.h"
#include "MemoryPool.h"
#include "Morton.h"

#include "../data/GlobalDefs.h"
#include "../grid/Grid.h"
#include "../physics/diagnostics/VelocityDiagnostics.h"
#include "../physics/species/Species.h"

namespace amr {

class AmrTree {
private:
    /**
     * @brief Owns one freshly allocated pool block until a logical owner records it.
     *
     * AllocateBlock makes the block active before any staging vector can record
     * its id.  Keeping that short ownership window in a non-copyable guard gives
     * the caller a strong per-allocation guarantee even when the recording
     * push_back (or a diagnostic observer immediately before it) throws.
     */
    class PoolBlockAllocationGuard {
    public:
        PoolBlockAllocationGuard(MemoryPool& pool, int id) noexcept
            : pool_(&pool), id_(id)
        {
        }

        PoolBlockAllocationGuard(const PoolBlockAllocationGuard&) = delete;
        PoolBlockAllocationGuard& operator=(const PoolBlockAllocationGuard&)
            = delete;
        PoolBlockAllocationGuard(PoolBlockAllocationGuard&&) = delete;
        PoolBlockAllocationGuard& operator=(PoolBlockAllocationGuard&&) = delete;

        ~PoolBlockAllocationGuard() noexcept
        {
            if (pool_ == nullptr) return;
            try {
                pool_->FreeBlock(id_);
            } catch (...) {
                std::terminate();
            }
        }

        int id() const noexcept { return id_; }

        void transfer_to(std::vector<int>& allocated)
        {
            allocated.push_back(id_);
            pool_ = nullptr;
        }

    private:
        MemoryPool* pool_ = nullptr;
        int id_ = -1;
    };

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
                    PoolBlockAllocationGuard allocation(
                        *pool, pool->AllocateBlock());
                    const int id = allocation.id();
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

                    allocation.transfer_to(active_blocks);
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
            PoolBlockAllocationGuard allocation(
                *pool, pool->AllocateBlock());
            const int id = allocation.id();
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
            allocation.transfer_to(active_blocks);
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

    using PreApplyRegridObserver = std::function<void(const AmrTree&)>;
    // Called while a no-throw guard still owns the freshly allocated block.
    // This is useful for diagnostics and deterministic exception-safety tests.
    using StagedAllocationObserver =
        std::function<void(const AmrTree&, int)>;

    class PreparedRegrid {
    public:
        PreparedRegrid(const PreparedRegrid&) = delete;
        PreparedRegrid& operator=(const PreparedRegrid&) = delete;
        PreparedRegrid& operator=(PreparedRegrid&&) = delete;

        PreparedRegrid(PreparedRegrid&& other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)),
              config_(std::move(other.config_)),
              old_active_(std::move(other.old_active_)),
              proposed_active_(std::move(other.proposed_active_)),
              allocated_(std::move(other.allocated_)),
              retire_(std::move(other.retire_)),
              old_refinement_(std::move(other.old_refinement_)),
              refinements_(std::move(other.refinements_)),
              restrictions_(std::move(other.restrictions_)),
              old_handles_(std::move(other.old_handles_)),
              proposed_handles_(std::move(other.proposed_handles_)),
              prolongation_(std::move(other.prolongation_)),
              restriction_(std::move(other.restriction_)),
              changed_(other.changed_), plans_built_(other.plans_built_),
              migration_complete_(other.migration_complete_),
              activated_(other.activated_), published_(other.published_),
              retired_released_(other.retired_released_)
        {
            other.published_ = true;
            other.retired_released_ = true;
        }

        ~PreparedRegrid() { abort_noexcept(); }

        bool topology_changed() const noexcept { return changed_; }

        std::span<const int> proposed_active_blocks() const noexcept
        {
            return proposed_active_;
        }

        const ProlongationPlan& prolongation_plan() const
        {
            require_owner();
            if (!plans_built_)
                throw std::logic_error("prolongation plan is not built");
            return prolongation_;
        }

        const RestrictionPlan& restriction_plan() const
        {
            require_owner();
            if (!plans_built_)
                throw std::logic_error("restriction plan is not built");
            return restriction_;
        }

        void BuildMigrationPlans(std::span<const BlockHandle> old_handles,
                                 std::span<const BlockHandle> proposed_handles,
                                 const AmrPlanScope& scope)
        {
            require_owner();
            if (!changed_ || plans_built_ || old_handles.size() != old_active_.size()
                || proposed_handles.size() != proposed_active_.size()
                || scope.transaction_id == 0
                || scope.from_epoch == scope.to_epoch)
                throw std::invalid_argument("invalid staged AMR migration inputs");
            old_handles_.assign(old_handles.begin(), old_handles.end());
            proposed_handles_.assign(
                proposed_handles.begin(), proposed_handles.end());
            std::tie(prolongation_, restriction_) = make_migration_plans(scope);
            plans_built_ = true;
        }

        void ExecuteMigration()
        {
            require_owner();
            if (!plans_built_ || migration_complete_)
                throw std::logic_error("staged AMR migration is not executable");
            const auto expected = make_migration_plans(prolongation_.scope);
            if (expected.first.operations != prolongation_.operations
                || expected.first.fingerprint != prolongation_.fingerprint
                || expected.second.operations != restriction_.operations
                || expected.second.fingerprint != restriction_.fingerprint)
                throw std::invalid_argument("staged AMR migration plan drifted");

            // All plans and endpoint lowering validate before the first write.
            validate_amr_plan(prolongation_);
            validate_amr_plan(restriction_);
            std::map<BlockHandle, int> old_lowering;
            std::map<BlockHandle, int> proposed_lowering;
            for (std::size_t index = 0; index < old_active_.size(); ++index)
                old_lowering.emplace(old_handles_[index], old_active_[index]);
            for (std::size_t index = 0; index < proposed_active_.size(); ++index)
                proposed_lowering.emplace(
                    proposed_handles_[index], proposed_active_[index]);

            // The logical plan, rather than the staging relations, is the
            // execution authority.  Multiple field records for one endpoint
            // pair collapse to the one existing all-field reconstruction leaf.
            std::map<std::pair<BlockHandle, BlockHandle>, int>
                prolongation_groups;
            for (const auto& operation : prolongation_.operations) {
                const int child_index =
                    (operation.destination.logical.logical_x1 & 1U)
                    | ((owner_->root_grid.dim >= 2
                            ? operation.destination.logical.logical_x2 & 1U
                            : 0U)
                       << 1U)
                    | ((owner_->root_grid.dim == 3
                            ? operation.destination.logical.logical_x3 & 1U
                            : 0U)
                       << 2U);
                const auto [entry, inserted] = prolongation_groups.emplace(
                    std::pair{operation.source.handle,
                              operation.destination.handle},
                    child_index);
                if (!inserted && entry->second != child_index)
                    throw std::invalid_argument(
                        "prolongation endpoint child index drifted");
            }
            for (const auto& [endpoints, child_index]
                 : prolongation_groups) {
                const Block& parent = owner_->pool->GetBlock(
                    old_lowering.at(endpoints.first));
                owner_->pool->GetBlock(
                    proposed_lowering.at(endpoints.second))
                    .InterpolateFromCoarse(
                        parent, child_index, owner_->root_grid.dim,
                        config_.numerics.sml_rho,
                        config_.numerics.min_eint);
            }

            // Restriction needs the complete child group in geometric child
            // order.  Recover that order from the logical source coordinates
            // carried by the plan before invoking the shared averaging leaf.
            std::map<BlockHandle, std::map<int, BlockHandle>>
                restriction_groups;
            for (const auto& operation : restriction_.operations) {
                const int child_index =
                    (operation.source.logical.logical_x1 & 1U)
                    | ((owner_->root_grid.dim >= 2
                            ? operation.source.logical.logical_x2 & 1U
                            : 0U)
                       << 1U)
                    | ((owner_->root_grid.dim == 3
                            ? operation.source.logical.logical_x3 & 1U
                            : 0U)
                       << 2U);
                auto& children = restriction_groups[
                    operation.destination.handle];
                const auto [entry, inserted] = children.emplace(
                    child_index, operation.source.handle);
                if (!inserted && entry->second != operation.source.handle)
                    throw std::invalid_argument(
                        "restriction child endpoint drifted");
            }
            const int expected_children = 1 << owner_->root_grid.dim;
            for (const auto& [destination, child_handles]
                 : restriction_groups) {
                if (static_cast<int>(child_handles.size())
                    != expected_children)
                    throw std::invalid_argument(
                        "restriction plan has an incomplete child group");
                const Block* children[8]{};
                for (const auto& [child_index, handle] : child_handles) {
                    if (child_index < 0 || child_index >= expected_children)
                        throw std::invalid_argument(
                            "restriction child index is out of range");
                    children[child_index] = &owner_->pool->GetBlock(
                        old_lowering.at(handle));
                }
                owner_->pool->GetBlock(proposed_lowering.at(destination))
                    .AverageToCoarse(
                        children, owner_->root_grid.dim,
                        config_.numerics.sml_rho,
                        config_.numerics.min_eint);
            }
            migration_complete_ = true;
        }

        void ActivateForFinalization()
        {
            require_owner();
            if (!changed_ || !migration_complete_ || activated_ || published_)
                throw std::logic_error("staged AMR topology is not activatable");
            owner_->active_blocks.swap(proposed_active_);
            try {
                owner_->SortActiveBlocks();
                owner_->UpdateNeighbors(config_);
                activated_ = true;
            } catch (...) {
                owner_->active_blocks.swap(proposed_active_);
                owner_->SortActiveBlocks();
                owner_->UpdateNeighbors(config_);
                throw;
            }
        }

        void PublishNoexcept() noexcept
        {
            if (owner_ == nullptr || !activated_ || published_)
                std::terminate();
            published_ = true;
        }

        void PublishNoChangeNoexcept() noexcept
        {
            if (owner_ == nullptr || changed_ || published_)
                std::terminate();
            published_ = true;
        }

        void ReleaseRetired()
        {
            if (owner_ == nullptr || !published_ || retired_released_)
                throw std::logic_error("retired AMR blocks are not releasable");
            for (const int id : retire_) owner_->pool->FreeBlock(id);
            retire_.clear();
            allocated_.clear();
            retired_released_ = true;
        }

        void AbortNoexcept() noexcept { abort_noexcept(); }

    private:
        friend class AmrTree;

        struct RefinementSnapshot {
            int id = -1;
            int refine_flag = 0;
        };
        struct RefinementRelation {
            int parent = -1;
            std::vector<int> children;
        };
        struct RestrictionRelation {
            std::vector<int> children;
            int parent = -1;
        };

        PreparedRegrid(AmrTree& owner, const SimConfig& config)
            : owner_(&owner), config_(config), old_active_(owner.active_blocks)
        {
            old_refinement_.reserve(old_active_.size());
            for (const int id : old_active_) {
                const Block& block = owner_->pool->GetBlock(id);
                old_refinement_.push_back({id, block.refine_flag});
            }
        }

        void require_owner() const
        {
            if (owner_ == nullptr || published_)
                throw std::logic_error("staged AMR regrid is no longer usable");
        }

        static LogicalBlockKey logical_key(const Block& block, int dim)
        {
            return {dim, block.level, block.logical_x1,
                    block.logical_x2, block.logical_x3};
        }

        std::pair<ProlongationPlan, RestrictionPlan>
        make_migration_plans(const AmrPlanScope& scope) const
        {
            std::map<int, BlockHandle> old_lowering;
            std::map<int, BlockHandle> proposed_lowering;
            for (std::size_t index = 0; index < old_active_.size(); ++index)
                old_lowering.emplace(old_active_[index], old_handles_[index]);
            for (std::size_t index = 0; index < proposed_active_.size(); ++index)
                proposed_lowering.emplace(
                    proposed_active_[index], proposed_handles_[index]);

            ProlongationPlan prolongation{};
            RestrictionPlan restriction{};
            prolongation.dimension = restriction.dimension = owner_->root_grid.dim;
            prolongation.scope = restriction.scope = scope;
            const std::array<std::uint32_t, 3> extent{
                static_cast<std::uint32_t>(BLOCK_NX),
                static_cast<std::uint32_t>(owner_->root_grid.dim >= 2 ? BLOCK_NY : 1),
                static_cast<std::uint32_t>(owner_->root_grid.dim == 3 ? BLOCK_NZ : 1)};
            const LogicalAmrBox active_box{{0, 0, 0}, extent};
            const auto append_fields = [&](auto& plan,
                                           const AmrEndpoint& source,
                                           const AmrEndpoint& destination,
                                           RefinementRule rule,
                                           int species) {
                const auto append = [&](AmrField field, int component) {
                    plan.operations.push_back({
                        0, source, destination, active_box, active_box,
                        AmrAxis::X, AmrSide::Lower, field, component,
                        rule, 1.0, 1.0});
                };
                append(AmrField::Rho, -1);
                append(AmrField::MomU, -1);
                append(AmrField::MomV, -1);
                append(AmrField::MomW, -1);
                append(AmrField::Energy, -1);
                for (int component = 0; component < species; ++component)
                    append(AmrField::Species, component);
            };

            for (const auto& relation : refinements_) {
                const Block& parent = owner_->pool->GetBlock(relation.parent);
                const auto parent_handle = old_lowering.at(relation.parent);
                const AmrEndpoint source{
                    logical_key(parent, owner_->root_grid.dim), parent_handle};
                for (const int child_id : relation.children) {
                    const Block& child = owner_->pool->GetBlock(child_id);
                    const AmrEndpoint destination{
                        logical_key(child, owner_->root_grid.dim),
                        proposed_lowering.at(child_id)};
                    append_fields(
                        prolongation, source, destination,
                        RefinementRule::ConservativeMinmodProlongation,
                        child.fluid_state.GetNumSpecies());
                }
            }
            for (const auto& relation : restrictions_) {
                const Block& parent = owner_->pool->GetBlock(relation.parent);
                const AmrEndpoint destination{
                    logical_key(parent, owner_->root_grid.dim),
                    proposed_lowering.at(relation.parent)};
                for (const int child_id : relation.children) {
                    const Block& child = owner_->pool->GetBlock(child_id);
                    const AmrEndpoint source{
                        logical_key(child, owner_->root_grid.dim),
                        old_lowering.at(child_id)};
                    append_fields(
                        restriction, source, destination,
                        RefinementRule::VolumeRestriction,
                        child.fluid_state.GetNumSpecies());
                }
            }
            finalize_amr_plan(prolongation);
            finalize_amr_plan(restriction);
            return {std::move(prolongation), std::move(restriction)};
        }

        void abort_noexcept() noexcept
        {
            if (owner_ == nullptr || published_) return;
            try {
                if (activated_) {
                    owner_->active_blocks.swap(proposed_active_);
                    owner_->SortActiveBlocks();
                    owner_->UpdateNeighbors(config_);
                    activated_ = false;
                }
                for (const int id : allocated_) owner_->pool->FreeBlock(id);
                for (const auto& snapshot : old_refinement_) {
                    Block& block = owner_->pool->GetBlock(snapshot.id);
                    block.refine_flag = snapshot.refine_flag;
                }
                allocated_.clear();
                owner_ = nullptr;
            } catch (...) {
                std::terminate();
            }
        }

        AmrTree* owner_ = nullptr;
        SimConfig config_{};
        std::vector<int> old_active_;
        std::vector<int> proposed_active_;
        std::vector<int> allocated_;
        std::vector<int> retire_;
        std::vector<RefinementSnapshot> old_refinement_;
        std::vector<RefinementRelation> refinements_;
        std::vector<RestrictionRelation> restrictions_;
        std::vector<BlockHandle> old_handles_;
        std::vector<BlockHandle> proposed_handles_;
        ProlongationPlan prolongation_{};
        RestrictionPlan restriction_{};
        bool changed_ = false;
        bool plans_built_ = false;
        bool migration_complete_ = false;
        bool activated_ = false;
        bool published_ = false;
        bool retired_released_ = false;
    };

    PreparedRegrid PrepareRegrid(
        const SimConfig& config,
        const PreApplyRegridObserver& observer = {},
        const StagedAllocationObserver& allocation_observer = {})
    {
        PreparedRegrid prepared(*this, config);
        EvaluateRefinement(config);
        RippleCheck();
        if (observer) observer(*this);

        const int num_children = 1 << root_grid.dim;
        for (const int block_id : active_blocks) {
            Block& block = pool->GetBlock(block_id);
            if (block.refine_flag == 1) {
                prepared.changed_ = true;
                typename PreparedRegrid::RefinementRelation relation{};
                relation.parent = block_id;
                relation.children.reserve(num_children);
                for (int child_index = 0; child_index < num_children;
                     ++child_index) {
                    PoolBlockAllocationGuard allocation(
                        *pool, pool->AllocateBlock());
                    const int child_id = allocation.id();
                    if (allocation_observer)
                        allocation_observer(*this, child_id);
                    allocation.transfer_to(prepared.allocated_);
                    Block& child = pool->GetBlock(child_id);
                    child.level = block.level + 1;
                    child.logical_x1 = (block.logical_x1 << 1)
                        + ((child_index & 1) ? 1 : 0);
                    child.logical_x2 = (block.logical_x2 << 1)
                        + ((root_grid.dim >= 2 && (child_index & 2)) ? 1 : 0);
                    child.logical_x3 = (block.logical_x3 << 1)
                        + ((root_grid.dim == 3 && (child_index & 4)) ? 1 : 0);
                    child.morton_code = encodeMorton(
                        child.level, child.logical_x1,
                        child.logical_x2, child.logical_x3);
                    child.InitGeometry(
                        root_grid, root_dx1, root_dx2, root_dx3);
                    const int species = block.fluid_state.GetNumSpecies();
                    child.fluid_state.InitSpecies(species);
                    child.state_next.InitSpecies(species);
                    child.state_scratch.InitSpecies(species);
                    relation.children.push_back(child_id);
                    prepared.proposed_active_.push_back(child_id);
                }
                prepared.refinements_.push_back(std::move(relation));
                prepared.retire_.push_back(block_id);
            } else if (block.refine_flag == -1) {
                const bool first = ((block.logical_x1 & 1) == 0)
                    && (root_grid.dim < 2 || (block.logical_x2 & 1) == 0)
                    && (root_grid.dim < 3 || (block.logical_x3 & 1) == 0);
                bool can_merge = first;
                std::vector<int> siblings;
                if (can_merge) {
                    siblings.push_back(block_id);
                    for (int child_index = 1; child_index < num_children;
                         ++child_index) {
                        const int x = block.logical_x1
                            + ((child_index & 1) ? 1 : 0);
                        const int y = block.logical_x2
                            + ((root_grid.dim >= 2 && (child_index & 2)) ? 1 : 0);
                        const int z = block.logical_x3
                            + ((root_grid.dim == 3 && (child_index & 4)) ? 1 : 0);
                        const int sibling = FindBlock(block.level, x, y, z);
                        if (sibling == -1
                            || pool->GetBlock(sibling).refine_flag != -1) {
                            can_merge = false;
                            break;
                        }
                        siblings.push_back(sibling);
                    }
                }
                if (can_merge) {
                    prepared.changed_ = true;
                    PoolBlockAllocationGuard allocation(
                        *pool, pool->AllocateBlock());
                    const int parent_id = allocation.id();
                    if (allocation_observer)
                        allocation_observer(*this, parent_id);
                    allocation.transfer_to(prepared.allocated_);
                    Block& parent = pool->GetBlock(parent_id);
                    parent.level = block.level - 1;
                    parent.logical_x1 = block.logical_x1 >> 1;
                    parent.logical_x2 = block.logical_x2 >> 1;
                    parent.logical_x3 = block.logical_x3 >> 1;
                    parent.morton_code = encodeMorton(
                        parent.level, parent.logical_x1,
                        parent.logical_x2, parent.logical_x3);
                    parent.InitGeometry(
                        root_grid, root_dx1, root_dx2, root_dx3);
                    const int species = block.fluid_state.GetNumSpecies();
                    parent.fluid_state.InitSpecies(species);
                    parent.state_next.InitSpecies(species);
                    parent.state_scratch.InitSpecies(species);
                    prepared.restrictions_.push_back({siblings, parent_id});
                    prepared.proposed_active_.push_back(parent_id);
                    for (const int sibling : siblings) {
                        prepared.retire_.push_back(sibling);
                        pool->GetBlock(sibling).refine_flag = 2;
                    }
                } else {
                    prepared.proposed_active_.push_back(block_id);
                }
            } else if (block.refine_flag != 2) {
                prepared.proposed_active_.push_back(block_id);
            }
        }
        std::sort(prepared.proposed_active_.begin(),
                  prepared.proposed_active_.end(),
                  [this](int lhs, int rhs) {
                      return pool->GetBlock(lhs).morton_code
                          < pool->GetBlock(rhs).morton_code;
                  });
        return prepared;
    }

    bool Regrid(const SimConfig& config,
                const PreApplyRegridObserver& observer = {})
    {
        auto prepared = PrepareRegrid(config, observer);
        if (!prepared.topology_changed()) {
            prepared.PublishNoChangeNoexcept();
            return false;
        }

        std::map<LogicalBlockKey, BlockUid> old_uids;
        std::uint64_t next_uid = 1;
        std::vector<BlockHandle> old_handles;
        old_handles.reserve(active_blocks.size());
        for (const int id : active_blocks) {
            const auto key = PreparedRegrid::logical_key(
                pool->GetBlock(id), root_grid.dim);
            const BlockUid uid{next_uid++};
            old_uids.emplace(key, uid);
            old_handles.push_back({uid, {1}});
        }
        std::vector<BlockHandle> proposed_handles;
        proposed_handles.reserve(prepared.proposed_active_.size());
        for (const int id : prepared.proposed_active_) {
            const auto key = PreparedRegrid::logical_key(
                pool->GetBlock(id), root_grid.dim);
            const auto found = old_uids.find(key);
            const BlockUid uid = found == old_uids.end()
                ? BlockUid{next_uid++} : found->second;
            proposed_handles.push_back({uid, {2}});
        }
        prepared.BuildMigrationPlans(
            old_handles, proposed_handles, {1, {1}, {2}});
        prepared.ExecuteMigration();
        prepared.ActivateForFinalization();
        prepared.PublishNoexcept();
        prepared.ReleaseRetired();
        return true;
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
