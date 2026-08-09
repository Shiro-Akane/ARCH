/**
 * @file GridMetrics.h
 * @brief Shared finite-volume measures for Cartesian and curvilinear grids.
 */

/**
 * Workflow:
 * 1. Derive active logical extents and physical coordinates from RuntimeParams.
 * 2. Expose consistent cell, face, and metric information to numerical operators.
 * 3. Preserve compact 1D/2D storage while retaining the common contiguous pool layout.
 */

#pragma once

#include <cmath>

#include "Grid.h"

namespace GridMetrics {

inline double radial_shell_volume(double r_left, double r_right) {
    return (r_right * r_right * r_right - r_left * r_left * r_left) / 3.0;
}

inline double cylindrical_annulus_volume(double r_left, double r_right) {
    return 0.5 * (r_right * r_right - r_left * r_left);
}

inline double CellVolume(const Grid& grid, int i, int j, int /*k*/) {
    const double r_left = grid.GetFacePosL(i);
    const double r_right = grid.GetFacePosR(i);
    if (grid.geometry == "cartesian") {
        double volume = grid.dx1;
        if (grid.dim >= 2) volume *= grid.dx2;
        if (grid.dim == 3) volume *= grid.dx3;
        return volume;
    }
    if (grid.geometry == "cylindrical") {
        double volume = cylindrical_annulus_volume(r_left, r_right);
        if (grid.dim >= 2) volume *= grid.dx2;
        if (grid.dim == 3) volume *= grid.dx3;
        return volume;
    }

    // 2-D spherical grids are the project's polar (r, phi) specialization.
    if (grid.dim == 1) return radial_shell_volume(r_left, r_right);
    if (grid.dim == 2) return cylindrical_annulus_volume(r_left, r_right) * grid.dx2;

    const double theta_left = grid.x2_min + (j - grid.ng) * grid.dx2;
    const double theta_right = theta_left + grid.dx2;
    return radial_shell_volume(r_left, r_right)
         * (std::cos(theta_left) - std::cos(theta_right)) * grid.dx3;
}

inline double FaceArea(const Grid& grid, int dir, int i, int j, int /*k*/, bool high_face) {
    const double r_left = grid.GetFacePosL(i);
    const double r_right = grid.GetFacePosR(i);
    const double r_face = high_face ? r_right : r_left;
    if (grid.geometry == "cartesian") {
        if (dir == 0) return (grid.dim >= 2 ? grid.dx2 : 1.0) * (grid.dim == 3 ? grid.dx3 : 1.0);
        if (dir == 1) return grid.dx1 * (grid.dim == 3 ? grid.dx3 : 1.0);
        return grid.dx1 * grid.dx2;
    }
    if (grid.geometry == "cylindrical") {
        const double annulus = cylindrical_annulus_volume(r_left, r_right);
        if (dir == 0) return r_face * (grid.dim >= 2 ? grid.dx2 : 1.0) * (grid.dim == 3 ? grid.dx3 : 1.0);
        if (dir == 1) return grid.dim == 2 ? (r_right - r_left) : annulus * grid.dx3;
        return (r_right - r_left) * grid.dx2;
    }

    if (grid.dim == 1) return r_face * r_face;
    if (grid.dim == 2) return dir == 0 ? r_face * grid.dx2 : (r_right - r_left);

    const double theta_left = grid.x2_min + (j - grid.ng) * grid.dx2;
    const double theta_right = theta_left + grid.dx2;
    const double theta_face = high_face ? theta_right : theta_left;
    const double shell = radial_shell_volume(r_left, r_right);
    if (dir == 0) return r_face * r_face * (std::cos(theta_left) - std::cos(theta_right)) * grid.dx3;
    if (dir == 1) return shell * std::sin(theta_face) * grid.dx3;
    return shell * grid.dx2;
}

} // namespace GridMetrics
