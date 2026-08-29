#include "amr/TopologyTransaction.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible_v<amr::TopologyTransaction>);
static_assert(!std::is_copy_assignable_v<amr::TopologyTransaction>);
static_assert(std::is_move_constructible_v<amr::TopologyTransaction>);
static_assert(!std::is_move_assignable_v<amr::TopologyTransaction>);

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_rejected(Function&& function, const std::string& message)
{
    bool rejected = false;
    try {
        function();
    } catch (const std::logic_error&) {
        rejected = true;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, message);
}

struct PreparedPayload {
    int value = 0;
};

void test_success_order()
{
    amr::TopologyTransaction transaction(41, {7}, {8});
    expect(transaction.state() == amr::TopologyTransactionState::Prepared,
           "transaction did not begin prepared");
    transaction.begin_migration();
    transaction.mark_ready();

    std::vector<std::string> order;
    order.reserve(3);
    int published = 0;
    transaction.commit_after_success(
        [&](const amr::AmrPlanScope& scope) {
            expect(scope.transaction_id == 41
                       && scope.from_epoch.value == 7
                       && scope.to_epoch.value == 8,
                   "transaction scope drifted");
            order.push_back("prepare");
            return PreparedPayload{17};
        },
        [&](const amr::AmrPlanScope&, PreparedPayload& payload) {
            expect(payload.value == 17, "prepared payload drifted");
            order.push_back("finalize");
        },
        [&](PreparedPayload&& payload) noexcept {
            published = payload.value;
            order.push_back("publish");
        });

    expect(order == std::vector<std::string>{"prepare", "finalize", "publish"},
           "transaction callback order drifted");
    expect(published == 17, "transaction publish payload drifted");
    expect(transaction.state() == amr::TopologyTransactionState::Committed,
           "transaction did not commit");
    expect_rejected([&] { transaction.begin_migration(); },
                    "committed transaction restarted");
    expect_rejected([&] { transaction.abort([]() noexcept {}); },
                    "committed transaction aborted");
}

void test_prepare_failure_requires_abort()
{
    amr::TopologyTransaction transaction(42, {8}, {9});
    transaction.begin_migration();
    transaction.mark_ready();
    int finalized = 0;
    int published = 0;
    bool failed = false;
    try {
        transaction.commit_after_success(
            [](const amr::AmrPlanScope&) -> PreparedPayload {
                throw std::runtime_error("prepare failure");
            },
            [&](const amr::AmrPlanScope&, PreparedPayload&) { ++finalized; },
            [&](PreparedPayload&&) noexcept { ++published; });
    } catch (const std::runtime_error&) {
        failed = true;
    }
    expect(failed && finalized == 0 && published == 0,
           "prepare failure crossed the physical boundary");
    expect_rejected(
        [&] {
            transaction.commit_after_success(
                [](const amr::AmrPlanScope&) { return PreparedPayload{}; },
                [](const amr::AmrPlanScope&, PreparedPayload&) {},
                [](PreparedPayload&&) noexcept {});
        },
        "failed transaction retried");
    int cleaned = 0;
    transaction.abort([&]() noexcept { ++cleaned; });
    expect(cleaned == 1
               && transaction.state() == amr::TopologyTransactionState::Aborted,
           "failed transaction did not abort exactly once");
}

void test_finalize_failure_requires_abort()
{
    amr::TopologyTransaction transaction(43, {9}, {10});
    transaction.begin_migration();
    transaction.mark_ready();
    int finalized = 0;
    int published = 0;
    bool failed = false;
    try {
        transaction.commit_after_success(
            [](const amr::AmrPlanScope&) { return PreparedPayload{3}; },
            [&](const amr::AmrPlanScope&, PreparedPayload&) {
                ++finalized;
                throw std::runtime_error("finalize failure");
            },
            [&](PreparedPayload&&) noexcept { ++published; });
    } catch (const std::runtime_error&) {
        failed = true;
    }
    expect(failed && finalized == 1 && published == 0,
           "finalize failure published staged topology");
    transaction.abort([]() noexcept {});
}

void test_move_poison_and_reentrancy()
{
    amr::TopologyTransaction source(44, {10}, {11});
    amr::TopologyTransaction moved(std::move(source));
    expect_rejected([&] { (void)source.scope(); },
                    "moved-from transaction remained usable");

    moved.begin_migration();
    moved.mark_ready();
    int reentrant_rejections = 0;
    moved.commit_after_success(
        [&](const amr::AmrPlanScope&) {
            try {
                moved.abort([]() noexcept {});
            } catch (const std::logic_error&) {
                ++reentrant_rejections;
            }
            return PreparedPayload{9};
        },
        [](const amr::AmrPlanScope&, PreparedPayload&) {},
        [](PreparedPayload&&) noexcept {});
    expect(reentrant_rejections == 1,
           "reentrant transaction mutation was accepted");
}

void test_scope_validation()
{
    expect_rejected(
        [] { amr::TopologyTransaction invalid(0, {1}, {2}); },
        "zero transaction ID accepted");
    expect_rejected(
        [] { amr::TopologyTransaction invalid(1, {2}, {2}); },
        "equal topology epochs accepted");

    amr::TopologyTransaction transaction(45, {11}, {12});
    auto plan = amr::ProlongationPlan{};
    plan.dimension = 1;
    plan.scope = {46, {11}, {12}};
    expect_rejected([&] { transaction.require_scope(plan); },
                    "wrong migration transaction accepted");
}

} // namespace

int main()
{
    try {
        test_success_order();
        test_prepare_failure_requires_abort();
        test_finalize_failure_requires_abort();
        test_move_poison_and_reentrancy();
        test_scope_validation();
        std::cout << "TOPOLOGY_TRANSACTION_PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
