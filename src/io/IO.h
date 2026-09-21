/**
 * @file IO.h
 * @brief Common host-facing plot, checkpoint and restart interfaces.
 *
 * Output callers provide synchronized AMR state, independent of its execution
 * backend. Plot and checkpoint paths share the HDF5 format; restart validates
 * and restores state before the driver constructs its continuing evolution.
 */

#pragma once

#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include "data/StateDiagnostics.h"

// Forward declarations to keep this header extremely lightweight
struct SimConfig;
class SpeciesManager;
class RunState;
struct FluidVector;

namespace amr {
    class AMRControl;
}

namespace io {
    struct CheckpointProvenance;
}

using PressureFunc = double (*)(const FluidVector&, const double*, const void*);
using TemperatureFunc = double (*)(const FluidVector&, const double*, const void*);
using Gamma1Func = double (*)(const FluidVector&, const double*, const void*);

namespace io {
// Persistence is an explicit EOS validity boundary even when the selected
// plot fields omit thermodynamics. Exceptions and NaN-returning EOS both fail.
inline void require_output_thermodynamics(const FluidVector& state, const double* fractions,
    PressureFunc pressure, TemperatureFunc temperature, Gamma1Func gamma1, const void* eos)
{
    if (!pressure || !temperature || !gamma1)
        throw std::runtime_error("Output requires bound thermodynamic queries");
    for (double value : {pressure(state,fractions,eos), temperature(state,fractions,eos),
                         gamma1(state,fractions,eos)})
        if (!(value > 0.0) || !std::isfinite(value))
            throw std::runtime_error("Cannot persist a state outside the EOS valid domain");
}
}

// Plot-file output for analysis and visualization.
void write_plt(amr::AMRControl &amr_ctrl,
               PressureFunc p_func, TemperatureFunc t_func, Gamma1Func gamma1_func, const void* p_context,
               int file_index, double current_time,
               const SimConfig &config, const SpeciesManager &specs);

// Checkpoint output for restart.
void write_chk(amr::AMRControl &amr_ctrl,
               int chk_file_index, int plt_file_index,
               int step_count, double current_time,
               double dt_old, double dt_burn,
               bool resume_after_regrid,
               const SimConfig &config, const SpeciesManager &specs,
               const io::CheckpointProvenance &provenance,
               const arch::state::RepairBudget &repairs = arch::state::RepairBudget{});

// Checkpoint input for restart.
void read_chk(const std::string &filepath, amr::AMRControl &amr_ctrl,
              RunState &run_state, const SimConfig &config,
              const SpeciesManager &specs,
              const io::CheckpointProvenance &expected_provenance);
