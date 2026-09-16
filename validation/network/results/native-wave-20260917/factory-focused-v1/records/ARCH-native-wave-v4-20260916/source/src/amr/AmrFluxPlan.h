/**
 * @file AmrFluxPlan.h
 * @brief Value-independent, topology-scoped AMR flux registration plans.
 */

#pragma once

#include "AmrFluxMath.h"
#include "MemoryPool.h"
#include "grid/GridMetrics.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace amr {

struct AmrFluxRouteKey {
    int source_block = -1;
    AmrAxis axis = AmrAxis::X;
    friend constexpr auto operator<=>(const AmrFluxRouteKey&,
                                      const AmrFluxRouteKey&) = default;
};

struct AmrFluxRegistrationRoute {
    AmrFluxRouteKey key{};
    AmrEndpoint source{};
    FluxRegistrationPlan plan{};
};

/**
 * One immutable plan for a topology epoch.  Routes contain geometry only;
 * face-flux values and integrator stage weights are execution-time inputs.
 */
struct AmrFluxTopologyPlan {
    int dimension = 0;
    int species_count = 0;
    TopologyEpoch epoch{};
    std::vector<int> active_blocks;
    std::vector<AmrEndpoint> active_endpoints;
    std::map<AmrEndpoint, int> pool_lowering;
    std::vector<AmrFluxRegistrationRoute> routes;
    std::map<AmrFluxRouteKey, std::size_t> route_index;
    std::uint64_t fingerprint = 0;

    const AmrFluxRegistrationRoute* find(int block_id, int direction) const
        noexcept
    {
        if (direction < 0 || direction >= dimension) return nullptr;
        const auto found = route_index.find({
            block_id, static_cast<AmrAxis>(direction)});
        return found == route_index.end() ? nullptr
                                         : &routes[found->second];
    }
};

namespace flux_plan_detail {

inline LogicalBlockKey logical_key(const Block& block, int dimension)
{
    return {dimension, block.level, block.logical_x1,
            block.logical_x2, block.logical_x3};
}

inline bool same_grid_contract(const Grid& left, const Grid& right) noexcept
{
    return left.dim == right.dim && left.ng == right.ng
        && left.stride_y == right.stride_y
        && left.stride_z == right.stride_z
        && left.GetTotalSize() == right.GetTotalSize()
        && left.geometry == right.geometry
        && left.dx1 == right.dx1 && left.dx2 == right.dx2
        && left.dx3 == right.dx3
        && left.x1_min == right.x1_min && left.x1_max == right.x1_max
        && left.x2_min == right.x2_min && left.x2_max == right.x2_max
        && left.x3_min == right.x3_min && left.x3_max == right.x3_max;
}

inline int face_cell_count(int dimension, AmrAxis axis) noexcept
{
    switch (axis) {
    case AmrAxis::X:
        return (dimension >= 2 ? BLOCK_NY : 1)
            * (dimension == 3 ? BLOCK_NZ : 1);
    case AmrAxis::Y:
        return BLOCK_NX * (dimension == 3 ? BLOCK_NZ : 1);
    case AmrAxis::Z:
        return BLOCK_NX * (dimension >= 2 ? BLOCK_NY : 1);
    }
    return 0;
}

inline int face_cell_index(const LogicalAmrBox& box, AmrAxis axis)
{
    if (box.extent != std::array<std::uint32_t, 3>{1, 1, 1})
        throw std::invalid_argument("AMR flux destination is not one cell");
    switch (axis) {
    case AmrAxis::X: return box.first[2] * BLOCK_NY + box.first[1];
    case AmrAxis::Y: return box.first[2] * BLOCK_NX + box.first[0];
    case AmrAxis::Z: return box.first[1] * BLOCK_NX + box.first[0];
    }
    throw std::invalid_argument("invalid AMR flux axis");
}

inline int source_face_cell_index(const LogicalAmrBox& box, AmrAxis axis)
{
    return face_cell_index(box, axis);
}

inline std::array<std::int32_t, 3> face_cell_coordinates(
    int cell, AmrAxis axis, AmrSide side, int dimension)
{
    const int ny = dimension >= 2 ? BLOCK_NY : 1;
    const int nz = dimension == 3 ? BLOCK_NZ : 1;
    switch (axis) {
    case AmrAxis::X:
        return {side == AmrSide::Lower ? 0 : BLOCK_NX - 1,
                cell % ny, cell / ny};
    case AmrAxis::Y:
        return {cell % BLOCK_NX,
                side == AmrSide::Lower ? 0 : BLOCK_NY - 1,
                cell / BLOCK_NX};
    case AmrAxis::Z:
        return {cell % BLOCK_NX, cell / BLOCK_NX,
                side == AmrSide::Lower ? 0 : nz - 1};
    }
    throw std::invalid_argument("invalid AMR flux face");
}

inline double coarse_face_area(const Block& coarse, int normal_direction,
                               int coarse_face, int face_cell)
{
    const int ny = coarse.grid.dim >= 2 ? BLOCK_NY : 1;
    int i = coarse.grid.Is();
    int j = coarse.grid.Js();
    int k = coarse.grid.Ks();
    if (normal_direction == 0) {
        j += face_cell % ny;
        k += face_cell / ny;
        i += coarse_face % 2 == 0 ? 0 : BLOCK_NX - 1;
    } else if (normal_direction == 1) {
        i += face_cell % BLOCK_NX;
        k += face_cell / BLOCK_NX;
        j += coarse_face % 2 == 0 ? 0 : BLOCK_NY - 1;
    } else {
        i += face_cell % BLOCK_NX;
        j += face_cell / BLOCK_NX;
        k += coarse_face % 2 == 0 ? 0 : BLOCK_NZ - 1;
    }
    return GridMetrics::FaceArea(
        coarse.grid, normal_direction, i, j, k, coarse_face % 2 == 1);
}

inline void append_fields(FluxRegistrationPlan& plan,
                          const AmrTransferOperation& prototype,
                          int species_count)
{
    const auto append = [&](AmrField field, int component) {
        auto operation = prototype;
        operation.field = field;
        operation.component = component;
        plan.operations.push_back(std::move(operation));
    };
    append(AmrField::Rho, -1);
    append(AmrField::MomU, -1);
    append(AmrField::MomV, -1);
    append(AmrField::MomW, -1);
    append(AmrField::Energy, -1);
    for (int species = 0; species < species_count; ++species)
        append(AmrField::Species, species);
}

class Fingerprint {
public:
    void u64(std::uint64_t value) noexcept
    {
        for (int shift = 0; shift < 64; shift += 8) {
            value_ ^= static_cast<std::uint8_t>(value >> shift);
            value_ *= UINT64_C(0x100000001b3);
        }
    }

