/**
 * @file ProblemGenerator.h
 * @brief Defines the abstract interface for all simulation problems.
 * * This class enforces a standard contract that any specific problem case
 * * (e.g., Shock Tube, Blast Wave) must fulfill.
 * * It decouples the core solver engine from the specific problem setup logic.
 */

#pragma once

#include "../src/grid/Grid.h"

#include "../src/data/FluidState.h"
#include "../src/data/GlobalDefs.h"

#include "../src/physics/species/Species.h"
#include "../src/physics/eos/IdealGas.h"

class ProblemGenerator
{
public:
    virtual ~ProblemGenerator() = default;

    // [删除] virtual SimConfig GetConfig() = 0;
    // 理由：现在 Config 由 main.cpp 加载，不再由 Problem 类负责产生。

    /**
     * @brief Global Setup Routine.
     * * Replaces the old "SetupSpecies".
     * * Responsibilities:
     * * 1. Read problem-specific parameters from 'config' (e.g., shock_position).
     * * 2. Register necessary species into 'specs'.
     * *
     * * @param config Input/Output: The simulation configuration.
     * * (Can be read for params, or modified if enforcing BCs).
     * * @param specs  Output: The species manager to populate.
     */
    virtual void Setup(SimConfig &config, SpeciesManager &specs) = 0;

    /**
     * @brief Populates the mesh with initial physical conditions.
     * * Maps spatial coordinates (x,y,z) to primitive variables.
     * *
     * * Note: To ensure high performance, implementation classes (like GenericProblemGenerator)
     * * usually do NOT pass 'config' here. Instead, they use static/member variables
     * * cached during the Setup() phase.
     */
    virtual void InitializeData(FluidState &state, const Grid &grid, const IdealGas &eos) = 0;

    /**
     * @brief Returns the name of the problem for logging.
     */
    virtual std::string GetSolverName() = 0;
};