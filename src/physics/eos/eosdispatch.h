/**
 * @file eosdispatch.h
 * @brief Selects and constructs the requested equation-of-state policy.
 *
 * Host construction binds species and table ownership before handing a
 * concrete policy to the caller. Cache identity includes ordered species
 * values and table file identity; thermodynamic evaluation remains in the
 * selected EOS implementation rather than being duplicated in the factory.
 */

#pragma once

#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "HelmEos.h"
#include "IdealGas.h"
#include "Tabular3DEOS.h"
#include "Tabular4DEOS.h"
#include "eos.h"

#include "../../core/FileFingerprint.h"
#include "../../core/RuntimeParams.h"
#include "../../driver/dispatch/PolicyDescriptor.h"
#include "../species/Species.h"

int inspect_eos_table_rank(const std::string& path);

struct EOSDispatcher
{
    struct SpeciesCacheIdentity
    {
        const SpeciesManager* owner;
        const GasProperty* data;
        std::size_t size;
        std::vector<GasProperty> ordered_values;
    };

    inline static bool is_first_call = true;
    inline static int table_dimension = 0; // 0 is unloaded; 3 and 4 identify table rank.
    inline static std::unique_ptr<HelmEos> cached_helm = nullptr;
    inline static std::unique_ptr<Tabular3DEOS> cached_3d = nullptr;
    inline static std::unique_ptr<Tabular4DEOS> cached_4d = nullptr;
    inline static std::string cached_helm_path;
    inline static std::string cached_helm_sha256;
    inline static SpeciesCacheIdentity cached_helm_species{
        nullptr, nullptr, 0, {}};
    inline static std::string cached_table_path;
    inline static std::string cached_table_sha256;
    inline static SpeciesCacheIdentity cached_species{
        nullptr, nullptr, 0, {}};

    static SpeciesCacheIdentity species_identity(const SpeciesManager& specs)
    {
        SpeciesCacheIdentity result;
        result.owner = &specs;
        result.data = specs.species_list.empty()
            ? nullptr : specs.species_list.data();
        result.size = specs.species_list.size();
        result.ordered_values = specs.species_list;
        return result;
    }

    static bool same_species_identity(const SpeciesCacheIdentity& left,
                                      const SpeciesCacheIdentity& right)
    {
        if (left.owner != right.owner || left.data != right.data
            || left.size != right.size
            || left.ordered_values.size() != right.ordered_values.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.ordered_values.size(); ++index) {
            const GasProperty& lhs = left.ordered_values[index];
            const GasProperty& rhs = right.ordered_values[index];
            if (lhs.name != rhs.name || lhs.A != rhs.A || lhs.Z != rhs.Z
                || lhs.gamma_ref != rhs.gamma_ref
                || lhs.Cv_ref != rhs.Cv_ref) {
                return false;
            }
        }
        return true;
    }

    static void require_species_identity_unchanged(
        const SpeciesCacheIdentity& before,
        const SpeciesCacheIdentity& after, const char* label)
    {
        if (!same_species_identity(before, after)) {
            throw std::runtime_error(
                std::string(label)
                + " species registry changed while the EOS was loading");
        }
    }

    template <typename Func, typename Eos>
    static void invoke_callback(Func&& func, Eos&& eos,
                                const std::string& loaded_table_sha256)
    {
        if constexpr (std::is_invocable_v<
                          Func&&, Eos&&, std::string_view>) {
            std::invoke(std::forward<Func>(func), std::forward<Eos>(eos),
                        std::string_view(loaded_table_sha256));
        } else {
            static_assert(std::is_invocable_v<Func&&, Eos&&>,
                          "EOS callback must accept the EOS, optionally "
                          "followed by its loaded table SHA-256");
            std::invoke(std::forward<Func>(func), std::forward<Eos>(eos));
        }
    }

    static std::string table_path(const SimConfig& config, const char* label)
    {
        std::string path = config.physics.eos_table_path;
        path.erase(std::remove(path.begin(), path.end(), '\"'), path.end());
        path.erase(std::remove(path.begin(), path.end(), '\''), path.end());
        if (path.empty())
            throw std::runtime_error(std::string(label)
                + " EOS requires 'eos_table_path' in .par file!");
        return path;
    }

