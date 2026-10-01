/**
 * @file ControlRelations.h
 * @brief Shared predicates for relations between already supplied controls.
 *
 * Callers own presence checks. These functions never provide missing values
 * and preserve the existing runtime domains and thresholds.
 */
#pragma once
#include <cmath>
#include <array>
#include <optional>
#include <limits>
#include <string>
#include <vector>
#include <string_view>
#include "amr/topology/Morton.h"
#include "driver/dispatch/PolicyDescriptor.h"

namespace arch::config::relations {
inline bool AtLeast(double value, double lower) { return value >= lower; }
inline bool TimeCap(double cap, double minimum) {
    return cap == -1.0 || (std::isfinite(cap) && cap >= minimum);
}
inline bool HllSpeed(std::string_view solver, bool roe_speed) {
    return dispatch::ascii_iequals(solver, "HLL")
        || dispatch::ascii_iequals(solver, "HLLC") || roe_speed;
}
inline bool Coulomb(double factor, std::string_view eos) {
    return factor == 1.0 || dispatch::ascii_iequals(eos, "helmholtz");
}
inline bool AxisTopology(int second, int third) {
    return third >= 0 && (third == 0 || second > 0);
}
inline bool ActiveExtent(double lower, double upper) {
    return std::isfinite(lower) && std::isfinite(upper - lower) && upper > lower;
}
inline bool RefinementLevels(int lower, int upper) {
    return lower >= 0 && upper >= lower && upper <= amr::kMaxRefinementLevel;
}
inline bool CurvatureThresholds(double refine, double derefine) {
    return std::isfinite(refine) && std::isfinite(derefine)
        && refine <= 1.0 && derefine >= 0.0 && derefine < refine;
}
// Partial view of the existing self-gravity face contract. No missing member
// is replaced by a radius, dimension or boundary choice.
struct GravityTopology {
    std::optional<std::string> geometry, boundary;
    std::optional<int> dimension;
    std::array<std::optional<double>, 3> lower, upper;
    std::array<std::optional<std::string>, 6> faces;
};
template<class Report>
void CheckGravityTopology(const GravityTopology& g, Report report) {
    if (!g.geometry) return;
    const bool curved = *g.geometry == "spherical" || *g.geometry == "cylindrical";
    if (*g.geometry != "cartesian" && !curved)
        report("geometry", "Self-gravity supports Cartesian, cylindrical and spherical geometry.",
               std::vector<std::string>{"gravity_type"});
    if (curved) {
        if (g.boundary && *g.boundary != "isolated")
            report("gravity_boundary", "Curvilinear self-gravity requires isolated gravity boundary.",
                   std::vector<std::string>{"gravity_type", "geometry"});
        if (g.lower[0] && !(*g.lower[0] >= 0.0))
            report("x1_min", "Curvilinear self-gravity requires nonnegative radius.",
                   std::vector<std::string>{"gravity_type", "geometry"});
        if (g.faces[0] && *g.faces[0] != "reflecting")
            report("x1l_boundary_type", "The radial inner boundary requires reflecting fluid flow.",
                   std::vector<std::string>{"gravity_type", "geometry"});
        if (g.dimension && *g.dimension > 1) {
            const int azimuth = *g.dimension - 1;
            const double turn = 2.0 * std::acos(-1.0);
            if (g.lower[azimuth] && g.upper[azimuth]
                && !(std::abs((*g.upper[azimuth] - *g.lower[azimuth]) - turn)
                     <= 64.0 * std::numeric_limits<double>::epsilon() * turn))
                report(azimuth == 1 ? "x2_max" : "x3_max",
                       "Curvilinear gravity requires a full azimuthal turn.",
                       std::vector<std::string>{"geometry", azimuth == 1 ? "x2_min" : "x3_min"});
            if (*g.geometry == "spherical" && *g.dimension == 3) {
                const double pi = std::acos(-1.0);
                if (g.lower[1] && g.upper[1]
                    && !(*g.lower[1] >= 0.0 && *g.upper[1] <= pi))
                    report("x2_min", "Spherical polar bounds must remain within [0,pi].",
                           std::vector<std::string>{"geometry", "x2_max"});
                if (g.lower[1] && *g.lower[1] == 0.0
                    && g.faces[2] && *g.faces[2] != "reflecting")
                    report("x2l_boundary_type",
                           "The north pole requires reflecting fluid flow with coordinate-seam mapping.",
                           std::vector<std::string>{"geometry", "x2_min"});
                if (g.upper[1] && std::abs(*g.upper[1] - pi) <= 1e-12
                    && g.faces[3] && *g.faces[3] != "reflecting")
                    report("x2r_boundary_type",
                           "The south pole requires reflecting fluid flow with coordinate-seam mapping.",
                           std::vector<std::string>{"geometry", "x2_max"});
            }
        }
    } else if (g.boundary && *g.boundary == "isolated"
               && g.dimension && *g.dimension != 3)
        report("gravity_boundary", "Cartesian isolated gravity requires a 3D Newtonian domain.",
               std::vector<std::string>{"geometry", "nblockx2", "nblockx3"});
    if (!g.dimension || !g.boundary) return;
    const int azimuth = curved && *g.dimension > 1 ? *g.dimension - 1 : -1;
    for (int axis = 0; axis < 2 * *g.dimension; ++axis) {
        if (!g.faces[axis]) continue;
        const bool periodic = *g.boundary == "periodic" || axis / 2 == azimuth;
        const auto& face = *g.faces[axis];
        if (!(periodic ? face == "periodic" : face == "outflow" || face == "reflecting"))
            report("gravity_boundary",
                   "Fluid faces must match the gravity topology (periodic azimuth, physical radial/polar faces).",
                   std::vector<std::string>{"geometry", "x" + std::to_string(axis / 2 + 1)
                       + (axis % 2 == 0 ? "l_boundary_type" : "r_boundary_type")});
    }
}
} // namespace arch::config::relations
