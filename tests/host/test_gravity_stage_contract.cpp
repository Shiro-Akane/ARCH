/** @file test_gravity_stage_contract.cpp
 * @brief Whole-domain preparation ordering and gravity publication validity.
 */
#include "driver/StageScheduler.h"
#include "physics/gravity/GravitySolveTypes.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace arch::scheduler;
using namespace arch::state;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Action> void rejects(Action&& action, const char* message) {
    try { action(); } catch (const std::logic_error&) { return; }
    throw std::runtime_error(message);
}
struct Fixture {
    std::array<amr::BlockHandle, 2> handles{{{{1}, {1}}, {{2}, {1}}}};
    StateResidencyLedger ledger{amr::TopologyEpoch{1}};
    MonotonicSchedulerClock clock;
    StageExecutionContext context{ExecutionSide::Host, ledger, clock};
    Fixture(bool ghosts = true) {
        const auto witness = clock.next_publication();
        for (const auto handle : handles) {
            ledger.register_block(handle, witness.version, witness.completion);
            if (ghosts)
                ledger.publish_ghost({handle, StateSlot::Current}, ExecutionSide::Host,
                                     witness.version, witness.completion);
        }
        context.step_start_time = 2.0;
        context.step_dt = 0.25;
    }
};
struct Probe : HydroStagePreparation {
    std::vector<StateSlot> slots;
    std::vector<double> times;
    std::vector<std::vector<StateVersion>> versions;
    int executions = 0;
    bool pending = false, fail = false;
    CompletionToken prepare(const HydroStagePreparationRequest& request) override {
        require(request.handles.size() == 2, "preparation is domain-wide, not per patch");
        require(executions == static_cast<int>(slots.size()), "prepare precedes each executor");
        require(request.step_dt == 0.25, "preparation receives the Hydro step size");
        slots.push_back(request.descriptor.input_slot);
        times.push_back(request.input_time);
        versions.emplace_back();
        for (auto handle : request.handles) {
            const StateKey key{handle, request.descriptor.input_slot};
            const auto current = request.ledger.inspect(key).interior.version;
            request.ledger.require_readable(key, {request.side, current, true, true});
            versions.back().push_back(current);
        }
        if (fail) throw std::logic_error("controlled field preparation failure");
        return {123, pending ? CompletionState::Pending : CompletionState::Complete};
    }
};
void run_hydro(Fixture& f, Probe& p, HydroMethod method) {
    f.context.hydro_preparation = &p;
    execute_hydro_lane(f.context, f.handles, method,
        [&](const StageDescriptor&, CompletionToken token) {
            require(p.slots.size() == static_cast<std::size_t>(p.executions + 1),
                    "completed preparation must precede block execution");
            ++p.executions;
            return token;
        },
        [](StateSlot, StateVersion, CompletionToken token) { return token; },
        [](SlotRotation) {},
        [](const HydroPlan&, StateSlot, CompletionToken token) { return token; });
}
void test_preparation() {
    for (const auto method : {HydroMethod::Euler, HydroMethod::RK2, HydroMethod::RK3}) {
        Fixture f;
        Probe p;
        run_hydro(f, p, method);
        const int count = method == HydroMethod::Euler ? 1 : method == HydroMethod::RK2 ? 2 : 3;
        require(p.executions == count, "one preparation per actual RK stage");
        require(p.slots.front() == StateSlot::Current && p.times.front() == 2.0,
                "first stage uses accepted input");
        if (count >= 2) {
            require(p.slots[1] == StateSlot::Scratch && p.times[1] == 2.25,
                    "second stage uses its evolved scratch state and time");
            require(p.versions[1][0] != p.versions[0][0], "stage input version advances");
        }
        if (count == 3)
            require(p.slots[2] == StateSlot::Next && p.times[2] == 2.125,
                    "SSPRK3 third stage samples the half-time input");
    }
    // A domain request exposes every block's authoritative version, even when
    // the accepted input versions differ; no first-block shortcut is valid.
    Fixture mixed;
    const auto later = mixed.clock.next_publication();
    mixed.ledger.publish_interior({mixed.handles[1], StateSlot::Current}, ExecutionSide::Host,
                                   later.version, later.completion);
    mixed.ledger.publish_ghost({mixed.handles[1], StateSlot::Current}, ExecutionSide::Host,
                                later.version, later.completion);
    Probe p;
    run_hydro(mixed, p, HydroMethod::Euler);
    require(p.versions[0][0] != p.versions[0][1], "all block input dependencies remain observable");
}
void test_failures() {
    for (int mode = 0; mode < 3; ++mode) {
        Fixture f(mode != 0);
        Probe p;
        p.pending = mode == 1;
        p.fail = mode == 2;
        const auto version = f.clock.last_version(), token = f.clock.last_token();
        rejects([&] { run_hydro(f, p, HydroMethod::RK3); }, "invalid preparation accepted");
        require(p.executions == 0, "failed preparation must not execute patches");
        require(f.clock.last_version() == version && f.clock.last_token() == token,
                "failed preparation must not issue a publication witness");
        require(f.ledger.inspect({f.handles[0], StateSlot::Next}).interior.residency
                    == StateResidency::Invalid, "failed prepare cannot publish output");
        if (mode == 0) require(p.slots.empty(), "input validation must precede the service");
    }
    // No field service is the production none/external route in P1.
    Fixture f;
    int executed = 0;
    execute_euler_lane(f.context, f.handles,
        [&](const StageDescriptor&, CompletionToken t) { ++executed; return t; },
        [](StateSlot, StateVersion, CompletionToken t) { return t; },
        [](SlotRotation) {},
        [](const HydroPlan&, StateSlot, CompletionToken t) { return t; });
    require(executed == 1, "unbound preparation preserves existing execution");
}
void test_field_identity() {
    using namespace Physical::Gravity;
    GravitySolveIdentity identity;
    identity.topology = {1};
    identity.inputs = {{{{1}, {1}}, StateSlot::Current, {1}, 1},
                       {{{2}, {1}}, StateSlot::Current, {2}, 2}};
    identity.gravitational_constant = 6.67430e-8;
    identity.operator_revision = identity.boundary_revision = identity.accuracy_revision = 1;
    GravityFieldValidity field;
    require(!field.matches(identity, 3), "unpublished gravity cannot be consumed");
    field.publish({identity, 3, {9, CompletionState::Complete}});
    require(field.matches(identity, 3), "complete matching publication is usable");
    for (int change = 0; change < 9; ++change) {
        auto changed = identity;
        switch (change) {
        case 0: changed.inputs.back().version = {3}; break;
        case 1: changed.inputs.back().storage_generation = 4; break;
        case 2: changed.inputs.back().slot = StateSlot::Scratch; break;
        case 3: changed.topology = {2}; break;
        case 4: ++changed.operator_revision; break;
        case 5: ++changed.boundary_revision; break;
        case 6: ++changed.accuracy_revision; break;
        case 7: changed.input_time = 0.25; break;
        case 8: changed.gravitational_constant *= 2.0; break;
        }
        require(!field.matches(changed, 3), "stale field dependency accepted");
    }
    require(!field.matches(identity, 4), "retired field storage accepted");
    rejects([&] { field.publish({identity, 3, {10, CompletionState::Pending}}); },
            "pending field published");
    require(field.matches(identity, 3), "failed publication changed the accepted metadata");
    auto invalid = identity;
    invalid.inputs.back().block.epoch = {2};
    rejects([&] { field.publish({invalid, 3, {10, CompletionState::Complete}}); },
            "mixed topology input published");
    field.invalidate();
    require(!field.matches(identity, 3), "explicit regrid/restart invalidation failed");
}
} // namespace
int main() {
    try { test_preparation(); test_failures(); test_field_identity(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "Gravity stage preparation and field identity contracts passed\n";
}
