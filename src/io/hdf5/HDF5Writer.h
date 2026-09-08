/**
 * @file HDF5Writer.h
 * @brief Declares the HDF5 writing interface shared by plot and checkpoint paths.
 *
 * Workflow:
 * 1. Collect synchronized leaf metadata and field values from the driver.
 * 2. Serialize them through the selected backend with explicit dimensions and geometry.
 * 3. Write restart- or analysis-ready output without changing simulation state.
 */

#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace io {

inline constexpr int checkpoint_format_version = 4;

/** Scientific/state-layout identity required for a verified restart. */
struct CheckpointProvenance {
    bool available = false;
    std::string eos_type;
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
    double time = 0.0;
    double dt_old = 0.0;
    double dt_burn = 0.0;
    int step_count = 0;
    int chk_file_index = 0;
    int plt_file_index = 0;
    int dim = 1;
    int num_species = 0;
    std::string geometry;
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

void write_hdf5_plt_impl(const std::string& filepath, double current_time, int dim, const std::string& geom,
                         const std::vector<size_t>& dims,
                         const std::vector<double>& coord_x, const std::vector<double>& coord_y, const std::vector<double>& coord_z,
                         const std::vector<int>& block_levels, const std::vector<int>& block_mortons,
                         const std::map<std::string, std::vector<double>>& data_map);

void write_hdf5_chk_impl(const std::string& filepath, const CheckpointData& checkpoint);
CheckpointData read_hdf5_chk_impl(const std::string& filepath);

} // namespace io
