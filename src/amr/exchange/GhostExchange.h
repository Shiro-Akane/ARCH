/**
 * @file GhostExchange.h
 * @brief Synchronizes same-level and coarse-fine AMR ghost cells.
 *
 * Host plan construction resolves neighbours and logical source/destination
 * cells. Execution binds those cells to block storage, using direct same-level
 * copies or the shared coarse-fine prolongation/restriction rules. Ghost
 * synchronization changes field data, not hierarchy ownership.
 * Workflow:
 * 1. Borrow stable AMR topology and native cell/face storage.
 * 2. Build or execute ghost, boundary and coarse-fine exchange plans.
 * 3. Return filled ghost state without changing active-cell ownership.
 */

#pragma once

#if ARCH_MPI_ENABLED
#include <mpi.h>
#endif

#include <array>
#include <bit>
#include <cmath>
#include <climits>
#include <exception>
#include <functional>
#include <limits>
#include <set>
#include <tuple>
#include <memory>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "amr/exchange/CoarseFineCellPlan.h"
#include "amr/exchange/CoarseFineHostExecution.h"
#include "amr/exchange/CoordinateSeamPlan.h"
#include "amr/exchange/ExchangePlan.h"
#include "amr/storage/Block.h"
#include "amr/topology/AmrTree.h"
#include "amr/transfer/AmrTransferPlans.h"
#include "amr/transfer/ConservativeRestriction.h"
#include "amr/transfer/LimitedLinearProlongation.h"
#include "amr/transfer/RegridTransferMath.h"
#include "data/GlobalDefs.h"

namespace amr {

class GhostExchange {
public:
    GhostExchange() = default;

    struct CachedPlans {
        std::vector<SameLevelExchangePlan> same_level;
        std::vector<SameLevelExchangePlan> same_level_corners;
        CoarseFineTransferPlan coarse_fine;
        CoordinateSeamPlan coordinate_seam;
        std::vector<std::vector<std::size_t>> level_indices;
    };

    // One entry per exchange owner, not one entry per historical topology.
    // Logical plans contain no storage/view pointers. Slots, generations,
    // layouts and physical measures are deliberately rebound by each executor.
    // Geometry keys this cache because seam stencils store physical donor
    // weights; field/slot contents remain outside the key.
    // The returned reference is valid until the next cache miss on this owner.
    const CachedPlans& GetPlans(
        const std::shared_ptr<MemoryPool>& pool,
        const std::shared_ptr<AmrTree>& tree, int dim,
        std::span<const BlockHandle> handles) const
    {
        if (!pool || !tree || dim < 1 || dim > 3)
            throw std::invalid_argument("invalid exchange plan cache context");
        const auto& active = tree->GetActiveBlocks();
        if (active.empty() || handles.size() != active.size())
            throw std::invalid_argument("exchange cache requires committed handles");
        auto& key = cache_probe_;
        key.clear();
        key.reserve(3 + active.size() * 72);
        key.push_back(static_cast<std::uint64_t>(dim));
        key.push_back(active.size());
        for (std::size_t i = 0; i < active.size(); ++i) {
            if (!is_valid(handles[i]))
                throw std::invalid_argument("exchange cache has invalid handle");
            const auto& block = pool->GetBlock(active[i]);
            key.insert(key.end(), {
                static_cast<std::uint64_t>(active[i]),
                handles[i].uid.value, handles[i].epoch.value,
                static_cast<std::uint64_t>(block.level),
                block.logical_x1, block.logical_x2, block.logical_x3,
                static_cast<std::uint64_t>(block.fluid_state.GetNumSpecies())});
            // Seam donor positions/weights depend on chart and native
            // geometry, even if logical topology and handles are unchanged.
            // Preserve binary64 identity instead of a lossy numeric hash.
            const auto& grid = block.grid;
            key.push_back(grid.geometry.size());
            for (unsigned char byte : grid.geometry) key.push_back(byte);
            for (double value : {grid.x1_min,grid.x1_max,grid.x2_min,grid.x2_max,
                    grid.x3_min,grid.x3_max,grid.dx1,grid.dx2,grid.dx3})
                key.push_back(std::bit_cast<std::uint64_t>(value));

            key.insert(key.end(), {static_cast<std::uint64_t>(grid.dim),
                static_cast<std::uint64_t>(grid.nblockx1),
                static_cast<std::uint64_t>(grid.nblockx2),
                static_cast<std::uint64_t>(grid.nblockx3),
                static_cast<std::uint64_t>(grid.ng)});
            for (int face = 0; face < 2 * dim; ++face) {
                const auto& neighbor = block.face_neighbors[face];
                if (neighbor.count < 0 || neighbor.count > 4)
                    throw std::invalid_argument("exchange cache has invalid neighbor count");
                key.push_back(static_cast<std::uint64_t>(neighbor.count));
                key.push_back(static_cast<std::uint64_t>(neighbor.level_diff));
                for (const auto& endpoint : neighbor.blocks)
                    key.insert(key.end(), {static_cast<std::uint64_t>(endpoint.pool_index),
                                           static_cast<std::uint64_t>(endpoint.owner_rank),
                                           endpoint.handle.uid.value, endpoint.handle.epoch.value,
                                           static_cast<std::uint64_t>(endpoint.logical.dimension),
                                           static_cast<std::uint64_t>(endpoint.logical.level),
                                           endpoint.logical.logical_x1, endpoint.logical.logical_x2,
                                           endpoint.logical.logical_x3});
            }
        }
        // Exact equality avoids a fingerprint collision becoming a cache hit.
        if (cached_plans_ && key == cached_key_) {
            ++cache_hits_;
            return *cached_plans_;
        }
        auto candidate = std::make_unique<CachedPlans>();
        candidate->same_level = BuildSameLevelPlans(pool, tree, dim, handles);
        candidate->coarse_fine = BuildCoarseFinePlan(pool, tree, dim, handles);
        if(dim==2&&!candidate->coarse_fine.operations.empty())
            for(const auto& faces:candidate->same_level)
                candidate->same_level_corners.push_back(make_same_level_corner_completion_plan(faces));
        candidate->coordinate_seam = make_coordinate_seam_plan(
            pool, active, dim);
        std::map<BlockHandle, std::size_t> indices;
        for (std::size_t i = 0; i < handles.size(); ++i)
            if (!indices.emplace(handles[i], i).second)
                throw std::invalid_argument("exchange cache has duplicate handles");
        for (const auto& plan : candidate->same_level) {
            auto& level = candidate->level_indices.emplace_back();
            level.reserve(plan.blocks.size());
            for (const auto& endpoint : plan.blocks)
                level.push_back(indices.at(endpoint.handle));
        }
        cached_key_.swap(key);
        cached_plans_.swap(candidate);
        host_compiled_.clear();
        ++cache_builds_;
        return *cached_plans_;
    }

    std::size_t PlanCacheBuilds() const noexcept { return cache_builds_; }
    std::size_t PlanCacheHits() const noexcept { return cache_hits_; }
    std::size_t HostPlanCacheBuilds() const noexcept { return host_cache_builds_; }

    SameLevelExchangePlan BuildSameLevelPlan(
        const std::shared_ptr<MemoryPool>& pool,
        const std::shared_ptr<AmrTree>& tree, int dim,
        std::span<const BlockHandle> handles = {}) const
    {
        auto plans = BuildSameLevelPlans(pool, tree, dim, handles);
        if (plans.size() != 1)
            throw std::invalid_argument(
                "one same-level plan cannot represent mixed AMR levels");
        return std::move(plans.front());
    }

    std::vector<SameLevelExchangePlan> BuildSameLevelPlans(
        const std::shared_ptr<MemoryPool>& pool,
        const std::shared_ptr<AmrTree>& tree, int dim,
        std::span<const BlockHandle> handles = {}) const
    {
        const auto& active_blocks = tree->GetActiveBlocks();
        if (active_blocks.empty())
            throw std::invalid_argument(
                "same-level exchange requires active blocks");
        if (handles.empty() || handles.size() != active_blocks.size())
            throw std::invalid_argument(
                "same-level exchange requires committed handles");

        std::map<int, std::size_t> active_index;
        std::map<int, std::vector<std::size_t>> indices_by_level;
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            if (!active_index.emplace(active_blocks[index], index).second)
                throw std::invalid_argument(
                    "same-level active pool index is duplicated");
            const Block& block = pool->GetBlock(active_blocks[index]);
            indices_by_level[block.level].push_back(index);
        }

