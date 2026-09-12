/**
 * @file GhostExchange.h
 * @brief Synchronizes same-level and coarse-fine AMR ghost cells.
 *
 * Host plan construction resolves neighbours and logical source/destination
 * cells. Execution binds those cells to block storage, using direct same-level
 * copies or the shared coarse-fine prolongation/restriction rules. Ghost
 * synchronization changes field data, not hierarchy ownership.
 */

#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <map>
#include <span>
#include <vector>

#include "AmrTree.h"
#include "AmrTransferPlans.h"
#include "Block.h"
#include "CoarseFineCellPlan.h"
#include "ConservativeRestriction.h"
#include "ExchangePlan.h"
#include "LimitedLinearProlongation.h"

#include "../data/GlobalDefs.h"

namespace amr {

class GhostExchange {
public:
    GhostExchange() = default;

    struct CachedPlans {
        std::vector<SameLevelExchangePlan> same_level;
        CoarseFineTransferPlan coarse_fine;
        std::vector<std::vector<std::size_t>> level_indices;
    };

    // One entry per exchange owner, not one entry per historical topology.
    // Logical plans contain no storage/view pointers. Slots, generations,
    // layouts and physical measures are deliberately rebound by each executor.
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
        std::vector<std::uint64_t> key;
        key.reserve(2 + active.size() * 44);
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
            for (int face = 0; face < 2 * dim; ++face) {
                const auto& neighbor = block.face_neighbors[face];
                if (neighbor.count < 0 || neighbor.count > 4)
                    throw std::invalid_argument("exchange cache has invalid neighbor count");
                key.push_back(static_cast<std::uint64_t>(neighbor.count));
                key.push_back(static_cast<std::uint64_t>(neighbor.level_diff));
                for (const auto id : neighbor.ids)
                    key.push_back(static_cast<std::uint64_t>(id));
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
        ++cache_builds_;
        return *cached_plans_;
    }

