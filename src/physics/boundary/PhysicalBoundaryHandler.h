/**
 * @file PhysicalBoundaryHandler.h
 * @brief Execute built-in ghost plans and optional face-only case callbacks.
 *
 * Workflow:
 * 1. Compile the existing logical plan, retaining its built-in fast path.
 *    Epoch preparation reserves only unbound physical-control capacity,
 *    preserving actual values/bindings before any resident macro savepoint.
 * 2. Bind the optional immutable case callback to the selected EOS.
 * 3. Apply callbacks in their existing face order and retain separate
 *    diffusion face controls. Runtime owns final boundary acceptance.
 */
#pragma once

#include <array>
#include <cstddef>
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
#include "driver/runtime/ComputeBackend.h"
#include "physics/boundary/BoundaryFlux.h"
#include "physics/boundary/PhysicalBoundary.h"
#include "physics/boundary/UserBoundary.h"

namespace arch::boundary { class HostHydroBoundaryAuthority; class HostHydroBoundaryDomainAuthority; }

/** Boundary adapter used by all Host integrators and the shared CUDA driver. */
struct BCHandler {
    explicit BCHandler(const SimConfig& config);
    /** Bind the original optional user conversion path. */
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
        StageContextSnapshot(double t,std::uint64_t r,
            arch::boundary::BoundaryPurpose p)
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
        return time_==s.time_&&stage_revision_==s.revision_
            &&purpose_==s.purpose_;
    }
    /** Identity of the exact time/purpose snapshot; equal requests may reuse ghosts. */
    std::uint64_t stage_revision() const noexcept { return stage_revision_; }
private:
    friend class arch::boundary::HostHydroBoundaryAuthority;
    friend class arch::boundary::HostHydroBoundaryDomainAuthority;
    /** One borrowed metadata identity; never owns scientific surface payload. */
    struct InputIdentity {
        const BCHandler* owner_ = nullptr;
        const FluidState* state_ = nullptr;
        const Grid* grid_ = nullptr;
        arch::boundary::host::HostBoundaryLayout layout_{};
        std::array<const double*,7> pointers_{};
        std::array<std::size_t,7> sizes_{};
        std::array<std::uint64_t,9> geometry_{};
        std::array<std::uint64_t,11> full_root_{};
        GridMetrics::Geometry chart_ = GridMetrics::Geometry::Cartesian;
        int dimension_ = 0;
        std::uint64_t revision_ = 0, time_bits_ = 0;
        int species_ = 0;
        arch::boundary::BoundaryPurpose purpose_ = arch::boundary::BoundaryPurpose::Hydro;
    };
public:
    /** Closed move-only Hydro metadata; moving permanently expires the old frame.
     * It cannot carry surface values or publish ghosts, wall flags or EOS status.
     */
    class HydroInputFrame {
        friend struct BCHandler;
        friend class arch::boundary::HostHydroBoundaryAuthority;
        friend class arch::boundary::HostHydroBoundaryDomainAuthority;
        InputIdentity identity_{};
        HydroInputFrame() = default;
    public:
        HydroInputFrame(HydroInputFrame&& other) noexcept : identity_(other.identity_) {
            other.identity_.owner_=nullptr;
        }
        HydroInputFrame& operator=(HydroInputFrame&& other) noexcept {
            if(this!=&other){identity_=other.identity_;other.identity_.owner_=nullptr;}
            return *this;
        }
        HydroInputFrame(const HydroInputFrame&)=delete;
        HydroInputFrame& operator=(const HydroInputFrame&)=delete;
    };
    /** Freeze only actual input metadata at the completed Hydro snapshot.
     * No wall, ledger or scientific permission follows from this public observation.
     */
    HydroInputFrame capture_hydro_input_frame(const FluidState&,const Grid&,double expected_time) const;
    /** Recheck the original metadata, without allocation, EOS or field writes. */
    void require_hydro_input_frame(const HydroInputFrame&,const FluidState&,const Grid&,double expected_time) const;
    /** Fill only the original logical seeds; scientific acceptance stays pending. */
    void apply_builtin(FluidState& state, const Grid& grid) const;
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
    /** Enumerate the same physical faces for an explicit purpose, without
     * changing the current stage or evaluating any case callback/EOS.
     */
    std::vector<Ghost> ghosts(const Grid&,arch::boundary::BoundaryPurpose) const;
    /** Shared actual face/count recipe for capacity and real control storage. */
    std::array<std::size_t,6> diffusion_control_extents(const Grid&,int species) const;
    std::shared_ptr<arch::boundary::DiffusionBoundaryStorage> make_diffusion_storage(const Grid&, int) const;
    void store_conditions(arch::boundary::DiffusionBoundaryStorage&, const Ghost&,
                          const arch::boundary::PhysicalBoundaryData&, int) const;
    /** Capture the same borrowed storage/chart identity for either private role. */
    InputIdentity capture_input_identity(const FluidState&,const Grid&) const;
    /** Require the same actual input metadata before publication. */
    void validate_input_identity(const InputIdentity&,const FluidState&,const Grid&) const;
    /** Exact three-axis configured root identity, with no face-string copies. */
    std::array<std::uint64_t,11> input_root_identity() const;
    const SimConfig* config_;
    arch::boundary::BoundaryPlan logical_plan_;
    arch::boundary::host::HostCompiledBoundaryPlan compiled_;
    arch::boundary::PhysicalBoundaryFunction callback_;
    std::function<arch::boundary::PhysicalBoundaryEvaluation(
        const arch::boundary::BoundaryCoordinates&, const FluidVector&, std::span<const double>,
        const FluidVector*, std::span<const double>)> evaluate_;
    double time_ = 0.;
    std::uint64_t stage_revision_ = 1;
    arch::boundary::BoundaryPurpose purpose_ = arch::boundary::BoundaryPurpose::Hydro;
};
