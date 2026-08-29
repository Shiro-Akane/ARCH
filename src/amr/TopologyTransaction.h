/**
 * @file TopologyTransaction.h
 * @brief Move-only transaction guard for staged AMR topology publication.
 */

#pragma once

#include "AmrTransferPlans.h"

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace amr {

enum class TopologyTransactionState : std::uint8_t {
    Prepared = 0,
    Migrating = 1,
    ReadyToCommit = 2,
    Committed = 3,
    Aborted = 4,
};

class TopologyTransaction {
public:
    TopologyTransaction(std::uint64_t transaction_id,
                        TopologyEpoch from_epoch,
                        TopologyEpoch to_epoch)
        : scope_{transaction_id, from_epoch, to_epoch}
    {
        validate_scope(scope_);
    }

    TopologyTransaction(const TopologyTransaction&) = delete;
    TopologyTransaction& operator=(const TopologyTransaction&) = delete;
    TopologyTransaction& operator=(TopologyTransaction&&) = delete;

    TopologyTransaction(TopologyTransaction&& other)
    {
        other.require_idle();
        scope_ = other.scope_;
        state_ = other.state_;
        commit_attempted_ = other.commit_attempted_;
        valid_ = true;
        other.valid_ = false;
        other.commit_attempted_ = true;
        other.state_ = TopologyTransactionState::Aborted;
        other.scope_ = {};
    }

    const AmrPlanScope& scope() const
    {
        require_usable();
        return scope_;
    }

    TopologyTransactionState state() const
    {
        require_usable();
        return state_;
    }

    void begin_migration()
    {
        require_idle();
        if (state_ != TopologyTransactionState::Prepared)
            throw std::logic_error(
                "topology migration did not begin from Prepared");
        state_ = TopologyTransactionState::Migrating;
    }

    void mark_ready()
    {
        require_idle();
        if (state_ != TopologyTransactionState::Migrating)
            throw std::logic_error(
                "topology transaction is not migrating");
        state_ = TopologyTransactionState::ReadyToCommit;
    }

    template <AmrPlanKind Kind>
    void require_scope(const BasicAmrPlan<Kind>& plan) const
    {
        require_usable();
        if (!is_migration_plan(Kind) || plan.scope != scope_)
            throw std::invalid_argument(
                "AMR plan does not belong to the topology transaction");
    }

    template <typename Prepare, typename Finalize, typename Publish>
    void commit_after_success(Prepare&& prepare, Finalize&& finalize,
                              Publish&& publish)
    {
        require_idle();
        if (state_ != TopologyTransactionState::ReadyToCommit)
            throw std::logic_error(
                "topology transaction is not ready to commit");
        if (commit_attempted_)
            throw std::logic_error(
                "topology transaction commit cannot be retried");

        using Prepared = std::invoke_result_t<Prepare, const AmrPlanScope&>;
        static_assert(!std::is_void_v<Prepared>,
                      "topology preparation must return a payload");
        static_assert(
            std::is_invocable_v<Finalize, const AmrPlanScope&, Prepared&>,
            "topology finalizer must accept scope and prepared payload");
        static_assert(std::is_nothrow_invocable_v<Publish, Prepared&&>,
                      "topology publication must be noexcept");

        commit_attempted_ = true;
        CommitGuard guard(*this);

        // All allocation/copy work belongs here, before physical finalization.
        Prepared prepared = std::invoke(
            std::forward<Prepare>(prepare), scope_);
        std::invoke(std::forward<Finalize>(finalize), scope_, prepared);
        // Publication may only consume already prepared state and cannot fail.
        std::invoke(std::forward<Publish>(publish), std::move(prepared));
        state_ = TopologyTransactionState::Committed;
    }

    template <typename Cleanup>
    void abort(Cleanup&& cleanup)
    {
        require_idle();
        static_assert(std::is_nothrow_invocable_v<Cleanup>,
                      "topology abort cleanup must be noexcept");
        if (state_ == TopologyTransactionState::Committed)
            throw std::logic_error("committed topology cannot be aborted");
        if (state_ == TopologyTransactionState::Aborted)
            throw std::logic_error("topology transaction already aborted");
        std::invoke(std::forward<Cleanup>(cleanup));
        state_ = TopologyTransactionState::Aborted;
        commit_attempted_ = true;
    }

private:
    class CommitGuard {
    public:
        explicit CommitGuard(TopologyTransaction& owner) : owner_(owner)
        {
            if (owner_.commit_in_progress_)
                throw std::logic_error(
                    "topology commit is already in progress");
            owner_.commit_in_progress_ = true;
        }

        ~CommitGuard() { owner_.commit_in_progress_ = false; }

        CommitGuard(const CommitGuard&) = delete;
        CommitGuard& operator=(const CommitGuard&) = delete;

    private:
        TopologyTransaction& owner_;
    };

    static void validate_scope(const AmrPlanScope& scope)
    {
        if (scope.transaction_id == 0 || !is_valid(scope.from_epoch)
            || !is_valid(scope.to_epoch)
            || scope.from_epoch == scope.to_epoch)
            throw std::invalid_argument("invalid topology transaction scope");
    }

    void require_usable() const
    {
        if (!valid_)
            throw std::logic_error("moved-from topology transaction");
    }

    void require_idle() const
    {
        require_usable();
        if (commit_in_progress_)
            throw std::logic_error(
                "topology transaction callback is already in progress");
    }

    AmrPlanScope scope_{};
    TopologyTransactionState state_ = TopologyTransactionState::Prepared;
    bool valid_ = true;
    bool commit_attempted_ = false;
    bool commit_in_progress_ = false;
};

static_assert(!std::is_copy_constructible_v<TopologyTransaction>);
static_assert(!std::is_move_assignable_v<TopologyTransaction>);

} // namespace amr
