/**
 * @file AmrFluxExecutionPlan.h
 * @brief Backend-neutral lowering of canonical AMR flux plans.
 */

#pragma once

#include "AmrFluxPlan.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace amr {

struct AmrFluxGridLayout {
    int dimension = 0;
    int total_size = 0;
    int stride_y = 0;
    int stride_z = 0;
    int is = 0;
    int ie = 0;
    int js = 0;
    int je = 0;
    int ks = 0;
    int ke = 0;
};

inline AmrFluxGridLayout make_amr_flux_grid_layout(const Grid& grid) noexcept
{
    return {grid.dim, grid.GetTotalSize(), grid.stride_y, grid.stride_z,
            grid.Is(), grid.Ie(), grid.Js(), grid.Je(), grid.Ks(), grid.Ke()};
}

struct AmrFluxEndpointBinding {
    AmrEndpoint endpoint{};
    int runtime_index = -1;
    int species_count = 0;
    AmrFluxGridLayout grid{};
};

struct AmrFluxRegistrationTerm {
    int source_cell = -1;          // Full direction scratch.
    int source_surface_cell = -1;  // Compact initial-flux surface.
    double geometric_weight = 0.0;
};

struct AmrFluxRegistrationTarget {
    int destination_block = -1;
    int destination_face = -1;
    int destination_cell = -1;
    int source_face = -1;
    int first_term = 0;
    int term_count = 0;
    RefinementRule rule = RefinementRule::FineFluxContribution;
};

struct AmrCompiledFluxRegistrationRoute {
    int source_block = -1;
    int source_direction = -1;
    int species_count = 0;
    std::uint64_t logical_fingerprint = 0;
    std::vector<AmrFluxRegistrationTarget> targets;
    std::vector<AmrFluxRegistrationTerm> terms;
};

struct AmrCompiledFluxTopologyPlan {
    int dimension = 0;
    int species_count = 0;
    TopologyEpoch epoch{};
    std::uint64_t logical_fingerprint = 0;
    std::vector<AmrCompiledFluxRegistrationRoute> routes;
};

enum class AmrFluxSurfaceRole : std::uint8_t {
    Register = 1,
    InitialOperatorCache = 2,
};

struct AmrFluxSurfaceRequirement {
    int block = -1;
    int face = -1;
    int cell_count = 0;
    int species_count = 0;
    std::uint8_t roles = 0;
};

struct AmrRefluxContribution {
    int register_face = -1;
    int register_cell = -1;
    double geometric_weight = 0.0;
    double sign = 1.0;
};

struct AmrRefluxTarget {
    int block = -1;
    int state_cell = -1;
    int species_count = 0;
    int first_contribution = 0;
    int contribution_count = 0;
};

struct AmrCompiledRefluxPlan {
    int dimension = 0;
    TopologyEpoch epoch{};
    std::uint64_t logical_fingerprint = 0;
    std::vector<AmrRefluxTarget> targets;
    std::vector<AmrRefluxContribution> contributions;
};