    /**
     * @brief Visit the already-resolved EOS without parsing configuration.
     * A callback may accept a second `std::string_view`; it is the digest
     * bound to the loaded table owner, or empty for the ideal-gas policy.
     */
    template <typename Func>
    static void dispatch_eos(arch::dispatch::EosId eos_id,
                             const SimConfig& config,
                             const SpeciesManager& specs, Func&& func)
    {
        using arch::dispatch::EosId;
        if (is_first_call) {
            std::cout << "[EOS Dispatch] Resolving EOS Policy: "
                      << config.physics.eos_type << std::endl;
        }

        switch (eos_id) {
        case EosId::Ideal: {
            IdealGas eos(config.physics.gamma, specs);
            invoke_callback(std::forward<Func>(func), eos, {});
            break;
        }
        case EosId::Helmholtz: {
            const std::string path = table_path(config, "Helmholtz");
            const std::string before = arch::core::file_sha256(path);
            const auto species_before = species_identity(specs);
            if (!cached_helm || cached_helm_path != path
                || cached_helm_sha256 != before
                || !same_species_identity(
                    cached_helm_species, species_before)) {
                auto replacement = std::make_unique<HelmEos>(path, &specs);
                const std::string after = arch::core::file_sha256(path);
                const auto species_after = species_identity(specs);
                if (before != after) {
                    throw std::runtime_error(
                        "Helmholtz EOS table changed while it was loading");
                }
                require_species_identity_unchanged(
                    species_before, species_after, "Helmholtz EOS");
                cached_helm = std::move(replacement);
                cached_helm_path = path;
                cached_helm_sha256 = after;
                cached_helm_species = species_after;
            }
            invoke_callback(std::forward<Func>(func), *cached_helm,
                            cached_helm_sha256);
            break;
        }
        case EosId::Tabular3D: {
            const std::string path = table_path(config, "Tabular");
            const std::string before = arch::core::file_sha256(path);
            const auto species_before = species_identity(specs);
            if (table_dimension != 3 || !cached_3d
                || cached_table_path != path
                || cached_table_sha256 != before
                || !same_species_identity(cached_species, species_before)) {
                auto replacement =
                    std::make_unique<Tabular3DEOS>(path, &specs);
                const std::string after = arch::core::file_sha256(path);
                const auto species_after = species_identity(specs);
                if (before != after) {
                    throw std::runtime_error(
                        "Tabular EOS table changed while it was loading");
                }
                require_species_identity_unchanged(
                    species_before, species_after, "Tabular EOS");
                cached_4d.reset();
                cached_3d = std::move(replacement);
                cached_table_path = path;
                cached_table_sha256 = after;
                cached_species = species_after;
            }
            table_dimension = 3;
            invoke_callback(std::forward<Func>(func), cached_3d->get_view(),
                            cached_table_sha256);
            break;
        }
        case EosId::Tabular4D: {
            const std::string path = table_path(config, "Tabular");
            const std::string before = arch::core::file_sha256(path);
            const auto species_before = species_identity(specs);
            if (table_dimension != 4 || !cached_4d
                || cached_table_path != path
                || cached_table_sha256 != before
                || !same_species_identity(cached_species, species_before)) {
                auto replacement =
                    std::make_unique<Tabular4DEOS>(path, &specs);
                const std::string after = arch::core::file_sha256(path);
                const auto species_after = species_identity(specs);
                if (before != after) {
                    throw std::runtime_error(
                        "Tabular EOS table changed while it was loading");
                }
                require_species_identity_unchanged(
                    species_before, species_after, "Tabular EOS");
                cached_3d.reset();
                cached_4d = std::move(replacement);
                cached_table_path = path;
                cached_table_sha256 = after;
                cached_species = species_after;
            }
            table_dimension = 4;
            invoke_callback(std::forward<Func>(func), cached_4d->get_view(),
                            cached_table_sha256);
            break;
        }
        default:
            throw std::runtime_error("Unknown resolved EOS policy");
        }
        is_first_call = false;
    }

    /**
     * @brief Resolve a runtime EOS name and invoke a callback with its concrete policy.
     * The generic callback preserves compile-time EOS specialization after this
     * single runtime branch.
     */
    template <typename Func>
    static void dispatch_eos(const SimConfig &config, const SpeciesManager &specs, Func &&func)
    {
        using namespace arch::dispatch;
        EosId eos_id{};
        if (registration_matches<Tabular3DPolicy>(config.physics.eos_type)) {
            const int rank = inspect_eos_table_rank(
                table_path(config, "Tabular"));
            eos_id = rank == 4 ? EosId::Tabular4D : EosId::Tabular3D;
        } else {
            const auto parsed = parse_registered_policy<EosPolicies>(
                config.physics.eos_type);
            if (!parsed.ok)
                throw std::runtime_error(
                    "Unknown EOS Type: " + config.physics.eos_type);
            eos_id = parsed.value;
        }
        dispatch_eos(eos_id, config, specs, std::forward<Func>(func));
    }
};
