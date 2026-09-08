/** Host-only lowering of logical all-field migration plans into block groups. */
#pragma once

#include "AmrTransferPlans.h"
#include "AmrDefines.h"
#include <map>
#include <set>

namespace amr {

struct RegridProlongationGroup {
    AmrEndpoint source{}, destination{};
    int child_index = 0;
};
struct RegridRestrictionGroup {
    AmrEndpoint destination{};
    std::array<AmrEndpoint, 8> children{};
};
struct RegridExecutionPlan {
    std::vector<RegridProlongationGroup> prolongations;
    std::vector<RegridRestrictionGroup> restrictions;
};

inline RegridExecutionPlan compile_regrid_execution_plan(
    const ProlongationPlan& prolongation, const RestrictionPlan& restriction,
    int species_count)
{
    validate_amr_plan(prolongation);
    validate_amr_plan(restriction);
    if (species_count < 0 || prolongation.scope != restriction.scope
        || prolongation.dimension != restriction.dimension)
        throw std::invalid_argument("inconsistent regrid migration plans");
    const int dim = prolongation.dimension;
    const std::array<std::uint32_t, 3> active_extent{
        static_cast<std::uint32_t>(BLOCK_NX),
        static_cast<std::uint32_t>(dim >= 2 ? BLOCK_NY : 1),
        static_cast<std::uint32_t>(dim == 3 ? BLOCK_NZ : 1)};
    const auto child_index = [dim](const LogicalBlockKey& child) {
        return static_cast<int>((child.logical_x1 & 1U)
            | ((dim >= 2 ? child.logical_x2 & 1U : 0U) << 1U)
            | ((dim == 3 ? child.logical_x3 & 1U : 0U) << 2U));
    };
    const auto require_family = [dim](const AmrEndpoint& parent,
                                     const AmrEndpoint& child) {
        if (child.logical.level != parent.logical.level + 1
            || (child.logical.logical_x1 >> 1U) != parent.logical.logical_x1
            || (dim >= 2 && (child.logical.logical_x2 >> 1U) != parent.logical.logical_x2)
            || (dim == 3 && (child.logical.logical_x3 >> 1U) != parent.logical.logical_x3))
            throw std::invalid_argument("regrid endpoints are not a parent/child family");
    };
    using Pair = std::pair<AmrEndpoint, AmrEndpoint>;
    using Fields = std::set<std::pair<AmrField, int>>;
    const auto compile_fields = [&](const auto& plan) {
        std::map<Pair, Fields> groups;
        for (const auto& operation : plan.operations) {
            if (operation.weight != 1.0 || operation.sign != 1.0
                || operation.source_box.first != std::array<std::int32_t, 3>{}
                || operation.destination_box != operation.source_box
                || operation.source_box.extent != active_extent
                || (operation.field == AmrField::Species
                    && operation.component >= species_count))
                throw std::invalid_argument("regrid group is not an all-field interior transfer");
            if (!groups[{operation.source, operation.destination}]
                     .emplace(operation.field, operation.component).second)
                throw std::invalid_argument("duplicate regrid group field");
        }
        for (const auto& [endpoints, fields] : groups) {
            if (fields.size() != static_cast<std::size_t>(6) + species_count)
                throw std::invalid_argument("incomplete regrid all-field group");
        }
        return groups;
    };
    RegridExecutionPlan result;
    std::set<BlockHandle> destinations;
    for (const auto& [endpoints, fields] : compile_fields(prolongation)) {
        require_family(endpoints.first, endpoints.second);
        if (!destinations.insert(endpoints.second.handle).second)
            throw std::invalid_argument("multiple regrid prolongation sources");
        result.prolongations.push_back({endpoints.first, endpoints.second,
                                       child_index(endpoints.second.logical)});
    }
    std::map<AmrEndpoint, std::map<int, AmrEndpoint>> restrictions;
    for (const auto& [endpoints, fields] : compile_fields(restriction)) {
        require_family(endpoints.second, endpoints.first);
        auto& children = restrictions[endpoints.second];
        if (!children.emplace(child_index(endpoints.first.logical), endpoints.first).second)
            throw std::invalid_argument("duplicate regrid restriction child");
    }
    for (const auto& [destination, children] : restrictions) {
        if (children.size() != static_cast<std::size_t>(1U << dim)
            || !destinations.insert(destination.handle).second)
            throw std::invalid_argument("incomplete or overlapping restriction family");
        RegridRestrictionGroup group{};
        group.destination = destination;
        for (const auto& [index, child] : children) group.children[index] = child;
        result.restrictions.push_back(group);
    }
    return result;
}

} // namespace amr
