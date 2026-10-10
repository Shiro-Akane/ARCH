/**
 * @file PlotIO.cpp
 * @brief Export synchronized leaf fields and derived plot diagnostics.
 *
 * Workflow:
 * 1. Gather materialized Host leaf interiors and their actual native geometry.
 * 2. Keep evolved density/energy/angular state in its original V/W measures.
 * 3. Construct the shared native RZ thermodynamic mean from real density ghosts
 *    before requesting pressure, temperature, Gamma1 or Jeans diagnostics.
 * 4. Read representative velocities directly as m_i/rho and retain their
 *    declared basis/averaging; these are distinct from ephemeral EOS momenta.
 * 5. Publish native bounds, measures, units and frozen provenance through HDF5.
 * Device visibility and completed ghost/stage identity belong to the caller.
 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "amr/AMRControl.h"
#include "core/config/RuntimeParams.h" // For SimConfig
#include "data/FluidState.h"
#include "data/GlobalDefs.h"
#include "grid/Grid.h"
#include "numerics/state/RzNativeClosure.h"
#include "physics/diagnostics/VelocityDiagnostics.h"
#include "physics/diagnostics/JeansDiagnostics.h"
#include "physics/species/Species.h"

#include "io/IO.h"
#include "core/files/FileFingerprint.h"
#include "io/plot/PlotGridMetadata.h"
#include "io/plot/PlotFieldMetadata.h"
#include "io/hdf5/HDF5Writer.h"

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
               const SimConfig &config, const SpeciesManager &specs,
               std::span<const io::PlotScalarField> extra_fields,
               const io::CheckpointProvenance* runtime_provenance, std::string_view run_id,
               GridMetrics::GeometrySemantics semantics,
               const io::PlotSourceIdentity* frozen_source_identity)
{
    // A profile mismatch must fail before creating any output directory/file.
    if (semantics != GridMetrics::GeometrySemantics::Existing
        && semantics != GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::invalid_argument("Unknown Plotfile geometry profile.");
    if (config.io.vars.jens && config.physics.gravity.type != "self")
        throw std::invalid_argument("JENS output requires self gravity.");
    const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
    if (active_blocks.empty())
        throw std::invalid_argument("Cannot publish Plotfile without active leaf blocks.");
    for (int id : active_blocks)
        (void)GridMetrics::make_geometry_view(amr_ctrl.pool->GetBlock(id).grid,semantics);
    const bool rz = semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
    if (!frozen_source_identity || !frozen_source_identity->formal)
        throw std::invalid_argument("Production Plotfile requires frozen complete runtime provenance");
    if (!fs::exists(config.io.out_dir))
        fs::create_directories(config.io.out_dir);

    std::ostringstream oss;
    oss << config.io.out_dir << "/"
        << config.io.base_name << "_"
        << config.numerics.solver_name << "_plt_"
        << std::setw(4) << std::setfill('0') << file_index << ".h5";

    int dim = 3;
    std::string geom = "cartesian";
    if (!active_blocks.empty()) {
        const auto& b = amr_ctrl.pool->GetBlock(active_blocks[0]);
        dim = b.grid.dim;
        geom = b.grid.geometry;
    }

    size_t num_blocks = active_blocks.size();

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

    io::PlotNativeGrid native_grid;
    io::PlotRzAngularState angular_state;
    const bool has_native_grid = io::supports_plot_native_grid(first_b.grid,semantics);
    if (has_native_grid) {
        for (size_t axis=0;axis<3;++axis) {
            native_grid.lower[axis].reserve(total_cells);
            native_grid.upper[axis].reserve(total_cells);
            native_grid.logical[axis].reserve(num_blocks);
        }
        native_grid.cell_measure.reserve(total_cells);
        if (rz) {
            native_grid.angular_measure.reserve(total_cells);
            angular_state.m_phi.reserve(total_cells);
            angular_state.angular_momentum_density.reserve(total_cells);
        }
    }
    size_t cell_idx = 0;
    for (size_t b_idx = 0; b_idx < num_blocks; ++b_idx) {
        const amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[b_idx]);
        block_levels[b_idx] = b.level;
        block_mortons[b_idx] = b.morton_code;
        if (b.grid.dim != dim || b.grid.geometry != geom)
            throw std::invalid_argument("Mixed plotfile block geometry.");
        if (has_native_grid) {
            native_grid.logical[0].push_back(b.logical_x1);
            native_grid.logical[1].push_back(b.logical_x2);
            native_grid.logical[2].push_back(b.logical_x3);
        }

        for (int k = b.grid.Ks(); k < b.grid.Ke(); ++k) {
            for (int j = b.grid.Js(); j < b.grid.Je(); ++j) {
                for (int i = b.grid.Is(); i < b.grid.Ie(); ++i) {
                    PointCoords p = b.grid.GetPhysicalCoords(i, j, k,semantics);
                    coord_x[cell_idx] = p.x;
                    coord_y[cell_idx] = p.y;
                    coord_z[cell_idx] = p.z;
                    if (has_native_grid)
                        io::append_plot_native_cell(native_grid,b.grid,i,j,k,semantics);
                    if (rz) {
                        const double m_phi = b.fluid_state.mom_w[b.grid.GetIndex(i,j,k)];
                        angular_state.m_phi.push_back(m_phi);
                        angular_state.angular_momentum_density.push_back(
                            arch::state::rz_angular_density(m_phi,
                                native_grid.angular_measure.back(), native_grid.cell_measure.back()));
                    }
                    cell_idx++;
                }
            }
        }
    }

    std::map<std::string, std::vector<double>> data_map;
    /** Borrow one real cell's EOS mean without changing its native conserved U.
     * The shared closure makes ordinary kinetic recovery subtract
     * J^2/(2*I_*V), using the actual density reconstruction and signed stencil.
     * The original path passes U unchanged to the same EOS callbacks.
     */
    const auto mean_for_eos=[&](const FluidState& state,int index,
        const GridMetrics::GeometryView& geometry,int i) {
        const auto read=[&state](int cell) { return state.get(cell); };
        if (!rz) return read(index);
        const auto& limits=config.numerics;
        const auto closure=RzThermodynamics::make_cell(read,index,geometry,i,
            {limits.sml_rho,limits.min_eint,limits.max_eint});
        if (!closure.valid())
            throw std::runtime_error("Invalid native RZ Plotfile thermodynamic closure at cell "
                +std::to_string(index));
        return closure.effective_mean;
    };
    /** Traverse original leaf order, supplying actual chart/i to each extractor. */
    auto extract_and_store = [&](const std::string &name, auto extract_func)
    {
        std::vector<double> buffer(total_cells);
        size_t buf_idx = 0;
        for (size_t b_idx = 0; b_idx < num_blocks; ++b_idx) {
            const amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[b_idx]);
            const auto geometry=GridMetrics::make_geometry_view(b.grid,semantics);
            for (int k = b.grid.Ks(); k < b.grid.Ke(); ++k) {
                for (int j = b.grid.Js(); j < b.grid.Je(); ++j) {
                    for (int i = b.grid.Is(); i < b.grid.Ie(); ++i) {
                        buffer[buf_idx++] = extract_func(b.fluid_state, b.grid.GetIndex(i, j, k),geometry,i);
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
                if (rho > 0.0) {
                    vel_x[index] = state.mom_u[index] / rho;
                    vel_y[index] = state.mom_v[index] / rho;
                    vel_z[index] = state.mom_w[index] / rho;
                }
            }
            for (int k = grid.Ks(); k < grid.Ke(); ++k) {
                for (int j = grid.Js(); j < grid.Je(); ++j) {
                    for (int i = grid.Is(); i < grid.Ie(); ++i) {
                        const VelocityDiagnostics::Values diagnostic =
                            VelocityDiagnostics::evaluate(GridMetrics::make_geometry_view(grid,semantics), vel_x, vel_y, vel_z, i, j, k);
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
        extract_and_store("DENS", [](const FluidState &s,int idx,
            const GridMetrics::GeometryView&,int) { return s.get(idx).rho; });

    if (vars.p) {
        std::vector<double> Xi_temp(specs.count());
        extract_and_store("PRES", [&](const FluidState &s,int idx,
            const GridMetrics::GeometryView& geometry,int i) {
            for(int k=0; k<s.GetNumSpecies(); ++k) Xi_temp[k] = s.X(k, idx);
            return p_func(mean_for_eos(s,idx,geometry,i), Xi_temp.data(), p_context);
        });
    }

    if (vars.temp) {
        std::vector<double> Xi_temp(specs.count());
        extract_and_store("TEMP", [&](const FluidState &s,int idx,
            const GridMetrics::GeometryView& geometry,int i) {
            for (int k = 0; k < s.GetNumSpecies(); ++k) Xi_temp[k] = s.X(k, idx);
            return t_func(mean_for_eos(s,idx,geometry,i), Xi_temp.data(), p_context);
        });
    }
    if (vars.eng)
        extract_and_store("ENER", [](const FluidState &s,int idx,
            const GridMetrics::GeometryView&,int) { return s.get(idx).eng; });

    // These are native representative m_i/rho values. Thermal recovery is
    // unrelated to reading velocity and would misinterpret RZ J/W as a point
    // momentum; effective_mean.w must never replace the VELZ convention.
    if (vars.u)
        extract_and_store("VELX", [](const FluidState &s,int idx,
            const GridMetrics::GeometryView&,int) { return s.mom_u[idx]/s.rho[idx]; });

    if (vars.v && dim >= 2)
        extract_and_store("VELY", [](const FluidState &s,int idx,
            const GridMetrics::GeometryView&,int) { return s.mom_v[idx]/s.rho[idx]; });

    if (vars.w && (dim == 3 || rz))
        extract_and_store("VELZ", [](const FluidState &s,int idx,
            const GridMetrics::GeometryView&,int) { return s.mom_w[idx]/s.rho[idx]; });

    if (vars.entr) {
        std::vector<double> Xi_temp(specs.count());
        extract_and_store("ENTR", [&](const FluidState& state,int index,
            const GridMetrics::GeometryView& geometry,int i) {
            for (int species = 0; species < state.GetNumSpecies(); ++species) Xi_temp[species] = state.X(species, index);
            const double rho = state.rho[index];
            const FluidVector U = mean_for_eos(state,index,geometry,i);
            const double gamma1 = gamma1_func(U, Xi_temp.data(), p_context);
            return p_func(U, Xi_temp.data(), p_context) / std::pow(rho, gamma1);
        });
    }

    if (vars.jens) {
        if (!p_func || !gamma1_func)
            throw std::invalid_argument("JENS output requires authoritative EOS callbacks.");
        std::vector<double> buffer(total_cells);
        std::vector<double> fractions(specs.count());
        std::size_t index=0;
        for (int id:active_blocks) {
            const auto& block=amr_ctrl.pool->GetBlock(id);
            const auto& grid=block.grid;
            const auto geometry=GridMetrics::make_geometry_view(grid,semantics);
            const auto& state=block.fluid_state;
            for (int k=grid.Ks();k<grid.Ke();++k)
                for (int j=grid.Js();j<grid.Je();++j)
                    for (int i=grid.Is();i<grid.Ie();++i) {
                        const int cell=grid.GetIndex(i,j,k);
                        for (int species=0;species<state.GetNumSpecies();++species)
                            fractions[species]=state.X(species,cell);
                        const auto fluid=mean_for_eos(state,cell,geometry,i);
                        const double pressure=p_func(fluid,fractions.data(),p_context);
                        // Gamma1 callback is rho*adiabatic_cs^2/P from the
                        // current EOS, not the configurable fallback gamma.
                        const double gamma1=gamma1_func(fluid,fractions.data(),p_context);
                        if (!std::isfinite(pressure) || pressure<=0.
                            || !std::isfinite(gamma1) || gamma1<=0.)
                            throw std::runtime_error("JENS output requires finite positive EOS pressure and Gamma1.");
                        const auto diagnostic=JeansDiagnostics::evaluate_cell(
                            fluid.rho,gamma1*pressure/fluid.rho,geometry,i,j);
                        if (diagnostic.status!=JeansDiagnostics::Status::valid)
                            throw std::runtime_error("JENS output rejected invalid or unrepresentable state.");
                        buffer[index++]=diagnostic.cells;
                    }
        }
        data_map.emplace("JENS",std::move(buffer));
    }

    if (vars.enuc)
        extract_and_store("ENUC", [](const FluidState& state,int index,
            const GridMetrics::GeometryView&,int) { return state.enuc_rate[index]; });

    if (vars.vort || vars.divv) {
        extract_velocity_diagnostics(vars.vort, vars.divv);
    }

    // Species keep their dedicated declaration owner; materialize them before
    // the shared metadata loop so name collisions remain rejected.
    std::map<std::string, io::PlotFieldMetadata> field_metadata;
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
        field_metadata[var_name] = io::plot_species_metadata();
        extract_and_store(var_name, [species](const FluidState &s,int idx,
            const GridMetrics::GeometryView&,int) {
            return s.X(species, idx);
        });
    }

    // Actual producer extras (GPOT/GAC*) are validated and copied through the
    // same data_map before the metadata loop, so they are declared by the
    // existing plot_field_metadata owner instead of an unknown default.
    for (const auto& field : extra_fields) {
        if (field.name.empty() || field.values.size()!=total_cells || data_map.contains(std::string(field.name)))
            throw std::invalid_argument("Invalid additional plot field");
        if (!std::all_of(field.values.begin(),field.values.end(),[](double x){return std::isfinite(x);}))
            throw std::invalid_argument("Nonfinite additional plot field");
        data_map.emplace(std::string(field.name),std::vector<double>(field.values.begin(),field.values.end()));
    }

    // Velocity and gravitational-acceleration payloads store physical
    // orthonormal components in native coordinate-axis order on every chart.
    const auto is_directional_component=[](const std::string& name) {
        return name=="VELX"||name=="VELY"||name=="VELZ"
            ||name=="GACX"||name=="GACY"||name=="GACZ";
    };
    for (const auto& [name, values] : data_map) {
        auto declaration = io::plot_field_metadata(name, geom == "cartesian");
        if (!rz && geom!="cartesian" && is_directional_component(name))
            declaration.basis=dim==3?(geom=="cylindrical"?"local-orthonormal-r-z-phi":"local-orthonormal-r-theta-phi"):
                dim==2?"local-orthonormal-r-phi-inactive":"local-orthonormal-r-inactive-inactive";
        if (rz && is_directional_component(name)) {
            declaration.basis = "local-orthonormal-r-z-phi";
            if (name == "VELX") declaration.meaning = "radial_velocity";
            else if (name == "VELY") declaration.meaning = "axial_velocity";
            else if (name == "VELZ") declaration.meaning = "representative_azimuthal_velocity";
        }
        // RZ radial/axial acceleration carries its chart meaning; the azimuthal
        // component keeps the honest generic meaning and no gravity averaging
        // or normalization is invented.
        if (rz && name == "GACX") declaration.meaning = "radial_gravitational_acceleration";
        else if (rz && name == "GACY") declaration.meaning = "axial_gravitational_acceleration";
        if (rz) {
            if (name == "DENS" || name == "ENER") declaration.averaging = "native-volume-average";
            else if (name == "VELZ") declaration.averaging = "representative-m_phi-over-rho";
            else if (name == "VELX" || name == "VELY")
                declaration.averaging = "recovered-from-native-volume-averaged-conserved-state";
            else if (name == "PRES" || name == "TEMP" || name == "ENTR" || name == "JENS")
                declaration.averaging = "evaluated-from-native-mean-thermodynamic-closure";
        }
        field_metadata.emplace(name,std::move(declaration));
    }
    // Encoding/EOS/build identities were frozen once by the production startup
    // owner. The output-session UUID remains independent of this scientific ID.
    io::PlotSourceIdentity source_identity=*frozen_source_identity;
    source_identity.run_id=run_id;
    (void)runtime_provenance; // Already incorporated at the immutable freeze boundary.
    io::write_hdf5_plt_impl(oss.str(), current_time, dim, geom, dims, coord_x, coord_y, coord_z, block_levels, block_mortons, data_map, has_native_grid ? &native_grid : nullptr, &source_identity, &field_metadata,semantics,rz ? &angular_state : nullptr);
}
