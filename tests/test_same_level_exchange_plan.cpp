#include "amr/ExchangePlan.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

using amr::BlockHandle;
using amr::BlockUid;
using amr::ExchangeFace;
using amr::ExchangePhaseId;
using amr::LogicalBlockKey;
using amr::SameLevelTopologyEntry;
using amr::TopologyEpoch;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

LogicalBlockKey key(int dimension, std::uint32_t x, std::uint32_t y = 0)
{
    return {dimension, 0, x, y, 0};
}

SameLevelTopologyEntry entry(
    LogicalBlockKey logical, std::uint64_t uid, std::uint64_t epoch)
{
    SameLevelTopologyEntry result{};
    result.logical = logical;
    result.handle = BlockHandle{BlockUid{uid}, TopologyEpoch{epoch}};
    return result;
}

void connect(
    SameLevelTopologyEntry& left, ExchangeFace left_face,
    SameLevelTopologyEntry& right, ExchangeFace right_face)
{
    left.neighbors[static_cast<std::size_t>(left_face)] = right.logical;
    right.neighbors[static_cast<std::size_t>(right_face)] = left.logical;
}

void test_two_block_1d()
{
    std::vector<SameLevelTopologyEntry> topology{
        entry(key(1, 0), 11, 7), entry(key(1, 1), 12, 7)};
    connect(topology[0], ExchangeFace::X1Upper,
            topology[1], ExchangeFace::X1Lower);

    const auto plan = amr::make_same_level_exchange_plan(
        topology, 1, {8, 1, 1}, 2, TopologyEpoch{7});
    require(amr::compute_same_level_exchange_fingerprint(plan)
                == plan.fingerprint,
            "1D plan fingerprint cannot be independently revalidated");
    require(plan.dimension == 1, "1D plan dimension drifted");
    require(plan.ghost_depth == 2, "1D ghost depth drifted");
    require(plan.operations.size() == 2,
            "two-block interface must be directed both ways");
    require(plan.phases[0].id == ExchangePhaseId::X
                && plan.phases[0].first == 0
                && plan.phases[0].count == 2,
            "1D X phase metadata drifted");
    require(plan.phases[1].count == 0 && plan.phases[2].count == 0,
            "inactive phases must be empty");

    const auto& left_upper = plan.operations[0];
    const auto& right_lower = plan.operations[1];
    require(left_upper.destination.logical == key(1, 0)
                && left_upper.source.logical == key(1, 1),
            "left upper logical endpoint mapping drifted");
    require(left_upper.destination_box.first == std::array<std::int32_t, 3>{8, 0, 0}
                && left_upper.destination_box.extent == std::array<std::uint32_t, 3>{2, 1, 1}
                && left_upper.source_box.first == std::array<std::int32_t, 3>{0, 0, 0},
            "left upper ghost/source boxes drifted");
    require(right_lower.destination.logical == key(1, 1)
                && right_lower.source.logical == key(1, 0),
            "right lower logical endpoint mapping drifted");
    require(right_lower.destination_box.first == std::array<std::int32_t, 3>{-2, 0, 0}
                && right_lower.source_box.first == std::array<std::int32_t, 3>{6, 0, 0},
            "right lower ghost/source boxes drifted");
    require(plan.fingerprint == UINT64_C(0xf8086a26b2a9896e),
            "1D plan fingerprint drifted from the independent FNV oracle");

    auto stale = plan;
    stale.operations[0].source_box.first[0] += 1;
    require(amr::compute_same_level_exchange_fingerprint(stale)
                != stale.fingerprint,
            "mutated logical plan retained a valid fingerprint");
}

