/** @file GravityStage.h
 * Domain solve lifecycle and explicit output/CFL preparation for the Driver.
 */
#pragma once
#include "driver/schedule/StageScheduler.h"
#include "io/IO.h"
#include <fstream>
#include <limits>
namespace Physical::Gravity { class IGravityPolicy; class SelfGravity; }
namespace arch::driver {
class DriverRuntime;
class GravityStage final : public scheduler::HydroStagePreparation {
public:
    GravityStage(DriverRuntime&, const Physical::Gravity::IGravityPolicy*);
    state::CompletionToken prepare(const scheduler::HydroStagePreparationRequest&) override;
    void prepare_current(double time);
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
