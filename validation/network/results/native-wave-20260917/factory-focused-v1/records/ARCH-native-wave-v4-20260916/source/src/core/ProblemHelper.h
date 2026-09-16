/**
 * @file ProblemHelper.h
 * @brief Stable case-facing helpers for EOS-backed initialization and networks.
 *
 * Workflow:
 * 1. Re-export this public helper surface through UserInterface.h.
 * 2. Let case Setup() obtain network compositions or an EOS pressure without EOS headers.
 * 3. Keep hierarchy-population mechanics private to GenericProblem and ProblemHelper::detail.
 */

#pragma once

#include <functional>
#include <vector>

struct PointCoords;
struct PrimitiveData;
struct SimConfig;
struct SpeciesManager;
struct ProblemInitializationContext;

namespace amr {
class AMRControl;
}

namespace ProblemHelper {

/** @brief Thermodynamic point reached along a fixed-composition isentrope. */
struct IsentropicState {
    double rho = 0.0;
    double temperature = 0.0;
    double pressure = 0.0;
    double sound_speed = 0.0;
};

/**
 * @brief Registers a built-in/generated network and its initial composition.
 * With network_name=none and burning disabled, leaves gas definitions to the
 * problem. Unknown networks and burning without a network are rejected.
 */
void SetupNetworkAndFractions(SimConfig& config, SpeciesManager& specs,
                              std::vector<double>& default_X);

/**
 * @brief Returns pressure from (rho, T, X) through the active configured EOS.
 *
 * Intended for Setup() precomputation of an initial state. The EOS dispatcher is
 * deliberately not exposed to cases and this helper must not be called per cell
 * inside Init(), where repeated runtime dispatch would be unnecessarily expensive.
 */
double GetPressureFromRhoT(const SimConfig& config, const SpeciesManager& specs,
                           double rho, double temperature, const double* mass_fractions);

/**
 * @brief Returns the physical width of one active root-level cell.
 *
 * logical_axis is one-based (1=x1, 2=x2, 3=x3).  The calculation uses the
 * configured root-block count and ARCH's active cells per block; guard cells
 * are storage only and do not contribute to the physical domain width.
 * Cases should use this helper instead of including internal AMR headers.
 */
double GetRootCellWidth(const SimConfig& config, int logical_axis);

/**
 * @brief Moves a reference (rho,T,X) state to a requested pressure factor at
 * fixed composition and entropy.
 *
 * The active EOS is selected at runtime, then the common EOS-policy isentrope
 * implementation is used.  Cases obtain this helper through UserInterface.h
 * and must not include EOS policy or dispatch headers.  The path is integrated using
 * d ln(T) / d ln(rho) = (dP/dT)_rho / (rho c_v), then a Newton iteration in
 * ln(rho) matches the target pressure.  This avoids an ideal-gas assumption
 * when constructing weak acoustic-compression surrogate states.  The solve is
 * local and rejects a result farther than 0.25 in ln(rho) from the reference.
 */
IsentropicState GetIsentropicStateAtPressureFactor(
    const SimConfig& config, const SpeciesManager& specs,
    double reference_rho, double reference_temperature,
    const double* mass_fractions, double pressure_factor);

namespace detail {

/** @brief Internal bridge from a case Init callback to AMR state initialization. */
void PopulateState(amr::AMRControl& amr_ctrl, const SimConfig& config,
                   const SpeciesManager& specs,
                   ProblemInitializationContext context,
                   std::function<void(const PointCoords&, PrimitiveData&)> init_callback);

} // namespace detail
} // namespace ProblemHelper
