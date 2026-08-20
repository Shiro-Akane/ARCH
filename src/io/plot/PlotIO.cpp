/**
 * @file PlotIO.cpp
 * @brief Exports leaf-block fields and metadata for post-processing plots.
 *
 * Workflow:
 * 1. Collect synchronized leaf metadata and field values from the driver.
 * 2. Serialize them through the selected backend with explicit dimensions and geometry.
 * 3. Write restart- or analysis-ready output without changing simulation state.
 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "../../amr/AMRControl.h"
#include "../../core/RuntimeParams.h" // For SimConfig
#include "../../data/FluidState.h"
#include "../../data/GlobalDefs.h"
#include "../../grid/Grid.h"
#include "../../physics/diagnostics/VelocityDiagnostics.h"
#include "../../physics/species/Species.h"

#include "../IO.h"
#include "../hdf5/HDF5Writer.h"

namespace fs = std::filesystem;

// Return computational-domain HDF5 dimensions in row-major Z, Y, X order.
inline std::vector<size_t> get_hdf5_dims(const Grid &grid)
{
    size_t n1 = amr::BLOCK_NX;
    size_t n2 = grid.dim >= 2 ? amr::BLOCK_NY : 1;
    size_t n3 = grid.dim == 3 ? amr::BLOCK_NZ : 1;
    if (grid.dim == 1)
        return {n1};
    if (grid.dim == 2)
        return {n2, n1};
    return {n3, n2, n1};
}

void write_plt(amr::AMRControl &amr_ctrl,
               PressureFunc p_func, TemperatureFunc t_func, Gamma1Func gamma1_func, const void* p_context,
               int file_index, double current_time,
               const SimConfig &config, const SpeciesManager &specs)
{
    if (!fs::exists(config.io.out_dir))
        fs::create_directories(config.io.out_dir);

    std::ostringstream oss;
    oss << config.io.out_dir << "/"
        << config.io.base_name << "_"
        << config.numerics.solver_name << "_plt_"
        << std::setw(4) << std::setfill('0') << file_index << ".h5";

    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    int dim = 3;
    std::string geom = "cartesian";
    if (!active_blocks.empty()) {
        const auto& b = amr_ctrl.pool->GetBlock(active_blocks[0]);
        dim = b.grid.dim;
        geom = b.grid.geometry;
    }

    size_t num_blocks = active_blocks.size();
    if (num_blocks == 0) return;

    const amr::Block& first_b = amr_ctrl.pool->GetBlock(active_blocks[0]);
    std::vector<size_t> block_dims = get_hdf5_dims(first_b.grid);
    std::vector<size_t> dims = {num_blocks};
    dims.insert(dims.end(), block_dims.begin(), block_dims.end());

    size_t cells_per_block = 1;
    for (size_t d : block_dims) cells_per_block *= d;
    size_t total_cells = num_blocks * cells_per_block;

    std::vector<double> coord_x(total_cells), coord_y(total_cells), coord_z(total_cells);
    std::vector<int> block_levels(num_blocks);
    std::vector<int> block_mortons(num_blocks);

    size_t cell_idx = 0;
    for (size_t b_idx = 0; b_idx < num_blocks; ++b_idx) {
        const amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[b_idx]);
        block_levels[b_idx] = b.level;
        block_mortons[b_idx] = b.morton_code;

        for (int k = b.grid.Ks(); k < b.grid.Ke(); ++k) {
            for (int j = b.grid.Js(); j < b.grid.Je(); ++j) {
                for (int i = b.grid.Is(); i < b.grid.Ie(); ++i) {
                    PointCoords p = b.grid.GetPhysicalCoords(i, j, k);
                    coord_x[cell_idx] = p.x;
                    coord_y[cell_idx] = p.y;
                    coord_z[cell_idx] = p.z;
                    cell_idx++;
                }
            }
        }
    }

    std::map<std::string, std::vector<double>> data_map;
    auto extract_and_store = [&](const std::string &name, auto extract_func)
    {
        std::vector<double> buffer(total_cells);
        size_t buf_idx = 0;
        for (size_t b_idx = 0; b_idx < num_blocks; ++b_idx) {
            const amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[b_idx]);
            for (int k = b.grid.Ks(); k < b.grid.Ke(); ++k) {
                for (int j = b.grid.Js(); j < b.grid.Je(); ++j) {
                    for (int i = b.grid.Is(); i < b.grid.Ie(); ++i) {
                        buffer[buf_idx++] = extract_func(b.fluid_state, b.grid.GetIndex(i, j, k));
                    }
                }
            }
        }
        data_map[name] = std::move(buffer);
    };

    const auto &vars = config.io.vars;
    const auto extract_velocity_diagnostics = [&](bool include_vorticity, bool include_divergence) {
        std::vector<double> vorticity(include_vorticity ? total_cells : 0);
        std::vector<double> divergence(include_divergence ? total_cells : 0);
        size_t buffer_index = 0;
        for (size_t block_index = 0; block_index < num_blocks; ++block_index) {
            const amr::Block& block = amr_ctrl.pool->GetBlock(active_blocks[block_index]);
            const Grid& grid = block.grid;
            const FluidState& state = block.fluid_state;
            const int state_size = grid.GetTotalSize();
            std::vector<double> vel_x(state_size, 0.0);
            std::vector<double> vel_y(state_size, 0.0);
            std::vector<double> vel_z(state_size, 0.0);
            for (int index = 0; index < state_size; ++index) {
                const double rho = state.rho[index];
                if (rho > config.numerics.sml_rho) {
                    vel_x[index] = state.mom_u[index] / rho;
                    vel_y[index] = state.mom_v[index] / rho;
                    vel_z[index] = state.mom_w[index] / rho;
                }
            }
            for (int k = grid.Ks(); k < grid.Ke(); ++k) {
                for (int j = grid.Js(); j < grid.Je(); ++j) {
                    for (int i = grid.Is(); i < grid.Ie(); ++i) {
                        const VelocityDiagnostics::Values diagnostic =
                            VelocityDiagnostics::evaluate(grid, vel_x, vel_y, vel_z, i, j, k);
                        if (include_vorticity) vorticity[buffer_index] = diagnostic.vorticity;
                        if (include_divergence) divergence[buffer_index] = diagnostic.divergence;
                        ++buffer_index;
                    }
                }
            }
        }
        if (include_vorticity) data_map["VORT"] = std::move(vorticity);
        if (include_divergence) data_map["DIVV"] = std::move(divergence);
    };

    if (vars.rho)
        extract_and_store("DENS", [](const FluidState &s, int idx) { return s.get(idx).rho; });

    if (vars.p) {
        std::vector<double> Xi_temp(specs.count());
        extract_and_store("PRES", [&](const FluidState &s, int idx) {
            for(int k=0; k<s.GetNumSpecies(); ++k) Xi_temp[k] = s.X(k, idx);
            return p_func(s.get(idx), Xi_temp.data(), p_context);
        });
    }

    if (vars.temp) {
        std::vector<double> Xi_temp(specs.count());
        extract_and_store("TEMP", [&](const FluidState &s, int idx) {
            for (int k = 0; k < s.GetNumSpecies(); ++k) Xi_temp[k] = s.X(k, idx);
            return t_func(s.get(idx), Xi_temp.data(), p_context);
        });
    }
    if (vars.eng)
        extract_and_store("ENER", [](const FluidState &s, int idx) { return s.get(idx).eng; });

    if (vars.u)
        extract_and_store("VELX", [](const FluidState &s, int idx) { auto U = s.get(idx); return U.rho > 1e-12 ? U.mom_u / U.rho : 0.0; });

    if (vars.v && dim >= 2)
        extract_and_store("VELY", [](const FluidState &s, int idx) { auto U = s.get(idx); return U.rho > 1e-12 ? U.mom_v / U.rho : 0.0; });

    if (vars.w && dim == 3)
        extract_and_store("VELZ", [](const FluidState &s, int idx) { auto U = s.get(idx); return U.rho > 1e-12 ? U.mom_w / U.rho : 0.0; });

    if (vars.entr) {
        std::vector<double> Xi_temp(specs.count());
        extract_and_store("ENTR", [&](const FluidState& state, int index) {
            for (int species = 0; species < state.GetNumSpecies(); ++species) Xi_temp[species] = state.X(species, index);
            const double rho = std::max(state.rho[index], config.numerics.sml_rho);
            const FluidVector U = state.get(index);
            const double gamma1 = gamma1_func(U, Xi_temp.data(), p_context);
            return p_func(U, Xi_temp.data(), p_context) / std::pow(rho, gamma1);
        });
    }

    if (vars.enuc)
        extract_and_store("ENUC", [](const FluidState& state, int index) { return state.enuc_rate[index]; });

    if (vars.vort || vars.divv) {
        extract_velocity_diagnostics(vars.vort, vars.divv);
    }
    std::vector<int> selected_species;
    if (vars.species) {
        for (int species = 0; species < specs.count(); ++species) selected_species.push_back(species);
    }
    for (const std::string& requested_name : config.io.plot_species_names) {
        const int species = specs.GetSpeciesID(requested_name);
        if (species < 0) {
            std::cerr << "[PlotIO] Warning: requested PLT species '" << requested_name
                      << "' is not registered; no field was written." << std::endl;
            continue;
        }
        if (std::find(selected_species.begin(), selected_species.end(), species) == selected_species.end()) {
            selected_species.push_back(species);
        }
    }
    for (const int species : selected_species) {
        const std::string& var_name = specs.get_name(species);
        extract_and_store(var_name, [species](const FluidState &s, int idx) {
            return s.X(species, idx);
        });
    }

    io::write_hdf5_plt_impl(oss.str(), current_time, dim, geom, dims, coord_x, coord_y, coord_z, block_levels, block_mortons, data_map);
}
