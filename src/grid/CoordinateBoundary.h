/**
 * @file CoordinateBoundary.h
 * @brief Shared identity of native origin and polar coordinate joins.
 *
 * Workflow:
 * 1. Receive the actual geometry, active dimension, axis and domain endpoint.
 * 2. Identify r=0 or the 3D spherical theta=0/pi chart join exactly.
 * 3. Let physical callbacks and Poisson assembly retain the regularity owner.
 *
 * A small positive radius or polar angle is a physical surface, not a join.
 * The classification therefore has no dimensional area threshold. In 2D,
 * spherical/cylindrical axis 1 is phi and has no polar join. Power-of-two root
 * extents retain supplied endpoints under mesh scaling.
 */
#pragma once

#include <cmath>

#include "grid/GridGeometryView.h"

namespace GridMetrics {
/** Identify exact zero-area native chart endpoints before evaluating a flux. */
template<class GeometryKind>
inline bool IsCoordinateJoin(GeometryKind geometry, int dimension, int axis, double coordinate) {
    if (geometry != GeometryKind::Cylindrical && geometry != GeometryKind::Spherical) return false;
    if (axis == 0) return coordinate == 0.;
    return geometry == GeometryKind::Spherical && dimension == 3 && axis == 1
        && (coordinate == 0. || coordinate == std::acos(-1.));
}
} // namespace GridMetrics
