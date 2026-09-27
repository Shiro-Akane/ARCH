/** @file GravityStage.h
 * Domain solve lifecycle and explicit output/CFL preparation for the Driver.
 * Workflow:
 * 1. Receive a resolved configuration, stage request and current state identity.
 * 2. Declare gravity stage preparation, invalidation, plotting and timestep hooks.
 * 3. Hand completed state and diagnostics to the next scheduled stage.
 */

#pragma once

#include <fstream>
#include <limits>

#include "driver/schedule/StageScheduler.h"
#include "io/IO.h"

namespace Physical::Gravity { class IGravityPolicy; class SelfGravity; }
namespace arch::driver {
class DriverRuntime;
class GravityStage final : public scheduler::HydroStagePreparation {
public:
    GravityStage(DriverRuntime&, const Physical::Gravity::IGravityPolicy*);
    state::CompletionToken prepare(const scheduler::HydroStagePreparationRequest&) override;
    void prepare_current(double time, bool reset_solver_history);
    void invalidate() const override;
    double timestep() const;
    std::vector<io::PlotScalarField> plot_fields() const;
    bool active() const { return gravity_!=nullptr; }
private:
    state::CompletionToken solve(state::StateSlot, const state::StateResidencyLedger&, double, int);
    DriverRuntime& runtime_;
    const Physical::Gravity::SelfGravity* gravity_;
    amr::TopologyEpoch epoch_{};
    std::uint64_t generation_=0;
    std::ofstream diagnostics_;
};
}
