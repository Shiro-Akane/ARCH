/** @file DriverIO.h
 * @brief Output scheduling adapters and run diagnostics for the shared Driver.
 */
#pragma once
#include "io/IO.h"
#include <span>
#include <cstdint>
#include <chrono>
struct SimulationController;
namespace arch::driver {
class DriverRuntime;
struct CudaDiffusionScheduleRecord {
    std::uint64_t macro_step = 0, cache_generation = 0;
    int order = 0, stages = 0, negative_gamma_stages = 0;
    bool captures_initial_operator = false;
    double diffusion_dt = 0.0, dt_forward_euler = 0.0;
};
class DriverIO {
public:
    DriverIO(DriverRuntime& runtime, SimulationController& controller,
             const io::CheckpointProvenance& provenance,
             PressureFunc pressure, TemperatureFunc temperature, Gamma1Func gamma1,
             const void* eos)
        : runtime(runtime), ctrl(controller), checkpoint_provenance(provenance),
          p_func(pressure), t_func(temperature), gamma1_func(gamma1), eos(eos) {}
    void write_plot(std::span<const io::PlotScalarField> extra_fields = {});
    void write_checkpoint(double dt_burn_global, bool resume_after_regrid);
    void write_measurements(std::span<const CudaDiffusionScheduleRecord> cuda_diffusion_schedule);
private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point started_ = Clock::now();
    double output_seconds_ = 0.;
    std::size_t output_calls_ = 0;
    DriverRuntime& runtime;
    SimulationController& ctrl;
    const io::CheckpointProvenance& checkpoint_provenance;
    PressureFunc p_func;
    TemperatureFunc t_func;
    Gamma1Func gamma1_func;
    const void* eos;
};
} // namespace arch::driver
