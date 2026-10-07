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
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "amr/topology/BlockHandle.h"
#include "driver/runtime/StateResidency.h"
#include "grid/ScalarFieldView.h"

namespace arch::driver { class GravityStage; }
namespace amr { struct EllipticMeshBinding; }

namespace Physical::Gravity {
class SelfGravity;
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
/** Runtime use is independent of numerical/physical qualification scope. */
enum class GravityFieldPurpose { AcceptedCurrent, HydroStage };
/** Validate explicit tags without treating a tag as Runtime authority. */
inline bool valid_gravity_field_purpose(GravityFieldPurpose purpose) noexcept {
    return purpose==GravityFieldPurpose::AcceptedCurrent||purpose==GravityFieldPurpose::HydroStage;
}
/** A nonmoving capability issued only by the actual GravityStage owner.
 * Workflow: owner freezes metadata -> prepare checks owner/binding on both
 * sides of the unchanged solver -> owner seals -> purpose readers may borrow.
 * No configuration, mathematical fixture or public enum can construct/seal it.
 * Callback implementations belong to the Runtime owner, not the physics math.
 */
class RuntimeGravitySourceLease final {
public:
    RuntimeGravitySourceLease(const RuntimeGravitySourceLease&)=delete;
    RuntimeGravitySourceLease(RuntimeGravitySourceLease&&)=delete;
    RuntimeGravitySourceLease& operator=(const RuntimeGravitySourceLease&)=delete;
    RuntimeGravitySourceLease& operator=(RuntimeGravitySourceLease&&)=delete;
    std::uint64_t generation() const noexcept {return generation_;}
    GravityFieldPurpose purpose() const noexcept {return purpose_;}
    /** Require a sealed actual owner without scanning unrelated patches. */
    void require(GravityFieldPurpose purpose) const {
        if(!live_||!sealed_||purpose!=purpose_)
            throw std::logic_error("Gravity Runtime source lease is unsealed, retired or used for another purpose");
        require_owner_(owner_,false);
    }
private:
    friend class arch::driver::GravityStage;
    friend class SelfGravity;
    using OwnerCheck=void(*)(const void*,bool);
    using BindingCheck=void(*)(const void*,const amr::EllipticMeshBinding&);
    RuntimeGravitySourceLease(const void* owner,std::uint64_t generation,
        GravityFieldPurpose purpose,OwnerCheck owner_check,BindingCheck binding_check)
        :owner_(owner),generation_(generation),purpose_(purpose),
          require_owner_(owner_check),require_binding_(binding_check) {
        if(!owner_||!generation_||!valid_gravity_field_purpose(purpose_)||!require_owner_||!require_binding_)
            throw std::invalid_argument("Incomplete actual gravity source lease issuer");
    }
    /** Authenticate the same source/binding while its synchronous prepare runs. */
    void require_preparation(GravityFieldPurpose purpose,const amr::EllipticMeshBinding& binding) const {
        if(!live_||sealed_||purpose!=purpose_)
            throw std::logic_error("Gravity Runtime preparation lease is stale or used for another purpose");
        require_owner_(owner_,true);require_binding_(owner_,binding);
    }
    /** Owner-only seal after both prepare-side checks returned successfully. */
    void seal() noexcept {sealed_=true;}
    /** Owner-only permanent retirement before field or fluid mutation. */
    void retire() noexcept {live_=false;sealed_=false;}
    const void* owner_;const std::uint64_t generation_;const GravityFieldPurpose purpose_;
    const OwnerCheck require_owner_;const BindingCheck require_binding_;
    bool live_=true,sealed_=false;
};
struct GravitySolveRequest {
    const GravitySolveIdentity& identity;
    std::span<const GravityDensityView> blocks;
    // Absent tags preserve standalone mathematical fixtures and Existing paths.
    // A tag alone never grants a Runtime Current reader or Hydro frame.
    std::optional<GravityFieldPurpose> purpose;
    const RuntimeGravitySourceLease* runtime_lease=nullptr;
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
    std::optional<GravityFieldPurpose> purpose;
    std::uint64_t runtime_lease_generation=0;
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
        if((stamp.purpose&&!valid_gravity_field_purpose(*stamp.purpose))
            ||(stamp.runtime_lease_generation&&!stamp.purpose))
            throw std::invalid_argument("Invalid gravity field purpose/issuer metadata");
        validate_gravity_solve_identity(stamp.source);
        published_ = std::move(stamp);
    }
    /** Require exact topology, time, operator settings and storage generation. */
    bool matches(const GravitySolveIdentity& source, std::uint64_t storage_generation,
        GravityFieldScope scope=GravityFieldScope::ExistingPhysics) const {
        return published_ && published_->scope==scope && published_->source == source
            && published_->storage_generation == storage_generation;
    }
    /** Match Runtime purpose and an actually issued generation, never tags alone. */
    bool matches_runtime(const GravitySolveIdentity& source,std::uint64_t generation,
        GravityFieldScope scope,GravityFieldPurpose purpose,std::uint64_t lease) const {
        return lease&&matches(source,generation,scope)&&published_->purpose==purpose
            &&published_->runtime_lease_generation==lease;
    }
    /** Retire the publication before any state or topology mutation. */
    void invalidate() noexcept { published_.reset(); }
private:
    std::optional<GravityFieldStamp> published_;
};
} // namespace Physical::Gravity
