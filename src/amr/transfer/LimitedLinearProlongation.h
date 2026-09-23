/**
 * @file LimitedLinearProlongation.h
 * @brief Shared conservative scalar reconstruction for 2:1 AMR prolongation.
 * Workflow:
 * 1. Receive a committed source topology and staged destination.
 * 2. Build or apply conservative restriction and limited prolongation.
 * 3. Validate transferred state before publishing new block ownership.
 */

#pragma once

#include "core/ArchPortability.h"

#include <cstddef>

namespace amr::prolongation_math {

ARCH_HOST_DEVICE inline double minimum(double left, double right)
{
    return left < right ? left : right;
}

ARCH_HOST_DEVICE inline double maximum(double left, double right)
{
    return left > right ? left : right;
}

ARCH_HOST_DEVICE inline double absolute(double value)
{
    return value < 0.0 ? -value : value;
}

ARCH_HOST_DEVICE inline bool finite_number(double value)
{
    constexpr double largest = 0x1.fffffffffffffp+1023;
    return value == value && value >= -largest && value <= largest;
}

ARCH_HOST_DEVICE inline double minmod(double left, double right)
{
    if (left * right <= 0.0) return 0.0;
    return left > 0.0
        ? minimum(left, right) : maximum(left, right);
}

/**
 * Reconstruct one fine-cell value at offsets +/-1/4 of a coarse cell.
 * One common multidimensional limiter keeps all 2^dim children inside the
 * coarse stencil bounds.  Because the limited slopes and limiter are shared
 * by the symmetric children, their arithmetic mean is the parent in exact arithmetic.
 */
ARCH_HOST_DEVICE inline double limited_linear_value(
    double center, const double lower[3], const double upper[3],
    const double position[3], int dimension)
{
    double slopes[3]{0.0, 0.0, 0.0};
    double stencil_min = center;
    double stencil_max = center;
    double maximum_excursion = 0.0;
    for (int axis = 0; axis < dimension; ++axis) {
        stencil_min = minimum(stencil_min, minimum(lower[axis], upper[axis]));
        stencil_max = maximum(stencil_max, maximum(lower[axis], upper[axis]));
        slopes[axis] = minmod(
            center - lower[axis], upper[axis] - center);
        maximum_excursion += 0.25 * absolute(slopes[axis]);
    }
    double theta = 1.0;
    if (maximum_excursion > 0.0) {
        theta = minimum(
            theta, (stencil_max - center) / maximum_excursion);
        theta = minimum(
            theta, (center - stencil_min) / maximum_excursion);
        theta = maximum(0.0, theta);
    }
    double result = center;
    for (int axis = 0; axis < dimension; ++axis)
        result += theta * position[axis] * slopes[axis];
    return result;
}

/**
 * Non-owning mathematical stencil, independent of Host/device allocation.
 * Backends bind their SoA samples and lowered cell indices; reconstruction,
 * admissibility, closure, and the family-wide fallback live only here.
 */
struct CompositionStencilView {
    const double* density = nullptr;
    const double* mass_fractions = nullptr;
    std::size_t species_stride = 0;
    int center = 0;
    const int* slope_cells = nullptr;
    int dimension = 0;
    int species_count = 0;
};

enum class CompositionFamily : int {
    Linear = 0,
    Constant = 1,
    InvalidDensity = 2,
};

ARCH_HOST_DEVICE inline double reconstruct_field(
    const CompositionStencilView& stencil, const double* values,
    const double position[3])
{
    double lower[3]{};
    double upper[3]{};
    for (int axis = 0; axis < stencil.dimension; ++axis) {
        lower[axis] = values[stencil.slope_cells[2 * axis]];
        upper[axis] = values[stencil.slope_cells[2 * axis + 1]];
    }
    return limited_linear_value(
        values[stencil.center], lower, upper, position, stencil.dimension);
}

ARCH_HOST_DEVICE inline double reconstruct_species_density(
    const CompositionStencilView& stencil, int species,
    const double position[3])
{
    const double* fractions = stencil.mass_fractions
        + static_cast<std::size_t>(species) * stencil.species_stride;
    double lower[3]{};
    double upper[3]{};
    for (int axis = 0; axis < stencil.dimension; ++axis) {
        const int lower_cell = stencil.slope_cells[2 * axis];
        const int upper_cell = stencil.slope_cells[2 * axis + 1];
        lower[axis] = stencil.density[lower_cell] * fractions[lower_cell];
        upper[axis] = stencil.density[upper_cell] * fractions[upper_cell];
    }
    return limited_linear_value(
        stencil.density[stencil.center] * fractions[stencil.center],
        lower, upper, position, stencil.dimension);
}

// One parent-wide choice for every sibling, with stable first-index tie
// breaking. Closing a trace/zero last species by rho - sum(other rhoX)
// invents O(epsilon) abundance; its fine-face flux can then drain a coarse
// cell whose true abundance is O(1e-20). Put closure roundoff in the dominant
// species, as the shared regrid transfer already does, not in a network-order
// dependent trace species. This does not clip abundances or relax a budget.
ARCH_HOST_DEVICE inline int composition_closure_species(
    const CompositionStencilView& stencil)
{
    int selected = -1;
    double largest = -1.0;
    for (int species = 0; species < stencil.species_count; ++species) {
        const double value = stencil.mass_fractions[
            static_cast<std::size_t>(species) * stencil.species_stride + stencil.center];
        if (value > largest) {
            largest = value;
            selected = species;
        }
    }
    return selected;
}

// Use identical sum-then-subtract operations for validation and output.
ARCH_HOST_DEVICE inline double reconstructed_closure(
    const CompositionStencilView& stencil, double density,
    const double position[3], int closure_species = -1)
{
    if (closure_species < 0) closure_species = composition_closure_species(stencil);
    double partial = 0.0;
    for (int species = 0; species < stencil.species_count; ++species)
        if (species != closure_species)
            partial += reconstruct_species_density(stencil, species, position);
    return density - partial;
}

ARCH_HOST_DEVICE inline CompositionFamily classify_composition_family(
    const CompositionStencilView& stencil, int closure_species = -1)
{
    if (closure_species < 0) closure_species = composition_closure_species(stencil);
    bool linear = true;
    for (int sibling = 0; sibling < (1 << stencil.dimension); ++sibling) {
        double position[3]{};
        for (int axis = 0; axis < stencil.dimension; ++axis)
            position[axis] = (sibling & (1 << axis)) != 0 ? 0.25 : -0.25;
        const double density = reconstruct_field(
            stencil, stencil.density, position);
        if (!finite_number(density) || density <= 0.0)
            return CompositionFamily::InvalidDensity;
        for (int species = 0; species < stencil.species_count; ++species) {
            if (species == closure_species) continue;
            const double candidate = reconstruct_species_density(
                stencil, species, position);
            if (!finite_number(candidate) || candidate < 0.0) linear = false;
        }
        const double closure = reconstructed_closure(stencil, density, position, closure_species);
        if (!finite_number(closure) || closure < 0.0) linear = false;
    }
    // All siblings must use one decision, including siblings outside a
    // particular ghost face.  Per-child rejection would lose conservation.
    return linear ? CompositionFamily::Linear : CompositionFamily::Constant;
}

// Precondition: family != InvalidDensity (checked by each backend before
// publishing any part of the exchange).  The constant family preserves the
// parent's X exactly and hence its rhoX average with symmetric density slopes.
ARCH_HOST_DEVICE inline double reconstruct_mass_fraction(
    const CompositionStencilView& stencil, CompositionFamily family,
    double density, int species, const double position[3], int closure_species = -1)
{
    if (family == CompositionFamily::Constant)
        return stencil.mass_fractions[
            static_cast<std::size_t>(species) * stencil.species_stride
            + stencil.center];
    if (closure_species < 0) closure_species = composition_closure_species(stencil);
    const double species_density = species != closure_species
        ? reconstruct_species_density(stencil, species, position)
        : reconstructed_closure(stencil, density, position, closure_species);
    return species_density / density;
}

inline const char* invalid_prolongation_density_message()
{
    return "coarse-fine prolongation produced invalid density";
}

} // namespace amr::prolongation_math
