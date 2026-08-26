/**
 * @file eosdispatch.h
 * @brief Selects and constructs the requested equation-of-state policy.
 *
 * Workflow:
 * 1. Construct or query the configured thermodynamic closure from canonical state variables.
 * 2. Return pressure, temperature, and transport quantities with validated bounds.
 * 3. Keep host and future device views consistent through one dispatch contract.
 */

#pragma once

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "HelmEos.h"
#include "IdealGas.h"
#include "Tabular3DEOS.h"
#include "Tabular4DEOS.h"
#include "eos.h"

#include "../../core/RuntimeParams.h"
#include "../../driver/dispatch/PolicyDescriptor.h"
#include "../species/Species.h"

int inspect_eos_table_rank(const std::string& path);

struct EOSDispatcher
{
    inline static bool is_first_call = true;
    inline static int table_dimension = 0; // 0 is unloaded; 3 and 4 identify table rank.
    inline static std::unique_ptr<Tabular3DEOS> cached_3d = nullptr;
    inline static std::unique_ptr<Tabular4DEOS> cached_4d = nullptr;
    inline static std::string cached_table_path;
    inline static const SpeciesManager *cached_species = nullptr;

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

    /** @brief Visit the already-resolved EOS without parsing configuration. */
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
            func(eos);
            break;
        }
        case EosId::Helmholtz: {
            HelmEos eos_manager(table_path(config, "Helmholtz"), &specs);
            func(eos_manager);
            break;
        }
        case EosId::Tabular3D: {
            const std::string path = table_path(config, "Tabular");
            if (table_dimension != 3 || !cached_3d
                || cached_table_path != path || cached_species != &specs) {
                auto replacement =
                    std::make_unique<Tabular3DEOS>(path, &specs);
                cached_4d.reset();
                cached_3d = std::move(replacement);
                cached_table_path = path;
                cached_species = &specs;
            }
            table_dimension = 3;
            func(cached_3d->get_view());
            break;
        }
        case EosId::Tabular4D: {
            const std::string path = table_path(config, "Tabular");
            if (table_dimension != 4 || !cached_4d
                || cached_table_path != path || cached_species != &specs) {
                auto replacement =
                    std::make_unique<Tabular4DEOS>(path, &specs);
                cached_3d.reset();
                cached_4d = std::move(replacement);
                cached_table_path = path;
                cached_species = &specs;
            }
            table_dimension = 4;
            func(cached_4d->get_view());
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
