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

#include "../data/GlobalDefs.h"

namespace amr {

class GhostExchange {
public:
    GhostExchange() = default;

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
            std::array<double, 8> source_measures{};
            std::uint8_t source_count = 0;
            double source_measure_sum = 0.0;
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
            if (!std::isfinite(lowered.source_measure_sum)
                || lowered.source_measure_sum <= 0.0)
                throw std::invalid_argument(
                    "coarse-fine Host source measure sum is invalid");
            compiled.push_back(lowered);
        }

        // No state changes occur until the complete logical plan, identities,
        // layouts, and cell addresses have validated.
        for (const CompiledTransfer& transfer : compiled) {
            FluidState& destination =
                pool->GetBlock(transfer.destination_id).*state_ptr;
            const FluidState& source =
                pool->GetBlock(transfer.source_id).*state_ptr;
            const auto restrict_field = [&](const std::vector<double>& field) {
                if (transfer.source_count == 1)
                    return field[transfer.source_cells[0]];
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
            destination.rho[transfer.destination_cell] =
                restrict_field(source.rho);
            destination.mom_u[transfer.destination_cell] =
                restrict_field(source.mom_u);
            destination.mom_v[transfer.destination_cell] =
                restrict_field(source.mom_v);
            destination.mom_w[transfer.destination_cell] =
                restrict_field(source.mom_w);
            destination.eng[transfer.destination_cell] =
                restrict_field(source.eng);
            destination.enuc_rate[transfer.destination_cell] =
                restrict_field(source.enuc_rate);
            for (int species = 0; species < species_count; ++species) {
                if (transfer.source_count == 1) {
                    destination.X(species, transfer.destination_cell) =
                        source.X(species, transfer.source_cells[0]);
                    continue;
                }
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
                destination.X(species, transfer.destination_cell) =
                    restriction_math::restricted_mass_fraction(
                        species_density_integral, density_integral);
            }
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
        const auto plans = BuildSameLevelPlans(pool, tree, dim, handles);
        std::map<LogicalBlockKey, std::size_t> view_by_logical;
        for (std::size_t index = 0; index < views.size(); ++index) {
            if (!view_by_logical.emplace(views[index].logical, index).second)
                throw std::invalid_argument(
                    "same-level Host views contain duplicate logical blocks");
        }
        for (const SameLevelExchangePlan& plan : plans) {
            std::vector<HostExchangeBlockView> level_views;
            level_views.reserve(plan.blocks.size());
            for (const ExchangeEndpoint& endpoint : plan.blocks) {
                const auto found = view_by_logical.find(endpoint.logical);
                if (found == view_by_logical.end())
                    throw std::invalid_argument(
                        "same-level plan endpoint has no Host view");
                level_views.push_back(views[found->second]);
            }
            const auto compiled = compile_host_exchange_plan(
                plan, level_views);
            execute_host_exchange_plan(compiled, level_views);
        }
        const auto coarse_fine = BuildCoarseFinePlan(
            pool, tree, dim, handles);
        ExecuteCoarseFinePlan(
            coarse_fine, pool, tree, dim, state_ptr, handles);
    }

private:
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
