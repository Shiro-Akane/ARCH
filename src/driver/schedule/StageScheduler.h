/**
 * @file StageScheduler.h
 * @brief Backend-neutral hydro/RKL plans and coherence ordering.
 *
 * Descriptors name stage inputs, outputs and required ghost validity. Scheduler
 * transitions coordinate the residency tracker and backend operations; numerical
 * field storage and its lifetime remain outside the scheduler descriptors.
 * Workflow:
 * 1. Receive a stage descriptor and current state ledger.
 * 2. Order hydro, gravity, burn, diffusion and output transitions.
 * 3. Require completed publications before consuming a state.
 * 4. When explicitly bound, validate completed boundary candidates before
 *    publishing ghost readiness; owner receipts retain their original order.
 * 5. Selected plans configure the actual input/output boundary snapshot from
 *    their stage abscissae; configuring a snapshot never publishes ghost data.
 */

#pragma once
#include <functional>

#include "driver/runtime/StateResidency.h"
#include "physics/boundary/BoundaryFlux.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace arch::scheduler {

enum class HydroMethod : std::uint8_t { Euler, RK2, RK3 };
enum class RklMethod : std::uint8_t { RKL1, RKL2 };

struct StageDescriptor {
    int stage = 0;
    state::StateSlot old_slot = state::StateSlot::Current;
    state::StateSlot input_slot = state::StateSlot::Current;
    state::StateSlot output_slot = state::StateSlot::Next;
    double old_weight = 0.0;
    double update_weight = 1.0;
    double flux_register_weight = 0.0;
    bool input_requires_ghost = true;
    bool refresh_ghost_after = false;
    // Time of the input state relative to the beginning of the Hydro step.
    double input_time_fraction = 0.0;
};

/** Compare the complete prepared stage, keeping source quadrature separate from repair weights. */
inline bool same_stage_descriptor(const StageDescriptor& a,const StageDescriptor& b) noexcept {
    return a.stage==b.stage&&a.old_slot==b.old_slot&&a.input_slot==b.input_slot
        &&a.output_slot==b.output_slot&&a.old_weight==b.old_weight
        &&a.update_weight==b.update_weight&&a.flux_register_weight==b.flux_register_weight
        &&a.input_requires_ghost==b.input_requires_ghost
        &&a.refresh_ghost_after==b.refresh_ghost_after&&a.input_time_fraction==b.input_time_fraction;
}

struct HydroPlan {
    HydroMethod method = HydroMethod::Euler;
    std::vector<StageDescriptor> stages;
    state::SlotRotation final_rotation{};
    bool final_reflux_after_rotation = true;
};

struct RklStageDescriptor {
    int stage = 0;
    state::StateSlot state_n_slot = state::StateSlot::Current;
    state::StateSlot previous_slot = state::StateSlot::Current;
    state::StateSlot older_slot = state::StateSlot::Current;
    state::StateSlot output_slot = state::StateSlot::Scratch;
    bool reflux_before_publish = true;
    bool refresh_ghost_after = true;
};

struct RklPlan {
    RklMethod method = RklMethod::RKL1;
    bool second_order = false;
    std::vector<RklStageDescriptor> stages;
    state::SlotRotation final_rotation{};
};

inline HydroPlan make_hydro_plan(HydroMethod method)
{
    using state::StateSlot;
    switch (method) {
    case HydroMethod::Euler:
        return {method,
                {{1, StateSlot::Current, StateSlot::Current,
                  StateSlot::Next, 0.0, 1.0, 1.0, true, false}},
                {StateSlot::Next, StateSlot::Current, StateSlot::Scratch},
                true};
    case HydroMethod::RK2:
        return {method,
                {{1, StateSlot::Current, StateSlot::Current,
                  StateSlot::Scratch, 0.0, 1.0, 0.5, true, true},
                 {2, StateSlot::Current, StateSlot::Scratch,
                  StateSlot::Next, 0.5, 0.5, 0.5, true, false, 1.0}},
                {StateSlot::Next, StateSlot::Current, StateSlot::Scratch},
                true};
    case HydroMethod::RK3:
        return {method,
                {{1, StateSlot::Current, StateSlot::Current,
                  StateSlot::Scratch, 0.0, 1.0, 1.0 / 6.0, true, true},
                 {2, StateSlot::Current, StateSlot::Scratch,
                  StateSlot::Next, 3.0 / 4.0, 1.0 / 4.0, 1.0 / 6.0,
                  true, true, 1.0},
                 {3, StateSlot::Current, StateSlot::Next,
                  StateSlot::Scratch, 1.0 / 3.0, 2.0 / 3.0,
                  2.0 / 3.0, true, false, 0.5}},
                {StateSlot::Scratch, StateSlot::Next, StateSlot::Current},
                true};
    }
    throw std::invalid_argument("unknown Hydro method");
}

/** Borrow the immutable selected table for physical-time validation.
 * The table is constructed once from the same factory used by execution;
 * there is no second coefficient table and no allocation per timed stage.
 */
