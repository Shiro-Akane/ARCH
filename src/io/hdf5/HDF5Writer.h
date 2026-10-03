/**
 * @file HDF5Writer.h
 * @brief Shared HDF5 payloads and interfaces for plots and checkpoints.
 *
 * Callers supply host-side fields and metadata. Checkpoint payloads include
 * native composition, controller state and scientific identity so the reader
 * can validate their layout; this interface owns no CPU or CUDA solver state.
 */

#pragma once
#include <cstdint>
#include "io/chk/CheckpointGeometryIdentity.h"
#include <array>
#include "data/StateDiagnostics.h"
#include <map>
#include <string>
#include <vector>

namespace io {

// On-disk layout discriminator, independent of the ARCH software release.
inline constexpr int checkpoint_format_version = 6;

/** Scientific/state-layout identity required for a verified restart. */
struct CheckpointProvenance {
    bool available = false;
    std::string eos_type;
    std::string gravity_type = "none", gravity_boundary = "none";
    std::vector<double> gravity_controls; // external: gx,gy,gz; self: G,rtol,atol,max_cycles
    double ideal_gamma = 0.0;
    bool burn_enabled = false;
    std::string active_network = "none";
    bool nse_enabled = false;
    // The path is audit metadata only.  Compatibility is determined by the
    // content digest so an unchanged table may be relocated between machines.
    std::string eos_table_path;
    std::string eos_table_sha256;
    std::vector<std::string> species_names;
    std::vector<double> species_A, species_Z;
    std::vector<double> species_gamma, species_Cv;
};

/** Complete restart payload in Morton-sorted AMR leaf and interior-cell order. */
struct CheckpointData {
    arch::state::RepairBudget repairs;
    std::vector<double> state_controls;
    double time = 0.0;
    double dt_old = 0.0;
    double dt_burn = 0.0;
    int step_count = 0;
    int chk_file_index = 0;
    int plt_file_index = 0;
    int dim = 1;
    int num_species = 0;
    std::string geometry;
    CheckpointGeometryIdentity geometry_identity;
    std::size_t cells_per_block = 0;
    bool has_timestep_state = false;
    bool resume_after_regrid = false;
    bool has_enuc_rate = false;
    bool has_mass_fractions = false;
    CheckpointProvenance provenance;
    std::vector<int> levels;
    std::vector<uint32_t> logical_x1, logical_x2, logical_x3;
    std::vector<double> rho, mom_u, mom_v, mom_w, eng, enuc_rate, rhoX;
    // Native evolved composition. rhoX alone cannot preserve X bit-for-bit:
    // division after a rounded multiplication is not an inverse operation.
    std::vector<double> mass_fractions;
};

// Partial evidence supplied from the immutable load boundary and resolved EOS.
// Missing run/config/build/binary identities remain explicitly unknown.
struct PlotSourceIdentity {
    std::string run_id;
    std::string case_id;
    std::string raw_config_sha256;
    std::string binary_sha256;
    std::string eos_type, eos_table_sha256;
    double ideal_gamma = 0.;
    std::string unit_system;
    std::vector<std::string> species_names;
    // Exact resolved constituents; absent vectors remain unknown for legacy callers.
    std::vector<double> species_A, species_Z, species_gamma, species_Cv;
};

// Candidate native metadata for Cartesian 1D/2D leaf interiors only.
// Cell arrays use exactly the Data field flattening; inactive bounds are zero.
struct PlotNativeGrid {
    std::array<std::vector<double>,3> lower, upper;
    std::vector<double> cell_measure;
    std::array<std::vector<uint32_t>,3> logical;
    std::string measure_unit = "unknown", normalization = "unknown";
};

// Declarations come from the actual producer, not inferred by the HDF serializer.
struct PlotFieldMetadata {
    std::string unit = "unknown", basis = "unknown", meaning = "unknown";
    std::string unit_reason = "producer declaration unavailable";
};

// Linux/WSL candidate: checked close, then atomic replacement. Throws on failure.
// Publication alone supplies no scientific provenance, units or native bounds.
void write_hdf5_plt_impl(const std::string& filepath, double current_time, int dim, const std::string& geom,
                         const std::vector<size_t>& dims,
                         const std::vector<double>& coord_x, const std::vector<double>& coord_y, const std::vector<double>& coord_z,
                         const std::vector<int>& block_levels, const std::vector<int>& block_mortons,
                         const std::map<std::string, std::vector<double>>& data_map,
                         const PlotNativeGrid* native_grid = nullptr,
                         const PlotSourceIdentity* source_identity = nullptr,
                         const std::map<std::string, PlotFieldMetadata>* field_metadata = nullptr);

void write_hdf5_chk_impl(const std::string& filepath, const CheckpointData& checkpoint);
CheckpointData read_hdf5_chk_impl(const std::string& filepath);

} // namespace io
