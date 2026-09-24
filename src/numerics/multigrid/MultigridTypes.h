/**
 * @file MultigridTypes.h
 * @brief Convergence contract shared by the uniform reference and composite solver.
 *
 * Workflow:
 * 1. Carry caller tolerances and iteration budget into either solver.
 * 2. Record the independently checked physical residual.
 * 3. Publish a potential only after convergence is accepted.
 */
#pragma once

#include <vector>

namespace arch::multigrid {
struct SolveControl {
    double relative_tolerance;
    double absolute_tolerance; // RHS units; positive even for zero source.
    int max_cycles;
};
enum class SolveStatus { Converged, MaxCycles, NumericalFailure };
struct SolveReport {
    SolveStatus status = SolveStatus::NumericalFailure;
    int cycles = 0;
    double rhs_rms = 0., initial_residual = 0., residual = 0., target = 0.;
    double removed_rhs_mean = 0.; // Roundoff only; incompatible RHS is rejected.
};
struct SolveResult {
    SolveReport report;
    std::vector<double> potential; // Empty unless converged.
};
} // namespace arch::multigrid