        std::vector<SameLevelExchangePlan> plans;
        plans.reserve(indices_by_level.size());
        for (const auto& [level, indices] : indices_by_level) {
            std::vector<SameLevelTopologyEntry> topology(indices.size());
            std::map<std::size_t, std::size_t> local_index;
            for (std::size_t local = 0; local < indices.size(); ++local) {
                const std::size_t global = indices[local];
                local_index.emplace(global, local);
                const Block& block = pool->GetBlock(active_blocks[global]);
                if (block.level != level)
                    throw std::logic_error(
                        "same-level grouping changed during plan construction");
                topology[local].logical = {
                    dim, block.level, block.logical_x1,
                    block.logical_x2, block.logical_x3};
                topology[local].handle = handles[global];
            }
            for (std::size_t local = 0; local < indices.size(); ++local) {
                const std::size_t global = indices[local];
                const Block& block = pool->GetBlock(active_blocks[global]);
                for (int face = 0; face < 2 * dim; ++face) {
                    const Block::FaceNeighbors& neighbors =
                        block.face_neighbors[face];
                    if (neighbors.count == 0 || neighbors.level_diff != 0)
                        continue;
                    if (neighbors.count != 1)
                        throw std::invalid_argument(
                            "same-level face has invalid neighbor cardinality");
                    (void)neighbor_endpoint(neighbors.blocks[0], *pool, active_index, handles, dim,
                                            tree->PartitionRank(), tree->PartitionSize());
                    // Local copy plans omit remote endpoints consumed by the
                    // existing MPI X executor; remote storage is never invented.
                    if (neighbors.blocks[0].owner_rank != tree->PartitionRank())
                        continue;
                    const auto found = active_index.find(neighbors.blocks[0].pool_index);
                    if (found == active_index.end())
                        throw std::invalid_argument(
                            "same-level neighbor is not active");
                    const auto grouped = local_index.find(found->second);
                    if (grouped == local_index.end())
                        throw std::invalid_argument(
                            "same-level neighbor crossed a level group");
                    topology[local].neighbors[
                        static_cast<std::size_t>(face)] =
                        topology[grouped->second].logical;
                }
            }
            const TopologyEpoch epoch = topology.front().handle.epoch;
            plans.push_back(make_same_level_exchange_plan(
                topology, dim,
                {BLOCK_NX, dim >= 2 ? BLOCK_NY : 1,
                 dim == 3 ? BLOCK_NZ : 1},
                MAX_NG, epoch));
        }
        return plans;
    }

    CoarseFineTransferPlan BuildCoarseFinePlan(
        const std::shared_ptr<MemoryPool>& pool,
        const std::shared_ptr<AmrTree>& tree, int dim,
        std::span<const BlockHandle> handles) const
    {
        const auto& active_blocks = tree->GetActiveBlocks();
        if (active_blocks.empty())
            throw std::invalid_argument(
                "coarse-fine exchange requires active blocks");
        if (handles.size() != active_blocks.size())
            throw std::invalid_argument(
                "coarse-fine exchange requires committed handles");

        std::map<int, std::size_t> active_index;
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            if (!active_index.emplace(active_blocks[index], index).second)
                throw std::invalid_argument(
                    "coarse-fine active pool index is duplicated");
        }

        CoarseFineTransferPlan plan{};
        plan.dimension = dim;
        plan.scope = {0, handles.front().epoch, handles.front().epoch};