    std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_ = UINT64_C(0xcbf29ce484222325);
};

inline std::uint64_t compute_fingerprint(const AmrFluxTopologyPlan& plan)
    noexcept
{
    Fingerprint hash;
    hash.u64(1); // Topology flux-plan contract version.
    hash.u64(static_cast<std::uint64_t>(plan.dimension));
    hash.u64(static_cast<std::uint64_t>(plan.species_count));
    hash.u64(plan.epoch.value);
    hash.u64(static_cast<std::uint64_t>(plan.routes.size()));
    for (const auto& route : plan.routes) {
        hash.u64(static_cast<std::uint64_t>(route.key.source_block));
        hash.u64(static_cast<std::uint64_t>(axis_value(route.key.axis)));
        hash.u64(route.plan.fingerprint);
    }
    return hash.value();
}

} // namespace flux_plan_detail

inline void validate_amr_flux_topology_plan(const AmrFluxTopologyPlan& plan)
{
    if (plan.dimension < 1 || plan.dimension > 3
        || plan.species_count < 0 || !is_valid(plan.epoch)
        || plan.active_blocks.empty()
        || plan.active_blocks.size() != plan.active_endpoints.size()
        || plan.pool_lowering.size() != plan.active_blocks.size()
        || plan.route_index.size() != plan.routes.size())
        throw std::invalid_argument("invalid AMR flux topology plan");

    std::map<int, AmrEndpoint> endpoints_by_block;
    for (std::size_t index = 0; index < plan.active_blocks.size(); ++index) {
        const int block = plan.active_blocks[index];
        const auto& endpoint = plan.active_endpoints[index];
        const auto lowered = plan.pool_lowering.find(endpoint);
        if (block < 0 || endpoint.handle.epoch != plan.epoch
            || lowered == plan.pool_lowering.end() || lowered->second != block
            || !endpoints_by_block.emplace(block, endpoint).second)
            throw std::invalid_argument("invalid AMR flux endpoint binding");
    }

    for (std::size_t index = 0; index < plan.routes.size(); ++index) {
        const auto& route = plan.routes[index];
        const auto route_lookup = plan.route_index.find(route.key);
        const auto endpoint = endpoints_by_block.find(route.key.source_block);
        if (route_lookup == plan.route_index.end()
            || route_lookup->second != index || endpoint == endpoints_by_block.end()
            || endpoint->second != route.source || route.plan.operations.empty()
            || route.plan.dimension != plan.dimension
            || route.plan.scope.from_epoch != plan.epoch
            || route.plan.scope.to_epoch != plan.epoch)
            throw std::invalid_argument("invalid AMR flux route binding");
        validate_amr_plan(route.plan);
        for (const auto& operation : route.plan.operations) {
            if (operation.source != route.source
                || operation.axis != route.key.axis
                || operation.sign
                    != flux_math::registration_route_sign(operation.rule))
                throw std::invalid_argument("AMR flux route is not canonical");
        }
    }
    if (plan.fingerprint != flux_plan_detail::compute_fingerprint(plan))
        throw std::invalid_argument("AMR flux topology fingerprint mismatch");
}