inline const HydroPlan& supported_hydro_time_plan(HydroMethod method)
{
    switch (method) {
    case HydroMethod::Euler: {
        static const HydroPlan plan = make_hydro_plan(HydroMethod::Euler);
        return plan;
    }
    case HydroMethod::RK2: {
        static const HydroPlan plan = make_hydro_plan(HydroMethod::RK2);
        return plan;
    }
    case HydroMethod::RK3: {
        static const HydroPlan plan = make_hydro_plan(HydroMethod::RK3);
        return plan;
    }
    }
    throw std::invalid_argument("unknown timed Hydro method");
}

/** Derive one supported Shu–Osher output clock without using field versions.
 * U_out=a*U_old+b*(U_in+dt*L), with the original Current at c_old=0,
 * gives c_out=a*c_old+b*(c_in+1)=b*(c_in+1). Exact table membership rejects
 * a changed old-state clock, coefficient or slot instead of guessing its time.
 */
inline double hydro_output_time_fraction(HydroMethod method,
                                         const StageDescriptor& descriptor)
{
    const auto& supported = supported_hydro_time_plan(method);
    if (descriptor.stage <= 0
        || static_cast<std::size_t>(descriptor.stage) > supported.stages.size()
        || !same_stage_descriptor(descriptor,
            supported.stages[static_cast<std::size_t>(descriptor.stage - 1)]))
        throw std::invalid_argument("unsupported timed Hydro descriptor");
    return descriptor.update_weight * (descriptor.input_time_fraction + 1.0);
}

/** Return one RKL clock abscissa, sharing the same input/output time authority.
 * The clock equation t'=1 in the recurrence gives RKL1
 * c_j=j*(j+1)/(s*(s+1)); RKL2 c_1=4/(3*(s*s+s-2)) and
 * c_j=(j*j+j-2)/(s*s+s-2) for j>=2. Both have c_0=0 and c_s=1.
 * This computes time only; the diffusion owner retains all update coefficients.
 */
inline double rkl_stage_time_fraction(RklMethod method, int stage, int stages)
{
    if ((method != RklMethod::RKL1 && method != RklMethod::RKL2)
        || stages <= 0 || stage < 0 || stage > stages
        || (method == RklMethod::RKL2 && stages < 2))
        throw std::invalid_argument("unsupported timed RKL stage");
    if (stage == 0) return 0.0;
    const bool second = method == RklMethod::RKL2;
    const double count = static_cast<double>(stages);
    const double index = static_cast<double>(stage);
    const double denominator = count * (count + 1.0) - (second ? 2.0 : 0.0);
    return !second ? index * (index + 1.0) / denominator
        : stage == 1 ? 4.0 / (3.0 * denominator)
                     : (index * (index + 1.0) - 2.0) / denominator;
}

inline RklPlan make_rkl_plan(RklMethod method, int stages)
{
    using state::StateSlot;
    if (stages <= 0)
        throw std::invalid_argument("RKL stage count must be positive");
    if (method != RklMethod::RKL1 && method != RklMethod::RKL2)
        throw std::invalid_argument("unknown RKL method");

    RklPlan plan;
    plan.method = method;
    plan.second_order = method == RklMethod::RKL2;
    plan.stages.reserve(static_cast<std::size_t>(stages));
    plan.stages.push_back({1, StateSlot::Current, StateSlot::Current,
                           StateSlot::Current, StateSlot::Scratch, true,
                           true});
    for (int stage = 2; stage <= stages; ++stage) {
        const bool previous_in_scratch = (stage % 2) == 0;
        const StateSlot previous = previous_in_scratch
            ? StateSlot::Scratch : StateSlot::Next;
        const StateSlot older_output = previous_in_scratch
            ? StateSlot::Next : StateSlot::Scratch;
        plan.stages.push_back({stage, StateSlot::Current, previous,
                               older_output, older_output, true, true});
    }
    plan.final_rotation = (stages % 2) == 0
        ? state::SlotRotation{StateSlot::Next, StateSlot::Current,
                              StateSlot::Scratch}
        : state::SlotRotation{StateSlot::Scratch, StateSlot::Next,
                              StateSlot::Current};
    return plan;
}

struct PublicationWitness {
    state::StateVersion version{};
    state::CompletionToken completion{};
};

class MonotonicSchedulerClock {
public:
    explicit constexpr MonotonicSchedulerClock(
        std::uint64_t last_token = 0,
        std::uint64_t last_version = 0) noexcept
        : last_token_(last_token), last_version_(last_version)
    {
    }

