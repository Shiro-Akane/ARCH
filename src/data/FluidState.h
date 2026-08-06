/**
 * @file FluidState.h
 * @brief Definitions for local fluid state vectors and global data containers.
 */

#pragma once

#include <vector>

#include "../grid/Grid.h"

/**
 * @brief Represents the conserved variables at a single point in 1D space.
 * * Corresponds to the state vector \f$ U = (\rho, \rho u, E)^T \f$ in Euler equations.
 */

struct FluidVector
{
    double rho;   ///< Mass density (\f$ \rho \f$).
    double mom_u; ///< Momentum density (\f$ \rho u \f$).
    double mom_v; ///< Momentum density (\f$ \rho v \f$).
    double mom_w; ///< Momentum density (\f$ \rho w \f$).
    double eng;   ///< Total energy density (\f$ E = \rho e + 0.5 \rho u^2 \f$).

    FluidVector() : rho(0), mom_u(0), mom_v(0), mom_w(0), eng(0) {}
    FluidVector(double r, double mx, double my, double mz, double e) : rho(r), mom_u(mx), mom_v(my), mom_w(mz), eng(e) {}

    /// Overload operator "+" for vector addition.
    FluidVector operator+(const FluidVector &other) const
    {
        return {rho + other.rho, mom_u + other.mom_u, mom_v + other.mom_v, mom_w + other.mom_w, eng + other.eng};
    }

    /// Overload operator "-" for vector subtraction.
    FluidVector operator-(const FluidVector &other) const
    {
        return {rho - other.rho, mom_u - other.mom_u, mom_v - other.mom_v, mom_w - other.mom_w, eng - other.eng};
    }

    /// Overload operator "*" for scalar multiplication.
    FluidVector operator*(double s) const
    {
        return {rho * s, mom_u * s, mom_v * s, mom_w * s, eng * s};
    }

    /// Overload operator "/" for scalar division.
    FluidVector operator/(double s) const
    {
        return {rho / s, mom_u / s, mom_v / s, mom_w / s, eng / s};
    }
};

/// Scalar multiplication (commutative): s * v
inline FluidVector operator*(double s, const FluidVector &v)
{
    return v * s;
}

// =========================================================

/**
 * @struct FluidState
 * @brief Global container for fluid variables using Structure-of-Arrays (SoA) layout.
 * * Stores the entire computational domain's data. SoA layout is preferred
 * for better vectorization and cache locality during independent field updates.
 */
struct FluidState
{
    // Conserved variables (SoA layout)
    std::vector<double> rho;   ///< Global array for density.
    std::vector<double> mom_u; ///< Global array for x-momentum density.
    std::vector<double> mom_v; ///< Global array for y-momentum density.
    std::vector<double> mom_w; ///< Global array for z-momentum density.
    std::vector<double> eng;   ///< Global array for total energy density.

    // Species data
    std::vector<double> mass_fractions; ///< Flattened array for species mass fractions.
    int n_species_ = 0;                 ///< Number of chemical species.
    int total_size_ = 0;                ///< Total number of grid cells (including ghosts).

    FluidState() = default;
    FluidState(const Grid &grid, int n_species)
    {
        Resize(grid, n_species);
    }

    int GetNumSpecies() const
    {
        return n_species_;
    }
    /**
     * @brief Allocates memory for the state arrays based on grid size.
     * @param grid The computational grid object.
     * @param n_species Number of species to track.
     */
    void Resize(const Grid &grid, int n_species)
    {
        total_size_ = grid.GetTotalSize();
        n_species_ = n_species;

        rho.assign(total_size_, 0.0);
        mom_u.assign(total_size_, 0.0);
        mom_v.assign(total_size_, 0.0);
        mom_w.assign(total_size_, 0.0);
        eng.assign(total_size_, 0.0);

        if (n_species_ > 0 && total_size_ > 0)
        {
            // Flattened 2D array: [Species_0... | Species_1... | ... ]
            mass_fractions.assign(n_species_ * total_size_, 0.0);
        }
    }

    /**
     * @brief Access reference to mass fraction X_k of species k at cell i.
     * Memory layout: Species-major order (blocks of grid size).
     */
    double &X(int k, int i)
    {
        // index：k * stride + i
        return mass_fractions[k * total_size_ + i];
    }

    /// Read-only access to mass fraction X_k of species k at cell i.
    double X(int k, int i) const
    {
        return mass_fractions[k * total_size_ + i];
    }

    /**
     * @brief Gathers all species fractions at cell i into a local buffer.
     * Useful for EOS calculations or reaction networks at a single point.
     * @param i Grid cell index.
     * @param buffer Pointer to an array of size n_species_.
     */
    void get_species_to_buffer(int i, double *buffer) const
    {
        for (int k = 0; k < n_species_; ++k)
        {
            buffer[k] = mass_fractions[k * total_size_ + i];
        }
    }

    /// Scatters species fractions from a local buffer back to the global state at cell i.
    void set_species_from_buffer(int i, const double *buffer)
    {
        for (int k = 0; k < n_species_; ++k)
        {
            mass_fractions[k * total_size_ + i] = buffer[k];
        }
    }

    /// Constructs a local FluidVector3 object from the global arrays (SoA to AoS).
    FluidVector get(int i) const
    {
        return FluidVector(rho[i], mom_u[i], mom_v[i], mom_w[i], eng[i]);
    }

    /// Writes a local FluidVector3 object back to the global arrays (AoS to SoA).
    void set(int i, const FluidVector &val)
    {
        rho[i] = val.rho;
        mom_u[i] = val.mom_u;
        mom_v[i] = val.mom_v;
        mom_w[i] = val.mom_w;
        eng[i] = val.eng;
    }

    /// Atomically adds a local increment to the global state (useful for flux updates).
    void add(int i, const FluidVector &val)
    {
        rho[i] += val.rho;
        mom_u[i] += val.mom_u;
        mom_v[i] += val.mom_v;
        mom_w[i] += val.mom_w;
        eng[i] += val.eng;
    }
};
