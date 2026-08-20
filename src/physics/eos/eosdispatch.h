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

bool check_eos_is_4d(const std::string& path);

struct EOSDispatcher
{
    inline static bool is_first_call = true;
    inline static int table_dimension = 0; // 0 is unloaded; 3 and 4 identify table rank.
    inline static std::unique_ptr<Tabular3DEOS> cached_3d = nullptr;
    inline static std::unique_ptr<Tabular4DEOS> cached_4d = nullptr;
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

            if (table_dimension == 0)
            {
                std::cout << "[EOS Dispatch] Inspecting HDF5 Metadata..." << std::endl;

                bool is_4d = check_eos_is_4d(path);

                if (is_4d)
                {
                    std::cout << "[EOS Dispatch] 4D Helmholtz EOS format." << std::endl;

                    // Keep the four-dimensional owner alive while its view is in use.
                    Tabular4DEOS eos_manager(path, &specs);
                    func(eos_manager.get_view());
                }
                else
                {
                    std::cout << "[EOS Dispatch] 3D Tabular EOS format." << std::endl;

                    // Keep the three-dimensional owner alive while its view is in use.
                    Tabular3DEOS eos_manager(path, &specs);
                    func(eos_manager.get_view());
                }
            }
            if (table_dimension == 4)
            {
                func(cached_4d->get_view());
            }
            else
            {
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