namespace flux_execution_detail {

inline std::array<int, 3> active_extent(const AmrFluxGridLayout& grid)
{
    return {grid.ie - grid.is, grid.je - grid.js, grid.ke - grid.ks};
}

inline void validate_grid(const AmrFluxGridLayout& grid, int dimension)
{
    if (dimension < 1 || dimension > 3 || grid.dimension != dimension
        || grid.total_size <= 0 || grid.stride_y <= 0 || grid.stride_z <= 0
        || grid.is < 0 || grid.is >= grid.ie
        || grid.js < 0 || grid.js >= grid.je
        || grid.ks < 0 || grid.ks >= grid.ke)
        throw std::invalid_argument("invalid AMR flux grid layout");
    const auto extent = active_extent(grid);
    if (extent[0] != BLOCK_NX
        || extent[1] != (dimension >= 2 ? BLOCK_NY : 1)
        || extent[2] != (dimension == 3 ? BLOCK_NZ : 1))
        throw std::invalid_argument("AMR flux active extent drifted");
    const std::int64_t last = static_cast<std::int64_t>(grid.ke - 1)
            * grid.stride_z
        + static_cast<std::int64_t>(grid.je - 1) * grid.stride_y
        + grid.ie - 1;
    if (last < 0 || last >= grid.total_size)
        throw std::invalid_argument("AMR flux grid exceeds storage");
}

struct BindingIndex {
    std::map<AmrEndpoint, const AmrFluxEndpointBinding*> by_endpoint;
};

inline BindingIndex index_bindings(
    std::span<const AmrFluxEndpointBinding> bindings, int dimension,
    int expected_species)
{
    if (bindings.empty())
        throw std::invalid_argument("AMR flux endpoint set is empty");
    BindingIndex result;
    std::set<int> runtime_indices;
    for (const auto& binding : bindings) {
        validate_grid(binding.grid, dimension);
        if (!is_valid(binding.endpoint.handle) || binding.runtime_index < 0
            || binding.species_count != expected_species
            || !runtime_indices.insert(binding.runtime_index).second
            || !result.by_endpoint.emplace(
                binding.endpoint, &binding).second)
            throw std::invalid_argument(
                "invalid or duplicate AMR flux endpoint");
    }
    return result;
}

inline const AmrFluxEndpointBinding& require_binding(
    const BindingIndex& bindings, const AmrEndpoint& endpoint)
{
    const auto found = bindings.by_endpoint.find(endpoint);
    if (found == bindings.by_endpoint.end())
        throw std::invalid_argument(
            "AMR flux endpoint is not backend-resident");
    return *found->second;
}

inline void require_unit_box(const LogicalAmrBox& box)
{
    if (box.extent != std::array<std::uint32_t, 3>{1, 1, 1})
        throw std::invalid_argument("AMR flux operation is not one cell");
}

inline int checked_full_cell(const AmrFluxGridLayout& grid,
                             const LogicalAmrBox& box,
                             AmrAxis axis, bool normal_face)
{
    require_unit_box(box);
    const auto extent = active_extent(grid);
    const int normal = axis_value(axis);
    for (int value_axis = 0; value_axis < 3; ++value_axis) {
        const int value = box.first[static_cast<std::size_t>(value_axis)];
        const int upper = extent[static_cast<std::size_t>(value_axis)]
            + (normal_face && value_axis == normal ? 1 : 0);
        if (value < 0 || value >= upper)
            throw std::invalid_argument("AMR flux cell is outside layout");
    }
    const std::int64_t cell = grid.is + box.first[0]
        + static_cast<std::int64_t>(grid.js + box.first[1]) * grid.stride_y
        + static_cast<std::int64_t>(grid.ks + box.first[2]) * grid.stride_z;
    if (cell < 0 || cell >= grid.total_size)
        throw std::invalid_argument("AMR flux cell exceeds storage");
    return static_cast<int>(cell);
}

inline int checked_source_face(const AmrFluxGridLayout& grid,
                               const LogicalAmrBox& box, AmrAxis axis)
{
    require_unit_box(box);
    const auto extent = active_extent(grid);
    const int normal = axis_value(axis);
    const int coordinate = box.first[static_cast<std::size_t>(normal)];
    if (coordinate == 0) return 2 * normal;
    if (coordinate == extent[static_cast<std::size_t>(normal)])
        return 2 * normal + 1;
    throw std::invalid_argument("AMR flux source is not on a face");
}

inline int checked_surface_cell(const AmrFluxGridLayout& grid,
                                const LogicalAmrBox& box, AmrAxis axis)
{
    require_unit_box(box);
    const auto extent = active_extent(grid);
    for (int coordinate_axis = 0; coordinate_axis < 3; ++coordinate_axis) {
        if (coordinate_axis == axis_value(axis)) continue;
        const int coordinate =
            box.first[static_cast<std::size_t>(coordinate_axis)];
        if (coordinate < 0
            || coordinate >= extent[static_cast<std::size_t>(coordinate_axis)])
            throw std::invalid_argument(
                "AMR flux tangential cell is outside face");
    }
    return flux_plan_detail::source_face_cell_index(box, axis);
}

inline int checked_register_cell(const AmrFluxGridLayout& grid,
                                 const LogicalAmrBox& box,
                                 AmrAxis axis, AmrSide side)
{
    require_unit_box(box);
    const auto extent = active_extent(grid);
    const int normal = axis_value(axis);
    const int expected = side == AmrSide::Lower
        ? 0 : extent[static_cast<std::size_t>(normal)] - 1;
    if (box.first[static_cast<std::size_t>(normal)] != expected)
        throw std::invalid_argument(
            "AMR register destination is not on its face");
    const int cell = checked_surface_cell(grid, box, axis);
    if (cell < 0
        || cell >= flux_plan_detail::face_cell_count(grid.dimension, axis))
        throw std::invalid_argument("AMR register cell exceeds face storage");
    return cell;
}

struct RawKey {
    AmrEndpoint source{};
    AmrEndpoint destination{};
    LogicalAmrBox source_box{};
    LogicalAmrBox destination_box{};
    AmrAxis axis = AmrAxis::X;
    AmrSide side = AmrSide::Lower;
    RefinementRule rule = RefinementRule::FineFluxContribution;
    double weight = 0.0;
    double sign = 1.0;

