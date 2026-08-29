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
#include "AmrTransferPlans.h"
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
        if (!handles.empty() && handles.size() != active_blocks.size())
            throw std::invalid_argument(
                "same-level exchange handle count mismatch");

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
                topology[local].handle = handles.empty()
                    ? BlockHandle{} : handles[global];
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
            const TopologyEpoch epoch = handles.empty()
                ? TopologyEpoch{} : topology.front().handle.epoch;
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
        validate_amr_plan(plan);
        const auto& active_blocks = tree->GetActiveBlocks();
        if (plan.dimension != dim || handles.size() != active_blocks.size())
            throw std::invalid_argument(
                "coarse-fine Host execution view mismatch");

        struct CompiledGroup {
            int source_id = -1;
            int destination_id = -1;
            int face = -1;
            RefinementRule rule = RefinementRule::CoarseGhostInjection;
        };
        std::map<LogicalBlockKey, std::size_t> active_index;
        for (std::size_t index = 0; index < active_blocks.size(); ++index) {
            const Block& block = pool->GetBlock(active_blocks[index]);
            if (!active_index.emplace(logical_key(block, dim), index).second)
                throw std::invalid_argument(
                    "coarse-fine Host view has duplicate logical block");
            if (handles[index].epoch != plan.scope.from_epoch)
                throw std::invalid_argument(
                    "coarse-fine Host view has stale handle");
        }

        std::vector<CompiledGroup> groups;
        for (std::size_t first = 0; first < plan.operations.size();) {
            const auto& head = plan.operations[first];
            const auto source = active_index.find(head.source.logical);
            const auto destination = active_index.find(
                head.destination.logical);
            if (source == active_index.end()
                || destination == active_index.end()
                || handles[source->second] != head.source.handle
                || handles[destination->second] != head.destination.handle)
                throw std::invalid_argument(
                    "coarse-fine logical endpoint is stale");
            const Block& source_block = pool->GetBlock(
                active_blocks[source->second]);
            const Block& destination_block = pool->GetBlock(
                active_blocks[destination->second]);
            const int species = destination_block.fluid_state.GetNumSpecies();
            if (source_block.fluid_state.GetNumSpecies() != species)
                throw std::invalid_argument(
                    "coarse-fine Host species count mismatch");
            const std::size_t count = static_cast<std::size_t>(6 + species);
            if (count > plan.operations.size() - first)
                throw std::invalid_argument(
                    "coarse-fine field group is incomplete");
            for (std::size_t offset = 0; offset < count; ++offset) {
                const auto& operation = plan.operations[first + offset];
                const AmrField expected_field = offset < 6
                    ? static_cast<AmrField>(offset)
                    : AmrField::Species;
                const int expected_component = offset < 6
                    ? -1 : static_cast<int>(offset - 6);
                if (operation.source != head.source
                    || operation.destination != head.destination
                    || operation.source_box != head.source_box
                    || operation.destination_box != head.destination_box
                    || operation.axis != head.axis
                    || operation.side != head.side
                    || operation.rule != head.rule
                    || operation.field != expected_field
                    || operation.component != expected_component)
                    throw std::invalid_argument(
                        "coarse-fine field group is noncanonical");
            }
            groups.push_back({
                active_blocks[source->second],
                active_blocks[destination->second],
                2 * axis_value(head.axis) + side_value(head.side),
                head.rule});
            first += count;
        }

        // Execute only after the complete lowering and field coverage validate.
        for (const CompiledGroup& group : groups) {
            Block& destination = pool->GetBlock(group.destination_id);
            const Block& source = pool->GetBlock(group.source_id);
            if (group.rule == RefinementRule::CoarseGhostInjection) {
                InterpolateFaceFromCoarse(
                    destination, source, group.face, dim, state_ptr);
            } else if (group.rule == RefinementRule::FineGhostAverage) {
                AverageFaceFromFine(
                    destination, source, group.face, dim, state_ptr);
            } else {
                throw std::logic_error(
                    "coarse-fine Host executor received an invalid rule");
            }
        }
    }

    void ExecuteExchange(
        std::shared_ptr<MemoryPool> pool, std::shared_ptr<AmrTree> tree,
        int dim, FluidState Block::* state_ptr,
        std::span<const BlockHandle> handles = {})
    {
        const auto& active_blocks = tree->GetActiveBlocks();
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
        if (handles.empty()) {
            // Bootstrap refinement precedes E1 identity adoption.  Task I0's
            // transactional initial-adoption step removes this temporary
            // uncommitted path before the final gate.
            UpdateGhostFromCoarse(pool, active_blocks, dim, state_ptr);
            UpdateGhostFromFine(pool, active_blocks, dim, state_ptr);
        } else {
            const auto coarse_fine = BuildCoarseFinePlan(
                pool, tree, dim, handles);
            ExecuteCoarseFinePlan(
                coarse_fine, pool, tree, dim, state_ptr, handles);
        }
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
                    f_state.enuc_rate[f_idx] = c_state.enuc_rate[c_idx];
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
                    double enuc_rate = 0;
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
                                enuc_rate += f_state.enuc_rate[f_idx];
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
                    c_state.enuc_rate[c_idx] = enuc_rate * inv;
                    for (int s = 0; s < n_sp; ++s) c_state.X(s, c_idx) = X[s] * inv;
                }
            }
        }
    }
};

} // namespace amr
