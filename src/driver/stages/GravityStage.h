/** @file GravityStage.h
 * Domain solve lifecycle and explicit output/CFL preparation for the Driver.
 * Workflow:
 * 1. Receive a resolved configuration, stage request and current state identity.
 * 2. Declare gravity stage preparation, invalidation, plotting and timestep hooks.
 * 3. Hand completed state and diagnostics to the next scheduled stage.
 */

#pragma once

#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "driver/schedule/StageScheduler.h"
#include "io/IO.h"
#include "physics/gravity/self/GravityBoundaryDiagnostics.h"
#include "physics/gravity/GravitySolveTypes.h"

namespace Physical::Gravity { class IGravityPolicy; class SelfGravity; class NativeExternalStageFrame; }
namespace arch::driver {
class DriverRuntime;
class GravityStage final : public scheduler::HydroStagePreparation {
public:
    // Explicit internal CPU verification only; never selected from SimConfig.
    // Candidate fields remain unreadable by normal Hydro/plot/CFL consumers.
    enum class Qualification { Production, NativeRzCandidate, NativeRzExternalCandidate };
    GravityStage(DriverRuntime&, const Physical::Gravity::IGravityPolicy*,
        Qualification = Qualification::Production);
    ~GravityStage() override;
    state::CompletionToken prepare(const scheduler::HydroStagePreparationRequest&) override;
    void prepare_current(double time, bool reset_solver_history);
    void invalidate() const override;
    double timestep() const;
    std::vector<io::PlotScalarField> plot_fields() const;
    bool active() const { return gravity_!=nullptr||native_external(); }
    /** Four accepted body-source integrals; no potential/self-gravity accounting. */
    std::array<long double,4> external_source_budget() const noexcept { return external_accepted_; }
    /** Journal capability is independent of the physical field/RZ qualification gate. */
    bool supports_host_macro_step_journal() const noexcept override;
    /** Freeze accepted observer state before any macro-step fluid producer runs. */
    void begin_macro_step() override;
    /** Accept only the exact prepared descriptor and genuinely consumed field. */
    void accept(const scheduler::StageDescriptor&) override;
    /** Publish/discard preallocated records; neither operation performs file I/O. */
    void commit_macro_step() noexcept override;
    void discard_macro_step() noexcept override;
    /** Flush an already accepted macro-step; an I/O failure remains an explicit run failure. */
    void flush_committed_diagnostics();
private:
    /** Internal source-only profile; public configuration never selects it. */
    bool native_external() const noexcept { return qualification_==Qualification::NativeRzExternalCandidate; }
    state::CompletionToken prepare_native_external(const scheduler::HydroStagePreparationRequest&);
    state::CompletionToken solve(state::StateSlot, const state::StateResidencyLedger&, double, int);
    Qualification qualification_;
    DriverRuntime& runtime_;
    const Physical::Gravity::IGravityPolicy* policy_;
    const Physical::Gravity::SelfGravity* gravity_;
    std::unique_ptr<Physical::Gravity::NativeExternalStageFrame> external_frame_;
    std::array<std::array<long double,4>,3> external_pending_{};
    std::array<long double,4> external_accepted_{};
    amr::TopologyEpoch epoch_{};
    std::uint64_t generation_=0;
    std::ofstream diagnostics_,boundary_diagnostics_;
    std::optional<Physical::Gravity::GravityBoundarySnapshot> boundary_snapshot_;
    double boundary_exchange_=0.;
    struct DiagnosticRow { std::string solve,boundary; };
    struct PreparedFrame {
        scheduler::HydroMethod method;
        scheduler::StageDescriptor descriptor;
        Physical::Gravity::GravitySolveIdentity source;
        double input_time=0.,step_dt=0.;
    };
    // At most three supported RK stages; rows are formatted before patch work.
    // Storage/publication leases remain monotonic even after a failed attempt.
    bool journal_active_=false;
    std::optional<PreparedFrame> prepared_;
    std::optional<scheduler::HydroMethod> journal_method_;
    double journal_start_=0.,journal_dt_=0.;
    std::array<DiagnosticRow,3> pending_rows_,committed_rows_;
    std::size_t pending_count_=0,committed_count_=0;
    std::size_t expected_count_=0;
    std::optional<Physical::Gravity::GravityBoundarySnapshot> pending_boundary_snapshot_;
    double pending_boundary_exchange_=0.;
};
}