    friend bool operator<(const RawKey& left, const RawKey& right) noexcept
    {
        return std::tuple{
            left.source, left.destination, left.source_box,
            left.destination_box, left.axis, left.side, left.rule,
            std::bit_cast<std::uint64_t>(left.weight),
            std::bit_cast<std::uint64_t>(left.sign)}
            < std::tuple{
                right.source, right.destination, right.source_box,
                right.destination_box, right.axis, right.side, right.rule,
                std::bit_cast<std::uint64_t>(right.weight),
                std::bit_cast<std::uint64_t>(right.sign)};
    }
};

struct RawGroup {
    RawKey key{};
    const AmrFluxEndpointBinding* source = nullptr;
    const AmrFluxEndpointBinding* destination = nullptr;
    std::array<bool, 5> conserved{};
    std::vector<bool> species;
    std::uint64_t first_ordinal = 0;
};

template <class Plan>
inline std::vector<RawGroup> group_fields(
    const Plan& plan, const BindingIndex& bindings, int species_count)
{
    std::map<RawKey, RawGroup> grouped;
    for (const auto& operation : plan.operations) {
        const auto& source = require_binding(bindings, operation.source);
        const auto& destination = require_binding(
            bindings, operation.destination);
        if (source.species_count != destination.species_count
            || source.species_count != species_count
            || operation.field == AmrField::EnucRate)
            throw std::invalid_argument("invalid AMR flux field layout");
        const RawKey key{
            operation.source, operation.destination,
            operation.source_box, operation.destination_box,
            operation.axis, operation.side, operation.rule,
            operation.weight, operation.sign};
        auto [entry, inserted] = grouped.try_emplace(key);
        auto& group = entry->second;
        if (inserted) {
            group.key = key;
            group.source = &source;
            group.destination = &destination;
            group.species.assign(
                static_cast<std::size_t>(species_count), false);
            group.first_ordinal = operation.ordinal;
        }
        if (operation.field == AmrField::Species) {
            if (operation.component < 0
                || operation.component >= species_count
                || group.species[
                    static_cast<std::size_t>(operation.component)])
                throw std::invalid_argument(
                    "duplicate or invalid AMR flux species field");
            group.species[
                static_cast<std::size_t>(operation.component)] = true;
        } else {
            const int field = static_cast<int>(operation.field);
            if (field < 0 || field >= 5
                || group.conserved[static_cast<std::size_t>(field)])
                throw std::invalid_argument(
                    "duplicate or invalid AMR conserved flux field");
            group.conserved[static_cast<std::size_t>(field)] = true;
        }
    }

    std::vector<RawGroup> result;
    result.reserve(grouped.size());
    for (auto& [key, group] : grouped) {
        (void)key;
        if (!std::all_of(group.conserved.begin(), group.conserved.end(),
                         [](bool value) { return value; })
            || !std::all_of(group.species.begin(), group.species.end(),
                            [](bool value) { return value; }))
            throw std::invalid_argument("AMR flux field group is incomplete");
        result.push_back(std::move(group));
    }
    std::sort(result.begin(), result.end(),
              [](const RawGroup& left, const RawGroup& right) {
                  return left.first_ordinal < right.first_ordinal;
              });
    return result;
}

inline AmrCompiledFluxRegistrationRoute compile_route(
    const AmrFluxRegistrationRoute& route,
    const BindingIndex& bindings, int species_count)
{
    validate_amr_plan(route.plan);
    const auto& source_binding = require_binding(bindings, route.source);
    AmrCompiledFluxRegistrationRoute result{};
    result.source_block = source_binding.runtime_index;
    result.source_direction = axis_value(route.key.axis);
    result.species_count = species_count;
    result.logical_fingerprint = route.plan.fingerprint;

    using TargetKey = std::tuple<int, int, int, int, RefinementRule>;
    struct TargetBuilder {
        TargetKey key{};
        std::vector<AmrFluxRegistrationTerm> terms;
    };
    std::map<TargetKey, std::size_t> target_index;
    std::vector<TargetBuilder> targets;
    for (const auto& group : group_fields(
             route.plan, bindings, species_count)) {
        if (group.source->runtime_index != result.source_block
            || group.key.source != route.source
            || group.key.axis != route.key.axis
            || !flux_math::is_flux_registration_rule(group.key.rule)
            || group.key.sign
                != flux_math::registration_route_sign(group.key.rule))
            throw std::invalid_argument("AMR registration route drifted");
        const int destination_face = 2 * axis_value(group.key.axis)
            + side_value(group.key.side);
        const TargetKey key{
            group.destination->runtime_index, destination_face,
            checked_register_cell(
                group.destination->grid, group.key.destination_box,
                group.key.axis, group.key.side),
            checked_source_face(
                group.source->grid, group.key.source_box, group.key.axis),
            group.key.rule};
        auto [found, inserted] = target_index.emplace(key, targets.size());
        if (inserted) targets.push_back({key, {}});
        targets[found->second].terms.push_back({
            checked_full_cell(
                group.source->grid, group.key.source_box,
                group.key.axis, true),
            checked_surface_cell(
                group.source->grid, group.key.source_box, group.key.axis),
            group.key.weight});
    }

    result.targets.reserve(targets.size());
    for (const auto& target : targets) {
        const int first = static_cast<int>(result.terms.size());
        result.terms.insert(
            result.terms.end(), target.terms.begin(), target.terms.end());
        result.targets.push_back({
            std::get<0>(target.key), std::get<1>(target.key),
            std::get<2>(target.key), std::get<3>(target.key),
            first, static_cast<int>(target.terms.size()),
            std::get<4>(target.key)});
    }
    return result;
}

} // namespace flux_execution_detail