/**
 * Compile every coarse-fine face exactly once for the current topology.
 * Complexity is O(blocks * face cells); stage execution performs an indexed
 * route lookup and never scans the hierarchy.
 */
inline AmrFluxTopologyPlan build_amr_flux_topology_plan(
    const MemoryPool& pool, std::span<const int> active_blocks,
    std::span<const BlockHandle> active_handles, int dimension,
    int species_count)
{
    using namespace flux_plan_detail;
    if (dimension < 1 || dimension > 3 || species_count < 0
        || active_blocks.empty()
        || active_blocks.size() != active_handles.size())
        throw std::invalid_argument("invalid AMR flux topology inputs");

    AmrFluxTopologyPlan result{};
    result.dimension = dimension;
    result.species_count = species_count;
    result.epoch = active_handles.front().epoch;
    if (!is_valid(result.epoch))
        throw std::invalid_argument("invalid AMR flux topology epoch");
    result.active_blocks.assign(active_blocks.begin(), active_blocks.end());
    result.active_endpoints.reserve(active_blocks.size());

    std::map<int, std::size_t> active_index;
    for (std::size_t index = 0; index < active_blocks.size(); ++index) {
        const int block_id = active_blocks[index];
        if (block_id < 0)
            throw std::invalid_argument("negative AMR flux block id");
        const Block& block = pool.GetBlock(block_id);
        if (!block.active || block.id != block_id
            || block.grid.dim != dimension
            || block.fluid_state.GetNumSpecies() != species_count
            || !is_valid(active_handles[index])
            || active_handles[index].epoch != result.epoch)
            throw std::invalid_argument("AMR flux active layout drifted");
        const AmrEndpoint endpoint{
            logical_key(block, dimension), active_handles[index]};
        if (!active_index.emplace(block_id, index).second
            || !result.pool_lowering.emplace(endpoint, block_id).second)
            throw std::invalid_argument("AMR flux lowering is not bijective");
        result.active_endpoints.push_back(endpoint);
    }

    const int nx = BLOCK_NX;
    const int ny = dimension >= 2 ? BLOCK_NY : 1;
    const int nz = dimension == 3 ? BLOCK_NZ : 1;
    for (std::size_t block_index = 0;
         block_index < active_blocks.size(); ++block_index) {
        const int block_id = active_blocks[block_index];
        const Block& block = pool.GetBlock(block_id);
        const AmrEndpoint source_endpoint = result.active_endpoints[block_index];
        for (int direction = 0; direction < dimension; ++direction) {
            AmrFluxRegistrationRoute route{};
            route.key = {block_id, static_cast<AmrAxis>(direction)};
            route.source = source_endpoint;
            route.plan.dimension = dimension;
            route.plan.scope = {0, result.epoch, result.epoch};

            for (int side = 0; side < 2; ++side) {
                const int face = 2 * direction + side;
                const auto& neighbours = block.face_neighbors[face];
                if (neighbours.count == 0) {
                    if (neighbours.level_diff != 0)
                        throw std::invalid_argument(
                            "empty AMR face has a refinement relation");
                    continue;
                }
                if (neighbours.level_diff == 0) continue;
                if (neighbours.level_diff != -1
                    && neighbours.level_diff != 1)
                    throw std::invalid_argument(
                        "invalid coarse-fine flux level relation");
                if (neighbours.level_diff == -1
                    && (neighbours.count != 1 || neighbours.ids[0] < 0))
                    throw std::invalid_argument(
                        "fine flux has no unique coarse endpoint");
                if (neighbours.level_diff == 1) {
                    const int expected = 1 << (dimension - 1);
                    if (neighbours.count != expected)
                        throw std::invalid_argument(
                            "coarse flux has an incomplete fine face");
                    for (int neighbour = 0;
                         neighbour < neighbours.count; ++neighbour) {
                        const auto found = active_index.find(
                            neighbours.ids[neighbour]);
                        if (found == active_index.end()
                            || pool.GetBlock(neighbours.ids[neighbour]).level
                                != block.level + 1)
                            throw std::invalid_argument(
                                "coarse flux fine endpoint drifted");
                    }
                }

                for (int k = 0; k < nz; ++k) {
                    for (int j = 0; j < ny; ++j) {
                        for (int i = 0; i < nx; ++i) {
                            if ((direction == 0
                                    && i != (side == 0 ? 0 : nx - 1))
                                || (direction == 1
                                    && j != (side == 0 ? 0 : ny - 1))
                                || (direction == 2
                                    && k != (side == 0 ? 0 : nz - 1)))
                                continue;
                            const double fine_area = GridMetrics::FaceArea(
                                block.grid, direction, block.grid.Is() + i,
                                block.grid.Js() + j, block.grid.Ks() + k,
                                side == 1);
                            if (!amr_plan_detail::is_finite_binary64(fine_area)
                                || fine_area <= 0.0)
                                throw std::invalid_argument(
                                    "invalid fine flux metric");
                            const LogicalAmrBox source_face{
                                {i + (direction == 0 && side == 1 ? 1 : 0),
                                 j + (direction == 1 && side == 1 ? 1 : 0),
                                 k + (direction == 2 && side == 1 ? 1 : 0)},
                                {1, 1, 1}};

                            if (neighbours.level_diff == -1) {
                                const int coarse_id = neighbours.ids[0];
                                const auto coarse_index = active_index.find(
                                    coarse_id);
                                if (coarse_index == active_index.end())
                                    throw std::invalid_argument(
                                        "coarse flux destination is not active");
                                const Block& coarse = pool.GetBlock(coarse_id);
                                if (coarse.level + 1 != block.level)
                                    throw std::invalid_argument(
                                        "fine flux endpoint level drifted");
                                int coarse_i =
                                    (block.logical_x1 & 1U) * (nx / 2) + i / 2;
                                int coarse_j = dimension >= 2
                                    ? (block.logical_x2 & 1U) * (ny / 2) + j / 2
                                    : 0;
                                int coarse_k = dimension == 3
                                    ? (block.logical_x3 & 1U) * (nz / 2) + k / 2
                                    : 0;
                                int coarse_cell = 0;
                                if (direction == 0)
                                    coarse_cell = coarse_k * ny + coarse_j;
                                else if (direction == 1)
                                    coarse_cell = coarse_k * nx + coarse_i;
                                else
                                    coarse_cell = coarse_j * nx + coarse_i;
                                const int coarse_face =
                                    2 * direction + (side == 0 ? 1 : 0);
                                const double coarse_area = coarse_face_area(
                                    coarse, direction, coarse_face, coarse_cell);
                                if (!amr_plan_detail::is_finite_binary64(
                                        coarse_area)
                                    || coarse_area <= 0.0)
                                    throw std::invalid_argument(
                                        "invalid coarse flux metric");
                                if (direction == 0)
                                    coarse_i = coarse_face % 2 == 0
                                        ? 0 : nx - 1;
                                else if (direction == 1)
                                    coarse_j = coarse_face % 2 == 0
                                        ? 0 : ny - 1;
                                else
                                    coarse_k = coarse_face % 2 == 0
                                        ? 0 : nz - 1;
                                const AmrEndpoint destination{
                                    logical_key(coarse, dimension),
                                    active_handles[coarse_index->second]};
                                const AmrTransferOperation prototype{
                                    0, source_endpoint, destination,
                                    source_face,
                                    {{coarse_i, coarse_j, coarse_k}, {1, 1, 1}},
                                    static_cast<AmrAxis>(direction),
                                    static_cast<AmrSide>(coarse_face % 2),
                                    AmrField::Rho, -1,
                                    RefinementRule::FineFluxContribution,
                                    fine_area / coarse_area, 1.0};
                                append_fields(
                                    route.plan, prototype, species_count);
                            } else {
                                const AmrTransferOperation prototype{
                                    0, source_endpoint, source_endpoint,
                                    source_face, {{i, j, k}, {1, 1, 1}},
                                    static_cast<AmrAxis>(direction),
                                    static_cast<AmrSide>(side),
                                    AmrField::Rho, -1,
                                    RefinementRule::CoarseFluxContribution,
                                    1.0, -1.0};
                                append_fields(
                                    route.plan, prototype, species_count);
                            }
                        }
                    }
                }
            }

            if (!route.plan.operations.empty()) {
                finalize_amr_plan(route.plan);
                result.routes.push_back(std::move(route));
            }
        }
    }

    std::sort(result.routes.begin(), result.routes.end(),
              [](const auto& left, const auto& right) {
                  return std::tuple{left.source, left.key.axis}
                      < std::tuple{right.source, right.key.axis};
              });
    for (std::size_t index = 0; index < result.routes.size(); ++index)
        if (!result.route_index.emplace(result.routes[index].key, index).second)
            throw std::invalid_argument("duplicate AMR flux route");
    result.fingerprint = compute_fingerprint(result);
    validate_amr_flux_topology_plan(result);
    return result;
}

