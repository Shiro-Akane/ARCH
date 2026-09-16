/**
 * @file Species.h
 * @brief Ordered species registry and shared composition averages.
 *
 * Registration order defines mass-fraction array indices for EOS, networks,
 * and output. A and Z describe isotope composition; gamma_ref and Cv_ref are
 * the per-species constant parameters used by the calorically perfect ideal
 * gas model, not a restriction on the other EOS policies.
 * For mass fractions X_i, Ye = sum_i X_i*Z_i/A_i,
 * Abar = 1/sum_i X_i/A_i, and Zbar = Abar*Ye.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "../../core/ArchPortability.h"

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

struct SpeciesHostView;
struct SpeciesPODView;

template <class SpeciesAccessor>
ARCH_INLINE double species_calc_Ye(const SpeciesAccessor &species, const double *Xi)
{
    double Ye = 0.0;
    for (int k = 0; k < species.size(); ++k)
        Ye += (species.get_Z(k) / species.get_A(k)) * Xi[k];
    return Ye;
}

template <class SpeciesAccessor>
ARCH_INLINE double species_calc_Abar(const SpeciesAccessor &species, const double *Xi)
{
    double sum_X_over_A = 0.0;
    for (int k = 0; k < species.size(); ++k)
        sum_X_over_A += Xi[k] / species.get_A(k);
    return (sum_X_over_A > 1e-16) ? (1.0 / sum_X_over_A) : 1.0;
}

template <class SpeciesAccessor>
ARCH_INLINE double species_calc_Zbar(const SpeciesAccessor &species, const double *Xi)
{
    return species_calc_Abar(species, Xi) * species_calc_Ye(species, Xi);
}

/**
 * @brief Central registry for all species involved in the simulation.
 * Responsibilities:
 * 1. Registers new species dynamically.
 * 2. Assigns a unique integer ID (0, 1, 2...) to each species based on registration order.
 * 3. Provides efficient O(1) access to properties via that ID.
 */
struct SpeciesManager
{
    /// Internal storage of species properties, indexed by ID.
    std::vector<GasProperty> species_list;

    /**
     * @brief Registers a new species into the simulation.
     * @param name Name of the species (for logging/output).
     * @param A The mass number.
     * @param Z The atomic number.
     * @param gamma The heat capacity ratio (Cp/Cv).
     * @param Cv The heat capacity at constant volume.
     * @return int The unique ID assigned to this species (used for array indexing).
     */
    int add_species(std::string name, double A, double Z, double gamma, double Cv)
    {
        species_list.push_back({name, A, Z, gamma, Cv});
        // The index of the newly added element is size - 1
        return species_list.size() - 1;
    }

    EOS_INLINE double get_A(int id) const { return species_list[id].A; }
    EOS_INLINE double get_Z(int id) const { return species_list[id].Z; }
    EOS_INLINE double get_gamma_ref(int id) const { return species_list[id].gamma_ref; }
    EOS_INLINE double get_Cv_ref(int id) const { return species_list[id].Cv_ref; }
    int count() const { return species_list.size(); }

    SpeciesHostView get_host_view() const;

    /**
     * @brief Compute electron fraction Ye.
     * Ye = Sum( (Z_i / A_i) * X_i )
     */
    EOS_INLINE double calc_Ye(const double *Xi) const;

    /**
     * @brief Compute mean atomic mass \bar{A}.
     */
    EOS_INLINE double calc_Abar(const double *Xi) const;

    /**
     * @brief Compute mean atomic number \bar{Z}.
     */
    EOS_INLINE double calc_Zbar(const double *Xi) const;
    /**
     * @brief Retrieves the name string for I/O purposes.
     */
    std::string get_name(int id) const { return species_list[id].name; }

    /**
     * @brief Retrieves the integer ID of a species by its string name.
     * @param target_name The name to search for (e.g., "c12").
     * @return int The ID of the species, or -1 if not found.
     */
    int GetSpeciesID(const std::string &target_name) const
    {
        std::string target = target_name;
        std::transform(target.begin(), target.end(), target.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (int i = 0; i < species_list.size(); ++i)
        {
            std::string candidate = species_list[i].name;
            std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (candidate == target) return i;
        }
        return -1;
    }
};

/** Host-only borrowed accessor over the authoritative GasProperty array. */
struct SpeciesHostView
{
    const GasProperty *host_data = nullptr;
    int count = 0;
    std::size_t extent = 0;
    const SpeciesManager *host_owner = nullptr;

    ARCH_INLINE int size() const { return count; }
    ARCH_INLINE double get_A(int id) const { return host_data[id].A; }
    ARCH_INLINE double get_Z(int id) const { return host_data[id].Z; }
    ARCH_INLINE double get_gamma_ref(int id) const { return host_data[id].gamma_ref; }
    ARCH_INLINE double get_Cv_ref(int id) const { return host_data[id].Cv_ref; }
    ARCH_INLINE double calc_Ye(const double *Xi) const { return species_calc_Ye(*this, Xi); }
    ARCH_INLINE double calc_Abar(const double *Xi) const { return species_calc_Abar(*this, Xi); }
    ARCH_INLINE double calc_Zbar(const double *Xi) const { return species_calc_Zbar(*this, Xi); }
};

/** Non-owning structure-of-arrays species metadata for device leaves. */
struct SpeciesPODView
{
    const double *A = nullptr;
    const double *Z = nullptr;
    const double *gamma = nullptr;
    const double *Cv = nullptr;
    int count = 0;

    ARCH_INLINE int size() const { return count; }
    ARCH_INLINE double get_A(int id) const { return A[id]; }
    ARCH_INLINE double get_Z(int id) const { return Z[id]; }
    ARCH_INLINE double get_gamma_ref(int id) const { return gamma[id]; }
    ARCH_INLINE double get_Cv_ref(int id) const { return Cv[id]; }
    ARCH_INLINE double calc_Ye(const double *Xi) const { return species_calc_Ye(*this, Xi); }
    ARCH_INLINE double calc_Abar(const double *Xi) const { return species_calc_Abar(*this, Xi); }
    ARCH_INLINE double calc_Zbar(const double *Xi) const { return species_calc_Zbar(*this, Xi); }
};

inline SpeciesHostView SpeciesManager::get_host_view() const
{
    return {species_list.empty() ? nullptr : species_list.data(), count(),
            species_list.size(), this};
}

inline double SpeciesManager::calc_Ye(const double *Xi) const
{
    return species_calc_Ye(get_host_view(), Xi);
}

inline double SpeciesManager::calc_Abar(const double *Xi) const
{
    return species_calc_Abar(get_host_view(), Xi);
}

inline double SpeciesManager::calc_Zbar(const double *Xi) const
{
    return species_calc_Zbar(get_host_view(), Xi);
}