    PublicationWitness next_publication()
    {
        if (last_token_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("scheduler completion token exhausted");
        if (last_version_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("scheduler state version exhausted");
        ++last_token_;
        ++last_version_;
        return {{last_version_},
                {last_token_, state::CompletionState::Complete}};
    }

    state::CompletionToken next_completion()
    {
        if (last_token_ == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("scheduler completion token exhausted");
        ++last_token_;
        return {last_token_, state::CompletionState::Complete};
    }

    constexpr std::uint64_t last_token() const noexcept { return last_token_; }
    constexpr std::uint64_t last_version() const noexcept
    {
        return last_version_;
    }

private:
    std::uint64_t last_token_ = 0;
    std::uint64_t last_version_ = 0;
};

struct HydroStagePreparationRequest {
    HydroMethod method;
    const StageDescriptor& descriptor;
    std::span<const amr::BlockHandle> handles;
    state::ExecutionSide side;
    const state::StateResidencyLedger& ledger;
    double input_time;
    double step_dt;
};

// Called once for the whole domain, after all inputs have been validated and
// before any block executor starts. The service must return completed work;
// neither it nor a worker may retain borrowed request spans after the stage.
class HydroStagePreparation {
public:
    virtual ~HydroStagePreparation() = default;
    virtual state::CompletionToken prepare(const HydroStagePreparationRequest&) = 0;
    virtual void invalidate() const {}
    /** Existing services are not implicitly accepted as atomic source journals. */
    virtual bool supports_host_macro_step_journal() const noexcept { return false; }
    /** Candidate hook: fallible private journal allocation; never writes accepted fluid/owners. */
    virtual void begin_macro_step() {}
    /** Candidate hook: validate complete prepared frame/consumption, then append a tentative receipt. */
    virtual void accept(const StageDescriptor&) {}
    /** Candidate hooks: preallocated, noexcept publication or discard of only this owner's journal. */
    virtual void commit_macro_step() noexcept {}
    virtual void discard_macro_step() noexcept {}
};

struct StageExecutionContext {
    state::ExecutionSide side;
    state::StateResidencyLedger& ledger;
    MonotonicSchedulerClock& clock;
    HydroStagePreparation* hydro_preparation = nullptr;
    std::function<void(const StageDescriptor&)> hydro_acceptance;
    double step_start_time = 0.0;
    double step_dt = 0.0;
    std::function<void(state::StateSlot, double, arch::boundary::BoundaryPurpose)> physical_boundary_preparation;
    double boundary_start_time = 0.0, boundary_step_dt = 0.0;
    // Optional physical-surface observation of the already-computed face flux.
    // Empty by default; only callback runs configure them. Accounting never
    // changes the stage descriptors or the coefficient recurrence.
    std::function<void(const StageDescriptor&)> hydro_flux_capture_begin;
    std::function<void(const StageDescriptor&)> hydro_flux_capture_accept;
    std::function<void(const RklStageDescriptor&, const RklPlan&)> rkl_flux_capture_begin;
    std::function<void(const RklStageDescriptor&, const RklPlan&)> rkl_flux_capture_accept;
    std::function<void(const RklStageDescriptor&)> rkl_acceptance;
    // Optional candidate-state gate after real whole-domain boundary work and
    // before ghost readiness. Empty preserves the existing execution sequence.
    std::function<void(const StageExecutionContext&, state::StateSlot,
                       state::StateVersion)> post_boundary_acceptance;
    // Configure only the actual physical time/purpose snapshot. The selected
    // plan calls this before the real boundary callback; it never fills arrays
    // or publishes readiness, unlike physical_boundary_preparation.
    std::function<void(double, arch::boundary::BoundaryPurpose)> configure_boundary_context;
};

static_assert(std::is_same_v<decltype(StageExecutionContext::side),
                             state::ExecutionSide>);

struct StageBinding {
    StageExecutionContext& context;
    std::span<const amr::BlockHandle> handles;
};

namespace detail {

/** Fail closed on an unrepresentable physical interval before stage writes. */
inline void require_boundary_interval(double start, double interval)
{
    if (!std::isfinite(start) || !std::isfinite(interval) || !(interval > 0.0)
        || !std::isfinite(start + interval))
        throw std::invalid_argument("boundary interval must be finite and positive");
}

/** Require the one supported Hydro plan before configuring real snapshots. */
inline void require_timed_hydro_plan(const HydroPlan& plan)
{
    const auto& supported = supported_hydro_time_plan(plan.method);
    if (plan.stages.size() != supported.stages.size()
        || plan.final_rotation.current_from != supported.final_rotation.current_from
        || plan.final_rotation.next_from != supported.final_rotation.next_from
        || plan.final_rotation.scratch_from != supported.final_rotation.scratch_from
        || plan.final_reflux_after_rotation != supported.final_reflux_after_rotation)
        throw std::invalid_argument("unsupported timed Hydro plan");
    for (std::size_t i = 0; i < plan.stages.size(); ++i) {
        if (!same_stage_descriptor(plan.stages[i], supported.stages[i]))
            throw std::invalid_argument("unsupported timed Hydro descriptor order");
        (void)hydro_output_time_fraction(plan.method, plan.stages[i]);
    }
}
inline thread_local StageBinding* active_stage_binding = nullptr;
} // namespace detail

class ScopedStageBinding {
public:
    explicit ScopedStageBinding(
        StageExecutionContext& context,
        std::span<const amr::BlockHandle> handles)
        : binding_{context, handles}
    {
        if (handles.empty())
            throw std::invalid_argument("stage binding requires active handles");
        if (detail::active_stage_binding != nullptr)
            throw std::logic_error("stage binding is already active");
        detail::active_stage_binding = &binding_;
    }

    ScopedStageBinding(const ScopedStageBinding&) = delete;
    ScopedStageBinding& operator=(const ScopedStageBinding&) = delete;
    ScopedStageBinding(ScopedStageBinding&&) = delete;
    ScopedStageBinding& operator=(ScopedStageBinding&&) = delete;

    ~ScopedStageBinding()
    {
        if (detail::active_stage_binding == &binding_)
            detail::active_stage_binding = nullptr;
    }

private:
    StageBinding binding_;
};

inline const StageBinding& current_stage_binding()
{
    if (detail::active_stage_binding == nullptr)
        throw std::logic_error("production stage scheduler is not bound");
    return *detail::active_stage_binding;
}

struct StageExecutionResult {
    state::StateVersion version{};
    state::CompletionToken interior_completion{};
    state::CompletionToken ghost_completion{};
};

struct HydroExecutionResult {
    std::vector<StageExecutionResult> stages;
    PublicationWitness final_reflux{};
};

struct RklExecutionResult {
    std::vector<StageExecutionResult> stages;
};

namespace detail {

inline void require_settled_destination(const state::SlotCoherence& slot)
{
    if (slot.interior.pending_transfer
            != state::PendingTransferPhase::None
        || slot.ghost.pending_transfer != state::PendingTransferPhase::None
        || state::is_pending(slot.interior.completion)
        || state::is_pending(slot.ghost.completion)) {
        throw std::logic_error("stage destination has pending transfer state");
    }
}

inline void validate_rotation(state::SlotRotation rotation)
{
    const auto valid = [](state::StateSlot slot) {
        return slot == state::StateSlot::Current
            || slot == state::StateSlot::Next
            || slot == state::StateSlot::Scratch;
    };
    if (!valid(rotation.current_from) || !valid(rotation.next_from)
        || !valid(rotation.scratch_from)
        || rotation.current_from == rotation.next_from
        || rotation.current_from == rotation.scratch_from
        || rotation.next_from == rotation.scratch_from) {
        throw std::logic_error("scheduler slot rotation must be a permutation");
    }
}

template <typename HandleRange>
void validate_stage_inputs(StageExecutionContext& context,
                           const HandleRange& handles,
                           const StageDescriptor& descriptor)
{
    bool any = false;
    for (const amr::BlockHandle handle : handles) {
        any = true;
        const state::StateKey input{handle, descriptor.input_slot};
        const state::SlotCoherence input_state = context.ledger.inspect(input);
        context.ledger.require_readable(
            input,
            {context.side, input_state.interior.version, true,
             descriptor.input_requires_ghost});
        if (descriptor.old_slot != descriptor.input_slot) {
            const state::StateKey old{handle, descriptor.old_slot};
            const state::SlotCoherence old_state = context.ledger.inspect(old);
            context.ledger.require_readable(
                old, {context.side, old_state.interior.version, true, false});
        }
        require_settled_destination(
            context.ledger.inspect({handle, descriptor.output_slot}));
    }
    if (!any) throw std::invalid_argument("stage requires at least one block");
}

template <typename HandleRange>
void publish_interior_batch(StageExecutionContext& context,
                            const HandleRange& handles,
                            state::StateSlot output,
                            PublicationWitness witness)
{
    for (const amr::BlockHandle handle : handles) {
        const state::SlotCoherence before =
            context.ledger.inspect({handle, output});
        if (before.interior.residency != state::StateResidency::Invalid
            && witness.version <= before.interior.version) {
            throw std::logic_error("stage version does not advance destination");
        }
    }
    for (const amr::BlockHandle handle : handles) {
        context.ledger.publish_interior({handle, output}, context.side,
                                        witness.version,
                                        witness.completion);
    }
}

template <typename HandleRange>
void publish_ghost_batch(StageExecutionContext& context,
                         const HandleRange& handles,
                         state::StateSlot output,
                         state::StateVersion version,
                         state::CompletionToken completion)
{
    for (const amr::BlockHandle handle : handles) {
        context.ledger.publish_ghost({handle, output}, context.side, version,
                                     completion);
    }
}

/** Execute actual boundary work, check its token, gate the candidate, then publish ghosts. */
template <typename HandleRange, typename Boundary>
state::CompletionToken execute_completed_boundary(
    StageExecutionContext& context, const HandleRange& handles,
    state::StateSlot slot, state::StateVersion version, Boundary&& boundary)
{
    const state::CompletionToken token = context.clock.next_completion();
    const state::CompletionToken completed =
        std::forward<Boundary>(boundary)(slot, version, token);
    if (!state::is_complete(completed) || completed.value != token.value)
        throw std::logic_error(
            "boundary completion does not match scheduler token");
    if (context.post_boundary_acceptance)
        context.post_boundary_acceptance(context, slot, version);
    publish_ghost_batch(context, handles, slot, version, completed);
    return completed;
}

} // namespace detail

struct NoStagePreparation {
    void operator()(const StageDescriptor&) const noexcept {}
};

template <typename HandleRange, typename Executor, typename BeforePublish,
          typename Boundary, typename Preparation = NoStagePreparation>
StageExecutionResult execute_stage(StageExecutionContext& context,
                                   const HandleRange& handles,
                                   const StageDescriptor& descriptor,
                                   Executor&& executor,
                                   BeforePublish&& before_publish,
                                   Boundary&& boundary,
                                   Preparation&& prepare = {})
{
    detail::validate_stage_inputs(context, handles, descriptor);
    std::forward<Preparation>(prepare)(descriptor);
    const PublicationWitness witness = context.clock.next_publication();
    const state::CompletionToken completed =
        std::forward<Executor>(executor)(descriptor, witness.completion);
    if (!state::is_complete(completed)
        || completed.value != witness.completion.value) {
        throw std::logic_error("executor completion does not match stage token");
    }
    const state::CompletionToken after_execution =
        std::forward<BeforePublish>(before_publish)(descriptor, completed);
    if (!state::is_complete(after_execution)
        || after_execution.value != witness.completion.value) {
        throw std::logic_error(
            "before-publication completion does not match stage token");
    }

    detail::publish_interior_batch(context, handles, descriptor.output_slot,
                                   witness);
    StageExecutionResult result{witness.version, witness.completion, {}};
    if (descriptor.refresh_ghost_after || context.post_boundary_acceptance) {
        result.ghost_completion = detail::execute_completed_boundary(
            context, handles, descriptor.output_slot, witness.version,
            std::forward<Boundary>(boundary));
    }
    return result;
}

template <typename HandleRange, typename Executor, typename Boundary>
StageExecutionResult execute_stage(StageExecutionContext& context,
                                   const HandleRange& handles,
                                   const StageDescriptor& descriptor,
                                   Executor&& executor, Boundary&& boundary)
{
    return execute_stage(
        context, handles, descriptor, std::forward<Executor>(executor),
        [](const StageDescriptor&, state::CompletionToken token) {
            return token;
        },
        std::forward<Boundary>(boundary));
}

template <typename HandleRange, typename Executor, typename Reflux,
          typename Boundary>
StageExecutionResult execute_rkl_stage(
    StageExecutionContext& context, const HandleRange& handles,
    const RklStageDescriptor& descriptor,
    Executor&& executor, Reflux&& reflux, Boundary&& boundary)
{
    for (const amr::BlockHandle handle : handles) {
        const state::StateKey older{handle, descriptor.older_slot};
        const state::SlotCoherence older_state = context.ledger.inspect(older);
        context.ledger.require_readable(
            older,
            {context.side, older_state.interior.version, true, false});
    }
    const StageDescriptor access{
        descriptor.stage, descriptor.state_n_slot,
        descriptor.previous_slot, descriptor.output_slot,
        0.0, 0.0, 0.0, true, descriptor.refresh_ghost_after};
    return execute_stage(
        context, handles, access,
        [&](const StageDescriptor&, state::CompletionToken token) {
            return std::forward<Executor>(executor)(descriptor, token);
        },
        [&](const StageDescriptor&, state::CompletionToken token) {
            const auto completed = descriptor.reflux_before_publish
                ? std::forward<Reflux>(reflux)(descriptor, token) : token;
            if (state::is_complete(completed) && completed.value == token.value
                && context.rkl_acceptance) context.rkl_acceptance(descriptor);
            return completed;
        },
        std::forward<Boundary>(boundary));
}

template <typename HandleRange, typename Executor, typename Boundary>
StageExecutionResult execute_rkl_stage(
    StageExecutionContext& context, const HandleRange& handles,
    const RklStageDescriptor& descriptor,
    Executor&& executor, Boundary&& boundary)
{
    return execute_rkl_stage(
        context, handles, descriptor, std::forward<Executor>(executor),
        [](const RklStageDescriptor&, state::CompletionToken token) {
            return token;
        },
        std::forward<Boundary>(boundary));
}

template <typename HandleRange, typename PhysicalRotation>
void rotate_slots(StageExecutionContext& context,
                  const HandleRange& handles,
                  state::SlotRotation rotation,
                  PhysicalRotation&& physical_rotation)
{
    detail::validate_rotation(rotation);
    bool any = false;
    for (const amr::BlockHandle handle : handles) {
        any = true;
        context.ledger.quiesce(handle);
    }
    if (!any) throw std::invalid_argument("rotation requires at least one block");

    std::forward<PhysicalRotation>(physical_rotation)();
    for (const amr::BlockHandle handle : handles)
        context.ledger.rotate_slots(handle, rotation);
}

template <typename HandleRange, typename Executor, typename Boundary,
          typename PhysicalRotation, typename Reflux>
HydroExecutionResult execute_hydro_plan(
    StageExecutionContext& context, const HandleRange& handles,
    const HydroPlan& plan, Executor&& executor, Boundary&& boundary,
    PhysicalRotation&& physical_rotation, Reflux&& reflux)
{
    if (plan.stages.empty())
        throw std::invalid_argument("Hydro plan requires at least one stage");
    if (context.configure_boundary_context) {
        detail::require_boundary_interval(context.step_start_time, context.step_dt);
        detail::require_timed_hydro_plan(plan);
    }

    HydroExecutionResult result;
    result.stages.reserve(plan.stages.size());
    for (const StageDescriptor& descriptor : plan.stages) {
        if (context.configure_boundary_context)
            context.configure_boundary_context(
                context.step_start_time + descriptor.input_time_fraction * context.step_dt,
                arch::boundary::BoundaryPurpose::Hydro);
        if (context.physical_boundary_preparation)
            context.physical_boundary_preparation(descriptor.input_slot,
                context.step_start_time + descriptor.input_time_fraction * context.step_dt,
                arch::boundary::BoundaryPurpose::Hydro);
        if (context.hydro_flux_capture_begin) context.hydro_flux_capture_begin(descriptor);
        result.stages.push_back(execute_stage(
            context, handles, descriptor, executor,
            [&](const StageDescriptor& stage, state::CompletionToken token) {
                if (context.hydro_acceptance) context.hydro_acceptance(stage);
                return token;
            },
            [&](state::StateSlot slot, state::StateVersion version,
                state::CompletionToken token) {
                if (context.configure_boundary_context)
                    context.configure_boundary_context(context.step_start_time
                        + hydro_output_time_fraction(plan.method, descriptor) * context.step_dt,
                        arch::boundary::BoundaryPurpose::Hydro);
                return boundary(slot, version, token);
            }, [&](const StageDescriptor& input) {
                if (!context.hydro_preparation) return;
                const auto completed = context.hydro_preparation->prepare({
                    plan.method, input, std::span<const amr::BlockHandle>(handles),
                    context.side, context.ledger,
                    context.step_start_time + input.input_time_fraction * context.step_dt,
                    context.step_dt});
                if (!state::is_complete(completed))
                    throw std::logic_error("Hydro preparation did not complete");
            }));
        if (context.hydro_flux_capture_accept) context.hydro_flux_capture_accept(descriptor);
    }
    rotate_slots(context, handles, plan.final_rotation,
                 std::forward<PhysicalRotation>(physical_rotation));

    if (plan.final_reflux_after_rotation) {
        const PublicationWitness witness = context.clock.next_publication();
        const state::CompletionToken completed = std::forward<Reflux>(reflux)(
            plan, state::StateSlot::Current, witness.completion);
        if (!state::is_complete(completed)
            || completed.value != witness.completion.value) {
            throw std::logic_error(
                "Hydro reflux completion does not match scheduler token");
        }
        detail::publish_interior_batch(
            context, handles, state::StateSlot::Current, witness);
        result.final_reflux = witness;
        if (context.post_boundary_acceptance)
            (void)detail::execute_completed_boundary(
                context, handles, state::StateSlot::Current, witness.version,
                [&](state::StateSlot slot, state::StateVersion version,
                    state::CompletionToken token) {
                    // Reflux corrects the completed macro-state; it does not
                    // advance an additional interval or change its endpoint.
                    if (context.configure_boundary_context)
                        context.configure_boundary_context(
                            context.step_start_time + context.step_dt,
                            arch::boundary::BoundaryPurpose::Hydro);
                    return boundary(slot, version, token);
                });
    }
    return result;
}

template <typename HandleRange, typename Executor, typename Reflux,
          typename Boundary, typename PhysicalRotation>
RklExecutionResult execute_rkl_plan(
    StageExecutionContext& context, const HandleRange& handles,
    const RklPlan& plan, Executor&& executor, Reflux&& reflux,
    Boundary&& boundary, PhysicalRotation&& physical_rotation)
{
    if (plan.stages.empty())
        throw std::invalid_argument("RKL plan requires at least one stage");
    if (context.configure_boundary_context) {
        detail::require_boundary_interval(context.boundary_start_time, context.boundary_step_dt);
        if (plan.stages.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())
            || plan.second_order != (plan.method == RklMethod::RKL2))
            throw std::invalid_argument("unsupported timed RKL plan");
        const int count = static_cast<int>(plan.stages.size());
        for (int i = 0; i < count; ++i) {
            if (plan.stages[static_cast<std::size_t>(i)].stage != i + 1)
                throw std::invalid_argument("unsupported timed RKL descriptor order");
            (void)rkl_stage_time_fraction(plan.method, i + 1, count);
        }
    }

    RklExecutionResult result;
    result.stages.reserve(plan.stages.size());
    for (const RklStageDescriptor& descriptor : plan.stages) {
        if (context.configure_boundary_context || context.physical_boundary_preparation) {
            const int j = descriptor.stage - 1, stages = static_cast<int>(plan.stages.size());
            const double time = context.boundary_start_time
                + rkl_stage_time_fraction(plan.method, j, stages) * context.boundary_step_dt;
            if (context.configure_boundary_context)
                context.configure_boundary_context(time, arch::boundary::BoundaryPurpose::Diffusion);
            if (context.physical_boundary_preparation)
                context.physical_boundary_preparation(
                    descriptor.stage == 1 ? descriptor.state_n_slot : descriptor.previous_slot,
                    time, arch::boundary::BoundaryPurpose::Diffusion);
        }
        if (context.rkl_flux_capture_begin) context.rkl_flux_capture_begin(descriptor, plan);
        result.stages.push_back(execute_rkl_stage(
            context, handles, descriptor, executor, reflux,
            [&](state::StateSlot slot, state::StateVersion version,
                state::CompletionToken token) {
                if (context.configure_boundary_context)
                    context.configure_boundary_context(context.boundary_start_time
                        + rkl_stage_time_fraction(plan.method, descriptor.stage,
                            static_cast<int>(plan.stages.size())) * context.boundary_step_dt,
                        arch::boundary::BoundaryPurpose::Diffusion);
                return boundary(slot, version, token);
            }));
        if (context.rkl_flux_capture_accept) context.rkl_flux_capture_accept(descriptor, plan);
    }
    rotate_slots(context, handles, plan.final_rotation,
                 std::forward<PhysicalRotation>(physical_rotation));
    return result;
}

template <typename HandleRange, typename Executor>
PublicationWitness execute_in_place_stage(
    StageExecutionContext& context, const HandleRange& handles,
    state::StateSlot slot, Executor&& executor)
{
    bool any = false;
    for (const amr::BlockHandle handle : handles) {
        const state::StateKey key{handle, slot};
        const state::SlotCoherence before = context.ledger.inspect(key);
        context.ledger.require_readable(
            key, {context.side, before.interior.version, true, false});
        detail::require_settled_destination(before);
        any = true;
    }
    if (!any)
        throw std::invalid_argument("in-place stage requires at least one block");

    const PublicationWitness witness = context.clock.next_publication();
    const state::CompletionToken completed =
        std::forward<Executor>(executor)(witness.completion);
    if (!state::is_complete(completed)
        || completed.value != witness.completion.value) {
        throw std::logic_error(
            "in-place completion does not match scheduler token");
    }
    detail::publish_interior_batch(context, handles, slot, witness);
    return witness;
}

template <typename HandleRange, typename Executor, typename Boundary,
          typename PhysicalRotation, typename Reflux>
HydroExecutionResult execute_hydro_lane(
    StageExecutionContext& context, const HandleRange& handles,
    HydroMethod method, Executor&& executor, Boundary&& boundary,
    PhysicalRotation&& physical_rotation, Reflux&& reflux)
{
    const HydroPlan plan = make_hydro_plan(method);
    return execute_hydro_plan(
        context, handles, plan, std::forward<Executor>(executor),
        std::forward<Boundary>(boundary),
        [&] { physical_rotation(plan.final_rotation); },
        std::forward<Reflux>(reflux));
}

template <typename HandleRange, typename Executor, typename Boundary,
          typename PhysicalRotation, typename Reflux>
HydroExecutionResult execute_euler_lane(
    StageExecutionContext& context, const HandleRange& handles,
    Executor&& executor, Boundary&& boundary,
    PhysicalRotation&& physical_rotation, Reflux&& reflux)
{
    return execute_hydro_lane(
        context, handles, HydroMethod::Euler,
        std::forward<Executor>(executor), std::forward<Boundary>(boundary),
        std::forward<PhysicalRotation>(physical_rotation),
        std::forward<Reflux>(reflux));
}

template <typename HandleRange, typename Executor, typename Boundary,
          typename PhysicalRotation, typename Reflux>
HydroExecutionResult execute_rk2_lane(
    StageExecutionContext& context, const HandleRange& handles,
    Executor&& executor, Boundary&& boundary,
    PhysicalRotation&& physical_rotation, Reflux&& reflux)
{
    return execute_hydro_lane(
        context, handles, HydroMethod::RK2,
        std::forward<Executor>(executor), std::forward<Boundary>(boundary),
        std::forward<PhysicalRotation>(physical_rotation),
        std::forward<Reflux>(reflux));
}

template <typename HandleRange, typename Executor, typename Boundary,
          typename PhysicalRotation, typename Reflux>
HydroExecutionResult execute_rk3_lane(
    StageExecutionContext& context, const HandleRange& handles,
    Executor&& executor, Boundary&& boundary,
    PhysicalRotation&& physical_rotation, Reflux&& reflux)
{
    return execute_hydro_lane(
        context, handles, HydroMethod::RK3,
        std::forward<Executor>(executor), std::forward<Boundary>(boundary),
        std::forward<PhysicalRotation>(physical_rotation),
        std::forward<Reflux>(reflux));
}

template <typename HandleRange, typename Executor, typename Reflux,
          typename Boundary, typename PhysicalRotation>
RklExecutionResult execute_rkl_lane(
    StageExecutionContext& context, const HandleRange& handles,
    RklMethod method, int stages, Executor&& executor, Reflux&& reflux,
    Boundary&& boundary, PhysicalRotation&& physical_rotation)
{
    const RklPlan plan = make_rkl_plan(method, stages);
    return execute_rkl_plan(
        context, handles, plan,
        [&](const RklStageDescriptor& descriptor,
            state::CompletionToken token) {
            return executor(plan, descriptor, token);
        },
        [&](const RklStageDescriptor& descriptor,
            state::CompletionToken token) {
            return reflux(plan, descriptor, token);
        },
        std::forward<Boundary>(boundary),
        [&] { physical_rotation(plan.final_rotation); });
}

template <typename HandleRange, typename Executor, typename Reflux,
          typename Boundary, typename PhysicalRotation>
RklExecutionResult execute_single_rkl1_lane(
    StageExecutionContext& context, const HandleRange& handles, int stages,
    Executor&& executor, Reflux&& reflux, Boundary&& boundary,
    PhysicalRotation&& physical_rotation)
{
    return execute_rkl_lane(
        context, handles, RklMethod::RKL1, stages,
        std::forward<Executor>(executor), std::forward<Reflux>(reflux),
        std::forward<Boundary>(boundary),
        std::forward<PhysicalRotation>(physical_rotation));
}

template <typename HandleRange, typename Executor, typename Reflux,
          typename Boundary, typename PhysicalRotation>
RklExecutionResult execute_single_rkl2_lane(
    StageExecutionContext& context, const HandleRange& handles, int stages,
    Executor&& executor, Reflux&& reflux, Boundary&& boundary,
    PhysicalRotation&& physical_rotation)
{
    return execute_rkl_lane(
        context, handles, RklMethod::RKL2, stages,
        std::forward<Executor>(executor), std::forward<Reflux>(reflux),
        std::forward<Boundary>(boundary),
        std::forward<PhysicalRotation>(physical_rotation));
}

template <typename HandleRange, typename Executor, typename Reflux,
          typename Boundary, typename PhysicalRotation>
RklExecutionResult execute_multi_rkl1_lane(
    StageExecutionContext& context, const HandleRange& handles, int stages,
    Executor&& executor, Reflux&& reflux, Boundary&& boundary,
    PhysicalRotation&& physical_rotation)
{
    return execute_rkl_lane(
        context, handles, RklMethod::RKL1, stages,
        std::forward<Executor>(executor), std::forward<Reflux>(reflux),
        std::forward<Boundary>(boundary),
        std::forward<PhysicalRotation>(physical_rotation));
}

template <typename HandleRange, typename Executor, typename Reflux,
          typename Boundary, typename PhysicalRotation>
RklExecutionResult execute_multi_rkl2_lane(
    StageExecutionContext& context, const HandleRange& handles, int stages,
    Executor&& executor, Reflux&& reflux, Boundary&& boundary,
    PhysicalRotation&& physical_rotation)
{
    return execute_rkl_lane(
        context, handles, RklMethod::RKL2, stages,
        std::forward<Executor>(executor), std::forward<Reflux>(reflux),
        std::forward<Boundary>(boundary),
        std::forward<PhysicalRotation>(physical_rotation));
}

template <typename HandleRange, typename Executor>
PublicationWitness execute_burn_first_lane(
    StageExecutionContext& context, const HandleRange& handles,
    Executor&& executor)
{
    return execute_in_place_stage(context, handles,
                                  state::StateSlot::Current,
                                  std::forward<Executor>(executor));
}

template <typename HandleRange, typename Executor>
PublicationWitness execute_burn_second_lane(
    StageExecutionContext& context, const HandleRange& handles,
    Executor&& executor)
{
    return execute_in_place_stage(context, handles,
                                  state::StateSlot::Current,
                                  std::forward<Executor>(executor));
}

template <typename HandleRange, typename PhysicalCopy>
StageExecutionResult copy_slot(StageExecutionContext& context,
                               const HandleRange& handles,
                               state::StateSlot source,
                               state::StateSlot destination,
                               PhysicalCopy&& physical_copy)
{
    if (source == destination)
        throw std::invalid_argument("logical slot copy requires two slots");

    bool any = false;
    state::StateVersion source_version{};
    for (const amr::BlockHandle handle : handles) {
        const state::StateKey source_key{handle, source};
        const state::SlotCoherence source_state =
            context.ledger.inspect(source_key);
        context.ledger.require_readable(
            source_key,
            {context.side, source_state.interior.version, true, true});
        if (!any) source_version = source_state.interior.version;
        if (source_state.interior.version != source_version)
            throw std::logic_error("logical copy source versions disagree");

        const state::SlotCoherence destination_state =
            context.ledger.inspect({handle, destination});
        detail::require_settled_destination(destination_state);
        if (destination_state.interior.residency
                != state::StateResidency::Invalid
            && source_version <= destination_state.interior.version) {
            throw std::logic_error(
                "logical copy source does not advance destination");
        }
        any = true;
    }
    if (!any) throw std::invalid_argument("logical copy requires a block");

    const state::CompletionToken interior_token =
        context.clock.next_completion();
    const state::CompletionToken ghost_token = context.clock.next_completion();
    std::forward<PhysicalCopy>(physical_copy)();
    for (const amr::BlockHandle handle : handles) {
        context.ledger.publish_interior({handle, destination}, context.side,
                                        source_version, interior_token);
    }
    for (const amr::BlockHandle handle : handles) {
        context.ledger.publish_ghost({handle, destination}, context.side,
                                     source_version, ghost_token);
    }
    return {source_version, interior_token, ghost_token};
}

template <typename HandleRange>
PublicationWitness publish_completed_interior(
    StageExecutionContext& context, const HandleRange& handles,
    state::StateSlot slot)
{
    const PublicationWitness witness = context.clock.next_publication();
    detail::publish_interior_batch(context, handles, slot, witness);
    return witness;
}

template <typename HandleRange, typename Boundary>
state::CompletionToken complete_boundary(
    StageExecutionContext& context, const HandleRange& handles,
    state::StateSlot slot, state::StateVersion version,
    Boundary&& boundary)
{
    return detail::execute_completed_boundary(
        context, handles, slot, version, std::forward<Boundary>(boundary));
}

} // namespace arch::scheduler
