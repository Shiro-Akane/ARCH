/**
 * @file HDF5Writer.cpp
 * @brief Implements low-level HDF5 dataset and attribute emission.
 *
 * Workflow:
 * 1. Collect synchronized leaf metadata and field values from the driver.
 * 2. Serialize them through the selected backend with explicit dimensions and geometry.
 * 3. Write restart- or analysis-ready output without changing simulation state.
 */

#include <iostream>
#include <algorithm>
#include <cmath>
#include <limits>

#include "HDF5Writer.h"

#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>
#include <highfive/H5File.hpp>

using namespace HighFive;

namespace io {

namespace {

bool has_valid_timestep_state(const CheckpointData& checkpoint)
{
    return checkpoint.has_timestep_state &&
           std::isfinite(checkpoint.dt_old) && checkpoint.dt_old > 0.0 &&
           std::isfinite(checkpoint.dt_burn) && checkpoint.dt_burn > 0.0;
}

bool has_consistent_checkpoint_payload(const CheckpointData& checkpoint)
{
    const size_t blocks = checkpoint.levels.size();
    if (blocks == 0 || checkpoint.cells_per_block == 0 ||
        checkpoint.num_species < 0 ||
        checkpoint.cells_per_block >
            std::numeric_limits<size_t>::max() / blocks) {
        return false;
    }
    const size_t cells = blocks * checkpoint.cells_per_block;
    if (checkpoint.num_species > 0 &&
        cells > std::numeric_limits<size_t>::max() /
                    static_cast<size_t>(checkpoint.num_species)) {
        return false;
    }
    return
           checkpoint.logical_x1.size() == blocks &&
           checkpoint.logical_x2.size() == blocks &&
           checkpoint.logical_x3.size() == blocks &&
           checkpoint.rho.size() == cells &&
           checkpoint.mom_u.size() == cells &&
           checkpoint.mom_v.size() == cells &&
           checkpoint.mom_w.size() == cells &&
           checkpoint.eng.size() == cells &&
           (!checkpoint.has_enuc_rate ||
            checkpoint.enuc_rate.size() == cells) &&
           checkpoint.rhoX.size() ==
               static_cast<size_t>(checkpoint.num_species) * cells;
}

bool has_consistent_checkpoint_provenance(const CheckpointData& checkpoint)
{
    const auto& provenance = checkpoint.provenance;
    const auto species = static_cast<size_t>(checkpoint.num_species);
    const auto is_canonical_identity = [](const std::string& identity) {
        return std::none_of(identity.begin(), identity.end(), [](char character) {
            return character >= 'A' && character <= 'Z';
        });
    };
    if (!provenance.available || provenance.eos_type.empty()
        || provenance.active_network.empty()
        || !is_canonical_identity(provenance.eos_type)
        || !is_canonical_identity(provenance.active_network)
        || provenance.species_names.size() != species
        || provenance.species_A.size() != species
        || provenance.species_Z.size() != species
        || provenance.species_gamma.size() != species
        || provenance.species_Cv.size() != species) {
        return false;
    }
    if (std::any_of(provenance.species_names.begin(),
                    provenance.species_names.end(),
                    [](const std::string& name) { return name.empty(); })) {
        return false;
    }
    if (provenance.burn_enabled) {
        if (provenance.active_network == "none") return false;
    } else if (provenance.active_network != "none"
               || provenance.nse_enabled) {
        return false;
    }
    if (provenance.eos_type == "ideal") {
        return std::isfinite(provenance.ideal_gamma)
            && provenance.ideal_gamma > 1.0
            && provenance.eos_table_path.empty()
            && provenance.eos_table_sha256.empty();
    }
    const bool valid_sha256 = provenance.eos_table_sha256.size() == 64
        && std::all_of(provenance.eos_table_sha256.begin(),
                       provenance.eos_table_sha256.end(), [](char character) {
                           return (character >= '0' && character <= '9')
                               || (character >= 'a' && character <= 'f');
                       });
    return !provenance.eos_table_path.empty()
        && valid_sha256;
}

} // namespace

void write_hdf5_plt_impl(const std::string& filepath, double current_time, int dim, const std::string& geom,
                         const std::vector<size_t>& dims,
                         const std::vector<double>& coord_x, const std::vector<double>& coord_y, const std::vector<double>& coord_z,
                         const std::vector<int>& block_levels, const std::vector<int>& block_mortons,
                         const std::map<std::string, std::vector<double>>& data_map)
{
    try
    {
        File file(filepath, File::ReadWrite | File::Create | File::Truncate);

        file.createAttribute("time", current_time);
        file.createAttribute("dim", dim);
        file.createAttribute("geometry", geom);

        Group grid_group = file.createGroup("Grid");
        Group data_group = file.createGroup("Data");

        grid_group.createDataSet("x", coord_x);
        grid_group.createDataSet("y", coord_y);
        grid_group.createDataSet("z", coord_z);
        grid_group.createDataSet("level", block_levels);
        grid_group.createDataSet("morton", block_mortons);

        for (const auto& [name, buffer] : data_map) {
            DataSet ds = data_group.createDataSet<double>(name, DataSpace(dims));
            ds.write_raw(buffer.data());
        }

        std::cout << "[IO] Saved PLT: " << filepath << " at t=" << current_time << std::endl;
    }
    catch (Exception &err)
    {
        std::cerr << "[IO Error] PLT write failed: " << err.what() << std::endl;
    }
}

void write_hdf5_chk_impl(const std::string& filepath, const CheckpointData& checkpoint)
{
    const size_t blocks = checkpoint.levels.size();
    if (!has_consistent_checkpoint_payload(checkpoint)) {
        throw std::invalid_argument("Checkpoint payload dimensions are inconsistent.");
    }
    if (!has_valid_timestep_state(checkpoint)) {
        throw std::invalid_argument("Checkpoint timestep-controller state is invalid.");
    }
    if (!checkpoint.has_enuc_rate) {
        throw std::invalid_argument(
            "Checkpoint ENUC restart state is unavailable.");
    }
    if (!has_consistent_checkpoint_provenance(checkpoint)) {
        throw std::invalid_argument(
            "Checkpoint scientific provenance is inconsistent.");
    }
    try {
        File file(filepath, File::ReadWrite | File::Create | File::Truncate);
        file.createAttribute("checkpoint_version", 3);
        file.createAttribute("time", checkpoint.time);
        file.createAttribute("dt_old", checkpoint.dt_old);
        file.createAttribute("dt_burn", checkpoint.dt_burn);
        file.createAttribute("resume_after_regrid", checkpoint.resume_after_regrid ? 1 : 0);
        file.createAttribute("step", checkpoint.step_count);
        file.createAttribute("chk_index", checkpoint.chk_file_index);
        file.createAttribute("plt_index", checkpoint.plt_file_index);
        file.createAttribute("dim", checkpoint.dim);
        file.createAttribute("geometry", checkpoint.geometry);
        file.createAttribute("num_species", checkpoint.num_species);
        file.createAttribute("cells_per_block", checkpoint.cells_per_block);
        file.createAttribute("eos_type", checkpoint.provenance.eos_type);
        file.createAttribute("ideal_gamma", checkpoint.provenance.ideal_gamma);
        file.createAttribute(
            "burn_enabled", checkpoint.provenance.burn_enabled ? 1 : 0);
        file.createAttribute(
            "active_network", checkpoint.provenance.active_network);
        file.createAttribute(
            "nse_enabled", checkpoint.provenance.nse_enabled ? 1 : 0);
        file.createAttribute(
            "eos_table_path", checkpoint.provenance.eos_table_path);
        file.createAttribute(
            "eos_table_sha256", checkpoint.provenance.eos_table_sha256);

        Group blocks_group = file.createGroup("Blocks");
        blocks_group.createDataSet("level", checkpoint.levels);
        blocks_group.createDataSet("logical_x1", checkpoint.logical_x1);
        blocks_group.createDataSet("logical_x2", checkpoint.logical_x2);
        blocks_group.createDataSet("logical_x3", checkpoint.logical_x3);

        Group data = file.createGroup("Data");
        const std::vector<size_t> field_dims = {blocks, checkpoint.cells_per_block};
        const auto write_field = [&](const std::string& name, const std::vector<double>& values) {
            DataSet dataset = data.createDataSet<double>(name, DataSpace(field_dims));
            dataset.write_raw(values.data());
        };
        write_field("rho", checkpoint.rho);
        write_field("mom_u", checkpoint.mom_u);
        write_field("mom_v", checkpoint.mom_v);
        write_field("mom_w", checkpoint.mom_w);
        write_field("eng", checkpoint.eng);
        write_field("enuc_rate", checkpoint.enuc_rate);
        if (checkpoint.num_species > 0) {
            const std::vector<size_t> dims = {
                static_cast<size_t>(checkpoint.num_species), blocks, checkpoint.cells_per_block};
            DataSet dataset = data.createDataSet<double>("rhoX", DataSpace(dims));
            dataset.write_raw(checkpoint.rhoX.data());
            Group species = file.createGroup("Species");
            species.createDataSet("name", checkpoint.provenance.species_names);
            species.createDataSet("A", checkpoint.provenance.species_A);
            species.createDataSet("Z", checkpoint.provenance.species_Z);
            species.createDataSet("gamma", checkpoint.provenance.species_gamma);
            species.createDataSet("Cv", checkpoint.provenance.species_Cv);
        }
        std::cout << "[IO] Saved CHK: " << filepath << " at step "
                  << checkpoint.step_count << " with " << blocks << " AMR leaves." << std::endl;
    } catch (const Exception& err) {
        throw std::runtime_error("Checkpoint write failed: " + std::string(err.what()));
    }
}

CheckpointData read_hdf5_chk_impl(const std::string& filepath)
{
    try {
        File file(filepath, File::ReadOnly);
        CheckpointData checkpoint;
        int version = 0;
        file.getAttribute("checkpoint_version").read(version);
        if (version != 1 && version != 2 && version != 3)
            throw std::runtime_error("Unsupported checkpoint format version.");
        file.getAttribute("time").read(checkpoint.time);
        if (version >= 2) {
            int resume_after_regrid = 0;
            file.getAttribute("dt_old").read(checkpoint.dt_old);
            file.getAttribute("dt_burn").read(checkpoint.dt_burn);
            file.getAttribute("resume_after_regrid").read(resume_after_regrid);
            checkpoint.has_timestep_state = true;
            checkpoint.resume_after_regrid = resume_after_regrid != 0;
            if (!has_valid_timestep_state(checkpoint)) {
                throw std::runtime_error("Checkpoint timestep-controller state is invalid.");
            }
        }
        file.getAttribute("step").read(checkpoint.step_count);
        file.getAttribute("chk_index").read(checkpoint.chk_file_index);
        file.getAttribute("plt_index").read(checkpoint.plt_file_index);
        file.getAttribute("dim").read(checkpoint.dim);
        file.getAttribute("geometry").read(checkpoint.geometry);
        file.getAttribute("num_species").read(checkpoint.num_species);
        file.getAttribute("cells_per_block").read(checkpoint.cells_per_block);
        if (version >= 3) {
            int burn_enabled = 0;
            int nse_enabled = 0;
            checkpoint.provenance.available = true;
            file.getAttribute("eos_type").read(checkpoint.provenance.eos_type);
            file.getAttribute("ideal_gamma").read(
                checkpoint.provenance.ideal_gamma);
            file.getAttribute("burn_enabled").read(burn_enabled);
            file.getAttribute("active_network").read(
                checkpoint.provenance.active_network);
            file.getAttribute("nse_enabled").read(nse_enabled);
            if ((burn_enabled != 0 && burn_enabled != 1)
                || (nse_enabled != 0 && nse_enabled != 1)) {
                throw std::runtime_error(
                    "Checkpoint burn/NSE provenance flags are not Boolean.");
            }
            checkpoint.provenance.burn_enabled = burn_enabled != 0;
            checkpoint.provenance.nse_enabled = nse_enabled != 0;
            file.getAttribute("eos_table_path").read(
                checkpoint.provenance.eos_table_path);
            file.getAttribute("eos_table_sha256").read(
                checkpoint.provenance.eos_table_sha256);
        }

        Group blocks_group = file.getGroup("Blocks");
        blocks_group.getDataSet("level").read(checkpoint.levels);
        blocks_group.getDataSet("logical_x1").read(checkpoint.logical_x1);
        blocks_group.getDataSet("logical_x2").read(checkpoint.logical_x2);
        blocks_group.getDataSet("logical_x3").read(checkpoint.logical_x3);
        Group data = file.getGroup("Data");
        const size_t blocks = checkpoint.levels.size();
        const std::vector<size_t> field_dims = {blocks, checkpoint.cells_per_block};
        const auto read_field = [&](const std::string& name,
                                    const std::vector<size_t>& expected_dims,
                                    std::vector<double>& values) {
            DataSet dataset = data.getDataSet(name);
            const auto actual_dims = dataset.getDimensions();
            if (actual_dims != expected_dims)
                throw std::runtime_error("Checkpoint dataset '" + name + "' has incompatible dimensions.");
            size_t count = 1;
            for (const size_t extent : actual_dims) {
                if (extent == 0 ||
                    count > std::numeric_limits<size_t>::max() / extent) {
                    throw std::runtime_error(
                        "Checkpoint dataset '" + name + "' size overflows.");
                }
                count *= extent;
            }
            values.resize(count);
            dataset.read(values.data());
        };
        read_field("rho", field_dims, checkpoint.rho);
        read_field("mom_u", field_dims, checkpoint.mom_u);
        read_field("mom_v", field_dims, checkpoint.mom_v);
        read_field("mom_w", field_dims, checkpoint.mom_w);
        read_field("eng", field_dims, checkpoint.eng);
        if (version >= 3) {
            read_field("enuc_rate", field_dims, checkpoint.enuc_rate);
            checkpoint.has_enuc_rate = true;
        }
        if (checkpoint.num_species > 0) {
            read_field("rhoX", {static_cast<size_t>(checkpoint.num_species),
                                blocks, checkpoint.cells_per_block}, checkpoint.rhoX);
            if (version >= 3) {
                Group species = file.getGroup("Species");
                species.getDataSet("name").read(
                    checkpoint.provenance.species_names);
                species.getDataSet("A").read(checkpoint.provenance.species_A);
                species.getDataSet("Z").read(checkpoint.provenance.species_Z);
                species.getDataSet("gamma").read(
                    checkpoint.provenance.species_gamma);
                species.getDataSet("Cv").read(
                    checkpoint.provenance.species_Cv);
            }
        }

        if (!has_consistent_checkpoint_payload(checkpoint)) {
            throw std::runtime_error("Checkpoint datasets have inconsistent dimensions.");
        }
        if (version >= 3 && !has_consistent_checkpoint_provenance(checkpoint)) {
            throw std::runtime_error(
                "Checkpoint scientific provenance is inconsistent.");
        }
        return checkpoint;
    } catch (const Exception& err) {
        throw std::runtime_error("Checkpoint read failed: " + std::string(err.what()));
    }
}

} // namespace io
