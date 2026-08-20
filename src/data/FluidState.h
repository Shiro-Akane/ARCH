/**
 * @file FluidState.h
 * @brief Definitions for local fluid state vectors and global data containers.
 * Refactored for Block-Structured AMR (Static Memory Layout).
 */

/**
 * Workflow:
 * 1. Allocate or address state through the active-dimension layout contract.
 * 2. Read and write conservative variables and species with one shared indexing rule.
 * 3. Expose the result to numerical operators without hidden storage conversions.
 */

#pragma once

#include <array>
#include <vector>

#include "../amr/AmrDefines.h"

/**
 * @brief Represents the conserved variables at a single point.
 */
struct FluidVector
{
    double rho;
    double mom_u;
    double mom_v;
    double mom_w;
    double eng;

    FluidVector() : rho(0), mom_u(0), mom_v(0), mom_w(0), eng(0) {}
    FluidVector(double r, double mx, double my, double mz, double e) : rho(r), mom_u(mx), mom_v(my), mom_w(mz), eng(e) {}

    FluidVector operator+(const FluidVector &other) const
    {
        return {rho + other.rho, mom_u + other.mom_u, mom_v + other.mom_v, mom_w + other.mom_w, eng + other.eng};
    }

    FluidVector operator-(const FluidVector &other) const
    {
        return {rho - other.rho, mom_u - other.mom_u, mom_v - other.mom_v, mom_w - other.mom_w, eng - other.eng};
    }

    FluidVector operator*(double s) const
    {
        return {rho * s, mom_u * s, mom_v * s, mom_w * s, eng * s};
    }

    FluidVector operator/(double s) const
    {
        return {rho / s, mom_u / s, mom_v / s, mom_w / s, eng / s};
    }
};

inline FluidVector operator*(double s, const FluidVector &v)
{
    return v * s;
}

/**
 * @struct FluidState
 * @brief Local block container for fluid variables using Structure-of-Arrays (SoA) layout.
 * Fixed static size for GPU Memory Pool compatibility.
 */
struct FluidState
{
    // Dynamically allocated for dimensional degradation
    std::vector<double> rho;
    std::vector<double> mom_u;
    std::vector<double> mom_v;
    std::vector<double> mom_w;
    std::vector<double> eng;
    // Specific nuclear energy source rate (erg g^-1 s^-1); diagnostic only.
    std::vector<double> enuc_rate;

    // Species mass fractions use a contiguous species-major array sized once per
    // block. Backend adapters must preserve this layout or provide an equivalent
    // device view without changing the numerical indexing contract.
    std::vector<double> mass_fractions;
    int n_species_ = 0;

    int block_total_size_ = 0;

    FluidState() = default;

    // Preallocate all vectors to dynamic degraded size
    void Preallocate(int size)
    {
        block_total_size_ = size;
        rho.assign(size, 0.0);
        mom_u.assign(size, 0.0);
        mom_v.assign(size, 0.0);
        mom_w.assign(size, 0.0);
        eng.assign(size, 0.0);
        enuc_rate.assign(size, 0.0);
    }

    // Reset to zero without reallocating
    void Reset()
    {
        std::fill(rho.begin(), rho.end(), 0.0);
        std::fill(mom_u.begin(), mom_u.end(), 0.0);
        std::fill(mom_v.begin(), mom_v.end(), 0.0);
        std::fill(mom_w.begin(), mom_w.end(), 0.0);
        std::fill(eng.begin(), eng.end(), 0.0);
        std::fill(enuc_rate.begin(), enuc_rate.end(), 0.0);
        std::fill(mass_fractions.begin(), mass_fractions.end(), 0.0);
    }

    // Explicit initialization for species
    void InitSpecies(int n_species)
    {
        n_species_ = n_species;
        if (n_species_ > 0 && block_total_size_ > 0)
        {
            mass_fractions.assign(n_species_ * block_total_size_, 0.0);
        }
    }

    int GetNumSpecies() const
    {
        return n_species_;
    }

    double &X(int k, int i)
    {
        return mass_fractions[k * block_total_size_ + i];
    }

    double X(int k, int i) const
    {
        return mass_fractions[k * block_total_size_ + i];
    }

    void get_species_to_buffer(int i, double *buffer) const
    {
        for (int k = 0; k < n_species_; ++k)
        {
            buffer[k] = mass_fractions[k * block_total_size_ + i];
        }
    }

    void set_species_from_buffer(int i, const double *buffer)
    {
        for (int k = 0; k < n_species_; ++k)
        {
            mass_fractions[k * block_total_size_ + i] = buffer[k];
        }
    }

    FluidVector get(int i) const
    {
        return FluidVector(rho[i], mom_u[i], mom_v[i], mom_w[i], eng[i]);
    }

    void set(int i, const FluidVector &val)
    {
        rho[i] = val.rho;
        mom_u[i] = val.mom_u;
        mom_v[i] = val.mom_v;
        mom_w[i] = val.mom_w;
        eng[i] = val.eng;
    }

    void add(int i, const FluidVector &val)
    {
        rho[i] += val.rho;
        mom_u[i] += val.mom_u;
        mom_v[i] += val.mom_v;
        mom_w[i] += val.mom_w;
        eng[i] += val.eng;
    }
};
