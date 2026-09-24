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

#include <stdexcept>

#include "amr/AMRControl.h"
#include "data/GlobalDefs.h"
#include "data/UserTypes.h"
#include "grid/Grid.h"
#include "physics/eos/IdealGas.h"
#include "physics/species/Species.h"
#include "driver/dispatch/capability/ResolvedExecutionPlan.h"

struct ProblemInitializationContext
{
    arch::dispatch::EosId eos = arch::dispatch::EosId::Ideal;
};

class ProblemGenerator
{
public:
    virtual ~ProblemGenerator() = default;

    // Explicit inspection boundary. Production InitializeData has no observer
    // or extra per-cell branch. Restore the caller's observer even on failure.
    void InspectSetup(SimConfig& config, SpeciesManager& species,
                      const std::shared_ptr<arch::preview::ParameterReadTrace>& reads) {
        const auto previous = config.parameter_reads;
        config.parameter_reads = reads;
        try { Setup(config, species); }
        catch (...) { config.parameter_reads = previous; throw; }
        config.parameter_reads = previous;
    }
    void InspectInitialPrimitive(const PointCoords& point, PrimitiveData& data,
                                 arch::preview::InitializationObserver& observer) const {
        SampleInitialPrimitive(point, data);
        observer.initial_primitive(point, data);
    }

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

    virtual std::vector<arch::preview::AxisPosition> PreviewPositions(const SimConfig &) const
    {
        return {};
    }

    // Optional pointwise initialization seam. Call Setup first and supply a
    // zeroed PrimitiveData with the registered composition extent. Existing
    // mesh-only generators remain valid and explicitly reject point sampling.
    virtual void SampleInitialPrimitive(const PointCoords &, PrimitiveData &) const
    {
        throw std::logic_error("This problem does not support pointwise initialization");
    }

    /**
     * @brief Populates the mesh with initial physical conditions.
     * Maps spatial coordinates (x,y,z) to primitive variables.
     *
     * Case implementations may cache setup data required by this method.
     */
    virtual void InitializeData(amr::AMRControl &amr_ctrl,
                                const SimConfig &config,
                                const SpeciesManager &specs,
                                ProblemInitializationContext context) = 0;

};
