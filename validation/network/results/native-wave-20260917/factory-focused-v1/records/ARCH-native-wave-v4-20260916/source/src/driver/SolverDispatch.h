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
struct SimConfig;
struct SpeciesManager;

void DispatchSolver(const std::string &solver_name,
                    ProblemGenerator &problem,
                    const SimConfig &config,
                    const SpeciesManager &specs);
