/** @file UniformGravity.h
 * P2 standalone CGS gravity adapter. No Driver field publication or AMR coupling.
 */
#pragma once
#include "grid/ScalarFieldView.h"
#include "numerics/multigrid/HostMultigrid.h"
#include "physics/constant/PhysicalConstants.h"
#include <array>
#include <span>
#include <vector>

namespace arch::gravity {
struct UniformGravityResult {
    // removed_rhs_mean also accounts for roundoff in density-mean subtraction:
    // effective b = -4*pi*G*(rho-removed_density_mean) - report.removed_rhs_mean.
    multigrid::SolveReport report;
    double removed_density_mean = 0.; // g/cm^3, nonzero only for periodic problems.
    std::vector<double> potential; // cm^2/s^2, cell centered.
    // cm/s^2; x-fast face arrays have n_axis+1 in their normal direction.
    std::array<std::vector<double>, 3> face_acceleration, cell_acceleration;
};

// Density is a borrowed, padded x-fast Host cell view. The call is synchronous;
// only the active box is sampled. No density floor, hydro state repair or EOS call.
// No fields are returned on numerical failure; malformed inputs throw.
UniformGravityResult solve_uniform_gravity(multigrid::HostMultigrid& solver,
    grid::ConstScalarFieldView density, const elliptic::BoundaryData& boundary,
    multigrid::SolveControl control,
    double gravitational_constant = arch::constants::gravity::cgs::gravitational_constant,
    std::span<const double> initial = {});
} // namespace arch::gravity
