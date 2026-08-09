/**
 * @file IO.h
 * @brief Routes plot, checkpoint, and restart I/O through the configured backend.
 *
 * Workflow:
 * 1. Collect synchronized leaf metadata and field values from the driver.
 * 2. Serialize them through the selected backend with explicit dimensions and geometry.
 * 3. Write restart- or analysis-ready output without changing simulation state.
 */

#pragma once

#include <string>
#include <functional>

// Forward declarations to keep this header extremely lightweight
struct SimConfig;
class SpeciesManager;
class RunState;
struct FluidVector;

namespace amr {
    class AMRControl;
}

using PressureFunc = double (*)(const FluidVector&, const double*, const void*);
using TemperatureFunc = double (*)(const FluidVector&, const double*, const void*);
using Gamma1Func = double (*)(const FluidVector&, const double*, const void*);

// ======================================================================
// 1. Plot 文件输出 (给人�?后处�?
// ======================================================================
void write_plt(amr::AMRControl &amr_ctrl,
               PressureFunc p_func, TemperatureFunc t_func, Gamma1Func gamma1_func, const void* p_context,
               int file_index, double current_time,
               const SimConfig &config, const SpeciesManager &specs);

// ======================================================================
// 2. Checkpoint 文件输出 (断点重启)
// ======================================================================
void write_chk(amr::AMRControl &amr_ctrl,
               int chk_file_index, int plt_file_index,
               int step_count, double current_time,
               const SimConfig &config);

// ======================================================================
// 3. Checkpoint 文件读取 (断点重启载入)
// ======================================================================
void read_chk(const std::string &filepath, amr::AMRControl &amr_ctrl,
              RunState &run_state, const SimConfig &config, int expected_species);