    std::size_t PlanCacheBuilds() const noexcept { return cache_builds_; }
    std::size_t PlanCacheHits() const noexcept { return cache_hits_; }

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
                    if (neighbors.count != 1 || neighbors.ids[0] < 0)
                        throw std::invalid_argument(
                            "same-level face has invalid neighbor cardinality");
                    const auto found = active_index.find(neighbors.ids[0]);
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
        const auto append_fields = [&](const Block& source,
                                       const Block& destination,
                                       std::size_t source_index,
                                       std::size_t destination_index,
                                       int face, RefinementRule rule,
                                       const LogicalAmrBox& source_box,
                                       const LogicalAmrBox& destination_box) {
            const AmrEndpoint source_endpoint{
                logical_key(source, dim), handles[source_index]};
            const AmrEndpoint destination_endpoint{
                logical_key(destination, dim), handles[destination_index]};
            const int species = destination.fluid_state.GetNumSpecies();
            if (source.fluid_state.GetNumSpecies() != species)
                throw std::invalid_argument(
                    "coarse-fine blocks have inconsistent species counts");
            const auto append = [&](AmrField field, int component) {
                plan.operations.push_back({
                    0, source_endpoint, destination_endpoint,
                    source_box, destination_box,
                    static_cast<AmrAxis>(face / 2),
                    static_cast<AmrSide>(face % 2), field, component,
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

        for (std::size_t destination_index = 0;
             destination_index < active_blocks.size(); ++destination_index) {
            const Block& destination = pool->GetBlock(
                active_blocks[destination_index]);
            for (int face = 0; face < 2 * dim; ++face) {
                const Block::FaceNeighbors& neighbors =
                    destination.face_neighbors[face];
                if (neighbors.count == 0 || neighbors.level_diff == 0)
                    continue;
                if (neighbors.level_diff == -1) {
                    if (neighbors.count != 1 || neighbors.ids[0] < 0)
                        throw std::invalid_argument(
                            "fine block has invalid coarse neighbor");
                    const auto found = active_index.find(neighbors.ids[0]);
                    if (found == active_index.end())
                        throw std::invalid_argument(
                            "coarse ghost source is not active");
                    const Block& source = pool->GetBlock(neighbors.ids[0]);
                    const auto boxes = coarse_injection_boxes(
                        destination, face, dim);
                    append_fields(
                        source, destination, found->second,
                        destination_index, face,
                        RefinementRule::CoarseGhostInjection,
                        boxes.first, boxes.second);
                } else if (neighbors.level_diff == 1) {
                    for (int neighbor = 0; neighbor < neighbors.count;
                         ++neighbor) {
                        if (neighbors.ids[neighbor] < 0)
                            throw std::invalid_argument(
                                "coarse block has invalid fine neighbor");
                        const auto found = active_index.find(
                            neighbors.ids[neighbor]);
                        if (found == active_index.end())
                            throw std::invalid_argument(
                                "fine ghost source is not active");
                        const Block& source = pool->GetBlock(
                            neighbors.ids[neighbor]);
                        const auto boxes = fine_average_boxes(
                            source, face, dim);
                        append_fields(
                            source, destination, found->second,
                            destination_index, face,
                            RefinementRule::FineGhostAverage,
                            boxes.first, boxes.second);
                    }
                } else {
                    throw std::invalid_argument(
                        "coarse-fine neighbor level difference is invalid");
                }
            }
        }
        finalize_amr_plan(plan);
        return plan;
    }

    void ExecuteCoarseFinePlan(
        const CoarseFineTransferPlan& plan,
        const std::shared_ptr<MemoryPool>& pool,
        const std::shared_ptr<AmrTree>& tree, int dim,
        FluidState Block::* state_ptr,
        std::span<const BlockHandle> handles)
    {
        const auto& active_blocks = tree->GetActiveBlocks();
        if (state_ptr == nullptr || plan.dimension != dim
            || handles.size() != active_blocks.size())
            throw std::invalid_argument(
                "coarse-fine Host execution view mismatch");

        struct CompiledTransfer {
            int source_id = -1;
            int destination_id = -1;
            int destination_cell = -1;
            std::array<int, 8> source_cells{};
            std::array<int, 6> slope_cells{};
            std::array<double, 3> fine_position{};
            std::array<double, 8> source_measures{};
            std::uint8_t source_count = 0;
            double source_measure_sum = 0.0;
            RefinementRule rule = RefinementRule::CoarseGhostInjection;
        };
        std::map<LogicalBlockKey, std::size_t> active_index;
        int species_count = -1;
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            const Block& block = pool->GetBlock(active_blocks[index]);
            if (!active_index.emplace(logical_key(block, dim), index).second)
                throw std::invalid_argument(
                    "coarse-fine Host view has duplicate logical block");
            if (!is_valid(handles[index])
                || handles[index].epoch != plan.scope.from_epoch)
                throw std::invalid_argument(
                    "coarse-fine Host view has stale handle");
            const int block_species = (block.*state_ptr).GetNumSpecies();
            if (species_count >= 0 && block_species != species_count)
                throw std::invalid_argument(
                    "coarse-fine Host species count mismatch");
            species_count = block_species;
        }
        if (species_count < 0)
            throw std::invalid_argument(
                "coarse-fine Host execution requires active blocks");

        const CoarseFineCellPlan cell_plan =
            compile_coarse_fine_cell_plan(plan, species_count);
        const auto cell_index = [](const Grid& grid,
                                   const LogicalAmrCell& cell) {
            const std::array<std::int64_t, 3> absolute{
                static_cast<std::int64_t>(grid.Is()) + cell[0],
                static_cast<std::int64_t>(grid.Js()) + cell[1],
                static_cast<std::int64_t>(grid.Ks()) + cell[2]};
            if (absolute[0] < 0 || absolute[0] >= grid.GetTotalX()
                || absolute[1] < 0 || absolute[1] >= grid.GetTotalY()
                || absolute[2] < 0 || absolute[2] >= grid.GetTotalZ())
                throw std::out_of_range(
                    "coarse-fine cell is outside Host block layout");
            return grid.GetIndex(
                static_cast<int>(absolute[0]),
                static_cast<int>(absolute[1]),
                static_cast<int>(absolute[2]));
        };

        std::vector<CompiledTransfer> compiled;
        compiled.reserve(cell_plan.transfers.size());
        for (const CoarseFineCellTransfer& transfer : cell_plan.transfers) {
            const auto source = active_index.find(transfer.source.logical);
            const auto destination = active_index.find(
                transfer.destination.logical);
            if (source == active_index.end()
                || destination == active_index.end()
                || handles[source->second] != transfer.source.handle
                || handles[destination->second]
                    != transfer.destination.handle)
                throw std::invalid_argument(
                    "coarse-fine logical endpoint is stale");
            const Block& source_block = pool->GetBlock(
                active_blocks[source->second]);
            const Block& destination_block = pool->GetBlock(
                active_blocks[destination->second]);
            if ((source_block.*state_ptr).GetNumSpecies() != species_count
                || (destination_block.*state_ptr).GetNumSpecies()
                    != species_count)
                throw std::invalid_argument(
                    "coarse-fine Host species count mismatch");
            CompiledTransfer lowered{};
            lowered.source_id = active_blocks[source->second];
            lowered.destination_id = active_blocks[destination->second];
            lowered.destination_cell = cell_index(
                destination_block.grid, transfer.destination_cell);
            lowered.source_count = transfer.source_count;
            lowered.fine_position = transfer.fine_position;
            lowered.rule = transfer.rule;
            if (lowered.source_count == 0
                || lowered.source_count > lowered.source_cells.size())
                throw std::invalid_argument(
                    "coarse-fine Host source count is invalid");
            for (std::size_t cell = 0; cell < lowered.source_count; ++cell) {
                lowered.source_cells[cell] = cell_index(
                    source_block.grid, transfer.source_cells[cell]);
                // Cartesian fine cells have one common measure, so unit
                // weights preserve the existing arithmetic.  Curvilinear
                // restriction must use physical cell volumes.
                double measure = 1.0;
                if (source_block.grid.geometry != "cartesian") {
                    const LogicalAmrCell& logical =
                        transfer.source_cells[cell];
                    measure = GridMetrics::CellVolume(
                        source_block.grid,
                        source_block.grid.Is() + logical[0],
                        source_block.grid.Js() + logical[1],
                        source_block.grid.Ks() + logical[2]);
                }
                if (!std::isfinite(measure) || measure <= 0.0)
                    throw std::invalid_argument(
                        "coarse-fine Host source measure is invalid");
                lowered.source_measures[cell] = measure;
                lowered.source_measure_sum += measure;
            }
            if (lowered.rule == RefinementRule::CoarseGhostInjection) {
                for (std::size_t cell = 0;
                     cell < lowered.slope_cells.size(); ++cell)
                    lowered.slope_cells[cell] = cell_index(
                        source_block.grid, transfer.slope_cells[cell]);
            }
            if (!std::isfinite(lowered.source_measure_sum)
                || lowered.source_measure_sum <= 0.0)
                throw std::invalid_argument(
                    "coarse-fine Host source measure sum is invalid");
            compiled.push_back(lowered);
        }

        struct GatheredTransfer {
            std::array<double, 6> fields{};
            std::vector<double> mass_fractions;
        };
        std::vector<GatheredTransfer> gathered;
        gathered.reserve(compiled.size());

        // Gather the complete plan before scattering.  Besides matching the
        // CUDA two-kernel execution, this prevents the fine-to-coarse route
        // from overwriting a coarse stencil needed by its reciprocal
        // coarse-to-fine route.
        for (const CompiledTransfer& transfer : compiled) {
            const FluidState& source =
                pool->GetBlock(transfer.source_id).*state_ptr;
            const prolongation_math::CompositionStencilView stencil{
                source.rho.data(), source.mass_fractions.data(),
                source.rho.size(), transfer.source_cells[0],
                transfer.slope_cells.data(), plan.dimension, species_count};
            const auto transfer_field = [&](const std::vector<double>& field) {
                if (transfer.rule == RefinementRule::CoarseGhostInjection)
                    return prolongation_math::reconstruct_field(
                        stencil, field.data(),
                        transfer.fine_position.data());
                double integral = 0.0;
                for (std::size_t cell = 0;
                     cell < transfer.source_count; ++cell)
                    integral += restriction_math::weighted_conserved_value(
                        field[transfer.source_cells[cell]],
                        transfer.source_measures[cell]);
                return restriction_math::restricted_average(
                    integral, transfer.source_measure_sum);
            };
            double density_integral = 0.0;
            for (std::size_t cell = 0;
                 cell < transfer.source_count; ++cell)
                density_integral +=
                    restriction_math::weighted_conserved_value(
                        source.rho[transfer.source_cells[cell]],
                        transfer.source_measures[cell]);
            GatheredTransfer values{};
            values.fields = {
                transfer_field(source.rho),
                transfer_field(source.mom_u),
                transfer_field(source.mom_v),
                transfer_field(source.mom_w),
                transfer_field(source.eng),
                transfer_field(source.enuc_rate)};
            values.mass_fractions.resize(
                static_cast<std::size_t>(species_count));
            if (transfer.rule == RefinementRule::CoarseGhostInjection) {
                const double fine_density = values.fields[0];
                const auto family =
                    prolongation_math::classify_composition_family(stencil);
                if (family == prolongation_math::CompositionFamily::InvalidDensity)
                    throw std::runtime_error(
                        prolongation_math::invalid_prolongation_density_message());
                for (int species = 0; species < species_count; ++species)
                    values.mass_fractions[species] =
                        prolongation_math::reconstruct_mass_fraction(
                            stencil, family, fine_density, species,
                            transfer.fine_position.data());
            } else {
                for (int species = 0; species < species_count; ++species) {
                    double species_density_integral = 0.0;
                    for (std::size_t cell = 0;
                         cell < transfer.source_count; ++cell) {
                        const int source_cell = transfer.source_cells[cell];
                        species_density_integral +=
                            restriction_math::weighted_species_density(
                                source.rho[source_cell],
                                source.X(species, source_cell),
                                transfer.source_measures[cell]);
                    }
                    values.mass_fractions[species] =
                        restriction_math::restricted_mass_fraction(
                            species_density_integral, density_integral);
                }
            }
            gathered.push_back(std::move(values));
        }

        for (std::size_t index = 0; index < compiled.size(); ++index) {
            const CompiledTransfer& transfer = compiled[index];
            const GatheredTransfer& values = gathered[index];
            FluidState& destination =
                pool->GetBlock(transfer.destination_id).*state_ptr;
            destination.rho[transfer.destination_cell] = values.fields[0];
            destination.mom_u[transfer.destination_cell] = values.fields[1];
            destination.mom_v[transfer.destination_cell] = values.fields[2];
            destination.mom_w[transfer.destination_cell] = values.fields[3];
            destination.eng[transfer.destination_cell] = values.fields[4];
            destination.enuc_rate[transfer.destination_cell] = values.fields[5];
            for (int species = 0; species < species_count; ++species)
                destination.X(species, transfer.destination_cell) =
                    values.mass_fractions[species];
        }
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
        for (std::size_t group = 0; group < plans.same_level.size(); ++group) {
            const auto& plan = plans.same_level[group];
            std::vector<HostExchangeBlockView> level_views;
            level_views.reserve(plan.blocks.size());
            for (const auto index : plans.level_indices[group])
                level_views.push_back(views[index]);
            const auto compiled = compile_host_exchange_plan(
                plan, level_views);
            execute_host_exchange_plan(compiled, level_views);
        }
        ExecuteCoarseFinePlan(
            plans.coarse_fine, pool, tree, dim, state_ptr, handles);
    }

private:
    mutable std::vector<std::uint64_t> cached_key_;
    mutable std::unique_ptr<CachedPlans> cached_plans_;
    mutable std::size_t cache_builds_ = 0, cache_hits_ = 0;

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
    coarse_injection_boxes(const Block& fine, int face, int dim)
    {
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

    static std::pair<LogicalAmrBox, LogicalAmrBox>
    fine_average_boxes(const Block& fine, int face, int dim)
    {
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
