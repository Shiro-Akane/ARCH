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

#include "HDF5Writer.h"

#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>
#include <highfive/H5File.hpp>

using namespace HighFive;

namespace io {

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
    const size_t cells = blocks * checkpoint.cells_per_block;
    if (blocks == 0 || checkpoint.cells_per_block == 0 ||
        checkpoint.logical_x1.size() != blocks || checkpoint.logical_x2.size() != blocks ||
        checkpoint.logical_x3.size() != blocks || checkpoint.rho.size() != cells ||
        checkpoint.mom_u.size() != cells || checkpoint.mom_v.size() != cells ||
        checkpoint.mom_w.size() != cells || checkpoint.eng.size() != cells ||
        (checkpoint.num_species > 0 &&
         checkpoint.rhoX.size() != static_cast<size_t>(checkpoint.num_species) * cells)) {
        throw std::invalid_argument("Checkpoint payload dimensions are inconsistent.");
    }
    try {
        File file(filepath, File::ReadWrite | File::Create | File::Truncate);
        file.createAttribute("checkpoint_version", 1);
        file.createAttribute("time", checkpoint.time);
        file.createAttribute("step", checkpoint.step_count);
        file.createAttribute("chk_index", checkpoint.chk_file_index);
        file.createAttribute("plt_index", checkpoint.plt_file_index);
        file.createAttribute("dim", checkpoint.dim);
        file.createAttribute("geometry", checkpoint.geometry);
        file.createAttribute("num_species", checkpoint.num_species);
        file.createAttribute("cells_per_block", checkpoint.cells_per_block);

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
        if (checkpoint.num_species > 0) {
            const std::vector<size_t> dims = {
                static_cast<size_t>(checkpoint.num_species), blocks, checkpoint.cells_per_block};
            DataSet dataset = data.createDataSet<double>("rhoX", DataSpace(dims));
            dataset.write_raw(checkpoint.rhoX.data());
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
        if (version != 1) throw std::runtime_error("Unsupported checkpoint format version.");
        file.getAttribute("time").read(checkpoint.time);
        file.getAttribute("step").read(checkpoint.step_count);
        file.getAttribute("chk_index").read(checkpoint.chk_file_index);
        file.getAttribute("plt_index").read(checkpoint.plt_file_index);
        file.getAttribute("dim").read(checkpoint.dim);
        file.getAttribute("geometry").read(checkpoint.geometry);
        file.getAttribute("num_species").read(checkpoint.num_species);
        file.getAttribute("cells_per_block").read(checkpoint.cells_per_block);

        Group blocks_group = file.getGroup("Blocks");
        blocks_group.getDataSet("level").read(checkpoint.levels);
        blocks_group.getDataSet("logical_x1").read(checkpoint.logical_x1);
        blocks_group.getDataSet("logical_x2").read(checkpoint.logical_x2);
        blocks_group.getDataSet("logical_x3").read(checkpoint.logical_x3);
        Group data = file.getGroup("Data");
        data.getDataSet("rho").read(checkpoint.rho);
        data.getDataSet("mom_u").read(checkpoint.mom_u);
        data.getDataSet("mom_v").read(checkpoint.mom_v);
        data.getDataSet("mom_w").read(checkpoint.mom_w);
        data.getDataSet("eng").read(checkpoint.eng);
        if (checkpoint.num_species > 0) data.getDataSet("rhoX").read(checkpoint.rhoX);

        const size_t cells = checkpoint.levels.size() * checkpoint.cells_per_block;
        if (checkpoint.levels.empty() || checkpoint.logical_x1.size() != checkpoint.levels.size() ||
            checkpoint.logical_x2.size() != checkpoint.levels.size() ||
            checkpoint.logical_x3.size() != checkpoint.levels.size() ||
            checkpoint.rho.size() != cells || checkpoint.mom_u.size() != cells ||
            checkpoint.mom_v.size() != cells || checkpoint.mom_w.size() != cells ||
            checkpoint.eng.size() != cells ||
            (checkpoint.num_species > 0 &&
             checkpoint.rhoX.size() != static_cast<size_t>(checkpoint.num_species) * cells)) {
            throw std::runtime_error("Checkpoint datasets have inconsistent dimensions.");
        }
        return checkpoint;
    } catch (const Exception& err) {
        throw std::runtime_error("Checkpoint read failed: " + std::string(err.what()));
    }
}

} // namespace io
