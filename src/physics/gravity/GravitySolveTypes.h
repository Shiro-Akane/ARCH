/** @file GravitySolveTypes.h
 * @brief Domain-field identity and completion contracts for the gravity service.
 * These types do not enable a solver. Density has one dependency per active
 * block; the service cannot identify a whole domain by its first block alone.
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Describe density identity, completion and patch publication contracts.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#pragma once

#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "amr/topology/BlockHandle.h"
#include "driver/runtime/StateResidency.h"
#include "grid/ScalarFieldView.h"

namespace Physical::Gravity {
struct GravityInputIdentity {
    amr::BlockHandle block{};
    arch::state::StateSlot slot = arch::state::StateSlot::Current;
    arch::state::StateVersion version{};
    std::uint64_t storage_generation = 0;
    friend bool operator==(const GravityInputIdentity&, const GravityInputIdentity&) = default;
};
struct GravitySolveIdentity {
    amr::TopologyEpoch topology{};
    std::vector<GravityInputIdentity> inputs;
    double input_time = 0.0;
    double gravitational_constant = 0.0;
    std::uint64_t operator_revision = 0, boundary_revision = 0, accuracy_revision = 0;
    friend bool operator==(const GravitySolveIdentity&, const GravitySolveIdentity&) = default;
};
struct GravityDensityView {
    GravityInputIdentity identity;
    arch::grid::ConstScalarFieldView density;
};
struct GravitySolveRequest {
    const GravitySolveIdentity& identity;
    std::span<const GravityDensityView> blocks;
    // Geometry, physical BC and elliptic operator bindings are added with their
    // verified P2/P3 implementations; the request never owns fluid storage.
};
// Numerical candidate publications must not satisfy physical consumers.
enum class GravityFieldScope { ExistingPhysics, NativeRzCandidate };
struct GravityFieldStamp {
    GravitySolveIdentity source;
    std::uint64_t storage_generation = 0;
    arch::state::CompletionToken completion{};
    GravityFieldScope scope=GravityFieldScope::ExistingPhysics;
};

/** Validate density/domain dependencies before either source-cache or field use.
 * Completion remains a separate requirement: a valid source is not a solved field.
 */
inline void validate_gravity_solve_identity(const GravitySolveIdentity& source) {
    if(!source.topology.value || source.inputs.empty() || !std::isfinite(source.input_time)
        || !std::isfinite(source.gravitational_constant) || source.gravitational_constant<=0.
        || !source.operator_revision || !source.boundary_revision || !source.accuracy_revision)
        throw std::invalid_argument("Incomplete gravity solve identity");
    for(const auto& input:source.inputs) {
        if(!input.block.uid.value || input.block.epoch!=source.topology
            || !arch::state::is_valid(input.version) || !input.storage_generation
            || (input.slot!=arch::state::StateSlot::Current
                && input.slot!=arch::state::StateSlot::Next
                && input.slot!=arch::state::StateSlot::Scratch))
            throw std::invalid_argument("Invalid gravity density dependency");
    }
}
// Own only publication metadata. Field/workspace owners remain separate.
// A failed solve cannot replace a previously published stamp; callers must
// still require an exact source match before consuming any retained field.
class GravityFieldValidity {
public:
    /** Publish a completed gravity stamp only after validating every density dependency. */
    void publish(GravityFieldStamp stamp) {
        if(stamp.scope!=GravityFieldScope::ExistingPhysics
            &&stamp.scope!=GravityFieldScope::NativeRzCandidate)
            throw std::invalid_argument("Unknown gravity field qualification scope");
        if (!arch::state::is_complete(stamp.completion) || !stamp.storage_generation)
            throw std::invalid_argument("Incomplete gravity field publication");
        validate_gravity_solve_identity(stamp.source);
        published_ = std::move(stamp);
    }
    /** Require exact topology, time, operator settings and storage generation. */
    bool matches(const GravitySolveIdentity& source, std::uint64_t storage_generation,
        GravityFieldScope scope=GravityFieldScope::ExistingPhysics) const {
        return published_ && published_->scope==scope && published_->source == source
            && published_->storage_generation == storage_generation;
    }
    /** Retire the publication before any state or topology mutation. */
    void invalidate() noexcept { published_.reset(); }
private:
    std::optional<GravityFieldStamp> published_;
};
} // namespace Physical::Gravity
