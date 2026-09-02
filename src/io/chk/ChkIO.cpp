/**
 * @file ChkIO.cpp
 * @brief Serializes and restores checkpoint metadata and AMR block state.
 *
 * Workflow:
 * 1. Collect synchronized leaf metadata and field values from the driver.
 * 2. Serialize them through the selected backend with explicit dimensions and geometry.
 * 3. Write restart- or analysis-ready output without changing simulation state.
 */

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "../../amr/AMRControl.h"
#include "../../core/RuntimeParams.h" // For SimConfig
#include "../../data/GlobalDefs.h"

#include "../IO.h"
#include "CheckpointCompatibility.h"
#include "../hdf5/HDF5Writer.h"
#include "../../physics/species/Species.h"

namespace fs = std::filesystem;

// Checkpoint output for restart.
namespace {

size_t checkpoint_cells_per_block(int dim)
{
    return static_cast<size_t>(amr::BLOCK_NX)
         * static_cast<size_t>(dim >= 2 ? amr::BLOCK_NY : 1)
         * static_cast<size_t>(dim == 3 ? amr::BLOCK_NZ : 1);
}

} // namespace

// 2. Checkpoint file output
void write_chk(amr::AMRControl &amr_ctrl,
               int chk_file_index, int plt_file_index,
               int step_count, double current_time,
               double dt_old, double dt_burn,
               bool resume_after_regrid,
               const SimConfig &config, const SpeciesManager &specs,
               const io::CheckpointProvenance &provenance)
{
    if (!fs::exists(config.io.out_dir)) fs::create_directories(config.io.out_dir);

    std::ostringstream filename;
    filename << config.io.out_dir << "/" << config.io.base_name << "_chk_"
             << std::setw(4) << std::setfill('0') << chk_file_index << ".h5";

    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    if (active_blocks.empty()) throw std::runtime_error("Cannot checkpoint an empty AMR hierarchy.");

    const int dim = amr_ctrl.tree->GetRootGridDim();
    const size_t cells_per_block = checkpoint_cells_per_block(dim);
    const size_t field_size = active_blocks.size() * cells_per_block;
    io::CheckpointData checkpoint;
    checkpoint.time = current_time;
    checkpoint.dt_old = dt_old;
    checkpoint.dt_burn = dt_burn;
    checkpoint.step_count = step_count;
    checkpoint.chk_file_index = chk_file_index;
    checkpoint.plt_file_index = plt_file_index;
    checkpoint.dim = dim;
    checkpoint.geometry = config.grid.geometry;
    checkpoint.cells_per_block = cells_per_block;
    checkpoint.has_timestep_state = true;
    checkpoint.resume_after_regrid = resume_after_regrid;
    checkpoint.has_enuc_rate = true;
    checkpoint.provenance = provenance;
    checkpoint.num_species = amr_ctrl.pool->GetBlock(active_blocks.front()).fluid_state.GetNumSpecies();
    if (checkpoint.num_species != specs.count())
        throw std::runtime_error(
            "Checkpoint state and registered species counts disagree.");
    checkpoint.levels.reserve(active_blocks.size());
    checkpoint.logical_x1.reserve(active_blocks.size());
    checkpoint.logical_x2.reserve(active_blocks.size());
    checkpoint.logical_x3.reserve(active_blocks.size());
    checkpoint.rho.resize(field_size);
    checkpoint.mom_u.resize(field_size);
    checkpoint.mom_v.resize(field_size);
    checkpoint.mom_w.resize(field_size);
    checkpoint.eng.resize(field_size);
    checkpoint.enuc_rate.resize(field_size);
    checkpoint.rhoX.resize(static_cast<size_t>(checkpoint.num_species) * field_size);

    for (size_t block_index = 0; block_index < active_blocks.size(); ++block_index) {
        const amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks[block_index]);
        if (block.fluid_state.GetNumSpecies() != checkpoint.num_species)
            throw std::runtime_error("AMR blocks disagree on their species count.");
        checkpoint.levels.push_back(block.level);
        checkpoint.logical_x1.push_back(block.logical_x1);
        checkpoint.logical_x2.push_back(block.logical_x2);
        checkpoint.logical_x3.push_back(block.logical_x3);

        size_t local_cell = 0;
        for (int k = block.grid.Ks(); k < block.grid.Ke(); ++k) {
            for (int j = block.grid.Js(); j < block.grid.Je(); ++j) {
                for (int i = block.grid.Is(); i < block.grid.Ie(); ++i, ++local_cell) {
                    const size_t offset = block_index * cells_per_block + local_cell;
                    const int cell = block.grid.GetIndex(i, j, k);
                    checkpoint.rho[offset] = block.fluid_state.rho[cell];
                    checkpoint.mom_u[offset] = block.fluid_state.mom_u[cell];
                    checkpoint.mom_v[offset] = block.fluid_state.mom_v[cell];
                    checkpoint.mom_w[offset] = block.fluid_state.mom_w[cell];
                    checkpoint.eng[offset] = block.fluid_state.eng[cell];
                    checkpoint.enuc_rate[offset] =
                        block.fluid_state.enuc_rate[cell];
                    for (int species = 0; species < checkpoint.num_species; ++species)
                        checkpoint.rhoX[static_cast<size_t>(species) * field_size + offset] =
                            block.fluid_state.rho[cell] * block.fluid_state.X(species, cell);
                }
            }
        }
    }
    io::write_hdf5_chk_impl(filename.str(), checkpoint);
}

