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
#include <string>
#include <utility>

#include "amr/AMRControl.h"
#include "data/GlobalDefs.h"
#include "core/config/ConfigValidation.h"
#include "core/config/PreparedConfiguration.h"
#include "data/UserTypes.h"
#include "grid/Grid.h"
#include "physics/eos/IdealGas.h"
#include "physics/species/Species.h"
#include "driver/dispatch/capability/ResolvedExecutionPlan.h"

struct ProblemInitializationContext
{
    arch::dispatch::EosId eos = arch::dispatch::EosId::Ideal;
    GridMetrics::GeometrySemantics geometry_semantics = GridMetrics::GeometrySemantics::Existing;
};

class ProblemRegistry;

class ProblemGenerator
{
    friend class ProblemRegistry;
    // Assigned only by the registry factory, never by config input or a caller.
    std::string registered_case_id_;
public:
    virtual ~ProblemGenerator() = default;

    /**
     * @brief Record the registry name and source identity after creation.
     * Called once by ProblemRegistry::Create; no Setup/Init signature changes.
     */
    void BindRegistration(std::string registered_name, std::string source_file,
                          std::string source_sha256)
    {
        registered_name_ = std::move(registered_name);
        source_file_ = std::move(source_file);
        source_sha256_ = std::move(source_sha256);
    }

    /** @brief Registry key this instance was created from, empty if unattached. */
    const std::string &RegisteredName() const { return registered_name_; }

    /** @brief Case source path recorded at registration, as compiled. */
    const std::string &SourceFile() const { return source_file_; }

    /** @brief Compiled case source digest recorded at registration. */
    const std::string &SourceSha256() const { return source_sha256_; }

    // Input completeness and provenance precede model code. A successful
    // preparation owns an immutable copy; no stale mutable storage is certified.
    arch::config::PreparedConfiguration SetupChecked(SimConfig& config, SpeciesManager& species) {
        arch::config::ValidateControls(config, species.count());
        config.RequireLoadedValues();
        if (!registered_case_id_.empty()
            && config.LoadedCaseId() != registered_case_id_)
            throw ConfigValueError("case", "CASE_IDENTITY_MISMATCH",
                "Loaded configuration belongs to another registered model.");
        const auto before = config;
        Setup(config, species);
        arch::config::ValidateControls(config, species.count());
        config.RequireSamePreparation(before);
        species.ValidateRegistrationSources();
        return arch::config::PreparedConfiguration(config, species, *this);
    }

    // Explicit inspection boundary. Production InitializeData has no observer
    // or extra per-cell branch. Restore the caller's observer even on failure.
    void InspectSetup(SimConfig& config, SpeciesManager& species,
                      const std::shared_ptr<arch::preview::ParameterReadTrace>& reads) {
        const auto previous = config.parameter_reads;
        config.parameter_reads = reads;
        try { SetupChecked(config, species); }
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
     * @param config Loaded preparation configuration. Application checks reject
     * undeclared changes; record model state in members and register species.
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

private:
    std::string registered_name_;
    std::string source_file_;
    std::string source_sha256_;
};
