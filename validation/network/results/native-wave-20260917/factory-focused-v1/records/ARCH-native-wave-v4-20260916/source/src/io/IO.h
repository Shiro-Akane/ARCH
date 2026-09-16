/**
 * @file IO.h
 * @brief Common host-facing plot, checkpoint and restart interfaces.
 *
 * Output callers provide synchronized AMR state, independent of its execution
 * backend. Plot and checkpoint paths share the HDF5 format; restart validates
 * and restores state before the driver constructs its continuing evolution.
 */

#pragma once

#include <functional>
#include <string>

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
               const io::CheckpointProvenance &provenance);

// Checkpoint input for restart.
void read_chk(const std::string &filepath, amr::AMRControl &amr_ctrl,
              RunState &run_state, const SimConfig &config,
              const SpeciesManager &specs,
              const io::CheckpointProvenance &expected_provenance);
