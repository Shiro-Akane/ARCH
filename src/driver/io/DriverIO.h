/** @file DriverIO.h
 * @brief Output scheduling adapters and run diagnostics for the shared Driver.
 * Workflow:
 * 1. Receive a resolved configuration, stage request and current state identity.
 * 2. Declare Driver output services and their narrow state-facing inputs.
 * 3. Hand completed state and diagnostics to the next scheduled stage.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <span>

#include "io/IO.h"

struct SimulationController;
namespace arch::driver {
class DriverRuntime;
struct CudaDiffusionScheduleRecord {
    std::uint64_t macro_step = 0, cache_generation = 0;
    int order = 0, stages = 0, negative_gamma_stages = 0;
    bool captures_initial_operator = false;
    double diffusion_dt = 0.0, dt_forward_euler = 0.0;
};

// Non-overlapping host stage intervals. Output has its existing independent
// timer and is not added to these values a second time.
enum class CpuStage : std::size_t {
    Regrid, Gravity, Timestep, BurnFirst, Diffusion, Hydro, BurnSecond, Count
};
struct CpuStageTimings {
    static constexpr std::size_t size = static_cast<std::size_t>(CpuStage::Count);
    std::array<double, size> seconds{};
    std::array<std::uint64_t, size> calls{};
    void add(CpuStage stage, double elapsed) noexcept {
        const auto index = static_cast<std::size_t>(stage);
        seconds[index] += elapsed;
        ++calls[index];
    }
};

// Time only CPU work. CUDA launches can be asynchronous, so host enqueue
// intervals must not be mislabeled as device execution time.
class CpuStageTimer {
public:
    CpuStageTimer(CpuStageTimings& timings, CpuStage stage, bool enabled) noexcept
        : timings_(timings), stage_(stage), enabled_(enabled),
          started_(enabled ? Clock::now() : Clock::time_point{}) {}
    ~CpuStageTimer() noexcept {
        if (enabled_)
            timings_.add(stage_, std::chrono::duration<double>(Clock::now() - started_).count());
    }
    CpuStageTimer(const CpuStageTimer&) = delete;
    CpuStageTimer& operator=(const CpuStageTimer&) = delete;
private:
    using Clock = std::chrono::steady_clock;
    CpuStageTimings& timings_;
    CpuStage stage_;
    bool enabled_;
    Clock::time_point started_;
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
    void write_measurements(std::span<const CudaDiffusionScheduleRecord> cuda_diffusion_schedule,
                            const CpuStageTimings& cpu_stages);
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
