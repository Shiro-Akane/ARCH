/**
 * Plot producer declarations for the existing Cartesian 1D/2D fields.
 * No field calculation, EOS fallback or geometry conversion belongs here.
 */
#pragma once
#include "data/FieldUnits.h"
#include "io/hdf5/HDF5Writer.h"
#include <string_view>
namespace io {
inline PlotFieldMetadata plot_field_metadata(std::string_view name, bool cartesian) {
    PlotFieldMetadata m;
    if (name == "DENS") m.meaning = "mass_density";
    else if (name == "PRES") m.meaning = "pressure";
    else if (name == "TEMP") m.meaning = "temperature";
    else if (name == "ENER") m.meaning = "total_energy_density";
    else if (name == "VELX" || name == "VELY" || name == "VELZ") {
        m.meaning = "velocity_component";
        m.basis = cartesian ? "cartesian" : "unknown";
    }
    else if (name == "ENTR") {
        m.meaning = "pressure_density_gamma1_proxy";
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
    if (m.meaning != "unknown" && m.meaning != "velocity_component") m.basis = "scalar";
    if (const auto unit = arch::fields::cgs_unit(name); !unit.empty()) m.unit = unit;
    if (m.unit != "unknown") m.unit_reason.clear();
    return m;
}
inline PlotFieldMetadata plot_species_metadata() {
    return {"1", "scalar", "species_mass_fraction", ""};
}
}
