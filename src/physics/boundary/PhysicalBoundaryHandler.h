/**
 * @file PhysicalBoundaryHandler.h
 * @brief Execute built-in ghost plans and optional face-only case callbacks.
 *
 * Workflow:
 * 1. Compile the existing logical plan, retaining its built-in fast path.
 * 2. Bind the chosen EOS and immutable case callback once per simulation.
 * 3. At each stage evaluate only true domain faces; exchange fills AMR joins.
 * 4. Publish validated ghosts and separate diffusion-face controls.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "amr/exchange/HostBoundaryPlan.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "physics/boundary/PhysicalBoundary.h"
#include "physics/boundary/UserBoundary.h"

namespace arch::backend { class ComputeBackend; struct BackendStateAccess; }

/** Boundary adapter used by all Host integrators and the shared CUDA driver. */
struct BCHandler {
    explicit BCHandler(const SimConfig& config,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing);
    GridMetrics::GeometrySemantics geometry_semantics() const noexcept { return semantics_; }
    /** Bind shared EOS conversion without multiplying the integrator matrix. */
    template<class Eos> void bind(const Eos& eos, const SpeciesManager& species) {
        if (!callback_) return;
        evaluate_ = [this, &eos, &species](const arch::boundary::BoundaryCoordinates& coordinates,
            const FluidVector& interior, std::span<const double> composition,
            const FluidVector* inherited, std::span<const double> inherited_composition) {
            return arch::boundary::EvaluatePhysicalBoundary(coordinates, *config_, species,
                eos, callback_, interior, composition, inherited, inherited_composition);
        };
    }
    /** Set the physical time and operator whose boundary data are requested. */
    void configure_stage(double time, arch::boundary::BoundaryPurpose purpose);
    /** Opaque mutable context only; compiled logical/axis plans remain immutable. */
    class StageContextSnapshot {
        friend struct BCHandler;
        double time_;
        std::uint64_t revision_;
        arch::boundary::BoundaryPurpose purpose_;
        StageContextSnapshot(double t,std::uint64_t r,arch::boundary::BoundaryPurpose p)
            : time_(t),revision_(r),purpose_(p) {}
    };
    /** Capture a context identity at a quiescent Host scheduler boundary. */
    StageContextSnapshot snapshot_stage_context() const noexcept {
        return {time_,stage_revision_,purpose_};
    }
    /** Restore the context paired with the restored original ghost fields. */
    void restore_stage_context_noexcept(const StageContextSnapshot& s) noexcept {
        time_=s.time_;stage_revision_=s.revision_;purpose_=s.purpose_;
    }
    /** Inspect all context fields, including time/purpose hidden behind revision. */
    bool stage_context_matches(const StageContextSnapshot& s) const noexcept {
        return time_==s.time_&&stage_revision_==s.revision_&&purpose_==s.purpose_;
    }
    /** Identity of the exact time/purpose snapshot; equal requests may reuse ghosts. */
    std::uint64_t stage_revision() const noexcept { return stage_revision_; }
    void apply(FluidState& state, const Grid& grid) const;
    /** Gather/scatter a boundary slice; active domain arrays stay on device. */
    void apply_device(arch::backend::ComputeBackend&, arch::backend::BackendStateAccess,
                      const Grid&) const;
    bool has_user() const noexcept { return bool(callback_); }
    const arch::boundary::BoundaryPlan& logical_plan() const noexcept { return logical_plan_; }
    const arch::boundary::BoundaryPlan& logical_plan(const Grid&) const;
private:
    struct Ghost {
        int source, destination;
        int face, plane;
        bool first_active_layer;
        arch::boundary::BoundaryCoordinates coordinates;
    };
    static arch::boundary::BoundaryPlan make_logical_plan(const SimConfig&);
    std::vector<Ghost> ghosts(const Grid&) const;
    std::shared_ptr<arch::boundary::DiffusionBoundaryStorage> make_diffusion_storage(const Grid&, int) const;
    void store_conditions(arch::boundary::DiffusionBoundaryStorage&, const Ghost&,
                          const arch::boundary::PhysicalBoundaryData&, int) const;
    const SimConfig* config_;
    GridMetrics::GeometrySemantics semantics_;
    arch::boundary::BoundaryPlan logical_plan_;
    arch::boundary::host::HostCompiledBoundaryPlan compiled_;
    std::optional<arch::boundary::BoundaryPlan> axis_plan_;
    std::optional<arch::boundary::host::HostCompiledBoundaryPlan> axis_compiled_;
    arch::boundary::PhysicalBoundaryFunction callback_;
    std::function<arch::boundary::PhysicalBoundaryEvaluation(
        const arch::boundary::BoundaryCoordinates&, const FluidVector&, std::span<const double>,
        const FluidVector*, std::span<const double>)> evaluate_;
    double time_ = 0.;
    std::uint64_t stage_revision_ = 1;
    arch::boundary::BoundaryPurpose purpose_ = arch::boundary::BoundaryPurpose::Hydro;
};