std::vector<SameLevelTopologyEntry> make_2x2()
{
    std::vector<SameLevelTopologyEntry> result{
        entry(key(2, 0, 0), 21, 9), entry(key(2, 1, 0), 22, 9),
        entry(key(2, 0, 1), 23, 9), entry(key(2, 1, 1), 24, 9)};
    connect(result[0], ExchangeFace::X1Upper,
            result[1], ExchangeFace::X1Lower);
    connect(result[2], ExchangeFace::X1Upper,
            result[3], ExchangeFace::X1Lower);
    connect(result[0], ExchangeFace::X2Upper,
            result[2], ExchangeFace::X2Lower);
    connect(result[1], ExchangeFace::X2Upper,
            result[3], ExchangeFace::X2Lower);
    return result;
}

void test_2x2_phase_and_permutation()
{
    auto topology = make_2x2();
    const auto authority = amr::make_same_level_exchange_plan(
        topology, 2, {8, 6, 1}, 2, TopologyEpoch{9});
    require(authority.operations.size() == 8,
            "2x2 plan directed operation count drifted");
    require(authority.phases[0].count == 4
                && authority.phases[1].first == 4
                && authority.phases[1].count == 4
                && authority.phases[2].first == 8
                && authority.phases[2].count == 0,
            "2x2 phase ranges drifted");

    const auto y_operation = std::find_if(
        authority.operations.begin(), authority.operations.end(),
        [](const auto& operation) {
            return operation.phase == ExchangePhaseId::Y;
        });
    require(y_operation != authority.operations.end(),
            "2x2 plan omitted Y exchange");
    require(y_operation->source_box.first[0] == -2
                && y_operation->source_box.extent[0] == 12
                && y_operation->destination_box.first[0] == -2
                && y_operation->destination_box.extent[0] == 12,
            "Y phase must include completed X ghosts for corners");

    std::reverse(topology.begin(), topology.end());
    const auto permuted = amr::make_same_level_exchange_plan(
        topology, 2, {8, 6, 1}, 2, TopologyEpoch{9});
    require(permuted.fingerprint == authority.fingerprint
                && permuted.operations == authority.operations
                && permuted.phases == authority.phases,
            "plan depends on block traversal order");
}

void test_invalid_topology_rejected()
{
    auto topology = make_2x2();
    topology[3].handle.epoch = TopologyEpoch{10};
    bool stale_rejected = false;
    try {
        (void)amr::make_same_level_exchange_plan(
            topology, 2, {8, 6, 1}, 2, TopologyEpoch{9});
    } catch (const std::invalid_argument&) {
        stale_rejected = true;
    }
    require(stale_rejected, "mixed/stale topology epoch accepted");

    topology = make_2x2();
    topology[0].neighbors[static_cast<std::size_t>(
        ExchangeFace::X1Upper)] = std::nullopt;
    bool asymmetric_rejected = false;
    try {
        (void)amr::make_same_level_exchange_plan(
            topology, 2, {8, 6, 1}, 2, TopologyEpoch{9});
    } catch (const std::invalid_argument&) {
        asymmetric_rejected = true;
    }
    require(asymmetric_rejected, "nonreciprocal neighbor relation accepted");
}

void test_logical_only_bootstrap_plan()
{
    std::vector<SameLevelTopologyEntry> topology{
        entry(key(1, 0), 0, 0), entry(key(1, 1), 0, 0)};
    connect(topology[0], ExchangeFace::X1Upper,
            topology[1], ExchangeFace::X1Lower);
    const auto plan = amr::make_same_level_exchange_plan(
        topology, 1, {8, 1, 1}, 2, TopologyEpoch{});
    require(!amr::is_valid(plan.epoch) && plan.operations.size() == 2,
            "logical-only bootstrap plan drifted");
}

struct OwnedHostBlock {
    LogicalBlockKey logical{};
    BlockHandle handle{};
    amr::HostExchangeLayout layout{};
    std::array<std::vector<double>, 6> conserved;
    std::vector<double> species;
    int species_count = 0;

    amr::HostExchangeBlockView view()
    {
        amr::HostExchangeBlockView result{};
        result.logical = logical;
        result.handle = handle;
        result.layout = layout;
        for (std::size_t field = 0; field < conserved.size(); ++field)
            result.conserved[field] = conserved[field].data();
        result.species = species_count == 0 ? nullptr : species.data();
        result.species_count = species_count;
        result.species_stride = layout.total_size;
        return result;
    }
};

