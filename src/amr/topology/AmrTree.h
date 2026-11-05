/**
 * @file AmrTree.h
 * @brief Manages the hierarchy and active block list for AMR.
 *
 * The Host owns Morton lookup, leaf ordering, neighbour relations, and the
 * balanced refinement decision. Regridding prepares a candidate hierarchy
 * and logical transfer plans before committing the topology. Executors bind
 * those plans to storage without maintaining a second hierarchy algorithm.
 * Workflow:
 * 1. Receive current block identities and logical coordinates.
 * 2. Construct or validate the AMR tree and topology epoch.
 * 3. Publish changed topology only after a successful transaction.
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
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "amr/transfer/AmrTransferPlans.h"
#include "amr/transfer/RegridExecutionPlan.h"
#include "amr/refinement/RefinementIndicatorMath.h"
#include "physics/diagnostics/JeansDiagnostics.h"
#include "amr/storage/MemoryPool.h"
#include "amr/topology/Morton.h"

#include "data/GlobalDefs.h"
#include "data/StateDiagnostics.h"
#include "grid/Grid.h"
#include "physics/diagnostics/VelocityDiagnostics.h"
#include "physics/species/Species.h"

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
    using ThermodynamicEvaluator = std::function<void(const FluidState&,const Grid&,
        std::vector<double>*,std::vector<double>*,std::vector<double>*)>;
    ThermodynamicEvaluator thermodynamic_evaluator;
    using JeansEvaluator = std::function<JeansDiagnostics::Resolution(
        const FluidVector&, const double*, const GridMetrics::GeometryView&, int, int)>;
    JeansEvaluator jeans_evaluator;
    int deferred_initial_refinement_passes = 0;
    // Root coordinates remain global; pool indices and handles remain local.
    int partition_rank_ = 0, partition_size_ = 1;
    int root_begin_ = 0, root_end_ = 0;
    bool periodic_x_ = false;

public:
    AmrTree(std::shared_ptr<MemoryPool> memory_pool) : pool(memory_pool) {}

    void SetRootPartition(int rank, int size)
    {
        if (!active_blocks.empty() || size < 1 || rank < 0 || rank >= size)
            throw std::invalid_argument("invalid root partition or nonempty tree");
        partition_rank_ = rank;
        partition_size_ = size;
    }
    int PartitionRank() const noexcept { return partition_rank_; }
    int PartitionSize() const noexcept { return partition_size_; }
    int RootBegin() const noexcept { return root_begin_; }
    int RootEnd() const noexcept { return root_end_; }
    bool PeriodicX() const noexcept { return periodic_x_; }
    int GetRootGridDim() const { return root_grid.dim; }
    /** Immutable native domain identity for IO; no geometry reconstruction. */
    const Grid& GetRootGrid() const { return root_grid; }
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

    /** Bind the current immutable EOS; no result or stage state is cached. */
    void SetJeansEvaluator(JeansEvaluator evaluator)
    {
        jeans_evaluator = std::move(evaluator);
    }

    /** Inspect active accepted cells only, through authoritative EOS and metrics.
     */
    double MinimumJeansCells(const Block& block) const
    {
        if (!jeans_evaluator)
            throw std::runtime_error("JENS requires the current EOS evaluator before regridding.");
        const auto& state = block.fluid_state;
        const auto& grid = block.grid;
        const auto geometry = GridMetrics::make_geometry_view(grid);
        std::vector<double> fractions(state.GetNumSpecies());
        double minimum = std::numeric_limits<double>::infinity();
        for (int k=grid.Ks();k<grid.Ke();++k)
            for (int j=grid.Js();j<grid.Je();++j)
                for (int i=grid.Is();i<grid.Ie();++i) {
                    const int cell=grid.GetIndex(i,j,k);
                    for (int species=0;species<state.GetNumSpecies();++species)
                        fractions[species]=state.X(species,cell);
                    const auto value=[&] {

                        return jeans_evaluator(state.get(cell),fractions.data(),geometry,i,j);
                    }();
                    if (value.status!=JeansDiagnostics::Status::valid)
                        throw std::runtime_error("JENS rejected nonfinite, nonpositive or unrepresentable accepted state.");
                    minimum=std::min(minimum,value.cells);
                }
        return minimum;
    }

    /** Construct authoritative geometry for one complete logical child family.
     * No pool destination or accepted field is allocated/published by this hook.
     */
    Block CandidateParentGeometry(std::span<const int> siblings) const
    {
        if(siblings.size()!=static_cast<std::size_t>(1<<root_grid.dim))
            throw std::logic_error("candidate parent requires a complete child family");
        const auto& first=pool->GetBlock(siblings.front());
        if(first.level<=0 || (first.logical_x1&1)
            || (root_grid.dim>=2 && (first.logical_x2&1))
            || (root_grid.dim==3 && (first.logical_x3&1)))
            throw std::logic_error("candidate parent requires the lower logical child");
        for(std::size_t child=0;child<siblings.size();++child) {
            const auto& block=pool->GetBlock(siblings[child]);
            if(block.level!=first.level
                || block.logical_x1!=first.logical_x1+int(child&1)
                || block.logical_x2!=first.logical_x2+(root_grid.dim>=2?int((child>>1)&1):0)
                || block.logical_x3!=first.logical_x3+(root_grid.dim==3?int((child>>2)&1):0))
                throw std::logic_error("candidate parent child order/identity mismatch");
        }
        Block candidate{};
        candidate.level=first.level-1;
        candidate.logical_x1=first.logical_x1>>1;
        candidate.logical_x2=first.logical_x2>>1;
        candidate.logical_x3=first.logical_x3>>1;
        candidate.InitGeometry(root_grid,root_dx1,root_dx2,root_dx3);
        return candidate;
    }

    /** Preflight the real restricted parent without publishing a pool block.
     */
    bool CandidateParentResolved(const SimConfig& config, std::span<const int> siblings) const
    {
        auto candidate=CandidateParentGeometry(siblings);
        const auto& first=pool->GetBlock(siblings.front());
        candidate.fluid_state.Preallocate(candidate.grid.GetTotalSize());
        candidate.fluid_state.InitSpecies(first.fluid_state.GetNumSpecies());
        const Block* children[8]{};
        for (std::size_t child=0;child<siblings.size();++child)
            children[child]=&pool->GetBlock(siblings[child]);
        const auto status=candidate.TryAverageToCoarse(children,root_grid.dim,
            config.numerics.sml_rho,config.numerics.min_eint);

        // Existing-geometry inadmissible parents retain their original local
        // preflight veto; other contract failures remain fatal.
        if(status==regrid_math::Status::CoarseFluid) return false;
        if(status!=regrid_math::Status::Ok)
            throw std::runtime_error(regrid_math::status_message(status));
        // The same W/V-restricted candidate feeds authoritative EOS/JENS.
        return !config.amr.refine_on_jeans
            || MinimumJeansCells(candidate)>=config.amr.jeans_cells;
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
        if (partition_size_ > 1 && ((config.grid.dim < 1 || config.grid.dim > 3)
            || config.grid.geometry != "cartesian" || config.amr.lrefinemax != 0
            || config.grid.nblockx1 < partition_size_
            || ((config.grid.x1l_boundary_type == "periodic")
                != (config.grid.x1r_boundary_type == "periodic"))))
            throw std::invalid_argument("MPI root partition requires a fixed Cartesian grid, paired periodic faces and at least one block per rank");
        root_begin_ = static_cast<int>(static_cast<std::int64_t>(config.grid.nblockx1)
            * partition_rank_ / partition_size_);
        root_end_ = static_cast<int>(static_cast<std::int64_t>(config.grid.nblockx1)
            * (partition_rank_ + 1) / partition_size_);
        periodic_x_ = config.grid.x1l_boundary_type == "periodic"
            && config.grid.x1r_boundary_type == "periodic";
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

        for (int k = 0; k < loop_k; ++k) {
            for (int j = 0; j < loop_j; ++j) {
                for (int i = root_begin_; i < root_end_; ++i) {
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
    std::span<const int> RefinementSpecies() const noexcept
    {
        return refinement_species_indices;
    }

    /** Repair accepted-state JENS deficits without changing ordinary cadence. */
    void EvaluateJeansRepair(const SimConfig& config)
    {
        (void)indicator::make_selection(config.amr,root_grid.dim,refinement_species_indices,GridMetrics::geometry_kind(root_grid));
        for (int id:active_blocks) {
            auto& block=pool->GetBlock(id);
            block.refine_flag=0;
            if (!config.amr.refine_on_jeans) continue;
            if (MinimumJeansCells(block)<config.amr.jeans_cells) {
                if (block.level>=config.amr.lrefinemax)
                    throw std::runtime_error("JENS remains underresolved at lrefinemax; increase allowed resolution.");
                block.refine_flag=1;
            }
        }
    }

    void EvaluateRefinement(const SimConfig& config) {
        const auto selection = indicator::make_selection(
            config.amr, root_grid.dim, refinement_species_indices,GridMetrics::geometry_kind(root_grid));
        const bool needs_pressure = config.amr.refine_on_p || config.amr.refine_on_entropy;
        const bool needs_temperature = config.amr.refine_on_temp;
        const bool needs_gamma1 = config.amr.refine_on_entropy;
        const bool needs_thermodynamics = needs_pressure || needs_temperature || needs_gamma1;
        if (needs_thermodynamics && !thermodynamic_evaluator)
            throw std::runtime_error(
                "EOS-backed AMR indicators require the thermodynamic evaluator before regridding.");

        // EOS-backed block indicators are independent until RippleCheck.
        // Distribute their unchanged per-cell mathematics among Host workers;
        // publish no topology decision until every worker has succeeded.
        arch::state::HostFailure failure;
#pragma omp parallel for schedule(dynamic) if(active_blocks.size() >= 4)
        for (std::size_t block_index = 0;
             block_index < active_blocks.size(); ++block_index) {
            try {
                Block& block = pool->GetBlock(active_blocks[block_index]);
                const Grid& grid = block.grid;
                const FluidState& state = block.fluid_state;
                const int total = grid.GetTotalSize();
                std::vector<double> pressure, temperature, gamma1;
                if (needs_thermodynamics) {
                    thermodynamic_evaluator(state,grid,
                        needs_pressure ? &pressure : nullptr,needs_temperature ? &temperature : nullptr,
                        needs_gamma1 ? &gamma1 : nullptr);
                    if ((needs_pressure && pressure.size() != static_cast<std::size_t>(total))
                        || (needs_temperature && temperature.size() != static_cast<std::size_t>(total))
                        || (needs_gamma1 && gamma1.size() != static_cast<std::size_t>(total)))
                        throw std::runtime_error("EOS evaluator returned an invalid AMR buffer.");
                }
                const indicator::StateView view{
                    state.rho.data(), {state.mom_u.data(), state.mom_v.data(), state.mom_w.data()},
                    state.eng.data(), state.enuc_rate.data(), state.mass_fractions.data(),
                    pressure.data(), temperature.data(), gamma1.data(), total,
                    grid.Is(), grid.Ie(), grid.Js(), grid.Je(), grid.Ks(), grid.Ke(),
                    config.numerics.sml_rho,state.GetNumSpecies()};
                const auto geometry = GridMetrics::make_geometry_view(grid);
                double maximum = 0.0;
                for (int k = grid.Ks(); k < grid.Ke(); ++k)
                    for (int j = grid.Js(); j < grid.Je(); ++j)
                        for (int i = grid.Is(); i < grid.Ie(); ++i) {
                            const double error = indicator::cell_error(view, geometry,
                                selection.data(), static_cast<int>(selection.size()), i, j, k);
                            if (!std::isfinite(error))
                                throw std::runtime_error("Non-finite AMR indicator value encountered.");
                            maximum = std::max(maximum, error);
                        }
                block.refine_flag = indicator::refinement_flag(maximum, block.level,
                    config.amr.lrefinemin, config.amr.lrefinemax,
                    config.amr.refine_threshold, config.amr.derefine_threshold);
                if (config.amr.refine_on_jeans) {
                    const double minimum=MinimumJeansCells(block);
                    if (minimum<config.amr.jeans_cells) {
                        if (block.level>=config.amr.lrefinemax)
                            throw std::runtime_error("JENS remains underresolved at lrefinemax; increase allowed resolution.");
                        block.refine_flag=1;
                    }
                }
            } catch (...) { failure.capture_current(); }
        }
        failure.rethrow();
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
                        const auto& neighbor = b.face_neighbors[f].blocks[n];
                        if (neighbor.pool_index < 0) {
                            if (b.refine_flag != 0)
                                throw std::logic_error(
                                    "distributed refinement propagation is not implemented");
                            continue; // Fixed-grid no-change pass.
                        }
                        int n_id = neighbor.pool_index;
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
              reservation_(std::move(other.reservation_)),
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

        /** Exact proposed restricted-parent identity for a genuine final gate.
         * Workflow: require this live unpublished owner and built plans; look up
         * only actual restriction relations. Unchanged/prolonged cells return
         * nullopt, never a nearest geometry or guessed parent identity.
         */
        std::optional<LogicalBlockKey> restricted_parent_key(int proposed_pool_index) const
        {
            require_owner();
            if(!plans_built_)
                throw std::logic_error("restricted parent identities require built migration plans");
            for(const auto& relation:restrictions_)
                if(relation.parent==proposed_pool_index)
                    return logical_key(owner_->pool->GetBlock(relation.parent),
                        owner_->root_grid.dim);
            return std::nullopt;
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
            ValidateMigrationPlans();
            std::map<BlockHandle, int> old_lowering;
            std::map<BlockHandle, int> proposed_lowering;
            for (std::size_t index = 0; index < old_active_.size(); ++index)
                old_lowering.emplace(old_handles_[index], old_active_[index]);
            for (std::size_t index = 0; index < proposed_active_.size(); ++index)
                proposed_lowering.emplace(
                    proposed_handles_[index], proposed_active_[index]);

            // Both backends lower the same validated all-field groups. Only
            // the state/geometry adapters and execution location differ.
            const auto groups = compile_regrid_execution_plan(
                prolongation_, restriction_,
                owner_->pool->GetBlock(old_active_.front()).fluid_state.GetNumSpecies());
            for (const auto& group : groups.prolongations) {
                const Block& parent = owner_->pool->GetBlock(
                    old_lowering.at(group.source.handle));
                owner_->pool->GetBlock(
                    proposed_lowering.at(group.destination.handle))
                    .InterpolateFromCoarse(
                        parent, group.child_index, owner_->root_grid.dim,
                        config_.numerics.sml_rho, config_.numerics.min_eint);
            }
            for (const auto& group : groups.restrictions) {
                const Block* children[8]{};
                for (int child = 0; child < (1 << owner_->root_grid.dim); ++child)
                    children[child] = &owner_->pool->GetBlock(
                        old_lowering.at(group.children[child].handle));
                owner_->pool->GetBlock(proposed_lowering.at(group.destination.handle))
                    .AverageToCoarse(
                        children, owner_->root_grid.dim,
                        config_.numerics.sml_rho, config_.numerics.min_eint);
            }
            migration_complete_ = true;
        }

        void ActivateForFinalization()
        {
            require_owner();
            if (!changed_ || !migration_complete_ || activated_ || published_)
                throw std::logic_error("staged AMR topology is not activatable");
            activate_topology();
        }

        // A device executor needs the proposed neighbor geometry before its
        // private runtime is constructed. This changes metadata only: old
        // accepted fields remain untouched and abort restores the old tree.
        void ActivateForDeviceMigration()
        {
            ValidateMigrationPlans();
            if (!changed_ || activated_)
                throw std::logic_error("staged device topology is not activatable");
            activate_topology();
        }

        // Call only after checked backend migration AND ghost completion. The
        // Host arrays intentionally stay stale until an explicit IO consumer.
        void CompleteDeviceMigration()
        {
            require_owner();
            if (!plans_built_ || !activated_ || migration_complete_)
                throw std::logic_error("staged device migration is not completable");
            migration_complete_ = true;
        }

    private:
        void ValidateMigrationPlans() const
        {
            require_owner();
            if (!plans_built_ || migration_complete_ || activated_)
                throw std::logic_error("staged AMR migration is not executable");
            const auto expected = make_migration_plans(prolongation_.scope);
            if (expected.first.operations != prolongation_.operations
                || expected.first.fingerprint != prolongation_.fingerprint
                || expected.second.operations != restriction_.operations
                || expected.second.fingerprint != restriction_.fingerprint)
                throw std::invalid_argument("staged AMR migration plan drifted");
            validate_amr_plan(prolongation_);
            validate_amr_plan(restriction_);
        }

        void activate_topology()
        {
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

    public:
        void PublishNoexcept() noexcept
        {
            if (owner_ == nullptr || !activated_ || !migration_complete_ || published_)
                std::terminate();
            reservation_->CommitNoexcept();
            published_ = true;
        }

        void PublishNoChangeNoexcept() noexcept
        {
            if (owner_ == nullptr || changed_ || published_)
                std::terminate();
            reservation_->CommitNoexcept();
            published_ = true;
        }

        void ReleaseRetired()
        {
            if (owner_ == nullptr || !published_ || retired_released_)
                throw std::logic_error("retired AMR blocks are not releasable");
            ReleaseRetiredNoexcept();
        }

        void ReleaseRetiredNoexcept() noexcept
        {
            if (owner_ == nullptr || !published_ || retired_released_)
                std::terminate();
            for (const int id : retire_) owner_->pool->FreeBlock(id);
            retire_.clear();
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
                append(AmrField::EnucRate, -1);
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
                reservation_.reset();
                for (const auto& snapshot : old_refinement_) {
                    Block& block = owner_->pool->GetBlock(snapshot.id);
                    block.refine_flag = snapshot.refine_flag;
                }
                owner_ = nullptr;
            } catch (...) {
                std::terminate();
            }
        }

        AmrTree* owner_ = nullptr;
        SimConfig config_{};
        std::vector<int> old_active_;
        std::vector<int> proposed_active_;
        std::optional<MemoryPool::Reservation> reservation_;
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

public:
    /** Prepare conservative topology/transfer relations without publication.
     */
    PreparedRegrid PrepareRegrid(
        const SimConfig& config,
        const PreApplyRegridObserver& observer = {},
        const StagedAllocationObserver& allocation_observer = {},
        const std::function<void()>& evaluate_indicators = {},
        bool jeans_repair_only = false,
        const std::function<bool(const Block&,std::span<const int>)>& candidate_parent = {})
    {
        PreparedRegrid prepared(*this, config);
        if (evaluate_indicators) evaluate_indicators();
        else if (jeans_repair_only) EvaluateJeansRepair(config);
        else EvaluateRefinement(config);
        RippleCheck();
        if (observer) observer(*this);

        const int num_children = 1 << root_grid.dim;
        std::size_t required_slots = 0;
        // Select each relation once, before constructing any candidate storage.
        for (const int block_id : active_blocks) {
            Block& block = pool->GetBlock(block_id);
            if (block.refine_flag == 1) {
                prepared.changed_ = true;
                typename PreparedRegrid::RefinementRelation relation{};
                relation.parent = block_id;
                relation.children.reserve(num_children);
                prepared.refinements_.push_back(std::move(relation));
                prepared.retire_.push_back(block_id);
                required_slots += num_children;
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
                if (can_merge && config.amr.refine_on_jeans) {
                    if(candidate_parent) {
                        can_merge=candidate_parent(CandidateParentGeometry(siblings),siblings);
                    } else can_merge=CandidateParentResolved(config,siblings);
                }
                if (can_merge) {
                    prepared.changed_ = true;
                    prepared.restrictions_.push_back({siblings, -1});
                    ++required_slots;
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
        // Retired blocks remain live until publication: reserve the full
        // temporary peak, including all children and restricted parents.
        prepared.reservation_.emplace(pool->ReserveBlocks(required_slots));
        auto refinement = prepared.refinements_.begin();
        auto restriction = prepared.restrictions_.begin();
        // Preserve the original interleaved allocation order across relations.
        for (const int block_id : active_blocks) {
            const Block& block = pool->GetBlock(block_id);
            if (refinement != prepared.refinements_.end()
                && refinement->parent == block_id) {
                auto& relation = *refinement++;
                for (int child_index = 0; child_index < num_children;
                     ++child_index) {
                    const int child_id = prepared.reservation_->AllocateBlock();
                    if (allocation_observer)
                        allocation_observer(*this, child_id);
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
            } else if (restriction != prepared.restrictions_.end()
                       && restriction->children.front() == block_id) {
                const int parent_id = prepared.reservation_->AllocateBlock();
                if (allocation_observer) allocation_observer(*this, parent_id);
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
                restriction->parent = parent_id;
                ++restriction;
                prepared.proposed_active_.push_back(parent_id);
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

    /** Refresh local topology during construction/regrid before UID publication. */
    void UpdateNeighbors(const SimConfig& config) {
        std::vector<BlockTopology> local;
        local.reserve(active_blocks.size());
        for (int id : active_blocks) {
            const auto& block = pool->GetBlock(id);
            local.push_back(
                {{root_grid.dim, block.level, block.logical_x1, block.logical_x2, block.logical_x3},
                 {},
                 partition_rank_,
                 id});
        }
        UpdateNeighborsImpl(config, local, false);
    }

    /** Bind the registry's complete leaf set without allocating remote fields.
     * Validate every rank's face graph before replacing any local cache.
     */
    void UpdateNeighbors(const SimConfig& config, std::span<const BlockTopology> leaves) {
        UpdateNeighborsImpl(config, leaves, true);
    }

  private:
    void UpdateNeighborsImpl(const SimConfig& config, std::span<const BlockTopology> leaves,
                             bool complete) {
        const int dim = root_grid.dim;
        if (dim < 1 || dim > 3)
            throw std::invalid_argument("neighbor topology requires one to three dimensions");
        const std::array<std::uint64_t, 3> roots{
            static_cast<std::uint64_t>(std::max(1, root_grid.nblockx1)),
            static_cast<std::uint64_t>(std::max(1, root_grid.nblockx2)),
            static_cast<std::uint64_t>(std::max(1, root_grid.nblockx3))};
        const std::array<bool, 3> periodic{
            config.grid.x1l_boundary_type == "periodic" &&
                config.grid.x1r_boundary_type == "periodic",
            dim >= 2 && config.grid.x2l_boundary_type == "periodic" &&
                config.grid.x2r_boundary_type == "periodic",
            dim == 3 && config.grid.x3l_boundary_type == "periodic" &&
                config.grid.x3r_boundary_type == "periodic"};
        std::map<LogicalBlockKey, BlockTopology> ordered;
        const std::set<int> active_slots(active_blocks.begin(), active_blocks.end());
        std::set<int> local_slots;
        std::set<BlockHandle> handles;
        TopologyEpoch epoch{};
        for (const auto& leaf : leaves) {
            const auto& key = leaf.logical;
            const bool local = leaf.owner_rank == partition_rank_;
            if (key.dimension != dim || key.level < 0 || key.level >= 32 || leaf.owner_rank < 0 ||
                leaf.owner_rank >= partition_size_ ||
                (local ? leaf.pool_index < 0 : leaf.pool_index != -1) ||
                !ordered.emplace(key, leaf).second)
                throw std::invalid_argument("invalid or duplicate neighbor topology leaf");
            const std::array<std::uint32_t, 3> xyz{key.logical_x1, key.logical_x2, key.logical_x3};
            for (int axis = 0; axis < 3; ++axis)
                if (xyz[axis] >= (roots[axis] << key.level) || (axis >= dim && xyz[axis] != 0))
                    throw std::invalid_argument("neighbor leaf outside global domain");
            if (complete) {
                if (!is_valid(leaf.handle) || !handles.insert(leaf.handle).second ||
                    (is_valid(epoch) && epoch != leaf.handle.epoch))
                    throw std::invalid_argument("neighbor topology has stale or duplicate handles");
                epoch = leaf.handle.epoch;
            }
            if (local) {
                if (!local_slots.insert(leaf.pool_index).second ||
                    !active_slots.contains(leaf.pool_index))
                    throw std::invalid_argument("neighbor leaf has no unique local storage");
                const auto& block = pool->GetBlock(leaf.pool_index);
                if (key != LogicalBlockKey{dim, block.level, block.logical_x1, block.logical_x2,
                                           block.logical_x3})
                    throw std::invalid_argument("neighbor leaf local binding drifted");
            }
        }
        if (local_slots.size() != active_blocks.size())
            throw std::invalid_argument("neighbor topology omits local storage");
        using Faces = std::array<Block::FaceNeighbors, 6>;
        std::map<LogicalBlockKey, Faces> adjacency;
        const auto find = [&](int level,
                              const std::array<std::int64_t, 3>& xyz) -> const BlockTopology* {
            if (level < 0 || level >= 32)
                return nullptr;
            for (auto coordinate : xyz)
                if (coordinate < 0 || coordinate > std::numeric_limits<std::uint32_t>::max())
                    return nullptr;
            const LogicalBlockKey key{dim, level, static_cast<std::uint32_t>(xyz[0]),
                                      static_cast<std::uint32_t>(xyz[1]),
                                      static_cast<std::uint32_t>(xyz[2])};
            const auto found = ordered.find(key);
            return found == ordered.end() ? nullptr : &found->second;
        };
        for (const auto& [key, leaf] : ordered) {
            (void)leaf;
            auto& faces = adjacency[key];
            for (int face = 0; face < 2 * dim; ++face) {
                const int axis = face / 2;
                std::array<std::int64_t, 3> xyz{key.logical_x1, key.logical_x2, key.logical_x3};
                xyz[axis] += (face & 1) ? 1 : -1;
                const auto extent = static_cast<std::int64_t>(roots[axis] << key.level);
                if (xyz[axis] < 0 || xyz[axis] >= extent) {
                    if (!periodic[axis])
                        continue;
                    xyz[axis] = (xyz[axis] + extent) % extent;
                }
                auto& neighbors = faces[face];
                if (const auto* same = find(key.level, xyz)) {
                    neighbors.blocks[0] = *same;
                    neighbors.count = 1;
                    continue;
                }
                std::array<std::int64_t, 3> coarse{xyz[0] >> 1, xyz[1] >> 1, xyz[2] >> 1};
                if (const auto* parent = find(key.level - 1, coarse)) {
                    neighbors.blocks[0] = *parent;
                    neighbors.count = 1;
                    neighbors.level_diff = -1;
                    continue;
                }
                const int starts[3]{face == 0 ? 1 : 0, face == 2 ? 1 : 0, face == 4 ? 1 : 0};
                const int lengths[3]{axis == 0 ? 1 : 2, axis == 1 ? 1 : (dim >= 2 ? 2 : 1),
                                     axis == 2 ? 1 : (dim == 3 ? 2 : 1)};
                for (int z = starts[2]; z < starts[2] + lengths[2]; ++z)
                    for (int y = starts[1]; y < starts[1] + lengths[1]; ++y)
                        for (int x = starts[0]; x < starts[0] + lengths[0]; ++x)
                            if (const auto* child =
                                    find(key.level + 1,
                                         {2 * xyz[0] + x, 2 * xyz[1] + y, 2 * xyz[2] + z}))
                                neighbors.blocks[neighbors.count++] = *child;
                if (neighbors.count)
                    neighbors.level_diff = 1;
                if (complete && neighbors.count != (1 << (dim - 1)))
                    throw std::invalid_argument(
                        "neighbor topology has a gap or violates face 2:1 balance");
            }
        }
        if (complete) {
            for (const auto& [key, faces] : adjacency)
                for (int face = 0; face < 2 * dim; ++face) {
                    const auto& neighbors = faces[face];
                    for (int n = 0; n < neighbors.count; ++n) {
                        const auto& back = adjacency.at(neighbors.blocks[n].logical)[face ^ 1];
                        bool reciprocal = false;
                        for (int b = 0; b < back.count; ++b)
                            reciprocal |= back.blocks[b].logical == key;
                        if (!reciprocal || back.level_diff != -neighbors.level_diff)
                            throw std::invalid_argument("neighbor topology is not reciprocal");
                    }
                }
        }
        // All validation/allocation completes before changing existing caches.
        for (const auto& [key, leaf] : ordered) {
            if (leaf.owner_rank != partition_rank_)
                continue;
            auto& block = pool->GetBlock(leaf.pool_index);
            const auto& faces = adjacency.at(key);
            for (int face = 0; face < 6; ++face) {
                block.face_neighbors[face] = faces[face];
                block.grid.amr_coarse_fine_face[face] = faces[face].level_diff != 0;
            }
        }
    }
};

} // namespace amr
