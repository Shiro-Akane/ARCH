/**
 * @file Tabular3DEOS.cpp
 * @brief Validated 3D HDF5 EOS loader for (log10 rho, log10 T, composition).
 */

#include "Tabular3DEOS.h"
#include "TabularLoaderUtils.h"
#include "TabularSource.h"
#include "TabularCompletion.h"
#include "TabularBaryonSource.h"

#include <algorithm>
#include <cmath>
#include <numeric>

Tabular3DEOS::Tabular3DEOS(const std::string& h5_filename,
                           const SpeciesManager* specs_ptr,const std::string& helm_path)
    : table_path(h5_filename)
{
    const auto source=inspect_tabular_source(h5_filename);
    if (source.format==TabularSourceFormat::BaryonAscii) {
        load_baryon_ascii(h5_filename,specs_ptr,helm_path);
        return;
    }
    if (source.format == TabularSourceFormat::EosDriver) {
        load_eosdriver(h5_filename, specs_ptr);
        return;
    }
    std::cout << "[Tabular3DEOS] Loading validated 3D HDF5 table: "
              << h5_filename << std::endl;
    HighFive::File file(h5_filename, HighFive::File::ReadOnly);
    tabular_eos::loader::validate_schema_version(file);

    if (file.exist("table_rank")) {
        int rank = 0;
        file.getDataSet("table_rank").read(rank);
        if (rank != 3) {
            throw std::runtime_error(
                "Tabular3DEOS received a table whose table_rank is not 3");
        }
    }

    tabular_eos::loader::read_required(file, "n_rho", view.n_rho);
    tabular_eos::loader::read_required(file, "n_T", view.n_T);
    tabular_eos::loader::read_required(file, "n_X", view.n_X);
    if (view.n_rho < 2 || view.n_T < 2 || view.n_X < 2) {
        throw std::runtime_error(
            "A 3D tabular EOS requires at least two points on every axis");
    }

    tabular_eos::loader::read_required(file, "log_rho_min", view.log_rho_min);
    tabular_eos::loader::read_required(file, "log_rho_max", view.log_rho_max);
    tabular_eos::loader::read_required(file, "log_T_min", view.log_T_min);
    tabular_eos::loader::read_required(file, "log_T_max", view.log_T_max);
    tabular_eos::loader::read_required(file, "X_min", view.X_min);
    tabular_eos::loader::read_required(file, "X_max", view.X_max);
    tabular_eos::loader::validate_bounds(
        view.log_rho_min, view.log_rho_max, "log10_rho");
    tabular_eos::loader::validate_bounds(
        view.log_T_min, view.log_T_max, "log10_temperature");
    tabular_eos::loader::validate_bounds(view.X_min, view.X_max, "composition");

    view.dlog_rho =
        (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
    view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
    view.dX = (view.X_max - view.X_min) / (view.n_X - 1);
    tabular_eos::loader::warn_if_coarse_spacing(
        "Tabular3DEOS", view.dlog_rho, view.dlog_T);

    const std::vector<std::size_t> shape{
        static_cast<std::size_t>(view.n_rho),
        static_cast<std::size_t>(view.n_T),
        static_cast<std::size_t>(view.n_X)
    };
    std::string thermodynamic_model = "direct";
    if (file.exist("thermodynamic_model")) {
        file.getDataSet("thermodynamic_model").read(thermodynamic_model);
    }

    if (thermodynamic_model == "free_energy") {
        if (view.n_rho < 5 || view.n_T < 5) {
            throw std::runtime_error(
                "Free-energy tables require at least five rho and temperature points");
        }
        std::vector<double> free_energy =
            tabular_eos::loader::read_table_field(file, "free_energy", shape);
        std::vector<double> seed_rho,seed_temperature;
        if (file.exist("free_energy_dlnrho")) seed_rho=
            tabular_eos::loader::read_table_field(file,"free_energy_dlnrho",shape);
        if (file.exist("free_energy_dlnT")) seed_temperature=
            tabular_eos::loader::read_table_field(file,"free_energy_dlnT",shape);
        if (source.components.needs_completion()) {
            std::string axis="Ye";
            if (file.exist("composition_axis")) file.getDataSet("composition_axis").read(axis);
            if (!source.components.electrons_positrons && axis!="Ye")
                throw std::runtime_error("Electron completion needs a Ye composition axis, not a species fraction");
            if (!specs_ptr || specs_ptr->count()==0)
                throw std::runtime_error("Component-completed EOS requires species metadata");
            tabular_eos::complete_free_energy(free_energy,h_table_valid,
                tabular_eos::uniform_axis(view.n_rho,view.log_rho_min,view.log_rho_max),
                tabular_eos::uniform_axis(view.n_T,view.log_T_min,view.log_T_max),
                tabular_eos::uniform_axis(view.n_X,view.X_min,view.X_max),source.components,helm_path,
                source.baryon_mass_g,seed_rho.empty()?nullptr:&seed_rho,
                seed_temperature.empty()?nullptr:&seed_temperature);
        }
        h_free_energy_fields = tabular_eos::build_derivative_fields(
            free_energy, view.n_rho, view.n_T, view.n_X,
            std::log(10.0) * view.dlog_rho,
            std::log(10.0) * view.dlog_T,seed_rho.empty()?nullptr:&seed_rho,
            seed_temperature.empty()?nullptr:&seed_temperature);
        if (source.components.declared || source.nuclear_equilibrium) {
            if (h_table_valid.empty()) h_table_valid.assign(free_energy.size(),1.0);
            h_table_valid=tabular_eos::free_energy_derivative_validity(
                h_table_valid,view.n_rho,view.n_T,view.n_X);
            view.energy_reference_shift=tabular_eos::positive_energy_reference(
                h_free_energy_fields,h_table_valid);
            view.strict_domain=true;
            view.table_valid=h_table_valid.data();
        }
        for (int field = 0; field < tabular_eos::FieldCount; ++field) {
            view.free_energy_fields[field] =
                h_free_energy_fields[field].data();
        }
        view.uses_free_energy = true;
        view.table_P = nullptr;
        view.table_E = nullptr;
        view.table_cs = nullptr;
        view.table_cv = nullptr;
        view.table_dP_drho = nullptr;
        view.table_dP_dT = nullptr;
    } else if (thermodynamic_model == "direct") {
        h_table_P = tabular_eos::loader::read_table_field(
            file, "pressure", shape);
        h_table_E = tabular_eos::loader::read_table_field(
            file, "energy", shape);
        h_table_cs = tabular_eos::loader::read_table_field(
            file, "sound_speed", shape);
        h_table_cv = tabular_eos::loader::read_table_field(
            file, "cv", shape);
        tabular_eos::loader::validate_direct_thermodynamics(
            h_table_P, h_table_E, h_table_cs, h_table_cv);
        tabular_eos::loader::validate_energy_increases_with_temperature(
            h_table_E, view.n_rho, view.n_T, view.n_X);
        view.table_P = h_table_P.data();
        view.table_E = h_table_E.data();
        view.table_cs = h_table_cs.data();
        view.table_cv = h_table_cv.data();

        view.table_dP_drho = nullptr;
        view.table_dP_dT = nullptr;
        if (file.exist("dp_drho")) {
            h_table_dP_drho = tabular_eos::loader::read_table_field(
                file, "dp_drho", shape);
            view.table_dP_drho = h_table_dP_drho.data();
        }
        if (file.exist("dp_dT")) {
            h_table_dP_dT = tabular_eos::loader::read_table_field(
                file, "dp_dT", shape);
            view.table_dP_dT = h_table_dP_dT.data();
        }
    } else {
        throw std::runtime_error(
            "Unknown thermodynamic_model '" + thermodynamic_model +
            "'; expected 'free_energy' or 'direct'");
    }

    specs_owner = specs_ptr;
    view.specs = specs_ptr ? specs_ptr->get_host_view() : SpeciesHostView{};
    view.target_species_id = -1;
    if (file.exist("composition_axis")) {
        std::string composition_axis;
        file.getDataSet("composition_axis").read(composition_axis);
        constexpr const char* species_prefix = "species:";
        if (composition_axis.rfind(species_prefix, 0) == 0) {
            if (specs_ptr == nullptr) {
                throw std::runtime_error(
                    "A species composition_axis requires a SpeciesManager");
            }
            const std::string species_name =
                composition_axis.substr(std::char_traits<char>::length(species_prefix));
            view.target_species_id = specs_ptr->GetSpeciesID(species_name);
            if (view.target_species_id < 0) {
                throw std::runtime_error(
                    "composition_axis names an unregistered species: " + species_name);
            }
        } else if (composition_axis != "Ye") {
            throw std::runtime_error(
                "3D composition_axis must be 'Ye' or 'species:<registered-name>'");
        }
    }

    std::cout << "[Tabular3DEOS] Loaded "
              << (view.uses_free_energy ? "free-energy" : "direct")
              << " table with automatic rank validation." << std::endl;
}

void Tabular3DEOS::load_baryon_ascii(const std::string& path,
    const SpeciesManager* species,const std::string& helm_path)
{
    if (!species || species->count()==0)
        throw std::runtime_error("Baryon EOS requires species metadata to determine Ye");
    auto source=tabular_eos::source::read_baryon_ascii_table(path);
    view.n_rho=static_cast<int>(source.log_density.size());
    view.n_T=static_cast<int>(source.log_temperature.size());
    view.n_X=static_cast<int>(source.electron_fraction.size());
    h_axis_nodes={source.log_density,source.log_temperature,source.electron_fraction};
    view.log_rho_min=h_axis_nodes[0].front(); view.log_rho_max=h_axis_nodes[0].back();
    view.log_T_min=h_axis_nodes[1].front(); view.log_T_max=h_axis_nodes[1].back();
    view.X_min=h_axis_nodes[2].front(); view.X_max=h_axis_nodes[2].back();
    view.dlog_rho=tabular_eos::require_uniform_axis(h_axis_nodes[0],"log density");
    view.dlog_T=tabular_eos::require_uniform_axis(h_axis_nodes[1],"log temperature");
    view.dX=(view.X_max-view.X_min)/(view.n_X-1);
    h_table_valid=std::move(source.source_valid);
    std::vector<double> seed_rho(source.free_energy.size()),seed_temperature(source.free_energy.size());
    for (int r=0;r<view.n_rho;++r) for (int t=0;t<view.n_T;++t) for (int c=0;c<view.n_X;++c) {
        const std::size_t i=(static_cast<std::size_t>(r)*view.n_T+t)*view.n_X+c;
        seed_rho[i]=source.pressure[i]/source.density[r];
        // Entropy retains small thermal information that can be lost when
        // independently rounded, strongly degenerate source F/E are subtracted.
        seed_temperature[i]=-source.temperature[t]*source.entropy[i];
    }
    tabular_eos::complete_free_energy(source.free_energy,h_table_valid,
        h_axis_nodes[0],h_axis_nodes[1],h_axis_nodes[2],{true,false,false},helm_path,
        source.baryon_mass_g,&seed_rho,&seed_temperature);
    h_free_energy_fields=tabular_eos::build_derivative_fields(source.free_energy,
        view.n_rho,view.n_T,view.n_X,std::log(10.0)*view.dlog_rho,std::log(10.0)*view.dlog_T,
        &seed_rho,&seed_temperature);
    h_table_valid=tabular_eos::free_energy_derivative_validity(
        h_table_valid,view.n_rho,view.n_T,view.n_X);
    view.energy_reference_shift=tabular_eos::positive_energy_reference(h_free_energy_fields,h_table_valid);
    view.uses_free_energy=true;
    view.strict_domain=true;
    view.native_direct=false;
    view.target_species_id=-1;
    specs_owner=species;
    view=get_view();
    std::cout << "[Tabular3DEOS] Loaded baryon source and completed a single free-energy potential; "
              << std::count(h_table_valid.begin(),h_table_valid.end(),0.0)
              << " source/component derivative stencils excluded. Constant energy reference="
              << view.energy_reference_shift << " erg/g.\n";
}

namespace {
template <typename Value>
Value read_native_scalar(const HighFive::File& file, const char* name)
{
    const auto dataset = file.getDataSet(name);
    if (dataset.getElementCount() != 1)
        throw std::runtime_error(std::string("EOSDriver scalar has invalid shape: ") + name);
    Value value{};
    dataset.read(&value);
    return value;
}

std::vector<double> read_native_axis(const HighFive::File& file, const char* name, int count)
{
    auto values = tabular_eos::loader::read_table_field(file, name,
        {static_cast<std::size_t>(count)});
    for (int i = 1; i < count; ++i)
        if (!(values[i] > values[i - 1]))
            throw std::runtime_error(std::string("EOSDriver axis is not strictly increasing: ") + name);
    return values;
}

// EOSDriver's Fortran (rho,T,Ye) arrays appear as (Ye,T,rho) in HDF5 C order.
// Reordering changes storage only; retain native samples and encodings.
std::vector<double> read_native_field(const HighFive::File& file, const char* name,
                                     int nr, int nt, int nx)
{
    const auto source = tabular_eos::loader::read_table_field(file, name,
        {static_cast<std::size_t>(nx), static_cast<std::size_t>(nt), static_cast<std::size_t>(nr)});
    std::vector<double> result(source.size());
    for (int x = 0; x < nx; ++x)
        for (int t = 0; t < nt; ++t)
            for (int r = 0; r < nr; ++r)
                result[(static_cast<std::size_t>(r) * nt + t) * nx + x]
                    = source[(static_cast<std::size_t>(x) * nt + t) * nr + r];
    return result;
}
} // namespace

void Tabular3DEOS::load_eosdriver(const std::string& path, const SpeciesManager* species)
{
    const HighFive::File file(path, HighFive::File::ReadOnly);
    view.n_rho = read_native_scalar<int>(file, "pointsrho");
    view.n_T = read_native_scalar<int>(file, "pointstemp");
    view.n_X = read_native_scalar<int>(file, "pointsye");
    if (view.n_rho < 2 || view.n_T < 2 || view.n_X < 2)
        throw std::runtime_error("EOSDriver tables require at least two points per axis");
    if (!species || species->count() == 0)
        throw std::runtime_error("EOSDriver total EOS requires species metadata to determine Ye");
    view.source_energy_shift = read_native_scalar<double>(file, "energy_shift");
    if (!std::isfinite(view.source_energy_shift) || view.source_energy_shift < 0.0)
        throw std::runtime_error("EOSDriver energy_shift must be finite and nonnegative");
    h_axis_nodes[0] = read_native_axis(file, "logrho", view.n_rho);
    h_axis_nodes[1] = read_native_axis(file, "logtemp", view.n_T);
    h_axis_nodes[2] = read_native_axis(file, "ye", view.n_X);
    if (h_axis_nodes[2].front() < 0.0 || h_axis_nodes[2].back() > 1.0)
        throw std::runtime_error("EOSDriver Ye axis must lie in [0,1]");
    constexpr double mev_per_kelvin = arch::constants::statistical::cgs::boltzmann
        / arch::constants::units::erg_per_mev;
    for (double& log_temperature : h_axis_nodes[1])
        log_temperature -= std::log10(mev_per_kelvin);
    view.log_rho_min = h_axis_nodes[0].front();
    view.log_rho_max = h_axis_nodes[0].back();
    view.log_T_min = h_axis_nodes[1].front();
    view.log_T_max = h_axis_nodes[1].back();
    view.X_min = h_axis_nodes[2].front();
    view.X_max = h_axis_nodes[2].back();
    view.dlog_rho = (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
    view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
    view.dX = (view.X_max - view.X_min) / (view.n_X - 1);
    const int nr = view.n_rho, nt = view.n_T, nx = view.n_X;
    h_table_P = read_native_field(file, "logpress", nr, nt, nx);
    h_table_E = read_native_field(file, "logenergy", nr, nt, nx);
    h_table_cv = read_native_field(file, "dedt", nr, nt, nx);
    h_table_cs = read_native_field(file, "cs2", nr, nt, nx);
    h_table_dP_drho = read_native_field(file, "dpdrhoe", nr, nt, nx);
    h_table_dP_de = read_native_field(file, "dpderho", nr, nt, nx);
    h_table_valid.assign(h_table_E.size(), 1.0);
    for (int r = 0; r < nr; ++r) {
        const double rho = std::pow(10.0, h_axis_nodes[0][r]);
        for (int t = 0; t < nt; ++t) for (int x = 0; x < nx; ++x) {
            const std::size_t i = (static_cast<std::size_t>(r) * nt + t) * nx + x;
            h_table_cv[i] *= mev_per_kelvin;
            const double p = std::pow(10.0, h_table_P[i]);
            const double e = std::pow(10.0, h_table_E[i]);
            const double acoustic = h_table_dP_drho[i] + h_table_dP_de[i] * p / (rho * rho);
            if (!(p > 0.0) || !(e > 0.0) || !std::isfinite(p) || !std::isfinite(e)
                || !(h_table_cv[i] > 0.0) || !(h_table_cs[i] > 0.0)
                || !(acoustic > 0.0) || !std::isfinite(acoustic))
                h_table_valid[i] = 0.0;
            if (t + 1 < nt && !(h_table_E[i + nx] > h_table_E[i])) {
                h_table_valid[i] = 0.0;
                h_table_valid[i + nx] = 0.0;
            }
        }
    }
    const auto invalid = std::count(h_table_valid.begin(), h_table_valid.end(), 0.0);
    if (invalid == static_cast<std::ptrdiff_t>(h_table_valid.size()))
        throw std::runtime_error("EOSDriver table has no valid thermodynamic nodes");
    specs_owner = species;
    view.specs = species->get_host_view();
    view.target_species_id = -1;
    view.native_direct = true;
    view.uses_free_energy = false;
    view.pressure_transform.logarithmic = true;
    view.energy_transform.logarithmic = true;
    view = get_view();
    std::cout << "[Tabular3DEOS] Loaded EOSDriver total equilibrium EOS, native axes/fields; "
              << invalid << " invalid nodes excluded from the query domain. "
              << "Conserved energy includes the source reference shift "
              << view.source_energy_shift << " erg/g.\n";
}
