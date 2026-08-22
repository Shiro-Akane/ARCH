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

#include "HelmEos.h"
#include "IdealGas.h"
#include "Tabular3DEOS.h"
#include "Tabular4DEOS.h"
#include "eos.h"

#include "../../core/RuntimeParams.h"
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
    /**
     * @brief Resolve a runtime EOS name and invoke a callback with its concrete policy.
     * The generic callback preserves compile-time EOS specialization after this
     * single runtime branch.
     */
    template <typename Func>
    static void dispatch_eos(const SimConfig &config, const SpeciesManager &specs, Func &&func)
    {
        std::string eos_type = config.physics.eos_type;

        if (is_first_call)
        {
            std::cout << "[EOS Dispatch] Resolving EOS Policy: " << eos_type << std::endl;
        }

        if (eos_type == "ideal" || eos_type == "Ideal")
        {
            // IdealGas owns no table, so the policy object is passed directly.
            IdealGas eos(config.physics.gamma, specs);
            func(eos);
        }
        else if (eos_type == "tabular" || eos_type == "Tabular")
        {
            // Resolve the table path once before inspecting its dimensionality.
            std::string path = config.physics.eos_table_path;
            path.erase(std::remove(path.begin(), path.end(), '\"'), path.end());
            path.erase(std::remove(path.begin(), path.end(), '\''), path.end());
            if (path.empty())
            {
                throw std::runtime_error("Tabular EOS requires 'eos_table_path' in .par file!");
            }

            if (table_dimension == 0 || cached_table_path != path ||
                cached_species != &specs)
            {
                std::cout << "[EOS Dispatch] Inspecting HDF5 metadata..." << std::endl;
                cached_3d.reset();
                cached_4d.reset();
                table_dimension = inspect_eos_table_rank(path);
                cached_table_path = path;
                cached_species = &specs;
                if (table_dimension == 4) {
                    cached_4d = std::make_unique<Tabular4DEOS>(path, &specs);
                } else {
                    cached_3d = std::make_unique<Tabular3DEOS>(path, &specs);
                }
            }
            if (table_dimension == 4) {
                func(cached_4d->get_view());
            } else {
                func(cached_3d->get_view());
            }
        }
        else if (eos_type == "helmholtz" || eos_type == "Helmholtz")
        {
            std::string path = config.physics.eos_table_path;
            path.erase(std::remove(path.begin(), path.end(), '\"'), path.end());
            path.erase(std::remove(path.begin(), path.end(), '\''), path.end());
            if (path.empty())
            {
                throw std::runtime_error("Helmholtz EOS requires 'eos_table_path' in .par file!");
            }
            HelmEos eos_manager(path, &specs);
            func(eos_manager);
        }
        else
        {
            throw std::runtime_error("Unknown EOS Type: " + eos_type);
        }
        is_first_call = false;
    }
};