        for (std::size_t destination_index = 0;
             destination_index < active_blocks.size(); ++destination_index) {
            const Block& destination = pool->GetBlock(active_blocks[destination_index]);
            for (int face = 0; face < 2 * dim; ++face) {
                const auto& neighbors = destination.face_neighbors[face];
                if (neighbors.count == 0 || neighbors.level_diff == 0)
                    continue;
                const int expected = neighbors.level_diff == -1 ? 1 : 1 << (dim - 1);
                if ((neighbors.level_diff != -1 && neighbors.level_diff != 1) ||
                    neighbors.count != expected)
                    throw std::invalid_argument("coarse-fine face has invalid cardinality");
                for (int index = 0; index < neighbors.count; ++index) {
                    const auto& descriptor = neighbors.blocks[index];
                    const auto source =
                        neighbor_endpoint(descriptor, *pool, active_index, handles, dim,
                                          tree->PartitionRank(), tree->PartitionSize());
                    if (source.logical.level != destination.level + neighbors.level_diff)
                        throw std::invalid_argument("coarse-fine endpoint level drifted");
                    if (descriptor.owner_rank == tree->PartitionRank() &&
                        pool->GetBlock(descriptor.pool_index).fluid_state.GetNumSpecies() !=
                            destination.fluid_state.GetNumSpecies())
                        throw std::invalid_argument(
                            "coarse-fine blocks have inconsistent species counts");
                    append_coarse_fine_face(
                        plan, source, {logical_key(destination, dim), handles[destination_index]},
                        face, destination.fluid_state.GetNumSpecies());
                }
            }
        }
        finalize_amr_plan(plan);
        return plan;
    }

    void ExecuteCoarseFinePlan(const CoarseFineTransferPlan& plan,
                               const std::shared_ptr<MemoryPool>& pool,
                               const std::shared_ptr<AmrTree>& tree, int dim,
                               FluidState Block::* state_ptr,
                               std::span<const BlockHandle> handles) {
        if (tree && tree->PartitionSize() > 1) {
            ExecuteRemoteCoarseFinePlan(plan, pool, tree, dim, state_ptr, handles);
            return;
        }
        if (!pool || !tree || !state_ptr || dim < 1 || dim > 3 || plan.dimension != dim)
            throw std::invalid_argument("coarse-fine Host execution view mismatch");
        const auto& active = tree->GetActiveBlocks();
        if (active.empty() || handles.size() != active.size())
            throw std::invalid_argument("coarse-fine Host execution requires committed blocks");
        const int species = (pool->GetBlock(active.front()).*state_ptr).GetNumSpecies();
        std::map<AmrEndpoint, int> local;
        std::set<LogicalBlockKey> logical_blocks;
        for (std::size_t i = 0; i < active.size(); ++i) {
            const auto& block = pool->GetBlock(active[i]);
            coarse_fine_host::validate_state(block, state_ptr, species);
            if (!logical_blocks.insert(logical_key(block, dim)).second)
                throw std::invalid_argument("coarse-fine Host view has duplicate logical block");
            if (!is_valid(handles[i]) || handles[i].epoch != plan.scope.from_epoch ||
                !local.emplace(AmrEndpoint{logical_key(block, dim), handles[i]}, active[i]).second)
                throw std::invalid_argument("coarse-fine Host view has stale or duplicate handle");
        }
        const auto cells = compile_coarse_fine_cell_plan(plan, species);
        std::vector<std::pair<int, int>> destinations;
        destinations.reserve(cells.transfers.size());
        for (const auto& transfer : cells.transfers) {
            const auto source = local.find(transfer.source),
                       destination = local.find(transfer.destination);
            if (source == local.end() || destination == local.end())
                throw std::invalid_argument("coarse-fine logical endpoint is stale");
            destinations.emplace_back(
                destination->second,
                coarse_fine_host::cell_index(pool->GetBlock(destination->second).grid,
                                             transfer.destination_cell));
        }
        std::vector<coarse_fine_host::Values> gathered;
        gathered.reserve(cells.transfers.size());
        for (const auto& transfer : cells.transfers)
            gathered.push_back(coarse_fine_host::gather(
                transfer, pool->GetBlock(local.at(transfer.source)), state_ptr, dim, species));
        // All allocation, geometry validation and arithmetic precede writes.
        for (std::size_t i = 0; i < destinations.size(); ++i)
            coarse_fine_host::scatter(pool->GetBlock(destinations[i].first).*state_ptr,
                                      destinations[i].second, gathered[i]);
    }

    void ExecuteExchange(
        std::shared_ptr<MemoryPool> pool, std::shared_ptr<AmrTree> tree,
        int dim, FluidState Block::* state_ptr,
        std::span<const BlockHandle> handles = {})
    {
        const auto& active_blocks = tree->GetActiveBlocks();
        if (handles.empty() || handles.size() != active_blocks.size())
            throw std::invalid_argument(
                "Host exchange requires committed handles");
        if (active_blocks.empty()) return;

        int n_species = pool->GetBlock(active_blocks[0]).fluid_state.GetNumSpecies();
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
            view.handle = handles[index];
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
        const auto& plans = GetPlans(pool, tree, dim, handles);
        // Validate native topology/storage before any remote destination write.
        // No remote block is allocated and no alternate exchange owner exists.
        if (tree->PartitionSize() > 1) {
            for (const auto& plan : plans.same_level)
                for (const auto& view : views)
                    exchange_detail::validate_host_view(plan, view);
            ExchangeRemoteRootFaces(*tree, *pool, views, n_species);
        }
        host_compiled_.resize(plans.same_level.size()+plans.same_level_corners.size());
        std::vector<HostExchangeBlockView> level_views;
        const auto execute_level=[&](const SameLevelExchangePlan& plan,
                                    std::size_t group,std::size_t cache_index) {
            level_views.clear();
            level_views.reserve(plan.blocks.size());
            for (const auto index : plans.level_indices[group])
                level_views.push_back(views[index]);
            auto& compiled = host_compiled_[cache_index];
            bool reusable = compiled.has_value()
                && compiled->blocks.size() == level_views.size()
                && compiled->species_count == n_species;
            for (std::size_t i = 0; i < level_views.size(); ++i) {
                const auto& view = level_views[i];
                // Pointers/stride and live storage remain validated on hits.
                exchange_detail::validate_host_view(plan, view);
                if (reusable && (compiled->layouts[i] != view.layout
                    || compiled->blocks[i].logical != view.logical
                    || compiled->blocks[i].handle != view.handle))
                    reusable = false;
            }
            if (!reusable) {
                auto candidate = compile_host_exchange_plan(plan, level_views);
                exchange_detail::validate_compiled_host_exchange_plan(
                    candidate, level_views);
                compiled = std::move(candidate);
                ++host_cache_builds_;
            }
            // The private compiled plan has passed complete fail-before-scatter
            // validation once. Above, every current view and topology identity
            // is still checked on each exchange before any destination write.
            exchange_detail::execute_prevalidated_host_exchange_plan(
                *compiled, level_views, host_workspace_);
        };
        for(std::size_t group=0;group<plans.same_level.size();++group)
            execute_level(plans.same_level[group],group,group);
        // The fixed-root MPI transport above has no coarse/fine work.
        // Keep its established communication path free of empty AMR collectives.
        if (tree->PartitionSize() == 1 || !plans.coarse_fine.operations.empty())
            ExecuteCoarseFinePlan(plans.coarse_fine, pool, tree, dim, state_ptr, handles);
        for(std::size_t group=0;group<plans.same_level_corners.size();++group)
            execute_level(plans.same_level_corners[group],group,plans.same_level.size()+group);

        execute_coordinate_seam_plan(plans.coordinate_seam, pool, state_ptr);
    }

    /** Storage callbacks borrow the existing packet; they own neither topology
     * nor transport. Completion is synchronous, including exceptional exits.
     * Device consumers reuse Cartesian same-level and coarse/fine recipes.
     */
    struct DistributedStorage {
        std::uint64_t source_version = 0;
        std::function<void()> begin;
        std::function<void(std::span<const ExchangePacketSlice>, std::span<std::uint64_t>)> gather;
        std::function<void(std::span<const ExchangePacketSlice>, std::span<const std::uint64_t>)> scatter;
        std::function<void(const CoarseFineCellPlan&, std::span<double>)> gather_coarse_fine;
    };

    /** Agree local boundary failures before a peer can enter its next collective. */
    static void CompleteDistributedPhase(std::exception_ptr failure) {
#if ARCH_MPI_ENABLED
        (void)RequireMpiWorld();
        PrepareMpi([&] { if (failure) std::rethrow_exception(failure); });
#else
        (void)failure;
        throw std::logic_error("distributed boundary requires ARCH_ENABLE_MPI=ON");
#endif
    }

    /** Cached ranks join a refresh if any peer needs ghosts. Identities are
     * slot, field version and topology epoch; completion tokens remain local.
     */
    template <class Prepare>
    static bool PrepareDistributedBoundary(Prepare&& prepare,
        const std::array<std::uint64_t, 3>& identity) {
#if ARCH_MPI_ENABLED
        (void)RequireMpiWorld();
        int needed = 0, any = 0;
        PrepareMpi([&] { needed = prepare() ? 1 : 0; });
        std::array<std::uint64_t, 3> minimum{}, maximum{};
        CheckMpi(MPI_Allreduce(identity.data(), minimum.data(), 3, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD));
        CheckMpi(MPI_Allreduce(identity.data(), maximum.data(), 3, MPI_UINT64_T, MPI_MAX, MPI_COMM_WORLD));
        if (minimum != maximum)
            throw std::invalid_argument("distributed boundary slot/version/epoch mismatch");
        CheckMpi(MPI_Allreduce(&needed, &any, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD));
        return any != 0;
#else
        (void)prepare; (void)identity;
        throw std::logic_error("distributed boundary requires ARCH_ENABLE_MPI=ON");
#endif
    }

    /** All ranks call after Host workers join. Reuses serial X/Y/Z recipes,
     * coarse/fine faces and 2-D corner completion. Only compact topology is
     * global; fields stay on their owners. Failure leaves ghosts invalid:
     * completed earlier sweeps are not rolled back. Active cells are untouched.
     */
    void ExecuteDistributedExchange(const std::shared_ptr<MemoryPool>& pool,
                                    const std::shared_ptr<AmrTree>& tree, int dim,
                                    FluidState Block::* slot,
                                    std::span<const BlockHandle> handles,
                                    const DistributedStorage* storage = nullptr) {
        // A serial reference inside WORLD must not participate.
        if (tree && tree->PartitionSize() == 1) {
            if (storage) throw std::invalid_argument("distributed storage requires partitioned topology");
            ExecuteExchange(pool, tree, dim, slot, handles);
            return;
        }
#if ARCH_MPI_ENABLED
        const auto [rank, size] = RequireMpiWorld();
        struct Transfer {
            int source = -1, destination = -1, from = -1, to = -1;
            LogicalExchangeBox source_box{}, destination_box{};
            std::size_t send = 0, receive = 0;
        };
        struct Sweep {
            std::vector<int> send_counts, receive_counts, send_offsets, receive_offsets;
            std::vector<Transfer> transfers;
            std::vector<ExchangePacketSlice> sources, destinations;
            std::vector<std::uint64_t> send, receive;
        };
        std::array<Sweep, 6> sweeps;
        std::vector<HostExchangeBlockView> views;
        std::map<AmrEndpoint, int> local;
        CoarseFineTransferPlan coarse_fine;
        int species = 0;
        // Endpoint plus six (endpoint, owner) same-level descriptors.
        constexpr int words_per_block = 7 + 6 * 8;
        std::vector<std::uint64_t> words, global;
        std::vector<int> counts, offsets;
        std::array<std::uint64_t, 18> signature{}, minimum{}, maximum{};
        // Keep the common header extent shared with coarse/fine entry points:
        // mismatched calls must reject by tag, not truncate an MPI collective.
        std::array<std::uint64_t, 2> storage_identity{}, storage_min{}, storage_max{};
        const auto encode = [](auto& out, const AmrEndpoint& e) {
            out.insert(out.end(), {static_cast<std::uint64_t>(e.logical.dimension),
                                   static_cast<std::uint64_t>(e.logical.level),
                                   e.logical.logical_x1, e.logical.logical_x2, e.logical.logical_x3,
                                   e.handle.uid.value, e.handle.epoch.value});
        };
        PrepareMpi([&] {
            if (!pool || !tree || dim < 1 || dim > 3 || tree->PartitionRank() != rank ||
                tree->PartitionSize() != size || tree->GetRootGridDim() != dim)
                throw std::invalid_argument("distributed exchange communicator/topology mismatch");
            const int state_slot = slot == &Block::fluid_state     ? 0
                                   : slot == &Block::state_next    ? 1
                                   : slot == &Block::state_scratch ? 2
                                                                   : -1;
            if (state_slot < 0)
                throw std::invalid_argument("distributed exchange invalid state slot");
            const auto& active = tree->GetActiveBlocks();
            if (active.empty() || handles.size() != active.size())
                throw std::invalid_argument(
                    "distributed exchange requires nonempty committed storage");
            if (active.size() > static_cast<std::size_t>(INT_MAX / words_per_block))
                throw std::overflow_error("distributed exchange topology exceeds INT_MAX");
            species = (pool->GetBlock(active.front()).*slot).GetNumSpecies();
            if (species < 0 || species > INT_MAX - 6)
                throw std::invalid_argument("distributed exchange invalid species extent");
            const auto& root = tree->GetRootGrid();
            if (root.geometry != "cartesian")
                throw std::invalid_argument("distributed exchange requires Cartesian geometry");
            if (storage && (!storage->source_version || !storage->begin
                || !storage->gather || !storage->scatter))
                throw std::invalid_argument("distributed storage callbacks are incomplete");
            signature = {3,
                         static_cast<std::uint64_t>(dim),
                         static_cast<std::uint64_t>(species),
                         static_cast<std::uint64_t>(state_slot),
                         handles.front().epoch.value,
                         BLOCK_NX,
                         BLOCK_NY,
                         BLOCK_NZ,
                         MAX_NG,
                         static_cast<std::uint64_t>(root.nblockx1),
                         static_cast<std::uint64_t>(root.nblockx2),
                         static_cast<std::uint64_t>(root.nblockx3),
                         std::bit_cast<std::uint64_t>(root.x1_min),
                         std::bit_cast<std::uint64_t>(root.x1_max),
                         std::bit_cast<std::uint64_t>(root.x2_min),
                         std::bit_cast<std::uint64_t>(root.x2_max),
                         std::bit_cast<std::uint64_t>(root.x3_min),
                         std::bit_cast<std::uint64_t>(root.x3_max)};
            storage_identity = {storage ? 1ULL : 0ULL, storage ? storage->source_version : 0ULL};
            std::map<int, std::size_t> indices;
            for (std::size_t i = 0; i < active.size(); ++i)
                if (!indices.emplace(active[i], i).second)
                    throw std::invalid_argument("distributed exchange duplicate local storage");
            views.reserve(active.size());
            for (std::size_t i = 0; i < active.size(); ++i) {
                auto& b = pool->GetBlock(active[i]);
                b.RequireLogicalGeometryIdentity(root);
                coarse_fine_host::validate_state(b, slot, species);
                const AmrEndpoint endpoint{logical_key(b, dim), handles[i]};
                if (!is_valid(handles[i]) || handles[i].epoch != handles.front().epoch ||
                    !local.emplace(endpoint, static_cast<int>(i)).second)
                    throw std::invalid_argument(
                        "distributed exchange stale/duplicate local endpoint");
                encode(words, endpoint);
                for (int face = 0; face < 6; ++face) {
                    const auto& n = b.face_neighbors[face];
                    if (n.count < 0 || n.count > 4 || (face >= 2 * dim && n.count))
                        throw std::invalid_argument("distributed exchange invalid neighbor count");
                    if (n.count && n.level_diff == 0) {
                        if (n.count != 1)
                            throw std::invalid_argument(
                                "distributed exchange same-level cardinality");
                        const auto e = neighbor_endpoint(n.blocks[0], *pool, indices, handles, dim,
                                                         rank, size);
                        if (e.logical.level != b.level)
                            throw std::invalid_argument(
                                "distributed exchange neighbor level drifted");
                        encode(words, e);
                        words.push_back(n.blocks[0].owner_rank);
                    } else
                        words.insert(words.end(), 8, 0);
                }
                auto& state = b.*slot;
                views.push_back({endpoint.logical,
                                 endpoint.handle,
                                 {dim,
                                  {b.grid.Is(), b.grid.Js(), b.grid.Ks()},
                                  {b.grid.GetTotalX(), b.grid.GetTotalY(), b.grid.GetTotalZ()},
                                  {1, b.grid.stride_y, b.grid.stride_z},
                                  b.grid.GetTotalSize()},
                                 {state.rho.data(), state.mom_u.data(), state.mom_v.data(),
                                  state.mom_w.data(), state.eng.data(), state.enuc_rate.data()},
                                 species ? state.mass_fractions.data() : nullptr,
                                 species,
                                 b.grid.GetTotalSize()});
            }
            coarse_fine = BuildCoarseFinePlan(pool, tree, dim, handles);
            counts.resize(size);
        });
        CheckMpi(MPI_Allreduce(signature.data(), minimum.data(), signature.size(), MPI_UINT64_T,
                               MPI_MIN, MPI_COMM_WORLD));
        CheckMpi(MPI_Allreduce(signature.data(), maximum.data(), signature.size(), MPI_UINT64_T,
                               MPI_MAX, MPI_COMM_WORLD));
        if (minimum != maximum)
            throw std::invalid_argument("distributed exchange rank identity/layout/slot mismatch");
        CheckMpi(MPI_Allreduce(storage_identity.data(), storage_min.data(), 2, MPI_UINT64_T,
                               MPI_MIN, MPI_COMM_WORLD));
        CheckMpi(MPI_Allreduce(storage_identity.data(), storage_max.data(), 2, MPI_UINT64_T,
                               MPI_MAX, MPI_COMM_WORLD));
        if (storage_min != storage_max)
            throw std::invalid_argument("distributed exchange storage/version mismatch");
        const int count = static_cast<int>(words.size());
        CheckMpi(MPI_Allgather(&count, 1, MPI_INT, counts.data(), 1, MPI_INT, MPI_COMM_WORLD));
        PrepareMpi([&] {
            for (int n : counts)
                if (n <= 0 || n % words_per_block)
                    throw std::invalid_argument("distributed exchange truncated topology");
            global.resize(MpiOffsets(counts, offsets));
        });
        CheckMpi(MPI_Allgatherv(words.data(), count, MPI_UINT64_T, global.data(), counts.data(),
                                offsets.data(), MPI_UINT64_T, MPI_COMM_WORLD));
        int has_coarse_fine = !coarse_fine.operations.empty(), any_coarse_fine = 0;
        CheckMpi(
            MPI_Allreduce(&has_coarse_fine, &any_coarse_fine, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD));
        PrepareMpi([&] {
            if (storage && any_coarse_fine && !storage->gather_coarse_fine)
                throw std::invalid_argument("distributed storage lacks coarse-fine gather");
            const auto decode = [&](std::size_t at) {
                const auto* w = global.data() + at;
                if (w[0] != static_cast<std::uint64_t>(dim) || w[1] > 31 || w[2] > UINT32_MAX ||
                    w[3] > UINT32_MAX || w[4] > UINT32_MAX || !w[5] ||
                    w[6] != handles.front().epoch.value)
                    throw std::invalid_argument("distributed exchange invalid endpoint encoding");
                return AmrEndpoint{{dim, static_cast<int>(w[1]), static_cast<std::uint32_t>(w[2]),
                                    static_cast<std::uint32_t>(w[3]),
                                    static_cast<std::uint32_t>(w[4])},
                                   {{w[5]}, {w[6]}}};
            };
            std::map<AmrEndpoint, int> owners;
            std::map<int, std::vector<SameLevelTopologyEntry>> levels;
            std::map<LogicalBlockKey, std::size_t> entry_indices;
            std::set<BlockHandle> identities;
            for (int peer = 0; peer < size; ++peer) {
                const auto end = static_cast<std::size_t>(offsets[peer]) + counts[peer];
                for (std::size_t at = offsets[peer]; at < end; at += words_per_block) {
                    const auto e = decode(at);
                    auto& entries = levels[e.logical.level];
                    if (!entry_indices.emplace(e.logical, entries.size()).second ||
                        !identities.insert(e.handle).second || !owners.emplace(e, peer).second)
                        throw std::invalid_argument(
                            "distributed exchange duplicate global endpoint");
                    entries.push_back({e.logical, e.handle, {}});
                }
            }
            for (int peer = 0; peer < size; ++peer) {
                const auto end = static_cast<std::size_t>(offsets[peer]) + counts[peer];
                for (std::size_t at = offsets[peer]; at < end; at += words_per_block) {
                    const auto e = decode(at);
                    auto& entry = levels.at(e.logical.level).at(entry_indices.at(e.logical));
                    for (int face = 0; face < 6; ++face) {
                        const auto n = at + 7 + face * 8;
                        if (!global[n + 5])
                            continue;
                        const auto neighbor = decode(n);
                        if (face >= 2 * dim || neighbor.logical.level != e.logical.level ||
                            global[n + 7] >= static_cast<std::uint64_t>(size) ||
                            owners.at(neighbor) != static_cast<int>(global[n + 7]))
                            throw std::invalid_argument(
                                "distributed exchange neighbor owner mismatch");
                        entry.neighbors[face] = neighbor.logical;
                    }
                }
            }
            for (auto& sweep : sweeps) {
                sweep.send_counts.assign(size, 0);
                sweep.receive_counts.assign(size, 0);
            }
            const auto append = [&](const SameLevelExchangePlan& plan, int first_sweep) {
                for (const auto& operation : plan.operations) {
                    const AmrEndpoint source{operation.source.logical, operation.source.handle};
                    const AmrEndpoint destination{operation.destination.logical,
                                                  operation.destination.handle};
                    const int from = owners.at(source), to = owners.at(destination);
                    if (from != rank && to != rank)
                        continue;
                    auto& sweep = sweeps[first_sweep + static_cast<int>(operation.phase)];
                    const auto cells =
                        exchange_detail::checked_product(operation.source_box.extent);
                    if (cells > static_cast<std::uint64_t>(INT_MAX / (6 + species)))
                        throw std::overflow_error("distributed exchange face exceeds INT_MAX");
                    const int values = static_cast<int>(cells) * (6 + species);
                    Transfer transfer;
                    transfer.from = from;
                    transfer.to = to;
                    transfer.source_box = operation.source_box;
                    transfer.destination_box = operation.destination_box;
                    const auto bind = [&](const AmrEndpoint& endpoint,
                                          const LogicalExchangeBox& box) {
                        const int index = local.at(endpoint);
                        exchange_detail::validate_host_view(plan, views[index]);
                        exchange_detail::for_each_box_cell(box, [&](const auto& cell,
                                                                    std::uint64_t) {
                            (void)exchange_detail::flatten_host_exchange(views[index].layout, cell);
                        });
                        return index;
                    };
                    if (from == rank) {
                        transfer.source = bind(source, operation.source_box);
                        transfer.send = sweep.send_counts[to];
                        if (values > INT_MAX - sweep.send_counts[to])
                            throw std::overflow_error(
                                "distributed exchange peer send exceeds INT_MAX");
                        sweep.send_counts[to] += values;
                    }
                    if (to == rank) {
                        transfer.destination = bind(destination, operation.destination_box);
                        transfer.receive = sweep.receive_counts[from];
                        if (values > INT_MAX - sweep.receive_counts[from])
                            throw std::overflow_error(
                                "distributed exchange peer receive exceeds INT_MAX");
                        sweep.receive_counts[from] += values;
                    }
                    sweep.transfers.push_back(transfer);
                }
            };
            for (const auto& [level, entries] : levels) {
                (void)level;
                const auto plan = make_same_level_exchange_plan(
                    entries, dim, {BLOCK_NX, dim >= 2 ? BLOCK_NY : 1, dim == 3 ? BLOCK_NZ : 1},
                    MAX_NG, handles.front().epoch);
                append(plan, 0);
                if (dim == 2 && any_coarse_fine)
                    append(make_same_level_corner_completion_plan(plan), 3);
            }
            for (auto& sweep : sweeps) {
                sweep.send.resize(MpiOffsets(sweep.send_counts, sweep.send_offsets));
                sweep.receive.resize(MpiOffsets(sweep.receive_counts, sweep.receive_offsets));
                for (auto& transfer : sweep.transfers) {
                    if (transfer.source >= 0)
                        transfer.send += sweep.send_offsets[transfer.to];
                    if (transfer.destination >= 0)
                        transfer.receive += sweep.receive_offsets[transfer.from];
                    if (storage && transfer.source >= 0)
                        sweep.sources.push_back({handles[transfer.source], transfer.source_box, transfer.send});
                    if (storage && transfer.destination >= 0)
                        sweep.destinations.push_back({handles[transfer.destination], transfer.destination_box, transfer.receive});
                }
            }
        });
        if (storage) PrepareMpi([&] { storage->begin(); });
        const auto execute = [&](Sweep& sweep) {
            if (storage) PrepareMpi([&] { storage->gather(sweep.sources, sweep.send); });
            // All allocation/index checks precede the first sweep; metadata and
            // borrowed storage remain fixed until the entire exchange returns.
            if (!storage) for (const auto& transfer : sweep.transfers)
                if (transfer.source >= 0) {
                    const auto& view = views[transfer.source];
                    exchange_detail::for_each_box_cell(
                        transfer.source_box, [&](const auto& cell, std::uint64_t n) {
                            const auto index =
                                exchange_detail::flatten_host_exchange(view.layout, cell);
                            for (int field = 0; field < 6 + species; ++field)
                                sweep.send[transfer.send + n * (6 + species) + field] =
                                    std::bit_cast<std::uint64_t>(
                                        exchange_detail::host_exchange_field(view, field)[index]);
                        });
                }
            CheckMpi(MPI_Alltoallv(sweep.send.data(), sweep.send_counts.data(),
                                   sweep.send_offsets.data(), MPI_UINT64_T, sweep.receive.data(),
                                   sweep.receive_counts.data(), sweep.receive_offsets.data(),
                                   MPI_UINT64_T, MPI_COMM_WORLD));
            if (storage) PrepareMpi([&] { storage->scatter(sweep.destinations, sweep.receive); });
            if (!storage) for (const auto& transfer : sweep.transfers)
                if (transfer.destination >= 0) {
                    const auto& view = views[transfer.destination];
                    exchange_detail::for_each_box_cell(
                        transfer.destination_box, [&](const auto& cell, std::uint64_t n) {
                            const auto index =
                                exchange_detail::flatten_host_exchange(view.layout, cell);
                            for (int field = 0; field < 6 + species; ++field)
                                exchange_detail::host_exchange_field(view, field)[index] =
                                    std::bit_cast<double>(sweep.receive[transfer.receive +
                                                                        n * (6 + species) + field]);
                        });
                }
        };
        for (int axis = 0; axis < dim; ++axis)
            execute(sweeps[axis]);
        // Empty-work ranks participate; local plan emptiness cannot select collectives.
        if (!storage || any_coarse_fine)
            ExecuteRemoteCoarseFinePlan(coarse_fine, pool, tree, dim, slot, handles, storage);
        if (dim == 2 && any_coarse_fine)
            for (int axis = 0; axis < dim; ++axis)
                execute(sweeps[3 + axis]);
#else
        (void)pool;
        (void)dim;
        (void)slot;
        (void)handles;
        (void)storage;
        throw std::logic_error("distributed exchange requires ARCH_ENABLE_MPI=ON");
#endif
    }

  private:
