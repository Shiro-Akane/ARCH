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

namespace amr {
class AMRControl;
}

namespace ProblemHelper {

/**
 * @brief Registers the selected built-in network and initializes its reference composition.
 * @throws std::runtime_error when network_name is not a maintained built-in network.
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

namespace detail {

/** @brief Internal bridge from a case Init callback to AMR state initialization. */
void PopulateState(amr::AMRControl& amr_ctrl, const SimConfig& config,
                   const SpeciesManager& specs,
                   std::function<void(const PointCoords&, PrimitiveData&)> init_callback);

} // namespace detail
} // namespace ProblemHelper