/**
 * @file RefinementIndicatorMath.h
 * @brief Shared field sampling and dimensionless AMR refinement indicators.
 *
 * Host configuration selects the fields. Host and device executors then use
 * these scalar leaves on the same logical stencils and take the maximum
 * error over selected fields and active axes. Thresholding returns a refine,
 * keep, or derefine flag; hierarchy mutation remains a separate Host step.
 */
#pragma once

#include "core/ArchPortability.h"
#include "data/FluidState.h"
#include "data/GlobalDefs.h"
#include "physics/diagnostics/VelocityDiagnostics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace amr::indicator {

enum class Field { Density, Pressure, Temperature, VelocityX, VelocityY,
                   VelocityZ, Energy, Vorticity, Divergence, Entropy,
                   NuclearEnergyRate, Species };
struct Selection { Field field; int species = -1; };

// Selection and threshold policy are independent of the storage/executor.
inline std::vector<Selection> make_selection(
    const AmrConfig& config, int dimension, std::span<const int> species)
{
    if (config.refine_on_jeans)
        throw std::runtime_error("AMR JENS requires a self-gravity potential solver; self gravity is not implemented in this build.");
    if (config.refine_on_vely && dimension < 2)
        throw std::invalid_argument("VELY AMR indicator requires at least two spatial dimensions.");
    if (config.refine_on_velz && dimension < 3)
        throw std::invalid_argument("VELZ AMR indicator requires three spatial dimensions.");
    std::vector<Selection> result;
    const auto add = [&](bool enabled, Field field) {
        if (enabled) result.push_back({field});
    };
    add(config.refine_on_rho, Field::Density);
    add(config.refine_on_p, Field::Pressure);
    add(config.refine_on_temp, Field::Temperature);
    add(config.refine_on_eng, Field::Energy);
    add(config.refine_on_velx, Field::VelocityX);
    add(config.refine_on_vely, Field::VelocityY);
    add(config.refine_on_velz, Field::VelocityZ);
    add(config.refine_on_entropy, Field::Entropy);
    add(config.refine_on_enuc, Field::NuclearEnergyRate);
    if (config.refine_on_species)
        for (int index : species) result.push_back({Field::Species, index});
    add(config.refine_on_vorticity, Field::Vorticity);
    add(config.refine_on_div_v, Field::Divergence);
    return result;
}

ARCH_INLINE int refinement_flag(double error, int level, int minimum_level,
                                int maximum_level, double refine, double derefine)
{
    return error > refine && level < maximum_level ? 1
        : (error < derefine && level > minimum_level ? -1 : 0);
}

/**
 * Three-point curvature indicator for q at offsets -1, 0, +1. The numerator
 * is max(0, |q+ - 2q0 + q-| - uncertainty); the denominator combines the
 * two first differences with epsilon*(|q+| + 2|q0| + |q-|). Their common
 * field units cancel. A positive floor avoids division by zero, and the
 * result is capped at one. Nonfinite stencil data remain an invalid result.
 */