OwnedHostBlock make_owned_host_block(
    const SameLevelTopologyEntry& topology, int pitch, int species_count)
{
    OwnedHostBlock result{};
    result.logical = topology.logical;
    result.handle = topology.handle;
    result.layout = {
        1, {2, 0, 0}, {12, 1, 1},
        {1, pitch, pitch}, pitch};
    result.species_count = species_count;
    for (auto& field : result.conserved)
        field.assign(static_cast<std::size_t>(pitch), -777.0);
    result.species.assign(
        static_cast<std::size_t>(species_count * pitch), -888.0);
    return result;
}

std::uint64_t bits(double value)
{
    return std::bit_cast<std::uint64_t>(value);
}

void test_host_executor_bitwise_and_pitch()
{
    std::vector<SameLevelTopologyEntry> topology{
        entry(key(1, 0), 31, 12), entry(key(1, 1), 32, 12)};
    connect(topology[0], ExchangeFace::X1Upper,
            topology[1], ExchangeFace::X1Lower);
    const auto plan = amr::make_same_level_exchange_plan(
        topology, 1, {8, 1, 1}, 2, TopologyEpoch{12});

    auto left = make_owned_host_block(topology[0], 16, 2);
    auto right = make_owned_host_block(topology[1], 20, 2);
    for (int logical_i = 0; logical_i < 8; ++logical_i) {
        const int left_index = left.layout.active_origin[0] + logical_i;
        const int right_index = right.layout.active_origin[0] + logical_i;
        for (int field = 0; field < 6; ++field) {
            left.conserved[field][left_index] =
                1000.0 + 100.0 * field + logical_i;
            right.conserved[field][right_index] =
                2000.0 + 100.0 * field + logical_i;
        }
        for (int species = 0; species < 2; ++species) {
            left.species[species * left.layout.total_size + left_index] =
                3000.0 + 100.0 * species + logical_i;
            right.species[species * right.layout.total_size + right_index] =
                4000.0 + 100.0 * species + logical_i;
        }
    }
    right.conserved[0][2] = std::bit_cast<double>(UINT64_C(0x7ff8000000000042));
    right.conserved[1][2] = std::bit_cast<double>(UINT64_C(0x8000000000000000));
    left.conserved[4][9] = std::bit_cast<double>(UINT64_C(0x7ff0000000000000));

    std::array<amr::HostExchangeBlockView, 2> views{
        left.view(), right.view()};
    const auto compiled = amr::compile_host_exchange_plan(plan, views);
    amr::execute_host_exchange_plan(compiled, views);

    for (int depth = 0; depth < 2; ++depth) {
        const int left_destination = 10 + depth;
        const int right_source = 2 + depth;
        const int right_destination = depth;
        const int left_source = 8 + depth;
        for (int field = 0; field < 6; ++field) {
            require(bits(left.conserved[field][left_destination])
                        == bits(right.conserved[field][right_source]),
                    "Host upper interface copy is not bitwise exact");
            require(bits(right.conserved[field][right_destination])
                        == bits(left.conserved[field][left_source]),
                    "Host lower interface copy is not bitwise exact");
        }
        for (int species = 0; species < 2; ++species) {
            require(bits(left.species[
                             species * left.layout.total_size
                             + left_destination])
                        == bits(right.species[
                             species * right.layout.total_size + right_source]),
                    "Host upper species copy is not bitwise exact");
            require(bits(right.species[
                             species * right.layout.total_size
                             + right_destination])
                        == bits(left.species[
                             species * left.layout.total_size + left_source]),
                    "Host lower species copy is not bitwise exact");
        }
    }
}

} // namespace

int main()
{
    try {
        test_two_block_1d();
        test_2x2_phase_and_permutation();
        test_invalid_topology_rejected();
        test_logical_only_bootstrap_plan();
        test_host_executor_bitwise_and_pitch();
        std::cout << "same-level exchange plan contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
