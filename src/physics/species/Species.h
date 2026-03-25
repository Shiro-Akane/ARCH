/**
 * @file Species.h
 * @brief Manages the thermodynamic properties of chemical species.
 * * Defines a central registry to handle multi-species mixtures.
 * * Assumes a "Calorically Perfect Gas" model where properties like Gamma
 * * are constant constants for each species.
 */

#pragma once

#include <string>
#include <vector>

/**
 * @brief Simple container for a single species' constant properties.
 */
struct GasProperty
{
    std::string name; ///< String identifier (e.g., "H2", "O2") for IO.
    double gamma;     ///< Specific Heat Ratio (Cp/Cv), also known as Adiabatic Index.
    double Cv;        ///< Heat capacity (J/kg.K)
};

/**
 * @brief Central registry for all species involved in the simulation.
 * * Responsibilities:
 * * 1. Registers new species dynamically.
 * * 2. Assigns a unique integer ID (0, 1, 2...) to each species based on registration order.
 * * 3. Provides efficient O(1) access to properties via that ID.
 */
struct SpeciesManager
{
    /// Internal storage of species properties, indexed by ID.
    std::vector<GasProperty> species_list;

    /**
     * @brief Registers a new species into the simulation.
     * @param name Name of the species (for logging/output).
     * @param gamma The heat capacity ratio (Cp/Cv).
     * @return int The unique ID assigned to this species (used for array indexing).
     */
    int add_species(std::string name, double gamma, double Cv)
    {
        species_list.push_back({name, gamma, Cv});
        // The index of the newly added element is size - 1
        return species_list.size() - 1;
    }

    /**
     * @brief Retrieves the Gamma value for a specific species ID.
     * @param id The species index (0 to N-1).
     */
    double get_gamma(int id) const { return species_list[id].gamma; }
    double get_Cv(int id) const { return species_list[id].Cv; }
    /**
     * @brief Returns the total number of registered species.
     * Used to size data arrays (e.g., FluidState::Y).
     */
    int count() const { return species_list.size(); }

    /**
     * @brief Retrieves the name string for I/O purposes.
     */
    std::string get_name(int id) const { return species_list[id].name; }
};