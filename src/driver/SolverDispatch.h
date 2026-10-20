/**
 * @file SolverDispatch.h
 * @brief Lightweight runtime solver-dispatch interface.
 *
 * Startup orchestration lives in SolverDispatch.cpp. Integrator-specific
 * Dispatch translation units instantiate the shared typed dispatch body, so
 * callers of this declaration do not instantiate the full strategy matrix.
 */

#pragma once

#include <string>

class ProblemGenerator;
namespace arch::config { class PreparedConfiguration; }

void DispatchSolver(ProblemGenerator &problem,
                    const arch::config::PreparedConfiguration &prepared);
