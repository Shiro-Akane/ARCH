/**
 * @file PlotFieldMetadata.h
 * @brief Units and physical meaning for stored cell-centered Plotfile fields.
 * Workflow:
 * 1. Identify the existing stored field without changing its calculation.
 * 2. Attach CGS units and known basis/meaning from the shared field-unit owner.
 *    Gravity leaves stay declarations only: GPOT is a scalar potential and
 *    GAC* a vector component whose chart is supplied by the producer.
 * 3. Preserve explicit reasons where a physical unit cannot be declared.
 */
#pragma once

#include <string_view>

#include "data/FieldUnits.h"
#include "io/hdf5/HDF5Writer.h"
namespace io {
/** Describe one existing output field; values and scientific kernels stay unchanged. */
inline PlotFieldMetadata plot_field_metadata(std::string_view name, bool cartesian) {
    PlotFieldMetadata m;
    if (name == "JENS") m.meaning = "jeans_length_over_max_active_physical_spacing";
    else if (name == "DENS") m.meaning = "mass_density";
    else if (name == "PRES") m.meaning = "pressure";
    else if (name == "TEMP") m.meaning = "temperature";
    else if (name == "ENER") m.meaning = "total_energy_density";
    else if (name == "VELX" || name == "VELY" || name == "VELZ") {
        m.meaning = "velocity_component";
        m.basis = cartesian ? "cartesian" : "unknown";
    }
    else if (name == "GPOT") m.meaning = "gravitational_potential";
    else if (name == "GACX" || name == "GACY" || name == "GACZ") {
        m.meaning = "gravitational_acceleration_component";
        m.basis = cartesian ? "cartesian" : "unknown";
    }
    else if (name == "ENTR") {
        m.meaning = "pressure_density_proxy";
        m.unit_reason = "P/rho^Gamma1; exponent from local EOS; not thermodynamic entropy";
    }
    else if (name == "ENUC") {
        m.meaning = "specific_burning_energy_rate";
        m.unit = "erg/g/s";
    }
    else if (name == "VORT") {
        m.meaning = "vorticity_magnitude"; m.unit = "1/s";
    }
    else if (name == "DIVV") {
        m.meaning = "velocity_divergence"; m.unit = "1/s";
    }
    // Vector components keep their declared chart; only true scalars are forced.
    if (m.meaning != "unknown" && m.meaning != "velocity_component"
        && m.meaning != "gravitational_acceleration_component") m.basis = "scalar";
    if (const auto unit = arch::fields::cgs_unit(name); !unit.empty()) m.unit = unit;
    if (m.unit != "unknown") m.unit_reason.clear();
    return m;
}
inline PlotFieldMetadata plot_species_metadata() {
    return {"1", "scalar", "species_mass_fraction", ""};
}
}
