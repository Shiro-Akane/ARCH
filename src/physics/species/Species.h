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

#ifndef EOS_INLINE
#define EOS_INLINE inline
#endif

/**
 * @brief Simple container for a single species' constant properties.
 */
struct GasProperty
{
    std::string name; ///< String identifier (e.g., "H2", "O2") for IO.
    double A;         ///< Mass number
    double Z;         ///< Atomic number
    double gamma_ref; ///< Specific Heat Ratio (Cp/Cv), also known as Adiabatic Index.
    double Cv_ref;    ///< Heat capacity (J/kg.K)
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
     * @param Cv The heat capacity at constant volume.
     * @return int The unique ID assigned to this species (used for array indexing).
     */
    int add_species(std::string name, double gamma, double Cv)
    {
        species_list.push_back({name, gamma, Cv});
        // The index of the newly added element is size - 1
        return species_list.size() - 1;
    }

    EOS_INLINE double get_A(int id) const { return species_list[id].A; }
    EOS_INLINE double get_Z(int id) const { return species_list[id].Z; }
    EOS_INLINE double get_gamma_ref(int id) const { return species_list[id].gamma_ref; }
    EOS_INLINE double get_Cv_ref(int id) const { return species_list[id].Cv_ref; }
    int count() const { return species_list.size(); }

    /**
     * @brief 计算电子丰度 Ye (Electron Fraction)
     * Ye = Sum( (Z_i / A_i) * Y_i )
     */
    EOS_INLINE double calc_Ye(const double *Yi) const
    {
        double Ye = 0.0;
        int n_spec = count();
        for (int k = 0; k < n_spec; ++k)
        {
            Ye += (get_Z(k) / get_A(k)) * Yi[k];
        }
        return Ye;
    }

    /**
     * @brief 计算平均原子量 \bar{A}
     */
    EOS_INLINE double calc_Abar(const double *Yi) const
    {
        double sum_Y_over_A = 0.0;
        int n_spec = count();
        for (int k = 0; k < n_spec; ++k)
        {
            sum_Y_over_A += Yi[k] / get_A(k);
        }
        return (sum_Y_over_A > 1e-16) ? (1.0 / sum_Y_over_A) : 1.0; // 防除零
    }

    /**
     * @brief 计算平均原子序数 \bar{Z}
     */
    EOS_INLINE double calc_Zbar(const double *Yi) const
    {
        double A_bar = calc_Abar(Yi);
        double Y_e = calc_Ye(Yi);
        return A_bar * Y_e;
    }
    /**
     * @brief Retrieves the name string for I/O purposes.
     */
    std::string get_name(int id) const { return species_list[id].name; }
};