inline AmrCompiledFluxTopologyPlan compile_amr_flux_topology_plan(
    const AmrFluxTopologyPlan& topology,
    std::span<const AmrFluxEndpointBinding> bindings)
{
    validate_amr_flux_topology_plan(topology);
    const auto indexed = flux_execution_detail::index_bindings(
        bindings, topology.dimension, topology.species_count);
    if (bindings.size() != topology.active_endpoints.size())
        throw std::invalid_argument("AMR flux binding set is incomplete");
    for (const auto& endpoint : topology.active_endpoints)
        (void)flux_execution_detail::require_binding(indexed, endpoint);
    AmrCompiledFluxTopologyPlan result{
        topology.dimension, topology.species_count, topology.epoch,
        topology.fingerprint, {}};
    result.routes.reserve(topology.routes.size());
    for (const auto& route : topology.routes)
        result.routes.push_back(flux_execution_detail::compile_route(
            route, indexed, topology.species_count));
    return result;
}

/** Exact compact allocations required by a compiled topology. */
inline std::vector<AmrFluxSurfaceRequirement>
build_amr_flux_surface_requirements(
    const AmrCompiledFluxTopologyPlan& plan, bool cache_initial_operator)
{
    using Key = std::tuple<int, int>;
    std::map<Key, AmrFluxSurfaceRequirement> requirements;
    const auto add = [&](int block, int face, AmrFluxSurfaceRole role) {
        if (block < 0 || face < 0 || face >= 2 * plan.dimension)
            throw std::invalid_argument("invalid AMR surface requirement");
        const Key key{block, face};
        auto [entry, inserted] = requirements.try_emplace(key);
        auto& requirement = entry->second;
        if (inserted) {
            requirement.block = block;
            requirement.face = face;
            requirement.cell_count = flux_plan_detail::face_cell_count(
                plan.dimension, static_cast<AmrAxis>(face / 2));
            requirement.species_count = plan.species_count;
        }
        requirement.roles |= static_cast<std::uint8_t>(role);
    };
    for (const auto& route : plan.routes) {
        for (const auto& target : route.targets) {
            add(target.destination_block, target.destination_face,
                AmrFluxSurfaceRole::Register);
            if (cache_initial_operator)
                add(route.source_block, target.source_face,
                    AmrFluxSurfaceRole::InitialOperatorCache);
        }
    }
    std::vector<AmrFluxSurfaceRequirement> result;
    result.reserve(requirements.size());
    for (const auto& [key, requirement] : requirements) {
        (void)key;
        result.push_back(requirement);
    }
    return result;
}

