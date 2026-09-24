/**
 * @file EllipticMesh.h
 * @brief Logical dyadic mesh and physical geometry for a composite scalar operator.
 *
 * Workflow:
 * 1. Bind native Grid geometry and root logical extents once per topology.
 * 2. Refine logical coordinates while retaining the physical radial origin.
 * 3. Supply this description to every composite multigrid level.
 */
#pragma once

#include <array>

#include "core/ArchPortability.h"

namespace arch::elliptic {
enum class Geometry { Cartesian, Cylindrical, Spherical };

struct EllipticMesh {
    int dimension = 1;
    std::array<int, 3> cells{2, 1, 1};
    std::array<double, 3> spacing{1., 1., 1.};
    std::array<double, 3> origin{};
    Geometry geometry = Geometry::Cartesian;
    /** Return the number of root logical cells. */
    ARCH_HOST_DEVICE int size() const { return cells[0] * cells[1] * cells[2]; }
    /** Flatten x-fast logical cell coordinates. */
    ARCH_HOST_DEVICE int index(const std::array<int, 3>& p) const {
        return p[0] + cells[0] * (p[1] + cells[1] * p[2]);
    }
    /** Recover x-fast logical coordinates from a cell index. */
    ARCH_HOST_DEVICE std::array<int, 3> position(int i) const {
        return {i % cells[0], (i / cells[0]) % cells[1], i / (cells[0] * cells[1])};
    }
    /** Flatten an oriented face index with unit normal extent. */
    ARCH_HOST_DEVICE int face_index(std::array<int, 3> p, int axis) const {
        p[axis] = 0;
        return p[0] + (axis == 0 ? 1 : cells[0]) *
            (p[1] + (axis == 1 ? 1 : cells[1]) * p[2]);
    }
};

// Keep the standalone Cartesian reference's source-compatible type name.
using CartesianMesh = EllipticMesh;
} // namespace arch::elliptic
