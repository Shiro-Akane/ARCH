/**
 * @file test_block_handle.cpp
 * @brief Check stable block identity independently of memory-pool slots.
 *
 * Pool reuse, traversal and checkpoint reconstruction must not resurrect stale
 * handles or create collisions between roots, UIDs and topology epochs.
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "amr/BlockHandle.h"
#include "amr/Morton.h"

namespace {

using amr::BlockHandle;
using amr::BlockIdentityAuthority;
using amr::BlockUid;
using amr::CellLogicalKey;
using amr::RootLogicalKey;
using amr::TopologyEpoch;

template<class T>
concept HasPoolIndex = requires(T value) { value.pool_index; };

template<class T>
concept HasActiveIndex = requires(T value) { value.active_index; };

template<class T>
concept HasBlockUid = requires(T value) { value.uid; };

template<class T>
concept HasStorageGeneration = requires(T value) { value.storage_generation; };

template<class T>
concept HasPointer = requires(T value) { value.pointer; };

static_assert(std::is_standard_layout_v<BlockUid>);
static_assert(std::is_standard_layout_v<TopologyEpoch>);
static_assert(std::is_standard_layout_v<BlockHandle>);
static_assert(std::is_standard_layout_v<RootLogicalKey>);
static_assert(std::is_standard_layout_v<CellLogicalKey>);
static_assert(std::is_trivially_copyable_v<BlockUid>);
static_assert(std::is_trivially_copyable_v<TopologyEpoch>);
static_assert(std::is_trivially_copyable_v<BlockHandle>);
static_assert(std::is_trivially_copyable_v<RootLogicalKey>);
static_assert(std::is_trivially_copyable_v<CellLogicalKey>);
static_assert(!HasPoolIndex<CellLogicalKey>);
static_assert(!HasActiveIndex<CellLogicalKey>);
static_assert(!HasBlockUid<CellLogicalKey>);
static_assert(!HasStorageGeneration<CellLogicalKey>);
static_assert(!HasPointer<CellLogicalKey>);

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message)
{
    if (!condition) fail(message);
}

template<class Exception, class Operation>
void require_throws(Operation&& operation, const std::string& message)
{
    try {
        std::forward<Operation>(operation)();
    } catch (const Exception&) {
        return;
    } catch (...) {
        fail(message + " (wrong exception type)");
    }
    fail(message + " (no exception)");
}

void test_zero_is_invalid()
{
    require(!amr::is_valid(BlockUid{}), "zero BlockUid was accepted");
    require(!amr::is_valid(TopologyEpoch{}), "zero TopologyEpoch was accepted");
    require(!amr::is_valid(BlockHandle{}), "zero BlockHandle was accepted");
    require(!amr::is_valid(BlockHandle{BlockUid{1}, TopologyEpoch{}}),
            "BlockHandle with zero epoch was accepted");
    require(!amr::is_valid(BlockHandle{BlockUid{}, TopologyEpoch{1}}),
            "BlockHandle with zero UID was accepted");

    require_throws<std::invalid_argument>(
        [] { static_cast<void>(BlockIdentityAuthority{0, 1}); },
        "identity authority accepted a zero next UID");
    require_throws<std::invalid_argument>(
        [] { static_cast<void>(BlockIdentityAuthority{1, 0}); },
        "identity authority accepted a zero current epoch");

    BlockIdentityAuthority authority;
    require_throws<std::invalid_argument>(
        [&] { static_cast<void>(authority.bind(BlockUid{})); },
        "identity authority bound a zero UID");
}

void test_monotonic_uid_and_epoch()
{
    BlockIdentityAuthority authority{41, 7};
    const BlockUid first = authority.issue_uid();
    const BlockUid second = authority.issue_uid();
    require(first == BlockUid{41} && second == BlockUid{42},
            "UID issuance was not monotonic");
    require(authority.current_epoch() == TopologyEpoch{7},
            "UID issuance changed the topology epoch");

    const BlockHandle before = authority.bind(first);
    require(before == BlockHandle{BlockUid{41}, TopologyEpoch{7}},
            "bind did not use the current topology epoch");
    require(authority.accepts(before), "current handle was rejected");

    const TopologyEpoch committed = authority.commit_topology();
    require(committed == TopologyEpoch{8}, "topology epoch was not incremented");
    require(!authority.accepts(before), "stale epoch was accepted after topology commit");
    const BlockHandle rebound = authority.bind(first);
    require(rebound != before && before < rebound,
            "BlockHandle equality/order omitted the topology epoch");
    require(authority.accepts(rebound),
            "rebinding a live UID to the current epoch was rejected");
}

void test_counter_exhaustion_never_wraps_to_zero()
{
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    BlockIdentityAuthority uid_authority{maximum - 1, 1};
    require(uid_authority.issue_uid() == BlockUid{maximum - 1},
            "penultimate UID was not issued");
    require(uid_authority.issue_uid() == BlockUid{maximum},
            "maximum UID was not issued");
    require_throws<std::overflow_error>(
        [&] { static_cast<void>(uid_authority.issue_uid()); },
        "exhausted UID counter wrapped or reissued an identity");

    BlockIdentityAuthority epoch_authority{1, maximum - 1};
    require(epoch_authority.commit_topology() == TopologyEpoch{maximum},
            "maximum topology epoch was not committed");
    require_throws<std::overflow_error>(
        [&] { static_cast<void>(epoch_authority.commit_topology()); },
        "exhausted topology epoch wrapped to zero");
    require(epoch_authority.current_epoch() == TopologyEpoch{maximum},
            "failed topology commit changed the current epoch");
}

void test_root_derivation_rejects_invalid_shift_levels()
{
    require(!amr::root_logical_key_from_leaf(-1, 1, 2, 3),
            "negative leaf level produced a root key");
    require(!amr::root_logical_key_from_leaf(32, 1, 2, 3),
            "shift-width leaf level produced a root key");
    require(!amr::root_logical_key_from_leaf(64, 1, 2, 3),
            "oversized leaf level produced a root key");

    const auto deepest = amr::root_logical_key_from_leaf(
        31, std::uint32_t{1} << 31, std::uint32_t{1} << 31, 0);
    require(deepest && *deepest == RootLogicalKey{1, 1, 0},
            "largest valid leaf shift did not derive the expected root");
}

void test_multi_root_keys_do_not_collide()
{
    const auto root_a = amr::root_logical_key_from_leaf(2, 4, 8, 12);
    const auto root_b = amr::root_logical_key_from_leaf(2, 8, 8, 12);
    require(root_a && *root_a == RootLogicalKey{1, 2, 3},
            "first leaf did not reconstruct its root coordinate");
    require(root_b && *root_b == RootLogicalKey{2, 2, 3},
            "second leaf did not reconstruct its root coordinate");

    const CellLogicalKey cell_a{*root_a, 2, 99, 3, 4, 5, 1};
    const CellLogicalKey cell_b{*root_b, 2, 99, 3, 4, 5, 1};
    require(cell_a != cell_b, "different root blocks produced colliding cell keys");
    require((cell_a < cell_b) != (cell_b < cell_a),
            "different root blocks lack a total deterministic order");
}

void test_pool_slot_reuse_cannot_resurrect_an_old_handle()
{
    BlockIdentityAuthority authority;
    constexpr int reused_pool_slot = 7;
    std::map<BlockHandle, int> store;

    const BlockHandle old_handle = authority.bind(authority.issue_uid());
    store.emplace(old_handle, reused_pool_slot);
    require(store.erase(old_handle) == 1, "test model did not retire the old handle");

    const BlockHandle new_handle = authority.bind(authority.issue_uid());
    require(new_handle.uid != old_handle.uid,
            "pool-slot reuse caused a UID to be reissued");
    store.emplace(new_handle, reused_pool_slot);
    require(store.find(old_handle) == store.end(),
            "old handle resolved to a new block after pool-index ABA reuse");
    require(store.at(new_handle) == reused_pool_slot,
            "new handle did not resolve to the reused pool slot");
}

CellLogicalKey reconstruct_checkpoint_cell(
    int level, std::uint32_t x, std::uint32_t y, std::uint32_t z,
    int logical_i, int logical_j, int logical_k, int component)
{
    const auto root = amr::root_logical_key_from_leaf(level, x, y, z);
    require(root.has_value(), "valid checkpoint leaf could not reconstruct its root key");
    return CellLogicalKey{*root, level, amr::encodeMorton(level, x, y, z),
                          logical_i, logical_j, logical_k, component};
}

std::vector<CellLogicalKey> sorted_checkpoint_keys(const std::vector<std::array<int, 8>>& records)
{
    std::vector<CellLogicalKey> keys;
    keys.reserve(records.size());
    for (const auto& record : records) {
        keys.push_back(reconstruct_checkpoint_cell(
            record[0], static_cast<std::uint32_t>(record[1]),
            static_cast<std::uint32_t>(record[2]),
            static_cast<std::uint32_t>(record[3]),
            record[4], record[5], record[6], record[7]));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void test_traversal_and_checkpoint_reconstruction_are_stable()
{
    const std::vector<std::array<int, 8>> checkpoint_order_a{
        {2, 9, 4, 0, 7, 0, 0, 0},
        {1, 2, 0, 0, 2, 0, 0, 1},
        {2, 5, 4, 0, 1, 0, 0, 0},
    };
    const std::vector<std::array<int, 8>> checkpoint_order_b{
        checkpoint_order_a[2], checkpoint_order_a[0], checkpoint_order_a[1]};

    const auto before_restart = sorted_checkpoint_keys(checkpoint_order_a);
    const auto after_restart = sorted_checkpoint_keys(checkpoint_order_b);
    require(before_restart == after_restart,
            "checkpoint reconstruction changed cell keys with traversal order");

    const CellLogicalKey expected_first = reconstruct_checkpoint_cell(1, 2, 0, 0, 2, 0, 0, 1);
    require(before_restart.front() == expected_first,
            "cell-key ordering is not the hand-checked logical order");

    std::array<CellLogicalKey, 3> traversal{
        before_restart[2], before_restart[0], before_restart[1]};
    const CellLogicalKey selected = *std::min_element(traversal.begin(), traversal.end());
    std::reverse(traversal.begin(), traversal.end());
    require(*std::min_element(traversal.begin(), traversal.end()) == selected,
            "deterministic cell tie changed with block traversal permutation");

    BlockIdentityAuthority first_run{10, 3};
    BlockIdentityAuthority restarted_run{500, 20};
    const BlockUid transient_before = first_run.issue_uid();
    const BlockUid transient_after = restarted_run.issue_uid();
    require(transient_before != transient_after,
            "test setup did not vary transient BlockUid across restart");
    require(before_restart.front() == after_restart.front(),
            "transient BlockUid changed the restart-stable tie key");
}

} // namespace

int main()
{
    try {
        test_zero_is_invalid();
        test_monotonic_uid_and_epoch();
        test_counter_exhaustion_never_wraps_to_zero();
        test_root_derivation_rejects_invalid_shift_levels();
        test_pool_slot_reuse_cannot_resurrect_an_old_handle();
        test_traversal_and_checkpoint_reconstruction_are_stable();
        test_multi_root_keys_do_not_collide();
        std::cout << "block handle contract: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "block handle contract: FAIL: " << error.what() << '\n';
        return 1;
    }
}
