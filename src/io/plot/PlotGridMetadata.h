/**
 * Candidate Plotfile native metadata: reuse the shared geometry metric owner.
 * No EOS, field recovery, independent geometry formula or state mutation.
 */
#pragma once
#include "grid/GridMetrics.h"
#include "io/hdf5/HDF5Writer.h"
#include <stdexcept>

namespace io {
inline bool supports_plot_native_grid(const Grid& grid) {
    return grid.geometry == "cartesian" && (grid.dim == 1 || grid.dim == 2);
}
inline void append_plot_native_cell(PlotNativeGrid& output, const Grid& grid,
                                   int i, int j, int k) {
    if (!supports_plot_native_grid(grid))
        throw std::invalid_argument("Candidate native Plotfile geometry unsupported.");
    output.measure_unit = grid.dim == 1 ? "cm" : "cm^2";
    output.normalization = grid.dim == 1 ? "per_unit_transverse_area" : "per_unit_transverse_length";
    output.lower[0].push_back(grid.GetFacePosL(i));
    output.upper[0].push_back(grid.GetFacePosR(i));
    const double y_lower = grid.dim >= 2 ? grid.x2_min + (j-grid.ng)*grid.dx2 : 0.;
    output.lower[1].push_back(y_lower);
    output.upper[1].push_back(grid.dim >= 2 ? y_lower + grid.dx2 : 0.);
    output.lower[2].push_back(0.);
    output.upper[2].push_back(0.);
    output.cell_measure.push_back(GridMetrics::CellVolume(
        GridMetrics::make_geometry_view(grid),i,j,k));
}
} // namespace io
