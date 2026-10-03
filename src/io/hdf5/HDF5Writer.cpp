/**
 * @file HDF5Writer.cpp
 * @brief Read and write the common HDF5 datasets and attributes.
 *
 * Checkpoint serialization checks payload lengths, native composition and
 * restart metadata using the declared format.
 *
 * Workflow:
 * 1. Reject unsupported format/control identities before reading live state.
 * 2. Validate finite control values, repair accounting and scientific payloads.
 * 3. Return a complete host payload; ChkIO alone replaces the live AMR state.
 * Writers apply the same checks before opening/truncating the destination.
 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <unistd.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "io/hdf5/HDF5Writer.h"
#include "core/config/ConfigValidation.h"

#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>
#include <highfive/H5File.hpp>

using namespace HighFive;

namespace io {

namespace {

/** Explain identity/payload failures without changing reader/writer exception types. */
std::string state_control_error(const std::vector<double>& controls)
{
    if (controls.empty() || !std::all_of(controls.begin(), controls.end(),
                                      [](double value) { return std::isfinite(value); }))
        return "Invalid checkpoint state controls: missing or nonfinite values.";
    if (controls.front() != arch::config::StateControlRevision)
        return "Unsupported checkpoint state-control revision " + std::to_string(controls.front())
            + "; expected " + std::to_string(arch::config::StateControlRevision)
            + ". Continue with the original executable or start from new initial data; "
              "old controls are not migrated.";
    if (controls.size() != arch::config::StateControlCount)
        return "Invalid checkpoint state-control length: expected "
            + std::to_string(arch::config::StateControlCount) + ", got "
            + std::to_string(controls.size()) + ".";
    return {};
}

/** Validate repair magnitudes and locations after checking control identity. */
bool has_valid_repair_ledger(const CheckpointData& c)
{
    const auto finite = [](double value) { return std::isfinite(value); };
    const auto& v = c.repairs.values;
    if (c.num_species < 0 || v.size() != arch::state::RepairView::fixed_size + 2 * c.num_species
        || !std::all_of(v.begin(), v.end(), finite)
        || !std::all_of(c.repairs.position, c.repairs.position + 3, finite)
        || !finite(c.repairs.time) || c.repairs.time < 0.0 || c.repairs.time > c.time
        || c.repairs.stage < 0 || c.repairs.stage > 3)
        return false;
    if (v[0] < 0.0 || std::floor(v[0]) != v[0] || v[1] < 0.0
        || v[3] < std::abs(v[2]) || v[8] < std::abs(v[7])
        || v[9] < 0.0 || std::floor(v[9]) != v[9]) return false;
    for (int species = 0; species < c.num_species; ++species)
        if (v[11 + 2 * species] < std::abs(v[10 + 2 * species])) return false;
    return true;
}

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
               static_cast<size_t>(checkpoint.num_species) * cells &&
           (checkpoint.has_mass_fractions
                ? checkpoint.mass_fractions.size() == checkpoint.rhoX.size()
                : checkpoint.mass_fractions.empty());
}

