/**
 * @file SolverDispatch.h
 * @brief Lightweight runtime solver-dispatch interface.
 *
 * Heavy solver, EOS, burner, reconstruction, and integrator templates are
 * intentionally kept in SolverDispatch.cpp so including this header does not
 * instantiate the complete runtime-selectable strategy matrix.
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
