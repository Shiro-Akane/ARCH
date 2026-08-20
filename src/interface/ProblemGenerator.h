/**
 * @file ProblemGenerator.h
 * @brief Defines the abstract interface for all simulation problems.
 * This class enforces a standard contract that any specific problem case
 * (e.g., Shock Tube, Blast Wave) must fulfill.
 * It decouples the core solver engine from the specific problem setup logic.
 */

/**
 * Workflow:
 * 1. Receive the problem-specific setup request from the application boundary.
 * 2. Expose only the stable data and initialization contract needed by the driver.
 * 3. Keep problem registration independent of numerical implementation details.
 */

#pragma once

#include <string>

#include "../amr/AMRControl.h"
#include "../data/GlobalDefs.h"
#include "../grid/Grid.h"
#include "../physics/eos/IdealGas.h"
#include "../physics/species/Species.h"

class ProblemGenerator
{
public:
    virtual ~ProblemGenerator() = default;

    /**
     * @brief Global Setup Routine.
     * Responsibilities:
     * 1. Read problem-specific parameters from 'config' (e.g., shock_position).
     * 2. Register necessary species into 'specs'.
     *
     * @param config Input/Output: The simulation configuration.
     * (Can be read for params, or modified if enforcing BCs).
     * @param specs  Output: The species manager to populate.
     */
    virtual void Setup(SimConfig &config, SpeciesManager &specs) = 0;

    /**
     * @brief Populates the mesh with initial physical conditions.
     * Maps spatial coordinates (x,y,z) to primitive variables.
     *
     * Case implementations may cache setup data required by this method.
     */
    virtual void InitializeData(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs) = 0;

    /**
     * @brief Returns the name of the problem for logging.
     */
    virtual std::string GetSolverName() = 0;
};
