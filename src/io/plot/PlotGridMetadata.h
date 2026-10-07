/**
 * @file PlotGridMetadata.h
 * @brief Native Plotfile bounds and measures from the shared grid metric owner.
 * Workflow:
 * 1. Confirm an explicit supported native coordinate semantic profile.
 * 2. Emit exact indexed native faces in the field flattening order.
 * 3. Obtain V and optional W from shared GridMetrics without recovering geometry
 *    from cell-center spacing or changing any evolved physical state.
 */
#pragma once

#include <stdexcept>

#include "grid/GridMetrics.h"
#include "io/hdf5/HDF5Writer.h"

namespace io {
inline bool supports_plot_native_grid(const Grid& grid,
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing) {
    if (semantics == GridMetrics::GeometrySemantics::AxisymmetricRz)
        return grid.geometry == "cylindrical" && grid.dim == 2;
    if (semantics != GridMetrics::GeometrySemantics::Existing)
        throw std::invalid_argument("Unknown Plotfile geometry profile.");
    return (grid.geometry == "cartesian" || grid.geometry == "cylindrical"
        || grid.geometry == "spherical") && grid.dim >= 1 && grid.dim <= 3;
}
inline void append_plot_native_cell(PlotNativeGrid& output, const Grid& grid,
                                   int i, int j, int k,
    GridMetrics::GeometrySemantics semantics = GridMetrics::GeometrySemantics::Existing) {
    if (!supports_plot_native_grid(grid,semantics))
        throw std::invalid_argument("Candidate native Plotfile geometry unsupported.");
    const bool rz = semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
    output.axes={"inactive","inactive","inactive"};
    output.axis_units={"inactive","inactive","inactive"};
    if(rz) {
        output.axes={"r","z","inactive"}; output.axis_units={"cm","cm","inactive"};
        output.measure_unit="cm^3"; output.normalization="full_rotation";
        output.measure_convention="full-rotation-axisymmetric-ring";
    } else if(grid.geometry=="cartesian") {
        for(int axis=0;axis<grid.dim;++axis){output.axes[axis]=std::string(1,char('x'+axis));output.axis_units[axis]="cm";}
        output.measure_unit=grid.dim==1?"cm":grid.dim==2?"cm^2":"cm^3";
        output.normalization=grid.dim==1?"per_unit_transverse_area":grid.dim==2?"per_unit_transverse_length":"full_volume";
    } else {
        output.axes[0]="r"; output.axis_units[0]="cm";
        if(grid.dim==2){output.axes[1]="phi";output.axis_units[1]="rad";}
        if(grid.dim==3) {
            output.axes[1]=grid.geometry=="cylindrical"?"z":"theta";
            output.axis_units[1]=grid.geometry=="cylindrical"?"cm":"rad";
            output.axes[2]="phi";output.axis_units[2]="rad";
        }
        output.measure_convention="GridMetrics-native-coordinate-integral";
        output.measure_unit=grid.dim==3 || (grid.geometry=="spherical" && grid.dim==1)?"cm^3":"cm^2";
        output.normalization=grid.dim==3?"full_volume":grid.geometry=="spherical" && grid.dim==1?
            "per_unit_solid_angle":grid.dim==1?"per_unit_azimuth_and_axial_length":"per_unit_transverse_length";
    }
    output.lower[0].push_back(grid.GetFacePosL(i));
    output.upper[0].push_back(grid.GetFacePosR(i));
    const double y_lower = grid.dim >= 2 ? grid.GetAxialFacePosL(j) : 0.;
    output.lower[1].push_back(y_lower);
    // Evaluate the shared face at its integer index, just like the next row's
    // lower face; adding dx2 to a rounded lower face can leave a gap/overlap.
    output.upper[1].push_back(grid.dim >= 2
        ? grid.GetAxialFacePosR(j) : 0.);
    output.lower[2].push_back(grid.dim == 3 ? grid.x3_min+(k-grid.ng)*grid.dx3 : 0.);
    output.upper[2].push_back(grid.dim == 3 ? grid.x3_min+(k-grid.ng+1)*grid.dx3 : 0.);
    if (rz)
        output.angular_measure.push_back(GridMetrics::Rz::AngularMomentumMeasure(
            GridMetrics::make_geometry_view(grid,semantics),i,j));
    output.cell_measure.push_back(GridMetrics::CellVolume(
        GridMetrics::make_geometry_view(grid,semantics),i,j,k));
}
} // namespace io