#if ARCH_MPI_ENABLED
    static void CheckMpi(int code) {
        if (code != MPI_SUCCESS) {
            MPI_Abort(MPI_COMM_WORLD, code);
            throw std::runtime_error("ghost MPI transport failed");
        }
    }
    static std::pair<int, int> RequireMpiWorld() {
        int initialized = 0;
        CheckMpi(MPI_Initialized(&initialized));
        if (!initialized)
            throw std::logic_error("ghost MPI is not initialized");
        int main_thread = 0, thread_level = 0, rank = 0, size = 0;
        CheckMpi(MPI_Is_thread_main(&main_thread));
        CheckMpi(MPI_Query_thread(&thread_level));
        if (!main_thread || thread_level < MPI_THREAD_FUNNELED) {
            MPI_Abort(MPI_COMM_WORLD, 1);
            throw std::logic_error("ghost MPI requires the initializing thread");
        }
        CheckMpi(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
        CheckMpi(MPI_Comm_size(MPI_COMM_WORLD, &size));
        return {rank, size};
    }
    template <class Action> static void PrepareMpi(Action&& action) {
        std::exception_ptr error;
        try {
            action();
        } catch (...) {
            error = std::current_exception();
        }
        int failed = error ? 1 : 0, any_failed = 0;
        CheckMpi(MPI_Allreduce(&failed, &any_failed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD));
        if (any_failed) {
            if (error)
                std::rethrow_exception(error);
            throw std::runtime_error("ghost MPI peer rejected preparation");
        }
    }
    static int MpiOffsets(const std::vector<int>& counts, std::vector<int>& offsets) {
        offsets.resize(counts.size());
        int total = 0;
        for (std::size_t i = 0; i < counts.size(); ++i) {
            if (counts[i] < 0 || counts[i] > INT_MAX - total)
                throw std::overflow_error("ghost MPI count exceeds INT_MAX");
            offsets[i] = total;
            total += counts[i];
        }
        return total;
    }
#endif
    /** Collective CPU Cartesian face transfer; every rank enters on the MPI
     * initializing thread after Host workers join. Requests contain topology
     * only. The source owner runs the native gather and returns final ghost
     * values. Nothing is scattered until every rank has validated its reply.
     */
    void ExecuteRemoteCoarseFinePlan(const CoarseFineTransferPlan& plan,
                                     const std::shared_ptr<MemoryPool>& pool,
                                     const std::shared_ptr<AmrTree>& tree, int dim,
                                     FluidState Block::* slot,
                                     std::span<const BlockHandle> handles,
                                      const DistributedStorage* storage = nullptr) {
#if ARCH_MPI_ENABLED
        const auto [rank, size] = RequireMpiWorld();
        const auto check = CheckMpi;
        const auto phase = [](auto&& action) {
            PrepareMpi(std::forward<decltype(action)>(action));
        };
        struct Query {
            AmrEndpoint source, destination;
            int face;
        };
      constexpr int query_words = 15; // Two explicit 7-word endpoints and a face.
      int species = 0;
      std::array<std::uint64_t, 18> signature{}, minimum{}, maximum{};
      std::map<AmrEndpoint, int> local, owners;
      CoarseFineCellPlan cells, local_cells, reply_cells;
      std::vector<std::size_t> local_indices;
      std::array<std::uint64_t, 2> storage_identity{}, storage_min{}, storage_max{};
      std::vector<std::pair<int, int>> destinations;
      std::vector<coarse_fine_host::Values> gathered;
      std::vector<bool> ready;
      std::vector<std::vector<Query>> queries;
      std::vector<std::vector<std::size_t>> reply_indices;
      std::vector<int> send_counts, recv_counts, send_offsets, recv_offsets;
      std::vector<std::uint64_t> requests, incoming;
      const auto face_cells = [&](const Query& query) {
          CoarseFineTransferPlan face{};
          face.dimension = dim;
          face.scope = plan.scope;
          append_coarse_fine_face(face, query.source, query.destination, query.face, species);
          finalize_amr_plan(face);
          return compile_coarse_fine_cell_plan(face, species);
      };
      const auto offsets = MpiOffsets;
      const auto encode = [](std::vector<std::uint64_t>& words, const AmrEndpoint& endpoint) {
          const auto& k = endpoint.logical;
          words.insert(words.end(),
                       {static_cast<std::uint64_t>(k.dimension),
                        static_cast<std::uint64_t>(k.level), k.logical_x1, k.logical_x2,
                        k.logical_x3, endpoint.handle.uid.value, endpoint.handle.epoch.value});
      };
      phase([&] {
          if (!pool || !tree || dim < 1 || dim > 3 || plan.dimension != dim ||
              tree->PartitionRank() != rank || tree->PartitionSize() != size)
              throw std::invalid_argument("coarse-fine MPI topology/communicator mismatch");
          const int state_slot = slot == &Block::fluid_state     ? 0
                                 : slot == &Block::state_next    ? 1
                                 : slot == &Block::state_scratch ? 2
                                                                 : -1;
          if (state_slot < 0)
              throw std::invalid_argument("coarse-fine MPI state slot is invalid");
          const auto& active = tree->GetActiveBlocks();
          if (active.empty() || handles.size() != active.size())
              throw std::invalid_argument(
                  "coarse-fine MPI requires nonempty committed local storage");
          species = (pool->GetBlock(active.front()).*slot).GetNumSpecies();
          if (species < 0 || species > std::numeric_limits<int>::max() - 6)
              throw std::invalid_argument("coarse-fine MPI species extent is invalid");
          const auto& root = tree->GetRootGrid();
          if (root.geometry != "cartesian")
              throw std::invalid_argument("coarse-fine MPI requires Cartesian geometry");
          if (storage && (!storage->source_version || !storage->gather_coarse_fine || !storage->scatter))
              throw std::invalid_argument("coarse-fine MPI storage contract is incomplete");
          storage_identity = {storage ? 1ULL : 0ULL, storage ? storage->source_version : 0ULL};
          signature = {4,
                       static_cast<std::uint64_t>(dim),
                       static_cast<std::uint64_t>(species),
                       static_cast<std::uint64_t>(state_slot),
                       plan.scope.from_epoch.value,
                       BLOCK_NX,
                       BLOCK_NY,
                       BLOCK_NZ,
                       MAX_NG,
                       static_cast<std::uint64_t>(root.nblockx1),
                       static_cast<std::uint64_t>(root.nblockx2),
                       static_cast<std::uint64_t>(root.nblockx3),
                       std::bit_cast<std::uint64_t>(root.x1_min),
                       std::bit_cast<std::uint64_t>(root.x1_max),
                       std::bit_cast<std::uint64_t>(root.x2_min),
                       std::bit_cast<std::uint64_t>(root.x2_max),
                       std::bit_cast<std::uint64_t>(root.x3_min),
                       std::bit_cast<std::uint64_t>(root.x3_max)};
          const auto owner = [&](const AmrEndpoint& endpoint, int value) {
              if (value < 0 || value >= size || !is_valid(endpoint.handle) ||
                  endpoint.handle.epoch != plan.scope.from_epoch)
                  throw std::invalid_argument("coarse-fine MPI owner/epoch drifted");
              const auto [it, inserted] = owners.emplace(endpoint, value);
              if (!inserted && it->second != value)
                  throw std::invalid_argument("coarse-fine MPI conflicting owners");
          };
          std::set<BlockHandle> unique_handles;
          std::set<LogicalBlockKey> unique_logical;
          for (std::size_t i = 0; i < active.size(); ++i) {
              const auto& block = pool->GetBlock(active[i]);
              block.RequireLogicalGeometryIdentity(root);
              coarse_fine_host::validate_state(block, slot, species);
              const AmrEndpoint endpoint{logical_key(block, dim), handles[i]};
              owner(endpoint, rank);
              if (!unique_logical.insert(endpoint.logical).second ||
                  !unique_handles.insert(handles[i]).second ||
                  !local.emplace(endpoint, active[i]).second)
                  throw std::invalid_argument("coarse-fine MPI duplicate local endpoint");
              for (const auto& face : block.face_neighbors) {
                  if (face.count < 0 || face.count > 4)
                      throw std::invalid_argument("coarse-fine MPI invalid neighbor count");
                  for (int n = 0; n < face.count; ++n)
                      owner({face.blocks[n].logical, face.blocks[n].handle},
                            face.blocks[n].owner_rank);
              }
          }
          const auto expected = BuildCoarseFinePlan(pool, tree, dim, handles);
          validate_amr_plan(plan);
          if (plan.scope != expected.scope || plan.operations != expected.operations)
              throw std::invalid_argument("coarse-fine MPI plan differs from current topology");
          cells = compile_coarse_fine_cell_plan(plan, species);
          local_cells.dimension = reply_cells.dimension = dim;
          local_cells.species_count = reply_cells.species_count = species;
          local_cells.scope = reply_cells.scope = cells.scope;
          destinations.reserve(cells.transfers.size());
          gathered.resize(cells.transfers.size());
          ready.assign(cells.transfers.size(), false);
          std::map<std::pair<AmrEndpoint, LogicalAmrCell>, std::size_t> cell_indices;
          for (std::size_t i = 0; i < cells.transfers.size(); ++i) {
              const auto& transfer = cells.transfers[i];
              const int destination = local.at(transfer.destination);
              destinations.emplace_back(
                  destination, coarse_fine_host::cell_index(pool->GetBlock(destination).grid,
                                                            transfer.destination_cell));
              cell_indices.emplace(std::pair{transfer.destination, transfer.destination_cell}, i);
              if (const auto found = local.find(transfer.source); found != local.end()) {
                  if (storage) {
                      local_cells.transfers.push_back(transfer);
                      local_indices.push_back(i);
                  } else {
                      gathered[i] = coarse_fine_host::gather(transfer, pool->GetBlock(found->second),
                                                             slot, dim, species);
                      ready[i] = true;
                  }
              }
          }
          queries.resize(size);
          reply_indices.resize(size);
          send_counts.assign(size, 0);
          recv_counts.resize(size);
          for (const auto& operation : plan.operations) {
              if (operation.field != AmrField::Rho || local.contains(operation.source))
                  continue;
              const int peer = owners.at(operation.source);
              if (peer == rank)
                  throw std::invalid_argument("coarse-fine MPI source lacks local storage");
              Query query{operation.source, operation.destination,
                          2 * static_cast<int>(operation.axis) + static_cast<int>(operation.side)};
              queries[peer].push_back(query);
              for (const auto& transfer : face_cells(query).transfers) {
                  const auto i = cell_indices.at({transfer.destination, transfer.destination_cell});
                  if (cells.transfers[i].source != transfer.source)
                      throw std::invalid_argument("coarse-fine MPI reply mapping drifted");
                  reply_indices[peer].push_back(i);
              }
          }
          for (int peer = 0; peer < size; ++peer) {
              if (queries[peer].size() >
                  static_cast<std::size_t>(std::numeric_limits<int>::max() / query_words))
                  throw std::overflow_error("coarse-fine MPI request count exceeds INT_MAX");
              send_counts[peer] = static_cast<int>(queries[peer].size()) * query_words;
          }
          requests.reserve(offsets(send_counts, send_offsets));
          for (const auto& peer_queries : queries)
              for (const auto& query : peer_queries) {
                  encode(requests, query.source);
                  encode(requests, query.destination);
                  requests.push_back(query.face);
              }
      });
      check(MPI_Allreduce(signature.data(), minimum.data(), signature.size(), MPI_UINT64_T, MPI_MIN,
                          MPI_COMM_WORLD));
      check(MPI_Allreduce(signature.data(), maximum.data(), signature.size(), MPI_UINT64_T, MPI_MAX,
                          MPI_COMM_WORLD));
      if (minimum != maximum)
          throw std::invalid_argument("coarse-fine MPI rank identity/layout/slot mismatch");
      check(MPI_Allreduce(storage_identity.data(), storage_min.data(), 2, MPI_UINT64_T,
                          MPI_MIN, MPI_COMM_WORLD));
      check(MPI_Allreduce(storage_identity.data(), storage_max.data(), 2, MPI_UINT64_T,
                          MPI_MAX, MPI_COMM_WORLD));
      if (storage_min != storage_max)
          throw std::invalid_argument("coarse-fine MPI storage/version mismatch");
      check(MPI_Alltoall(send_counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT,
                         MPI_COMM_WORLD));
      phase([&] {
          for (int count : recv_counts)
              if (count % query_words)
                  throw std::invalid_argument("coarse-fine MPI truncated face request");
          incoming.resize(offsets(recv_counts, recv_offsets));
      });
      check(MPI_Alltoallv(requests.data(), send_counts.data(), send_offsets.data(), MPI_UINT64_T,
                          incoming.data(), recv_counts.data(), recv_offsets.data(), MPI_UINT64_T,
                          MPI_COMM_WORLD));
      std::vector<double> replies, received;
      std::vector<int> reply_send_counts, reply_recv_counts, reply_send_offsets, reply_recv_offsets;
      phase([&] {
          reply_send_counts.assign(size, 0);
          reply_recv_counts.resize(size);
          const auto decode = [&](std::size_t first) {
              const auto* w = incoming.data() + first;
              if (w[0] > 3 || w[1] > 31 || w[2] > UINT32_MAX || w[3] > UINT32_MAX ||
                  w[4] > UINT32_MAX)
                  throw std::invalid_argument("coarse-fine MPI invalid endpoint encoding");
              return AmrEndpoint{
                  {static_cast<int>(w[0]), static_cast<int>(w[1]), static_cast<std::uint32_t>(w[2]),
                   static_cast<std::uint32_t>(w[3]), static_cast<std::uint32_t>(w[4])},
                  {{w[5]}, {w[6]}}};
          };
          std::set<std::tuple<AmrEndpoint, AmrEndpoint, int>> unique;
          for (int peer = 0; peer < size; ++peer) {
              const auto start = replies.size();
              const std::size_t end =
                  static_cast<std::size_t>(recv_offsets[peer]) + recv_counts[peer];
              for (std::size_t first = recv_offsets[peer]; first < end; first += query_words) {
                  if (incoming[first + 14] >= static_cast<std::uint64_t>(2 * dim))
                      throw std::invalid_argument("coarse-fine MPI invalid face");
                  Query query{decode(first), decode(first + 7),
                              static_cast<int>(incoming[first + 14])};
                  if (!unique.emplace(query.source, query.destination, query.face).second ||
                      owners.at(query.source) != rank || owners.at(query.destination) != peer)
                      throw std::invalid_argument("coarse-fine MPI duplicate or misrouted request");
                  const auto& source = pool->GetBlock(local.at(query.source));
                  const auto& opposite = source.face_neighbors[query.face ^ 1];
                  bool reciprocal = false;
                  for (int n = 0; n < opposite.count; ++n) {
                      const auto& neighbor = opposite.blocks[n];
                      reciprocal |=
                          AmrEndpoint{neighbor.logical, neighbor.handle} == query.destination;
                  }
                  if (!reciprocal)
                      throw std::invalid_argument("coarse-fine MPI request is not a neighbor");
                  for (const auto& transfer : face_cells(query).transfers) {
                      if (replies.size() >
                          static_cast<std::size_t>(std::numeric_limits<int>::max() - (6 + species)))
                          throw std::overflow_error("coarse-fine MPI reply exceeds INT_MAX");
                      if (storage) {
                          reply_cells.transfers.push_back(transfer);
                          replies.resize(replies.size() + 6 + species);
                      } else {
                          const auto values =
                              coarse_fine_host::gather(transfer, source, slot, dim, species);
                          replies.insert(replies.end(), values.fields.begin(), values.fields.end());
                          replies.insert(replies.end(), values.mass_fractions.begin(),
                                         values.mass_fractions.end());
                      }
                  }
              }
              reply_send_counts[peer] = static_cast<int>(replies.size() - start);
          }
          (void)offsets(reply_send_counts, reply_send_offsets);
          if (storage) {
              if (local_indices.size() > static_cast<std::size_t>(INT_MAX / (6 + species)))
                  throw std::overflow_error("coarse-fine local packet exceeds INT_MAX");
              std::vector<double> local_values(local_indices.size() * (6 + species));
              // Both gathers finish before any destination scatter. Even empty-work
              // ranks enter these same phases; only source owners evaluate stencils.
              storage->gather_coarse_fine(local_cells, local_values);
              for (std::size_t at = 0; at < local_indices.size(); ++at) {
                  const auto index = local_indices[at];
                  const auto first = local_values.begin() + at * (6 + species);
                  std::copy_n(first, 6, gathered[index].fields.begin());
                  gathered[index].mass_fractions.assign(first + 6, first + 6 + species);
                  ready[index] = true;
              }
              storage->gather_coarse_fine(reply_cells, replies);
          }
      });
      check(MPI_Alltoall(reply_send_counts.data(), 1, MPI_INT, reply_recv_counts.data(), 1, MPI_INT,
                         MPI_COMM_WORLD));
      phase([&] {
          for (int peer = 0; peer < size; ++peer) {
              if (reply_indices[peer].size() >
                      static_cast<std::size_t>(std::numeric_limits<int>::max() / (6 + species)) ||
                  reply_recv_counts[peer] !=
                      static_cast<int>(reply_indices[peer].size()) * (6 + species))
                  throw std::invalid_argument("coarse-fine MPI reply extent differs from plan");
          }
          received.resize(offsets(reply_recv_counts, reply_recv_offsets));
      });
      check(MPI_Alltoallv(replies.data(), reply_send_counts.data(), reply_send_offsets.data(),
                          MPI_DOUBLE, received.data(), reply_recv_counts.data(),
                          reply_recv_offsets.data(), MPI_DOUBLE, MPI_COMM_WORLD));
      phase([&] {
          for (int peer = 0; peer < size; ++peer) {
              std::size_t offset = reply_recv_offsets[peer];
              for (const auto index : reply_indices[peer]) {
                  if (ready[index])
                      throw std::invalid_argument("coarse-fine MPI repeated destination");
                  auto& values = gathered[index];
                  std::copy_n(received.begin() + offset, 6, values.fields.begin());
                  offset += 6;
                  values.mass_fractions.assign(received.begin() + offset,
                                               received.begin() + offset + species);
                  offset += species;
                  ready[index] = true;
              }
          }
          if (std::find(ready.begin(), ready.end(), false) != ready.end())
              throw std::invalid_argument("coarse-fine MPI missing destination");
      });
      if (storage) phase([&] {
          if (destinations.size() > static_cast<std::size_t>(INT_MAX / (6 + species)))
              throw std::overflow_error("coarse-fine destination packet exceeds INT_MAX");
          std::vector<ExchangePacketSlice> slices;
          std::vector<std::uint64_t> packet;
          slices.reserve(destinations.size());
          packet.reserve(destinations.size() * (6 + species));
          for (std::size_t i = 0; i < destinations.size(); ++i) {
              const auto& transfer = cells.transfers[i];
              slices.push_back({transfer.destination.handle,
                  {transfer.destination_cell, {1, 1, 1}}, packet.size()});
              for (const double value : gathered[i].fields)
                  packet.push_back(std::bit_cast<std::uint64_t>(value));
              for (const double value : gathered[i].mass_fractions)
                  packet.push_back(std::bit_cast<std::uint64_t>(value));
          }
          storage->scatter(slices, packet);
      });
      else for (std::size_t i = 0; i < destinations.size(); ++i)
          coarse_fine_host::scatter(pool->GetBlock(destinations[i].first).*slot,
                                    destinations[i].second, gathered[i]);
#else
      (void)plan;
      (void)pool;
      (void)tree;
      (void)dim;
      (void)slot;
      (void)handles;
      (void)storage;
      throw std::logic_error("distributed coarse-fine exchange requires ARCH_ENABLE_MPI=ON");
#endif
  }

  static void append_coarse_fine_face(CoarseFineTransferPlan& plan, const AmrEndpoint& source,
                                      const AmrEndpoint& destination, int face, int species) {
      const int difference = source.logical.level - destination.logical.level;
      if (face < 0 || face >= 2 * plan.dimension || (difference != -1 && difference != 1))
          throw std::invalid_argument("coarse-fine face recipe has invalid topology");
      const auto boxes = difference == -1
                             ? coarse_injection_boxes(destination.logical, face, plan.dimension)
                             : fine_average_boxes(source.logical, face, plan.dimension);
      const auto rule = difference == -1 ? RefinementRule::CoarseGhostInjection
                                         : RefinementRule::FineGhostAverage;
      const auto append = [&](AmrField field, int component) {
          plan.operations.push_back({0, source, destination, boxes.first, boxes.second,
                                     static_cast<AmrAxis>(face / 2), static_cast<AmrSide>(face % 2),
                                     field, component, rule, 1.0, 1.0});
      };
      append(AmrField::Rho, -1);
      append(AmrField::MomU, -1);
      append(AmrField::MomV, -1);
      append(AmrField::MomW, -1);
      append(AmrField::Energy, -1);
      append(AmrField::EnucRate, -1);
      for (int component = 0; component < species; ++component)
          append(AmrField::Species, component);
  }

  static AmrEndpoint neighbor_endpoint(const BlockTopology& neighbor, const MemoryPool& pool,
                                       const std::map<int, std::size_t>& active_index,
                                       std::span<const BlockHandle> handles, int dim, int rank,
                                       int rank_count) {
      if (neighbor.logical.dimension != dim || neighbor.owner_rank < 0 ||
          neighbor.owner_rank >= rank_count)
          throw std::invalid_argument("invalid neighbor endpoint owner or dimension");
      if (neighbor.owner_rank != rank) {
          if (neighbor.pool_index != -1 || !is_valid(neighbor.handle) ||
              neighbor.handle.epoch != handles.front().epoch)
              throw std::invalid_argument("remote neighbor endpoint is stale or locally bound");
          return {neighbor.logical, neighbor.handle};
      }
      const auto local = active_index.find(neighbor.pool_index);
      if (local == active_index.end() ||
          logical_key(pool.GetBlock(neighbor.pool_index), dim) != neighbor.logical)
          throw std::invalid_argument("local neighbor endpoint binding drifted");
      const auto handle = handles[local->second];
      // Local tree construction precedes registry adoption; those descriptors
      // acquire handles from this already-validated execution binding.
      if (is_valid(neighbor.handle) && neighbor.handle != handle)
          throw std::invalid_argument("local neighbor endpoint handle is stale");
      return {neighbor.logical, handle};
  }

    mutable std::vector<std::uint64_t> cached_key_;
    mutable std::vector<std::uint64_t> cache_probe_;
    mutable std::unique_ptr<CachedPlans> cached_plans_;
    mutable std::size_t cache_builds_ = 0, cache_hits_ = 0;
    mutable std::vector<std::optional<HostCompiledSameLevelExchangePlan>> host_compiled_;
    std::size_t host_cache_builds_ = 0;
    HostExchangeWorkspace host_workspace_;

    void ExchangeRemoteRootFaces(const AmrTree& tree, const MemoryPool& pool,
                                 std::span<const HostExchangeBlockView> views, int species) {
#if ARCH_MPI_ENABLED
        const auto &grid = tree.GetRootGrid();
        const int dim = grid.dim;
        if (dim < 1 || dim > 3 || views.empty() || tree.RootBegin() >= tree.RootEnd())
            throw std::logic_error("MPI exchange root ownership mismatch");
        int initialized = 0, rank = 0, size = 1;
        MPI_Initialized(&initialized);
        if (!initialized)
            throw std::logic_error("MPI exchange before initialization");
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &size);
        if (rank != tree.PartitionRank() || size != tree.PartitionSize())
            throw std::logic_error("MPI communicator differs from root partition");
        const int expected_lower =
            rank > 0 ? rank - 1 : (tree.PeriodicX() ? size - 1 : MPI_PROC_NULL);
        const int expected_upper =
            rank + 1 < size ? rank + 1 : (tree.PeriodicX() ? 0 : MPI_PROC_NULL);
        const std::array<std::int32_t, 3> extent{
            BLOCK_NX, dim >= 2 ? BLOCK_NY : 1, dim == 3 ? BLOCK_NZ : 1};
        const auto rows = static_cast<std::uint64_t>(std::max(1, grid.nblockx2));
        const auto planes = static_cast<std::uint64_t>(std::max(1, grid.nblockx3));
        const auto faces = rows * planes;
        const auto face_values = static_cast<std::uint64_t>(6 + species)
            * MAX_NG * extent[1] * extent[2];
        if (faces > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) / face_values)
            throw std::overflow_error("MPI halo count exceeds the MPI int range");
        const auto count64 = faces * face_values;
        const int count = static_cast<int>(count64);
        if (views.size() != faces * static_cast<std::uint64_t>(tree.RootEnd() - tree.RootBegin()))
            throw std::logic_error("MPI exchange has incomplete root slabs");
        // Morton traversal differs across slabs. Pack by global (Z,Y) instead
        // of relying on front/back or local pool indices to identify a face.
        std::vector<std::array<const HostExchangeBlockView *, 2>> boundary(faces, {nullptr, nullptr});
        for (const auto &view : views) {
            const auto &key = view.logical;
            if (key.dimension != dim || key.level != 0 ||
                key.logical_x1 < static_cast<unsigned>(tree.RootBegin()) ||
                key.logical_x1 >= static_cast<unsigned>(tree.RootEnd()) ||
                key.logical_x2 >= rows || key.logical_x3 >= planes)
                throw std::logic_error("MPI exchange root ownership mismatch");
            auto &pair = boundary[key.logical_x3 * rows + key.logical_x2];
            for (int side = 0; side < 2; ++side) {
                const auto edge = static_cast<unsigned>(side ? tree.RootEnd() - 1 : tree.RootBegin());
                if (key.logical_x1 == edge) {
                    if (pair[side])
                        throw std::logic_error("MPI exchange has duplicate boundary roots");
                    pair[side] = &view;
                }
            }
        }
        for (const auto &pair : boundary)
            if (!pair[0] || !pair[1])
                throw std::logic_error("MPI exchange has missing boundary roots");
        // Derive peers from the same neighbor descriptors used by plan
        // construction. The current transport still requires fixed X slabs.
        std::array<int, 2> peers{MPI_PROC_NULL, MPI_PROC_NULL};
        for (const auto& pair : boundary) {
            for (int side = 0; side < 2; ++side) {
                const auto& key = pair[side]->logical;
                const int id =
                    tree.FindBlock(key.level, key.logical_x1, key.logical_x2, key.logical_x3);
                if (id < 0)
                    throw std::logic_error("MPI boundary view has no local block");
                const auto& face = pool.GetBlock(id).face_neighbors[side];
                const int expected = side ? expected_upper : expected_lower;
                if (expected == MPI_PROC_NULL) {
                    if (face.count != 0)
                        throw std::logic_error("physical MPI boundary has a neighbor");
                    continue;
                }
                if (face.count != 1 || face.level_diff != 0)
                    throw std::logic_error("MPI root face needs a complete same-level neighbor");
                const auto& peer = face.blocks[0];
                const auto x = side ? (key.logical_x1 + 1) % static_cast<unsigned>(grid.nblockx1)
                                    : (key.logical_x1 + grid.nblockx1 - 1) %
                                          static_cast<unsigned>(grid.nblockx1);
                if (peer.owner_rank != expected || peer.pool_index != -1 ||
                    !is_valid(peer.handle) || peer.handle.epoch != pair[side]->handle.epoch ||
                    peer.logical != LogicalBlockKey{dim, 0, x, key.logical_x2, key.logical_x3})
                    throw std::logic_error("MPI root neighbor ownership or identity drifted");
                peers[side] = peer.owner_rank;
            }
        }
        const int lower = peers[0], upper = peers[1];
        // Reuse the existing executor's scratch, retaining no FluidState views.
        host_workspace_.values.resize(4 * count64);
        double *send_lower = host_workspace_.values.data();
        double *send_upper = send_lower + count;
        double *receive_lower = send_upper + count;
        double *receive_upper = receive_lower + count;
        const auto copy = [&](const HostExchangeBlockView &view, bool upper_side, bool gather,
                              double *buffer) {
            // The source side is opposite the destination side in the native
            // plan recipe; use that same indexing and the same six+species fields.
            const auto box = exchange_detail::make_box(dim, extent, MAX_NG, 0, gather,
                                                       gather ? !upper_side : upper_side);
            exchange_detail::for_each_box_cell(box, [&](const auto &logical, std::uint64_t cell) {
                const auto index = exchange_detail::flatten_host_exchange(view.layout, logical);
                for (int field = 0; field < 6 + species; ++field) {
                    double &value = exchange_detail::host_exchange_field(view, field)[index];
                    double &packed = buffer[cell * (6 + species) + field];
                    if (gather)
                        packed = value;
                    else
                        value = packed;
                }
            });
        };
        for (std::size_t face = 0; face < boundary.size(); ++face) {
            copy(*boundary[face][0], false, true, send_lower + face * face_values);
            copy(*boundary[face][1], true, true, send_upper + face * face_values);
        }
        // Submit both directions into the existing scratch before waiting.
        // All receives finish before scattering or reusing any of these buffers.
        std::array<MPI_Request, 4> requests{};
        std::array<MPI_Status, 4> statuses{};
        MPI_Irecv(receive_lower, count, MPI_DOUBLE, lower, 310, MPI_COMM_WORLD, &requests[0]);
        MPI_Irecv(receive_upper, count, MPI_DOUBLE, upper, 311, MPI_COMM_WORLD, &requests[1]);
        MPI_Isend(send_upper, count, MPI_DOUBLE, upper, 310, MPI_COMM_WORLD, &requests[2]);
        MPI_Isend(send_lower, count, MPI_DOUBLE, lower, 311, MPI_COMM_WORLD, &requests[3]);
        MPI_Waitall(static_cast<int>(requests.size()), requests.data(), statuses.data());
        const auto require_count = [&](int peer, MPI_Status &status) {
            int received = 0;
            if (peer != MPI_PROC_NULL) {
                MPI_Get_count(&status, MPI_DOUBLE, &received);
                if (received != count)
                    throw std::runtime_error("MPI halo field count mismatch");
            }
        };
        require_count(lower, statuses[0]);
        require_count(upper, statuses[1]);
        for (std::size_t face = 0; face < boundary.size(); ++face) {
            if (lower != MPI_PROC_NULL)
                copy(*boundary[face][0], false, false, receive_lower + face * face_values);
            if (upper != MPI_PROC_NULL)
                copy(*boundary[face][1], true, false, receive_upper + face * face_values);
        }
        // Remote X and local X write disjoint faces. The existing local Y/Z
        // phases then propagate these X halos into edges/corners in order.