/**
 * Build the reflux geometry directly from coarse-fine topology.  The returned
 * plan stores area/volume only; callers pass dt to their executor.  It does
 * not inspect FluxRegister::HasData, so a device-only registration path does
 * not need to manufacture Host activity witnesses.
 */
inline RefluxPlan build_amr_reflux_topology_plan(
    const MemoryPool& pool, const AmrFluxTopologyPlan& topology)
{
    using namespace flux_plan_detail;
    validate_amr_flux_topology_plan(topology);
    RefluxPlan result{};
    result.dimension = topology.dimension;
    result.scope = {0, topology.epoch, topology.epoch};
    for (std::size_t block_index = 0;
         block_index < topology.active_blocks.size(); ++block_index) {
        const int block_id = topology.active_blocks[block_index];
        const Block& block = pool.GetBlock(block_id);
        if (block.fluid_state.GetNumSpecies() != topology.species_count)
            throw std::invalid_argument("AMR reflux species layout drifted");
        const AmrEndpoint endpoint = topology.active_endpoints[block_index];
        for (int face = 0; face < 2 * topology.dimension; ++face) {
            const auto& neighbours = block.face_neighbors[face];
            if (neighbours.count == 0 || neighbours.level_diff != 1)
                continue;
            const AmrAxis axis = static_cast<AmrAxis>(face / 2);
            const AmrSide side = static_cast<AmrSide>(face % 2);
            const auto* route = topology.find(block_id, face / 2);
            if (route == nullptr)
                throw std::invalid_argument(
                    "AMR reflux face has no registration route");
            const int cells = face_cell_count(topology.dimension, axis);
            for (int cell = 0; cell < cells; ++cell) {
                const auto logical = face_cell_coordinates(
                    cell, axis, side, topology.dimension);
                const int i = block.grid.Is() + logical[0];
                const int j = block.grid.Js() + logical[1];
                const int k = block.grid.Ks() + logical[2];
                const double area = GridMetrics::FaceArea(
                    block.grid, axis_value(axis), i, j, k,
                    side == AmrSide::Upper);
                const double volume = GridMetrics::CellVolume(
                    block.grid, i, j, k);
                if (!amr_plan_detail::is_finite_binary64(area)
                    || !amr_plan_detail::is_finite_binary64(volume)
                    || area <= 0.0 || volume <= 0.0)
                    throw std::invalid_argument(
                        "AMR reflux metric is nonpositive or nonfinite");
                const LogicalAmrBox box{logical, {1, 1, 1}};
                const auto append = [&](AmrField field, int component) {
                    result.operations.push_back({
                        0, endpoint, endpoint, box, box, axis, side,
                        field, component, RefinementRule::RefluxCorrection,
                        area / volume,
                        side == AmrSide::Lower ? 1.0 : -1.0});
                };
                append(AmrField::Rho, -1);
                append(AmrField::MomU, -1);
                append(AmrField::MomV, -1);
                append(AmrField::MomW, -1);
                append(AmrField::Energy, -1);
                for (int species = 0;
                     species < topology.species_count; ++species)
                    append(AmrField::Species, species);
            }
        }
    }
    finalize_amr_plan(result);
    return result;
}

} // namespace amr
