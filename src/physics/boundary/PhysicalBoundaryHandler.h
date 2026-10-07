/**
 * @file PhysicalBoundaryHandler.h
 * @brief Execute built-in ghost plans and optional face-only case callbacks.
 *
 * Workflow:
 * 1. Compile the existing logical plan, retaining its built-in fast path.
 * 2. Bind the chosen EOS for callback-free native reflecting point laws and
 *    the optional immutable case callback once per simulation.
 * 3. For native RZ, domain ownership separates builtin/exchange seed,
 *    immutable builtin/user layers per axis, validation and no-throw scatter.
 * 4. Complete shared axis parity; the Runtime checks final actual EOS before
 *    scheduler GhostValid. Existing and device gather/scatter retain their path.
 */
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "amr/exchange/HostBoundaryPlan.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "physics/boundary/BoundaryFlux.h"
#include "physics/boundary/NativeRzBoundary.h"
#include "physics/boundary/PhysicalBoundary.h"
#include "physics/boundary/UserBoundary.h"

namespace arch::backend { class ComputeBackend; struct BackendStateAccess; }

/** Boundary adapter used by all Host integrators and the shared CUDA driver. */
struct BCHandler {
    explicit BCHandler(const SimConfig& config,
        GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing);
    GridMetrics::GeometrySemantics geometry_semantics() const noexcept { return semantics_; }
    /** Bind native point EOS even when the simulation has no user callback.
     * Workflow: invalidate prior native EOS leases -> bind the callback-free
     * reflector -> preserve the existing optional user conversion path.
     */
    template<class Eos> void bind(const Eos& eos, const SpeciesManager& species) {
        if (semantics_ == GridMetrics::GeometrySemantics::AxisymmetricRz) {
            if (binding_revision_ == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Native boundary binding revision exhausted");
            // A new borrowed EOS lifetime invalidates every prepared surface.
            ++binding_revision_;
            native_reflecting_evaluate_ = [this, &eos, &species](const Grid& grid,
                const arch::boundary::NativeRzBoundaryRequest& request,
                const NativeConservedReader& read, const NativeFractionReader& fraction) {
                return arch::boundary::EvaluateNativeRzReflectingCell(grid, request,
                    *config_, species, eos, read, fraction);
            };
            if (callback_)
                native_evaluate_ = [this, &eos, &species](const Grid& grid,
                    const arch::boundary::NativeRzBoundaryRequest& request,
                    const NativeConservedReader& read, const NativeFractionReader& fraction) {
                    return arch::boundary::EvaluateNativeRzBoundaryCell(grid, request,
                        *config_, species, eos, callback_, read, fraction);
                };
        }
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
    public:
        /** Query actual immutable snapshot data, never stage authority. The
         * owner must still check stage_context_matches and its real binding. */
        double time() const noexcept {return time_;}
        arch::boundary::BoundaryPurpose purpose() const noexcept {return purpose_;}
        std::uint64_t revision() const noexcept {return revision_;}

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
    /** Opaque numerical surface candidate. No GhostValid publication is implied.
     * It borrows one exact seed layout/frame until domain-wide preparation joins.
     */
    class NativeCandidate {
        friend struct BCHandler;
        struct Entry {
            int destination;
            FluidVector conserved;
            std::vector<double> fractions;
            double enuc;
        };
        const BCHandler* owner_ = nullptr;
        std::uint64_t binding_revision_ = 0;
        const FluidState* state_ = nullptr;
        const Grid* grid_ = nullptr;
        arch::boundary::host::HostBoundaryLayout layout_{};
        std::array<const double*,7> pointers_{};
        std::array<std::size_t,7> sizes_{};
        std::array<std::uint64_t,20> geometry_{};
        std::array<std::uint64_t,7> root_context_{};
        std::uint64_t revision_ = 0, time_bits_ = 0;
        int species_ = 0;
        arch::boundary::BoundaryPurpose purpose_ = arch::boundary::BoundaryPurpose::Hydro;
        std::vector<Entry> entries_;
        std::shared_ptr<arch::boundary::DiffusionBoundaryStorage> storage_;
        bool publish_controls_ = false;
    public:
        NativeCandidate() = default;
        NativeCandidate(NativeCandidate&&) noexcept = default;
        NativeCandidate& operator=(NativeCandidate&&) noexcept = default;
        NativeCandidate(const NativeCandidate&) = delete;
        NativeCandidate& operator=(const NativeCandidate&) = delete;
    };
    /** Capture an empty opaque Hydro frame, borrowing no numerical array copy.
     * Exact time/purpose, native EOS binding and all original storage leases
     * must already belong to the real completed input boundary publication.
     */
    NativeCandidate capture_native_hydro_frame(const FluidState&,const Grid&,double expected_time) const;
    /** Revalidate that same opaque frame and return only genuine root wall flags.
     * Successful calls perform no allocation, EOS evaluation or field writes.
     */
    arch::boundary::HydroBoundaryView native_hydro_boundary_view(
        const NativeCandidate&,const FluidState&,const Grid&,double expected_time) const;
    /** Fill only the original logical seeds; scientific acceptance stays pending. */
    void apply_builtin(FluidState& state, const Grid& grid) const;
    /** Prepare ordered x1/x2 surface values from immutable seed/candidate views. */
    NativeCandidate prepare_native(const FluidState& state, const Grid& grid) const;
    /** Reject frame/layout drift before any domain callback value is scattered. */
    void validate_native_candidate(const NativeCandidate&,const FluidState&,const Grid&) const;
    /** Scatter a prevalidated candidate without callbacks, allocation or throws.
     * Caller must validate every domain candidate first and retain exclusive state.
     */
    void publish_native_noexcept(NativeCandidate&&,FluidState&) const noexcept;
    /** Complete only shared RzAxis operations, including positive-source corners. */
    void complete_axis(FluidState& state,const Grid& grid) const;
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
        std::array<int,2> source_logical, destination_logical;
    };
    static arch::boundary::BoundaryPlan make_logical_plan(const SimConfig&);
    std::vector<Ghost> ghosts(const Grid&) const;
    /** Shared allocation-free selection of authenticated physical reflecting faces. */
    arch::boundary::HydroBoundaryView native_reflecting_faces(const Grid&) const;
    /** Enumerate only genuine positive Native physical reflecting surfaces. */
    std::vector<Ghost> reflecting_ghosts(const Grid&) const;
    /** Recheck the actual root/config context before surface preparation/publication. */
    void require_native_root_frame(const Grid&) const;
    std::shared_ptr<arch::boundary::DiffusionBoundaryStorage> make_diffusion_storage(const Grid&, int) const;
    void store_conditions(arch::boundary::DiffusionBoundaryStorage&, const Ghost&,
                          const arch::boundary::PhysicalBoundaryData&, int) const;
    void capture_native_frame(NativeCandidate&,const FluidState&,const Grid&) const;
    /** Common immutable frame validation; final candidate entries stay separate. */
    void validate_native_frame(const NativeCandidate&,const FluidState&,const Grid&) const;
    using NativeConservedReader = std::function<FluidVector(int)>;
    using NativeFractionReader = std::function<double(int,int)>;
    std::function<arch::boundary::PhysicalBoundaryEvaluation(const Grid&,
        const arch::boundary::NativeRzBoundaryRequest&,
        const NativeConservedReader&,const NativeFractionReader&)> native_evaluate_;
    std::function<arch::boundary::PhysicalBoundaryEvaluation(const Grid&,
        const arch::boundary::NativeRzBoundaryRequest&,
        const NativeConservedReader&,const NativeFractionReader&)> native_reflecting_evaluate_;
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
    std::uint64_t binding_revision_ = 1;
    arch::boundary::BoundaryPurpose purpose_ = arch::boundary::BoundaryPurpose::Hydro;
};
