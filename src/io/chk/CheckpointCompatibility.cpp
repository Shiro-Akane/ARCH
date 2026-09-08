/**
 * @file CheckpointCompatibility.cpp
 * @brief Deterministic checkpoint provenance and compatibility validation.
 */

#include "CheckpointCompatibility.h"

#include "../../core/FileFingerprint.h"
#include "../../data/GlobalDefs.h"
#include "../../driver/dispatch/PolicyDescriptor.h"
#include "../../physics/species/Species.h"
#include "../../physics/eos/TabularSource.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace io {
namespace {

std::string lower_ascii(std::string value)
{
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string unquote(std::string value)
{
    value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\''), value.end());
    return value;
}

bool is_sha256(std::string_view digest)
{
    return digest.size() == 64
        && std::all_of(digest.begin(), digest.end(), [](char character) {
               return (character >= '0' && character <= '9')
                   || (character >= 'a' && character <= 'f');
           });
}

void require_equal(bool equal, const char* field)
{
    if (!equal)
        throw std::runtime_error(
            std::string("Checkpoint provenance mismatch: ") + field);
}

void require_execution_identity(const CheckpointProvenance& provenance)
{
    if (lower_ascii(provenance.active_network) != provenance.active_network)
        throw std::runtime_error(
            "Checkpoint active reaction network is not canonical lowercase");
    if (provenance.burn_enabled) {
        if (provenance.active_network.empty()
            || provenance.active_network == "none") {
            throw std::runtime_error(
                "Active burning requires a checkpoint reaction network");
        }
    } else {
        if (provenance.active_network != "none")
            throw std::runtime_error(
                "Disabled burning requires active_network=none");
        if (provenance.nse_enabled)
            throw std::runtime_error(
                "NSE cannot be active while burning is disabled");
    }
}

std::string canonical_eos_identity(arch::dispatch::EosId resolved_eos)
{
    const std::string_view canonical =
        arch::dispatch::canonical_policy_name<
            arch::dispatch::EosPolicies>(resolved_eos);
    if (canonical.empty())
        throw std::runtime_error(
            "Checkpoint provenance received an unknown resolved EOS policy");
    return std::string(canonical);
}

} // namespace

CheckpointProvenance make_checkpoint_provenance(
    const SimConfig& config, const SpeciesManager& species,
    arch::dispatch::EosId resolved_eos,
    bool burn_enabled, std::string_view active_network, bool nse_enabled,
    std::string_view loaded_eos_table_sha256)
{
    CheckpointProvenance result;
    result.available = true;
    result.eos_type = canonical_eos_identity(resolved_eos);
    result.ideal_gamma = config.physics.gamma;
    result.burn_enabled = burn_enabled;
    result.active_network = lower_ascii(std::string(active_network));
    result.nse_enabled = nse_enabled;

    if (result.eos_type.empty())
        throw std::runtime_error(
            "Checkpoint provenance requires an EOS identity");
    require_execution_identity(result);

    if (result.eos_type == "ideal") {
        if (!std::isfinite(result.ideal_gamma) || result.ideal_gamma <= 1.0)
            throw std::runtime_error(
                "Checkpoint provenance received an invalid ideal-gas gamma");
        if (!loaded_eos_table_sha256.empty())
            throw std::runtime_error(
                "Ideal-gas checkpoint provenance cannot name an EOS table");
    } else {
        result.eos_table_path = unquote(config.physics.eos_table_path);
        if (result.eos_table_path.empty())
            throw std::runtime_error(
                "Checkpoint provenance requires an EOS table path");
        if (!is_sha256(loaded_eos_table_sha256))
            throw std::runtime_error(
                "Checkpoint provenance requires a lowercase EOS table SHA-256");
        result.eos_table_sha256 = std::string(loaded_eos_table_sha256);
    }

    const std::size_t count = static_cast<std::size_t>(species.count());
    result.species_names.reserve(count);
    result.species_A.reserve(count);
    result.species_Z.reserve(count);
    result.species_gamma.reserve(count);
    result.species_Cv.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const int id = static_cast<int>(index);
        result.species_names.push_back(species.get_name(id));
        result.species_A.push_back(species.get_A(id));
        result.species_Z.push_back(species.get_Z(id));
        result.species_gamma.push_back(species.get_gamma_ref(id));
        result.species_Cv.push_back(species.get_Cv_ref(id));
    }
    return result;
}

