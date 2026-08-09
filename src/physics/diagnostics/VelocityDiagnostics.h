/**
 * @file VelocityDiagnostics.h
 * @brief Shared metric-aware divergence and vorticity diagnostics.
 *
 * Workflow:
 * 1. Convert conserved momentum to physical orthonormal velocity components.
 * 2. Evaluate divergence with finite-volume face measures from GridMetrics.
 * 3. Evaluate curl magnitude with the matching cylindrical or spherical metric terms.
 * 4. Reuse the same operator for AMR indicators and PLT diagnostics.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "../../grid/Grid.h"
#include "../../grid/GridMetrics.h"

namespace VelocityDiagnostics {

struct Values {
    double divergence = 0.0;
    double vorticity = 0.0;
};

/**
 * @brief Evaluates physical div(v) and |curl(v)| from orthonormal velocity components.
 *
 * Momentum storage follows the hydro source-term convention: VELX is radial for
 * curvilinear grids; VELY is azimuthal in 2-D polar/cylindrical grids and polar
 * in 3-D spherical grids; VELZ is azimuthal in 3-D cylindrical/spherical grids.
 * Logical-grid indices are materialized once because this routine is called in the
 * AMR and PLT inner loops; the component derivatives then reuse the same stencil.
 */
inline Values evaluate(const Grid& grid, const std::vector<double>& vel_x,
                       const std::vector<double>& vel_y, const std::vector<double>& vel_z,
                       int i, int j, int k)
{
    const int center = grid.GetIndex(i, j, k);
    const int x_minus = grid.GetIndex(i - 1, j, k);
    const int x_plus = grid.GetIndex(i + 1, j, k);
    const int y_minus = grid.dim >= 2 ? grid.GetIndex(i, j - 1, k) : center;
    const int y_plus = grid.dim >= 2 ? grid.GetIndex(i, j + 1, k) : center;
    const int z_minus = grid.dim == 3 ? grid.GetIndex(i, j, k - 1) : center;
    const int z_plus = grid.dim == 3 ? grid.GetIndex(i, j, k + 1) : center;

    const auto centered_difference = [](const std::vector<double>& component,
                                        int low, int high, double spacing) {
        return (component[high] - component[low]) / (2.0 * spacing);
    };
    const auto ddx = [&](const std::vector<double>& component) {
        return centered_difference(component, x_minus, x_plus, grid.dx1);
    };
    const auto ddy = [&](const std::vector<double>& component) {
        return grid.dim >= 2 ? centered_difference(component, y_minus, y_plus, grid.dx2) : 0.0;
    };
    const auto ddz = [&](const std::vector<double>& component) {
        return grid.dim == 3 ? centered_difference(component, z_minus, z_plus, grid.dx3) : 0.0;
    };
    const auto face_velocity = [&](const std::vector<double>& component, int neighbour) {
        return 0.5 * (component[center] + component[neighbour]);
    };

    const double vel_x_center = vel_x[center];
    const double vel_y_center = vel_y[center];
    const double vel_z_center = vel_z[center];
    Values result;
    const double volume = GridMetrics::CellVolume(grid, i, j, k);
    double face_flux = 0.0;
    const auto add_face_flux = [&](int direction, const std::vector<double>& component,
                                   int low, int high) {
        face_flux += GridMetrics::FaceArea(grid, direction, i, j, k, true) *
                         face_velocity(component, high) -
                     GridMetrics::FaceArea(grid, direction, i, j, k, false) *
                         face_velocity(component, low);
    };
    add_face_flux(0, vel_x, x_minus, x_plus);
    if (grid.dim >= 2) add_face_flux(1, vel_y, y_minus, y_plus);
    if (grid.dim == 3) add_face_flux(2, vel_z, z_minus, z_plus);
    result.divergence = volume > 0.0 ? face_flux / volume : 0.0;

    if (grid.geometry == "cartesian") {
        const double omega_x = ddy(vel_z) - ddz(vel_y);
        const double omega_y = ddz(vel_x) - ddx(vel_z);
        const double omega_z = ddx(vel_y) - ddy(vel_x);
        result.vorticity = std::sqrt(omega_x * omega_x + omega_y * omega_y + omega_z * omega_z);
        return result;
    }

    const double radius = std::max(std::abs(grid.GetCellCenterX(i)), 0.5 * grid.dx1);
    if (grid.dim <= 2) {
        // Both 2-D cylindrical and the project's 2-D spherical specialization
        // are polar (r, phi) grids with physical components (v_r, v_phi).
        const double omega = (vel_y_center + radius * ddx(vel_y) - ddy(vel_x)) / radius;
        result.vorticity = std::abs(omega);
        return result;
    }

    if (grid.geometry == "cylindrical") {
        // Logical axes are (r, z, phi), with stored components (v_r, v_z, v_phi).
        const double omega_r = ddz(vel_y) / radius - ddy(vel_z);
        const double omega_phi = ddy(vel_x) - ddx(vel_y);
        const double omega_z = vel_z_center / radius + ddx(vel_z) - ddz(vel_x) / radius;
        result.vorticity = std::sqrt(omega_r * omega_r + omega_phi * omega_phi + omega_z * omega_z);
        return result;
    }

    // 3-D spherical logical axes are (r, theta, phi) with physical components
    // (v_r, v_theta, v_phi). The floor prevents numerical singularities in ghosts.
    const double theta = grid.GetCellCenterY(j);
    const double sin_theta = std::max(std::abs(std::sin(theta)), 1.0e-12);
    const double cos_theta = std::cos(theta);
    const double omega_r = (cos_theta * vel_z_center + sin_theta * ddy(vel_z) - ddz(vel_y)) /
                           (radius * sin_theta);
    const double omega_theta = ddz(vel_x) / (radius * sin_theta) -
                               (vel_z_center + radius * ddx(vel_z)) / radius;
    const double omega_phi = (vel_y_center + radius * ddx(vel_y) - ddy(vel_x)) / radius;
    result.vorticity = std::sqrt(omega_r * omega_r + omega_theta * omega_theta + omega_phi * omega_phi);
    return result;
}

} // namespace VelocityDiagnostics