#else
        (void)tree;
        (void)pool;
        (void)views;
        (void)species;
        throw std::logic_error("distributed exchange requires ARCH_ENABLE_MPI=ON");
#endif
    }

    static LogicalBlockKey logical_key(const Block& block, int dim)
    {
        return {dim, block.level, block.logical_x1,
                block.logical_x2, block.logical_x3};
    }

    static std::array<int, 3> active_extents(int dim)
    {
        return {BLOCK_NX, dim >= 2 ? BLOCK_NY : 1,
                dim == 3 ? BLOCK_NZ : 1};
    }

    static std::pair<LogicalAmrBox, LogicalAmrBox>
    coarse_injection_boxes(const LogicalBlockKey& fine, int face, int dim) {
        const int normal = face / 2;
        const bool upper = (face & 1) != 0;
        const auto active = active_extents(dim);
        LogicalAmrBox source{};
        LogicalAmrBox destination{};
        for (int axis = 0; axis < 3; ++axis) {
            if (axis >= dim) {
                source.first[axis] = destination.first[axis] = 0;
                source.extent[axis] = destination.extent[axis] = 1;
            } else if (axis == normal) {
                const int coarse_cells = (MAX_NG + 1) / 2;
                source.first[axis] = upper
                    ? 0 : active[axis] - coarse_cells;
                source.extent[axis] = coarse_cells;
                destination.first[axis] = upper ? active[axis] : -MAX_NG;
                destination.extent[axis] = MAX_NG;
            } else {
                const std::uint32_t logical = axis == 0
                    ? fine.logical_x1
                    : axis == 1 ? fine.logical_x2 : fine.logical_x3;
                source.first[axis] = static_cast<std::int32_t>(
                    (logical & 1U) * static_cast<std::uint32_t>(
                        active[axis] / 2));
                source.extent[axis] = active[axis] / 2;
                destination.first[axis] = 0;
                destination.extent[axis] = active[axis];
            }
        }
        return {source, destination};
    }

    static std::pair<LogicalAmrBox, LogicalAmrBox> fine_average_boxes(const LogicalBlockKey& fine,
                                                                      int face, int dim) {
        const int normal = face / 2;
        const bool upper = (face & 1) != 0;
        const auto active = active_extents(dim);
        LogicalAmrBox source{};
        LogicalAmrBox destination{};
        for (int axis = 0; axis < 3; ++axis) {
            if (axis >= dim) {
                source.first[axis] = destination.first[axis] = 0;
                source.extent[axis] = destination.extent[axis] = 1;
            } else if (axis == normal) {
                source.first[axis] = upper ? 0 : active[axis] - 2 * MAX_NG;
                source.extent[axis] = 2 * MAX_NG;
                destination.first[axis] = upper ? active[axis] : -MAX_NG;
                destination.extent[axis] = MAX_NG;
            } else {
                const std::uint32_t logical = axis == 0
                    ? fine.logical_x1
                    : axis == 1 ? fine.logical_x2 : fine.logical_x3;
                source.first[axis] = 0;
                source.extent[axis] = active[axis];
                destination.first[axis] = static_cast<std::int32_t>(
                    (logical & 1U) * static_cast<std::uint32_t>(
                        active[axis] / 2));
                destination.extent[axis] = active[axis] / 2;
            }
        }
        return {source, destination};
    }
};

} // namespace amr
