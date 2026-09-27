/**
 * @file InitialMesh.h
 * @brief Create a bounded initial mesh snapshot for preview without entering the time loop.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Create a bounded initial mesh snapshot for preview without entering the time loop.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#pragma once

#include <chrono>

#include "amr/refinement/RefinementThermodynamics.h"
#include "api/preview/ResourceEstimates.h"
#include "driver/DriverUtils.h"
#include "driver/initialization/InitialMesh.h"

namespace arch::api {
struct MeshResult { detail::Json data; bool complete; bool constructed; };
struct MeshBudgetStop : std::runtime_error { using std::runtime_error::runtime_error; };

template<class Eos>
/** Construct the requested bounded initial AMR mesh without entering Driver time stepping. */
MeshResult BuildInitialMesh(ProblemGenerator& problem, const SimConfig& config,
                           const SpeciesManager& species, dispatch::EosId eos_id,
                           const Eos& eos, const PreviewRequest& request) {
    using detail::Json;
    const auto start = std::chrono::steady_clock::now();
    const auto check_time = [&] {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(contract::mesh_seconds))
            throw MeshBudgetStop("mesh-time-budget");
    };
    const auto cells = PaddedCells(config.grid.dim);
    // Three states plus deliberately conservative headroom for transfer plans,
    // backup states, species, geometry, exchange and response. This working-set
    // estimate is not an RSS guarantee; the worker has a separate process limit.
    const std::int64_t bytes_per_slot = cells * (6LL + species.count()) * 8 * 3 * 4 + 65536;
    const int configured_capacity = config.grid.amr_max_blocks > 0 ? config.grid.amr_max_blocks : 10000;
    const auto memory_capacity = std::int64_t(request.mesh_memory_mib) * 1024 * 1024 / bytes_per_slot;
    const int capacity = static_cast<int>(std::min<std::int64_t>({configured_capacity, request.mesh_max_blocks, memory_capacity}));
    std::int64_t roots = std::int64_t(config.grid.nblockx1) * std::max(1, config.grid.nblockx2);
    auto result = Json::object({{"kind", "amr-leaf-mesh"}, {"version", "1"},
        {"dimension", config.grid.dim}, {"geometry", config.grid.geometry}, {"unit", "cm"},
        {"refinementRatio", 2}, {"balance", "face-neighbor-2:1"},
        {"configuredMaxLevel", config.amr.lrefinemax}, {"configuredMaxBlocks", configured_capacity},
        {"workingCapacity", capacity}, {"workingBudgetBytes", std::int64_t(request.mesh_memory_mib) * 1024 * 1024},
        {"budgetedBytesPerSlot", bytes_per_slot}, {"completedPasses", 0},
        {"complete", false}, {"limitedReason", Json()}, {"leaves", Json::array()},
        {"leafCount", 0}, {"levelCounts", Json::array()},
        {"resources", AmrResourceMetadata(config, species.count())},
        {"fieldOverlay", "separate uniform Init samples; not AMR cell averages"}});
    if (roots > capacity) {
        result["limitedReason"] = "root-grid-exceeds-working-capacity";
        result["snapshot"] = "none";
        return {std::move(result), false, false};
    }
    amr::AMRControl control(capacity, config.grid.dim);
    control.tree->ConfigureRefinementSpecies(config.amr, species);
    driver::InitializeRootState(control, problem, config, species, {eos_id});
    amr::BindRefinementThermodynamics(*control.tree, eos);
    BCHandler boundaries(config);
    bool complete = true;
    int passes = 0;
    std::uint64_t epoch = 1;
    try {
        for (int pass = 0; pass < config.amr.lrefinemax; ++pass) {
            check_time();
            std::vector<amr::BlockHandle> handles;
            for (int id : control.tree->GetActiveBlocks()) {
                auto& block = control.pool->GetBlock(id);
                boundaries.apply(block.fluid_state, block.grid);
                handles.push_back({{std::uint64_t(id)+1}, {epoch}});
            }
            control.ghost_exchange.ExecuteExchange(control.pool, control.tree, config.grid.dim,
                                                   &amr::Block::fluid_state, handles);
            const bool changed = control.tree->Regrid(config, [&](const amr::AmrTree& tree) {
                check_time();
                std::int64_t required = control.pool->GetNumActiveBlocks();
                for (int id : tree.GetActiveBlocks()) {
                    const auto& block = control.pool->GetBlock(id);
                    if (block.refine_flag == 1) required += 1 << config.grid.dim;
                    // Conservative upper bound: a coarsening parent needs a slot
                    // before the retired children can be released.
                    if (block.refine_flag == -1) ++required;
                }
                if (required > capacity) throw MeshBudgetStop("regrid-exceeds-working-capacity");
            });
            ++passes; ++epoch;
            if (!changed) break;
        }
    } catch (const MeshBudgetStop& stop) {
        complete = false;
        result["limitedReason"] = stop.what();
    }
    auto leaves = Json::array();
    auto counts = Json::array();
    std::vector<int> level_counts(config.amr.lrefinemax + 1, 0);
    for (int id : control.tree->GetActiveBlocks()) {
        const auto& block = control.pool->GetBlock(id);
        ++level_counts.at(block.level);
        const auto& g = block.grid;
        const std::string key = std::to_string(block.level) + ":" + std::to_string(block.logical_x1)
            + ":" + std::to_string(block.logical_x2) + ":" + std::to_string(block.logical_x3);
        auto lower = Json::array({g.x1_min}); auto upper = Json::array({g.x1_max});
        auto shape = Json::array({amr::BLOCK_NX}); auto spacing = Json::array({(g.x1_max-g.x1_min)/amr::BLOCK_NX});
        if (config.grid.dim == 2) {
            lower.push(g.x2_min); upper.push(g.x2_max); shape.push(amr::BLOCK_NY);
            spacing.push((g.x2_max-g.x2_min)/amr::BLOCK_NY);
        }
        leaves.push(Json::object({{"logicalKey", key}, {"level", block.level},
            {"logicalIndex", Json::array({std::int64_t(block.logical_x1), std::int64_t(block.logical_x2), std::int64_t(block.logical_x3)})},
            {"lower", lower}, {"upper", upper}, {"cellShape", shape}, {"cellSpacing", spacing}}));
    }
    for (int level = 0; level <= config.amr.lrefinemax; ++level)
        counts.push(Json::object({{"level", level}, {"leafBlocks", level_counts[level]}}));
    result["leaves"] = leaves; result["levelCounts"] = counts;
    result["leafCount"] = std::int64_t(control.tree->GetActiveBlocks().size());
    result["complete"] = complete; result["completedPasses"] = passes;
    result["snapshot"] = "last-completed-balanced-hierarchy";
    return {std::move(result), complete, true};
}
} // namespace arch::api
