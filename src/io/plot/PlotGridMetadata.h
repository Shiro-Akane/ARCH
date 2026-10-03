/**
 * Candidate Plotfile native metadata: reuse the shared geometry metric owner.
 * No EOS, field recovery, independent geometry formula or state mutation.
 */
#pragma once
#include "grid/GridMetrics.h"
#include "io/hdf5/HDF5Writer.h"
#include <stdexcept>

namespace io {
inline bool supports_plot_native_grid(const Grid& grid,
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing) {
    if (semantics == GridMetrics::GeometrySemantics::AxisymmetricRz)
        return grid.geometry == "cylindrical" && grid.dim == 2;
    if (semantics != GridMetrics::GeometrySemantics::Existing)
        throw std::invalid_argument("Unknown Plotfile geometry profile.");
    return grid.geometry == "cartesian" && (grid.dim == 1 || grid.dim == 2);
}
inline void append_plot_native_cell(PlotNativeGrid& output, const Grid& grid,
                                   int i, int j, int k,
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing) {
    if (!supports_plot_native_grid(grid,semantics))
        throw std::invalid_argument("Candidate native Plotfile geometry unsupported.");
    const bool rz = semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
    output.measure_unit = rz ? "cm^3" : grid.dim == 1 ? "cm" : "cm^2";
    output.normalization = rz ? "full_rotation" : grid.dim == 1 ? "per_unit_transverse_area" : "per_unit_transverse_length";
    output.lower[0].push_back(grid.GetFacePosL(i));
    output.upper[0].push_back(grid.GetFacePosR(i));
    const double y_lower = grid.dim >= 2 ? grid.x2_min + (j-grid.ng)*grid.dx2 : 0.;
    output.lower[1].push_back(y_lower);
    // Evaluate the shared face at its integer index, just like the next row's
    // lower face; adding dx2 to a rounded lower face can leave a gap/overlap.
    output.upper[1].push_back(grid.dim >= 2
        ? grid.x2_min + (j-grid.ng+1)*grid.dx2 : 0.);
    output.lower[2].push_back(0.);
    output.upper[2].push_back(0.);
    output.cell_measure.push_back(GridMetrics::CellVolume(
        GridMetrics::make_geometry_view(grid,semantics),i,j,k));
}
} // namespace io