ARCH_INLINE double loehner_error(double qm, double q0, double qp,
                                 double curvature_uncertainty = 0.0)
{
    if (!std::isfinite(qm) || !std::isfinite(q0) || !std::isfinite(qp)
        || !std::isfinite(curvature_uncertainty) || curvature_uncertainty < 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    // The existing estimator coefficient is a mathematical policy, not a
    // backend tuning parameter. Both executors use this one definition.
    constexpr double epsilon = 1.0e-2;
    const double numerator = std::max(0.0,
        std::abs(qp - 2.0 * q0 + qm) - curvature_uncertainty);
    const double denominator = std::abs(qp - q0) + std::abs(q0 - qm)
        + epsilon * (std::abs(qp) + 2.0 * std::abs(q0) + std::abs(qm))
        + std::numeric_limits<double>::min();
    return std::min(1.0, numerator / denominator);
}

// Stored composition is a partition of UNITY (mass fractions), including a
// closure residual. Its absolute scale is one, not rho or a single abundance.
// gamma_(N+2) models N-term composition accumulation, closure and scaling.
// This is a local arithmetic-resolution policy, not a truncation-error bound.
// Apply equally to every species and backend; do not clamp stored fields.
ARCH_INLINE double composition_roundoff(int species_count)
{
    if (species_count <= 0) return 0.0;
    const double unit_roundoff = std::numeric_limits<double>::epsilon() / 2.0;
    const double product = (static_cast<double>(species_count) + 2.0) * unit_roundoff;
    return product / (1.0 - product);
}

struct Thermodynamics {
    double pressure = 0.0;
    double temperature = 0.0;
    double gamma1 = std::numeric_limits<double>::quiet_NaN();
};

template<class Eos>
ARCH_INLINE Thermodynamics thermodynamics(
    const FluidVector& state, const double* composition, const Eos& eos,
    bool pressure, bool temperature, bool gamma1)
{
    Thermodynamics value;
    if (pressure || gamma1) value.pressure = eos.get_pressure(state, composition);
    if (temperature && state.rho > 0.0) {
        const double kinetic = 0.5 * (state.mom_u * state.mom_u
            + state.mom_v * state.mom_v + state.mom_w * state.mom_w) / state.rho;
        value.temperature = eos.get_temperature(
            state.rho, (state.eng - kinetic) / state.rho, composition);
    }
    if (gamma1 && state.rho > 0.0 && value.pressure > 0.0) {
        const double sound = eos.get_sound_speed(state, value.pressure, composition);
        if (std::isfinite(sound) && sound > 0.0)
            value.gamma1 = state.rho * sound * sound / value.pressure;
    }
    return value;
}

struct VelocityView {
    const double* momentum = nullptr;
    const double* density = nullptr;
    double density_floor = 0.0;

    ARCH_INLINE double operator[](int cell) const {
        return density[cell] > density_floor ? momentum[cell] / density[cell] : 0.0;
    }
};

struct StateView {
    const double* density = nullptr;
    const double* momentum[3]{};
    const double* energy = nullptr;
    const double* nuclear_energy = nullptr;
    const double* species = nullptr;
    const double* pressure = nullptr;
    const double* temperature = nullptr;
    const double* gamma1 = nullptr;
    int cells = 0;
    int is = 0, ie = 0, js = 0, je = 0, ks = 0, ke = 0;
    double density_floor = 0.0;
    // Runtime metadata, not a compile-time species ceiling.
    int species_count = 0;

    ARCH_INLINE bool valid_entropy(int cell) const {
        return std::isfinite(pressure[cell]) && pressure[cell] > 0.0
            && std::isfinite(gamma1[cell]) && gamma1[cell] > 0.0;
    }

    template<class Geometry>
    ARCH_INLINE double value(Selection selection, const Geometry& grid,
                             int i, int j, int k) const
    {
        const int cell = grid.GetIndex(i, j, k);
        switch (selection.field) {
        case Field::Density: return density[cell];
        case Field::Pressure: return pressure[cell];
        case Field::Temperature: return temperature[cell];
        case Field::Energy: return energy[cell];
        case Field::NuclearEnergyRate: return nuclear_energy[cell];
        case Field::Species: return species[selection.species * cells + cell];
        case Field::VelocityX: return VelocityView{momentum[0], density, density_floor}[cell];
        case Field::VelocityY: return VelocityView{momentum[1], density, density_floor}[cell];
        case Field::VelocityZ: return VelocityView{momentum[2], density, density_floor}[cell];
        case Field::Entropy: {
            // P/rho^Gamma1 is an entropy proxy for refinement, not the general
            // EOS thermodynamic entropy. Invalid ghost closure uses the nearest
            // interior sample; invalid interior closure remains an error.
            const bool interior = i >= is && i < ie && j >= js && j < je
                && k >= ks && k < ke;
            if (interior && !valid_entropy(cell))
                return std::numeric_limits<double>::quiet_NaN();
            const int source = valid_entropy(cell) ? cell : grid.GetIndex(
                std::clamp(i, is, ie - 1), std::clamp(j, js, je - 1),
                std::clamp(k, ks, ke - 1));
            if (!valid_entropy(source)) return std::numeric_limits<double>::quiet_NaN();
            return pressure[source] / std::pow(
                std::max(density[source], density_floor), gamma1[source]);
        }
        case Field::Vorticity:
        case Field::Divergence: {
            const auto result = VelocityDiagnostics::evaluate(grid,
                VelocityView{momentum[0], density, density_floor},
                VelocityView{momentum[1], density, density_floor},
                VelocityView{momentum[2], density, density_floor}, i, j, k);
            return selection.field == Field::Vorticity ? result.vorticity : result.divergence;
        }
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
};

template<class Geometry>
ARCH_INLINE double cell_error(const StateView& state, const Geometry& grid,
    const Selection* selection, int count, int i, int j, int k)
{
    double maximum = 0.0;
    for (int field = 0; field < count; ++field) {
        const double center = state.value(selection[field], grid, i, j, k);
        for (int axis = 0; axis < grid.dim; ++axis) {
            const double low = state.value(selection[field], grid,
                i - (axis == 0), j - (axis == 1), k - (axis == 2));
            const double high = state.value(selection[field], grid,
                i + (axis == 0), j + (axis == 1), k + (axis == 2));
            double uncertainty = 0.0;
            if (selection[field].field == Field::Species) {
                // |1| + |-2| + |1| is the absolute stencil-weight sum.
                uncertainty = 4.0 * composition_roundoff(state.species_count);
            }
            const double error = loehner_error(low, center, high, uncertainty);
            if (!std::isfinite(error)) return error;
            maximum = std::max(maximum, error);
        }
    }
    return maximum;
}

} // namespace amr::indicator