CheckpointProvenance inspect_checkpoint_provenance(
    const SimConfig& config, const SpeciesManager& species,
    arch::dispatch::EosId resolved_eos,
    bool burn_enabled, std::string_view active_network, bool nse_enabled)
{
    const std::string eos_type = canonical_eos_identity(resolved_eos);
    const std::string path = unquote(config.physics.eos_table_path);
    if (eos_type.empty())
        throw std::runtime_error(
            "Checkpoint provenance requires an EOS identity");
    if (eos_type != "ideal" && path.empty())
        throw std::runtime_error(
            "Checkpoint provenance requires an EOS table path");
    const bool tabular = resolved_eos == arch::dispatch::EosId::Tabular3D
        || resolved_eos == arch::dispatch::EosId::Tabular4D;
    const std::string digest = eos_type == "ideal" ? std::string{}
        : tabular ? tabular_source_fingerprint(path,unquote(config.physics.eos_helm_table_path))
                  : arch::core::file_sha256(path);
    return make_checkpoint_provenance(
        config, species, resolved_eos, burn_enabled, active_network,
        nse_enabled, digest);
}

bool require_checkpoint_provenance_compatible(
    const CheckpointProvenance& saved,
    const CheckpointProvenance& expected)
{
    if (!saved.available) return false;
    if (!expected.available)
        throw std::logic_error("Expected checkpoint provenance is unavailable");
    require_execution_identity(saved);
    require_execution_identity(expected);
    const std::string saved_eos = lower_ascii(saved.eos_type);
    const std::string expected_eos = lower_ascii(expected.eos_type);
    require_equal(!saved_eos.empty() && saved_eos == expected_eos,
                  "EOS policy");
    if (expected_eos == "ideal") {
        require_equal(saved.eos_table_path.empty()
                          && saved.eos_table_sha256.empty()
                          && expected.eos_table_path.empty()
                          && expected.eos_table_sha256.empty(),
                      "ideal-gas table identity");
        require_equal(saved.ideal_gamma == expected.ideal_gamma,
                      "ideal-gas gamma");
    } else {
        require_equal(!saved.eos_table_path.empty()
                          && !expected.eos_table_path.empty()
                          && is_sha256(saved.eos_table_sha256)
                          && is_sha256(expected.eos_table_sha256)
                          && saved.eos_table_sha256
                              == expected.eos_table_sha256,
                      "EOS table content SHA-256");
    }
    require_equal(saved.burn_enabled == expected.burn_enabled,
                  "burn enabled state");
    require_equal(lower_ascii(saved.active_network)
                      == lower_ascii(expected.active_network),
                  "active reaction network");
    require_equal(saved.nse_enabled == expected.nse_enabled,
                  "NSE enabled state");
    require_equal(saved.species_names.size() == expected.species_names.size(),
                  "species count");
    require_equal(saved.species_A.size() == saved.species_names.size()
                      && saved.species_Z.size() == saved.species_names.size()
                      && saved.species_gamma.size() == saved.species_names.size()
                      && saved.species_Cv.size() == saved.species_names.size(),
                  "saved species metadata shape");
    require_equal(expected.species_A.size() == expected.species_names.size()
                      && expected.species_Z.size() == expected.species_names.size()
                      && expected.species_gamma.size() == expected.species_names.size()
                      && expected.species_Cv.size() == expected.species_names.size(),
                  "configured species metadata shape");
    for (std::size_t index = 0; index < saved.species_names.size(); ++index) {
        require_equal(!saved.species_names[index].empty()
                          && !expected.species_names[index].empty()
                          && lower_ascii(saved.species_names[index])
                          == lower_ascii(expected.species_names[index]),
                      "ordered species name");
        require_equal(saved.species_A[index] == expected.species_A[index]
                          && saved.species_Z[index] == expected.species_Z[index]
                          && saved.species_gamma[index]
                              == expected.species_gamma[index]
                          && saved.species_Cv[index]
                              == expected.species_Cv[index],
                      "ordered species properties");
    }
    return true;
}

void require_loaded_eos_table_compatible(
    std::string_view restart_verified_sha256,
    std::string_view loaded_eos_table_sha256)
{
    if (restart_verified_sha256.empty()) return;
    if (!is_sha256(restart_verified_sha256)
        || !is_sha256(loaded_eos_table_sha256)
        || restart_verified_sha256 != loaded_eos_table_sha256) {
        throw std::runtime_error(
            "Checkpoint provenance mismatch: EOS table changed between "
            "restart verification and runtime loading");
    }
}

} // namespace io