// 3. Checkpoint file input
void read_chk(const std::string &filepath, amr::AMRControl &amr_ctrl,
              RunState &run_state, const SimConfig &config,
              const SpeciesManager &specs,
              const io::CheckpointProvenance &expected_provenance)
{
    io::CheckpointData checkpoint = io::read_hdf5_chk_impl(filepath);
    const int expected_species = specs.count();
    const size_t cells_per_block = checkpoint_cells_per_block(config.grid.dim);
    if (checkpoint.dim != config.grid.dim || checkpoint.geometry != config.grid.geometry ||
        checkpoint.num_species != expected_species || checkpoint.cells_per_block != cells_per_block) {
        throw std::runtime_error("Checkpoint is incompatible with the configured dimension, geometry, or species network.");
    }
    const bool verified_identity = checkpoint.provenance.available
        ? io::require_checkpoint_provenance_compatible(
              checkpoint.provenance, expected_provenance)
        : false;
    if (!verified_identity) {
        std::cout << "[IO] Legacy checkpoint has no EOS/network/table or "
                     "ordered-species identity; only its historical shape "
                     "checks can be applied. Rewrite a checkpoint before "
                     "claiming verified restart compatibility."
                  << std::endl;
    }

    amr_ctrl.tree->LoadLeafGrid(config, expected_species, checkpoint.levels,
                                checkpoint.logical_x1, checkpoint.logical_x2, checkpoint.logical_x3);
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    if (active_blocks.size() != checkpoint.levels.size())
        throw std::runtime_error("Checkpoint AMR leaf reconstruction changed the block count.");

    const size_t field_size = active_blocks.size() * cells_per_block;
    for (size_t block_index = 0; block_index < active_blocks.size(); ++block_index) {
        amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks[block_index]);
        size_t local_cell = 0;
        for (int k = block.grid.Ks(); k < block.grid.Ke(); ++k) {
            for (int j = block.grid.Js(); j < block.grid.Je(); ++j) {
                for (int i = block.grid.Is(); i < block.grid.Ie(); ++i, ++local_cell) {
                    const size_t offset = block_index * cells_per_block + local_cell;
                    const int cell = block.grid.GetIndex(i, j, k);
                    block.fluid_state.rho[cell] = checkpoint.rho[offset];
                    block.fluid_state.mom_u[cell] = checkpoint.mom_u[offset];
                    block.fluid_state.mom_v[cell] = checkpoint.mom_v[offset];
                    block.fluid_state.mom_w[cell] = checkpoint.mom_w[offset];
                    block.fluid_state.eng[cell] = checkpoint.eng[offset];
                    block.fluid_state.enuc_rate[cell] =
                        checkpoint.has_enuc_rate
                            ? checkpoint.enuc_rate[offset] : 0.0;
                    for (int species = 0; species < expected_species; ++species) {
                        const double rho = checkpoint.rho[offset];
                        const double rhoX = checkpoint.rhoX[static_cast<size_t>(species) * field_size + offset];
                        block.fluid_state.X(species, cell) = rho > 0.0 ? rhoX / rho : 0.0;
                    }
                }
            }
        }
    }
    run_state.time = checkpoint.time;
    run_state.dt_old = checkpoint.dt_old;
    run_state.dt_burn = checkpoint.dt_burn;
    run_state.step = checkpoint.step_count;
    run_state.plt_idx = checkpoint.plt_file_index;
    run_state.chk_idx = checkpoint.chk_file_index;
    run_state.has_timestep_state = checkpoint.has_timestep_state;
    run_state.resume_after_regrid = checkpoint.resume_after_regrid;
    run_state.checkpoint_provenance_verified = verified_identity;
    run_state.verified_eos_table_sha256 = verified_identity
        ? checkpoint.provenance.eos_table_sha256 : std::string{};
    if (!run_state.has_timestep_state) {
        std::cout << "[IO] Legacy checkpoint has no timestep-controller state; "
                     "hydro recomputes its CFL limit and burn resumes conservatively from dt_init."
                  << std::endl;
    }
    if (!checkpoint.has_enuc_rate) {
        std::cout << "[IO] Legacy checkpoint has no ENUC restart field; "
                     "the diagnostic is initialized to zero. ENUC-based "
                     "dynamic-AMR split-run parity is not verifiable until "
                     "a version-3 checkpoint is written."
                  << std::endl;
    }
    std::cout << "[IO] Restored CHK: " << filepath << " at step " << run_state.step
              << " with " << active_blocks.size() << " AMR leaves." << std::endl;
}