bool has_consistent_checkpoint_composition(const CheckpointData& checkpoint)
{
    // Payload dimensions must be checked first. Preserve the native fraction
    // and verify the redundant conserved representation without renormalizing.
    if (!checkpoint.has_mass_fractions) return true;
    const size_t cells = checkpoint.rho.size();
    for (size_t entry = 0; entry < checkpoint.mass_fractions.size(); ++entry) {
        const double rho = checkpoint.rho[entry % cells];
        const double fraction = checkpoint.mass_fractions[entry];
        const double rhoX = checkpoint.rhoX[entry];
        if (!std::isfinite(rho) || !std::isfinite(fraction) || !std::isfinite(rhoX)
            || rho * fraction != rhoX)
            return false;
    }
    return true;
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
    const auto& gravity=provenance.gravity_controls;
    if (!std::all_of(gravity.begin(),gravity.end(),[](double x){return std::isfinite(x);})) return false;
    if (provenance.gravity_type=="self") {
        if ((provenance.gravity_boundary!="periodic" && provenance.gravity_boundary!="isolated") || gravity.size()!=4 || gravity[0]<=0.
            || gravity[1]<=0. || gravity[1]>=1. || gravity[2]<0. || gravity[3]<1. || std::floor(gravity[3])!=gravity[3]) return false;
    } else if (provenance.gravity_type=="external") {
        if (gravity.size()!=3 || provenance.gravity_boundary!="none") return false;
    } else if (provenance.gravity_type!="none" || !gravity.empty() || provenance.gravity_boundary!="none") return false;
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

// Explicit close is required: HighFive destructor errors are only logged.
class CheckedPlotFile final : public File {
public:
    using File::File;
    void close_checked() {
        if (H5Fget_obj_count(getId(), H5F_OBJ_ALL | H5F_OBJ_LOCAL) != 1)
            throw std::runtime_error("Plotfile still has open child handles.");
        if (H5Fclose(getId()) < 0) throw std::runtime_error("Plotfile close failed.");
        _hid = H5I_INVALID_HID;
    }
};

void write_hdf5_plt_impl(const std::string& filepath, double current_time, int dim, const std::string& geom,
                         const std::vector<size_t>& dims,
                         const std::vector<double>& coord_x, const std::vector<double>& coord_y, const std::vector<double>& coord_z,
                         const std::vector<int>& block_levels, const std::vector<int>& block_mortons,
                         const std::map<std::string, std::vector<double>>& data_map,
                         const PlotNativeGrid* native_grid,
                         const PlotSourceIdentity* source_identity)
{
    if (dim < 1 || dim > 3 || dims.size() != static_cast<size_t>(dim + 1)
        || !std::isfinite(current_time) || dims.front() == 0 || data_map.empty())
        throw std::invalid_argument("Invalid plotfile dimensions/time/fields.");
    size_t cells = 1;
    for (size_t extent : dims) {
        if (extent == 0 || cells > std::numeric_limits<size_t>::max() / extent)
            throw std::invalid_argument("Invalid plotfile shape.");
        cells *= extent;
    }
    if (coord_x.size() != cells || coord_y.size() != cells || coord_z.size() != cells
        || block_levels.size() != dims.front() || block_mortons.size() != dims.front())
        throw std::invalid_argument("Inconsistent plotfile coordinate/block payload.");
    for (const auto& [name, buffer] : data_map)
        if (name.empty() || name.find('/') != std::string::npos || buffer.size() != cells)
            throw std::invalid_argument("Invalid plotfile field name/length.");
    if (native_grid) {
        if (geom != "cartesian" || dim > 2 || native_grid->cell_measure.size() != cells)
            throw std::invalid_argument("Invalid candidate native grid geometry/length.");
        for (size_t axis = 0; axis < 3; ++axis) {
            if (native_grid->lower[axis].size() != cells
                || native_grid->upper[axis].size() != cells
                || native_grid->logical[axis].size() != dims.front())
                throw std::invalid_argument("Invalid native grid bounds/logical shape.");
            for (size_t cell = 0; cell < cells; ++cell) {
                double lo = native_grid->lower[axis][cell], hi = native_grid->upper[axis][cell];
                if (!std::isfinite(lo) || !std::isfinite(hi)
                    || (axis < static_cast<size_t>(dim) ? hi <= lo : lo != 0. || hi != 0.))
                    throw std::invalid_argument("Invalid native cell bounds.");
            }
        }
        for (double measure : native_grid->cell_measure)
            if (!std::isfinite(measure) || measure <= 0.)
                throw std::invalid_argument("Invalid native cell measure.");
    }
    if (source_identity) {
        const auto& id=*source_identity;
        const auto text_ok=[](const std::string& text) {
            return text.size()<=128 && text.find(char(0))==std::string::npos;
        };
        if (!text_ok(id.case_id) || !text_ok(id.eos_type) || id.species_names.size()>128
            || !std::all_of(id.species_names.begin(),id.species_names.end(),[&](const auto& n){return !n.empty()&&text_ok(n);}))
            throw std::invalid_argument("Invalid plot source identity text.");
        if (!id.eos_table_sha256.empty() &&
            (id.eos_table_sha256.size()!=64 || !std::all_of(id.eos_table_sha256.begin(),id.eos_table_sha256.end(),
             [](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})))
            throw std::invalid_argument("Invalid plot EOS table digest.");
        if (id.eos_type=="ideal" && (!std::isfinite(id.ideal_gamma) || id.ideal_gamma<=1. || !id.eos_table_sha256.empty()))
            throw std::invalid_argument("Invalid plot ideal EOS identity.");
        if (id.eos_type.empty() && (!id.eos_table_sha256.empty() || !id.species_names.empty()))
            throw std::invalid_argument("Plot EOS evidence requires its resolved policy.");
    }
    // Same-directory atomic replacement retains legacy overwrite semantics.
    // Atomic visibility does not promise power-loss durability (no fsync).
    std::string pattern = filepath + ".partial-XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back(0);
    int fd = ::mkstemp(temporary.data());
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "Create plot temporary");
    const std::filesystem::path temporary_path(temporary.data());
    if (::close(fd) != 0) {
        int error = errno;
        std::error_code ignored; std::filesystem::remove(temporary_path, ignored);
        throw std::system_error(error, std::generic_category(), "Close temporary descriptor");
    }
    try {
        CheckedPlotFile file(temporary_path.string(), File::ReadWrite | File::Truncate);
        file.createAttribute("time", current_time);
        file.createAttribute("dim", dim);
        file.createAttribute("geometry", geom);

        {
        file.createAttribute("plot_publication_version", std::string("candidate-1"));
        file.createAttribute("plot_publication_state", std::string("complete"));
        file.createAttribute("plot_publication_method", std::string("checked-close-atomic-replace"));
        file.createAttribute("plot_storage_order", std::string("x1-fastest"));
        file.createAttribute("plot_identity_state", std::string("unknown"));
        Group grid_group = file.createGroup("Grid");
        Group data_group = file.createGroup("Data");

        grid_group.createDataSet("x", coord_x);
        grid_group.createDataSet("y", coord_y);
        grid_group.createDataSet("z", coord_z);
        grid_group.createDataSet("level", block_levels);
        grid_group.createDataSet("morton", block_mortons);

        if (source_identity) {
            const auto& id=*source_identity;
            Group identity=file.createGroup("SourceIdentity");
            identity.createAttribute("version",std::string("candidate-identity-1"));
            identity.createAttribute("scope",std::string("partial"));
            identity.createAttribute("case_id",id.case_id.empty()?std::string("unknown"):id.case_id);
            identity.createAttribute("case_source",id.case_id.empty()?std::string("unknown"):std::string("ConfigurationInput.case_id"));
            identity.createAttribute("eos_type",id.eos_type.empty()?std::string("unknown"):id.eos_type);
            identity.createAttribute("eos_source",id.eos_type.empty()?std::string("unknown"):std::string("resolved-runtime-checkpoint-provenance"));
            identity.createAttribute("eos_table_sha256",id.eos_table_sha256.empty()?std::string("unknown"):id.eos_table_sha256);
            identity.createAttribute("eos_table_state",id.eos_type=="ideal"?std::string("not-applicable"):
                id.eos_table_sha256.empty()?std::string("unknown"):std::string("recorded"));
            identity.createAttribute("species_identity_state",id.eos_type.empty()?std::string("unknown"):std::string("recorded"));
            identity.createAttribute("ideal_gamma_available",id.eos_type=="ideal"?1:0);
            if(id.eos_type=="ideal")identity.createAttribute("ideal_gamma",id.ideal_gamma);
            identity.createAttribute("species_count",static_cast<int>(id.species_names.size()));
            if(!id.species_names.empty())identity.createDataSet("species_names",id.species_names);
            for(const char* name:{"run_id","raw_config_sha256","effective_config_sha256",
                 "build_id","binary_sha256","source_git_head","eos_unit_system"})
                identity.createAttribute(name,std::string("unknown"));
        }

        if (native_grid) {
            Group native = file.createGroup("NativeGrid");
            native.createAttribute("version", std::string("candidate-cartesian-1"));
            native.createAttribute("centering", std::string("cell"));
            native.createAttribute("ghost_cells", 0);
            native.createAttribute("block_kind", std::string("active-leaf"));
            native.createAttribute("center_basis", std::string("cartesian"));
            native.createAttribute("measure_source", std::string("GridMetrics::CellVolume"));
            native.createAttribute("measure_convention",
                std::string("active-coordinate-product; inactive-measures-omitted"));
            native.createAttribute("measure_unit", std::string("unknown"));
            native.createAttribute("logical_identity",
                std::string("file-local level/logical_x1/logical_x2/logical_x3"));
            for (size_t axis = 0; axis < 3; ++axis) {
                const std::string name = "x" + std::to_string(axis+1);
                native.createDataSet(name + "_lower", native_grid->lower[axis]);
                native.createDataSet(name + "_upper", native_grid->upper[axis]);
                native.createDataSet("logical_" + name, native_grid->logical[axis]);
            }
            native.createDataSet("cell_measure", native_grid->cell_measure);
        }

        for (const auto& [name, buffer] : data_map) {
            DataSet ds = data_group.createDataSet<double>(name, DataSpace(dims));
            ds.write_raw(buffer.data());
        }

        }
        file.flush();
        file.close_checked();
        std::filesystem::rename(temporary_path, filepath);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        throw;
    }
    std::cout << "[IO] Saved PLT: " << filepath << " at t=" << current_time << std::endl;
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
    if ((checkpoint.num_species > 0 && !checkpoint.has_mass_fractions)
        || !has_consistent_checkpoint_composition(checkpoint)) {
        throw std::invalid_argument(
            "Checkpoint native mass fractions are missing or inconsistent with rhoX.");
    }
    if (!has_consistent_checkpoint_provenance(checkpoint)) {
        throw std::invalid_argument(
            "Checkpoint scientific provenance is inconsistent.");
    }
    if (const auto error = state_control_error(checkpoint.state_controls); !error.empty())
        throw std::invalid_argument(error);
    if (!has_valid_repair_ledger(checkpoint))
        throw std::invalid_argument("Checkpoint repair ledger is invalid.");
    try {
        File file(filepath, File::ReadWrite | File::Create | File::Truncate);
        file.createAttribute("checkpoint_version", checkpoint_format_version);
        file.createDataSet("state_repairs", checkpoint.repairs.values);
        file.createDataSet("state_controls", checkpoint.state_controls);
        file.createAttribute("repair_block_uid", checkpoint.repairs.block_uid);
        file.createAttribute("repair_stage", checkpoint.repairs.stage);
        file.createAttribute("repair_time", checkpoint.repairs.time);
        file.createDataSet("repair_position", std::vector<double>(checkpoint.repairs.position, checkpoint.repairs.position+3));
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
        file.createAttribute("gravity_type", checkpoint.provenance.gravity_type);
        file.createAttribute("gravity_boundary", checkpoint.provenance.gravity_boundary);
        file.createDataSet("gravity_controls", checkpoint.provenance.gravity_controls);
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
            DataSet fractions = data.createDataSet<double>("X", DataSpace(dims));
            fractions.write_raw(checkpoint.mass_fractions.data());
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
        if (version != checkpoint_format_version)
            throw std::runtime_error("Unsupported checkpoint format version.");
        file.getDataSet("state_repairs").read(checkpoint.repairs.values);
        file.getDataSet("state_controls").read(checkpoint.state_controls);
        if (const auto error = state_control_error(checkpoint.state_controls); !error.empty())
            throw std::runtime_error(error);
        file.getAttribute("repair_block_uid").read(checkpoint.repairs.block_uid);
        file.getAttribute("repair_stage").read(checkpoint.repairs.stage);
        file.getAttribute("repair_time").read(checkpoint.repairs.time);
        std::vector<double> position;
        file.getDataSet("repair_position").read(position);
        if (position.size()!=3 || !std::all_of(position.begin(),position.end(),[](double v){return std::isfinite(v);})
            || !std::isfinite(checkpoint.repairs.time) || checkpoint.repairs.time < 0.0
            || checkpoint.repairs.stage < 0 || checkpoint.repairs.stage > 3)
            throw std::runtime_error("Invalid checkpoint repair location");
        std::copy_n(position.begin(),3,checkpoint.repairs.position);
        file.getAttribute("time").read(checkpoint.time);
        int resume_after_regrid = 0;
        file.getAttribute("dt_old").read(checkpoint.dt_old);
        file.getAttribute("dt_burn").read(checkpoint.dt_burn);
        file.getAttribute("resume_after_regrid").read(resume_after_regrid);
        checkpoint.has_timestep_state = true;
        checkpoint.resume_after_regrid = resume_after_regrid != 0;
        if (!has_valid_timestep_state(checkpoint)) {
            throw std::runtime_error("Checkpoint timestep-controller state is invalid.");
        }
        file.getAttribute("step").read(checkpoint.step_count);
        file.getAttribute("chk_index").read(checkpoint.chk_file_index);
        file.getAttribute("plt_index").read(checkpoint.plt_file_index);
        file.getAttribute("dim").read(checkpoint.dim);
        file.getAttribute("geometry").read(checkpoint.geometry);
        file.getAttribute("num_species").read(checkpoint.num_species);
        file.getAttribute("cells_per_block").read(checkpoint.cells_per_block);
        int burn_enabled = 0;
        int nse_enabled = 0;
        checkpoint.provenance.available = true;
        file.getAttribute("eos_type").read(checkpoint.provenance.eos_type);
        file.getAttribute("gravity_type").read(checkpoint.provenance.gravity_type);
        file.getAttribute("gravity_boundary").read(checkpoint.provenance.gravity_boundary);
        file.getDataSet("gravity_controls").read(checkpoint.provenance.gravity_controls);
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
        read_field("enuc_rate", field_dims, checkpoint.enuc_rate);
        checkpoint.has_enuc_rate = true;
        checkpoint.has_mass_fractions = true;
        if (checkpoint.num_species > 0) {
            read_field("rhoX", {static_cast<size_t>(checkpoint.num_species),
                                blocks, checkpoint.cells_per_block}, checkpoint.rhoX);
            read_field("X", {static_cast<size_t>(checkpoint.num_species),
                             blocks, checkpoint.cells_per_block}, checkpoint.mass_fractions);
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

        if (!has_consistent_checkpoint_payload(checkpoint)) {
            throw std::runtime_error("Checkpoint datasets have inconsistent dimensions.");
        }
        if (!has_consistent_checkpoint_composition(checkpoint)) {
            throw std::runtime_error(
                "Checkpoint native mass fractions are inconsistent with rhoX.");
        }
        if (!has_consistent_checkpoint_provenance(checkpoint)) {
            throw std::runtime_error(
                "Checkpoint scientific provenance is inconsistent.");
        }
        if (!has_valid_repair_ledger(checkpoint))
            throw std::runtime_error("Checkpoint repair ledger is invalid.");
        return checkpoint;
    } catch (const Exception& err) {
        throw std::runtime_error("Checkpoint read failed: " + std::string(err.what()));
    }
}

} // namespace io
