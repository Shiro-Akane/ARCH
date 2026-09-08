/**
 * @file eosdispatch.cpp
 * @brief HDF5 rank inspection kept out of the templated EOS dispatcher.
 */

#include "eosdispatch.h"
#include "TabularSource.h"
#include "TabularBaryonSource.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

#include <highfive/H5File.hpp>

namespace {
constexpr char completion_interpretation[] =
    ";completion=missing-components-free-energy;derivatives=shared-biquintic;"
    "electron-density=source-baryon-mass;validity=source-and-component-stencils;"
    "energy-reference=constant-grid-envelope";

int normalized_rank(const HighFive::File& file)
{
    if (file.exist("table_rank")) {
        int rank = 0;
        file.getDataSet("table_rank").read(rank);
        if (rank != 3 && rank != 4) {
            throw std::runtime_error(
                "Tabular EOS table_rank must be exactly 3 or 4");
        }
        const bool has_A = file.exist("n_A");
        const bool has_Z = file.exist("n_Z");
        const bool has_X = file.exist("n_X");
        const bool valid_3d = rank == 3 && has_X && !has_A && !has_Z;
        const bool valid_4d = rank == 4 && has_A && has_Z && !has_X;
        if (!valid_3d && !valid_4d) {
            throw std::runtime_error(
                "Tabular EOS table_rank disagrees with its composition axes");
        }
        return rank;
    }

    const bool has_A = file.exist("n_A");
    const bool has_Z = file.exist("n_Z");
    const bool has_X = file.exist("n_X");
    if (has_A != has_Z || (has_X && has_A)) {
        throw std::runtime_error(
            "Tabular EOS without table_rank has incomplete or ambiguous composition axes");
    }
    if (has_A && has_Z) return 4;
    if (has_X) return 3;
    throw std::runtime_error(
        "Cannot infer tabular EOS rank: provide table_rank or n_X/n_A/n_Z composition-axis datasets");
}
} // namespace

TabularSourceInfo inspect_tabular_source(const std::string& path)
{
    if (H5Fis_hdf5(path.c_str()) <= 0) {
        if (tabular_eos::source::is_baryon_ascii_table(path))
            return {3, TabularSourceFormat::BaryonAscii, true,
                std::string(tabular_eos::source::baryon_ascii16::interpretation)
                    + ";constraints=source-F-P-S" + completion_interpretation,
                {true,false,false},tabular_eos::source::baryon_ascii16::baryon_mass_g};
        throw std::runtime_error(
            "Unrecognized tabular EOS format: expected normalized HDF5, EOSDriver total HDF5 "
            "or a supported finite-temperature baryon-table schema");
    }
    const HighFive::File file(path, HighFive::File::ReadOnly);
    const bool normalized = file.exist("n_rho") || file.exist("table_rank")
        || file.exist("arch_eos_version");
    const bool eosdriver = file.exist("pointsrho") || file.exist("pointstemp")
        || file.exist("pointsye") || file.exist("logpress")
        || file.exist("logenergy");
    if (normalized && eosdriver)
        throw std::runtime_error("Ambiguous normalized/EOSDriver table schema");
    TabularComponents components;
    if (file.exist("eos_components")) {
        std::string declaration;
        file.getDataSet("eos_components").read(declaration);
        components={true,false,false};
        if (declaration=="total") components={true,true,true};
        else {
            std::set<std::string> tokens;
            std::istringstream input(declaration);
            std::string token;
            while (std::getline(input,token,',')) {
                if ((token!="baryons" && token!="electrons_positrons" && token!="photons")
                    || !tokens.insert(token).second)
                    throw std::runtime_error("Unknown or repeated eos_components entry: "+token);
            }
            if (!tokens.contains("baryons") || declaration.empty() || declaration.back()==',')
                throw std::runtime_error("eos_components must explicitly include baryons");
            components.electrons_positrons=tokens.contains("electrons_positrons");
            components.photons=tokens.contains("photons");
        }
    }
    if (eosdriver) {
        if (components.needs_completion())
            throw std::runtime_error("EOSDriver total-table schema cannot declare missing components");
        for (const char* name : {"pointsrho", "pointstemp", "pointsye",
                "logrho", "logtemp", "ye", "logpress", "logenergy",
                "energy_shift", "dedt", "dpdrhoe", "dpderho", "cs2"}) {
            if (!file.exist(name))
                throw std::runtime_error(
                    std::string("Incomplete EOSDriver total table: missing '")
                    + name + "'");
        }
        return {3, TabularSourceFormat::EosDriver, true,
            "eosdriver-total-v1;rho=cgs;T=MeV;Ye=charge-neutral;"
            "energy=source-shifted;fields=native-log-linear;"
            "axes=native;domain=valid-cells;thermal-acoustic=interpolant-first-law"};
    }
    bool equilibrium=false;
    if (file.exist("nuclear_equilibrium")) {
        const auto data=file.getDataSet("nuclear_equilibrium");
        if (data.getElementCount()!=1 || H5Tget_class(data.getDataType().getId())!=H5T_INTEGER)
            throw std::runtime_error("nuclear_equilibrium requires one integer 0 or 1");
        int flag=0;
        data.read(flag);
        if (flag!=0 && flag!=1) throw std::runtime_error("nuclear_equilibrium must be 0 or 1");
        equilibrium=flag==1;
    }
    if (components.needs_completion()) {
        std::string model;
        if (file.exist("thermodynamic_model")) file.getDataSet("thermodynamic_model").read(model);
        if (model!="free_energy")
            throw std::runtime_error("Automatic component completion requires a free_energy potential; "
                "independent pressure/energy fields do not define a thermodynamic potential");
    }
    double baryon_mass=0.0;
    if (file.exist("baryon_mass_g")) {
        const auto data=file.getDataSet("baryon_mass_g");
        if (data.getElementCount()!=1) throw std::runtime_error("baryon_mass_g must be one scalar");
        data.read(baryon_mass);
        if (!(baryon_mass>0.0) || !std::isfinite(baryon_mass))
            throw std::runtime_error("baryon_mass_g must be finite and positive");
    }
    return {normalized_rank(file), TabularSourceFormat::Normalized, equilibrium,
        components.declared || equilibrium
            ? std::string("declared-physics;constraints=F-with-optional-log-derivatives")
                + completion_interpretation : std::string{}, components,baryon_mass};
}