inline AmrCompiledRefluxPlan compile_amr_reflux_plan(
    const RefluxPlan& plan,
    std::span<const AmrFluxEndpointBinding> bindings,
    int species_count)
{
    validate_amr_plan(plan);
    const auto indexed = flux_execution_detail::index_bindings(
        bindings, plan.dimension, species_count);
    using TargetKey = std::tuple<int, int>;
    struct TargetBuilder {
        TargetKey key{};
        std::vector<AmrRefluxContribution> contributions;
    };
    std::map<TargetKey, std::size_t> target_index;
    std::vector<TargetBuilder> targets;
    for (const auto& group : flux_execution_detail::group_fields(
             plan, indexed, species_count)) {
        if (group.key.source != group.key.destination
            || group.key.source_box != group.key.destination_box
            || group.key.rule != RefinementRule::RefluxCorrection)
            throw std::invalid_argument("AMR reflux route drifted");
        const TargetKey key{
            group.destination->runtime_index,
            flux_execution_detail::checked_full_cell(
                group.destination->grid, group.key.destination_box,
                group.key.axis, false)};
        auto [found, inserted] = target_index.emplace(key, targets.size());
        if (inserted) targets.push_back({key, {}});
        targets[found->second].contributions.push_back({
            2 * axis_value(group.key.axis) + side_value(group.key.side),
            flux_execution_detail::checked_register_cell(
                group.destination->grid, group.key.destination_box,
                group.key.axis, group.key.side),
            group.key.weight, group.key.sign});
    }

    AmrCompiledRefluxPlan result{
        plan.dimension, plan.scope.from_epoch, plan.fingerprint, {}, {}};
    result.targets.reserve(targets.size());
    for (const auto& target : targets) {
        const int first = static_cast<int>(result.contributions.size());
        result.contributions.insert(
            result.contributions.end(), target.contributions.begin(),
            target.contributions.end());
        result.targets.push_back({
            std::get<0>(target.key), std::get<1>(target.key), species_count,
            first, static_cast<int>(target.contributions.size())});
    }
    return result;
}

static_assert(std::is_standard_layout_v<AmrFluxGridLayout>);
static_assert(std::is_trivially_copyable_v<AmrFluxGridLayout>);
static_assert(std::is_standard_layout_v<AmrFluxEndpointBinding>);
static_assert(std::is_trivially_copyable_v<AmrFluxEndpointBinding>);
static_assert(std::is_standard_layout_v<AmrFluxRegistrationTerm>);
static_assert(std::is_trivially_copyable_v<AmrFluxRegistrationTerm>);
static_assert(std::is_standard_layout_v<AmrFluxRegistrationTarget>);
static_assert(std::is_trivially_copyable_v<AmrFluxRegistrationTarget>);
static_assert(std::is_standard_layout_v<AmrFluxSurfaceRequirement>);
static_assert(std::is_trivially_copyable_v<AmrFluxSurfaceRequirement>);
static_assert(std::is_standard_layout_v<AmrRefluxContribution>);
static_assert(std::is_trivially_copyable_v<AmrRefluxContribution>);
static_assert(std::is_standard_layout_v<AmrRefluxTarget>);
static_assert(std::is_trivially_copyable_v<AmrRefluxTarget>);

} // namespace amr