int inspect_eos_table_rank(const std::string& path)
{
    return inspect_tabular_source(path).rank;
}

void EOSDispatcher::validate_coupling(const SimConfig& config,
    const TabularSourceInfo& source, bool requires_composition_gamma)
{
    if (source.nuclear_equilibrium && config.physics.burn.use_burn)
        throw std::invalid_argument(
            "This EOS source already includes nuclear-equilibrium binding energy. "
            "Independent kinetic burning/NSE would double-count nuclear energy; "
            "use_burn must be false until a consistent coupled model is supplied.");
    if (!source.components.declared && !source.nuclear_equilibrium) return;
    if (requires_composition_gamma)
        throw std::invalid_argument(
            "Steger-Warming requires a composition-only gamma and is not compatible "
            "with this declared tabular EOS; select a general-EOS flux.");
    const auto& diffusion = config.physics.diffusion;
    if (diffusion.use_diffusion && diffusion.use_thermal_diffusion
        && !(diffusion.alpha_therm > 0.0))
        throw std::invalid_argument(
            "This tabular EOS does not supply the electron diagnostics required by "
            "automatic stellar conductivity. Declare a positive alpha_therm for a "
            "constant-diffusivity model or disable thermal diffusion.");
}

std::string tabular_source_fingerprint(const std::string& path,const std::string& helm_path)
{
    const std::string digest = arch::core::file_sha256(path);
    // Fingerprinting is also used before construction by restart diagnostics.
    // The loader, not a content-identity operation, rejects non-table bytes.
    if (H5Fis_hdf5(path.c_str()) <= 0
        && !tabular_eos::source::is_baryon_ascii_table(path)) return digest;
    const auto source = inspect_tabular_source(path);
    if (source.interpretation.empty()) return digest;
    std::string identity=source.interpretation+"\n"+digest;
    if (!source.components.electrons_positrons)
        identity+="\nhelm-electrons="+arch::core::file_sha256(
            helm_path.empty() ? default_tabular_helm_path : helm_path);
    return arch::core::string_sha256(identity);
}
