/** @file SelfGravity.h
 * Backend-independent domain-field owner. Prepare is serial; patch consumers only read a fully
 * published field. No solve is hidden in patch callbacks or output routines.
 * Workflow:
 * 1. Receive active density with mesh and generation identity.
 * 2. Declare the backend-independent self-gravity domain-field owner.
 * 3. Publish a checked potential/acceleration field for the requested stage.
 */

#pragma once

#include <array>
#include <memory>

#include "physics/gravity/IGravityPolicy.h"

namespace amr { struct EllipticMeshBinding; }
namespace arch::state { struct CompletionToken; }
namespace arch::multigrid { struct SolveReport; }
namespace Physical::Gravity {
struct GravitySolveRequest;
class GravityExecution;
struct GravityPatchView;
class SelfGravity final : public IGravityPolicy {
public:
    explicit SelfGravity(GravityConfig config);
    ~SelfGravity();
    void bind(amr::EllipticMeshBinding binding) const;
    arch::state::CompletionToken prepare(const GravitySolveRequest&) const;
    void invalidate() const noexcept;
    void clear_solver_initial_guess() const noexcept;
    void set_execution(std::shared_ptr<GravityExecution>) const;
    GravityPatchView patch_view(std::size_t block) const;
    std::size_t cell_count() const;
    double timestep(double cfl) const;
    const std::vector<double>& potential() const;
    const std::array<std::vector<double>,3>& acceleration() const;
    const arch::multigrid::SolveReport& report() const;
    double density_mean() const;
    // Wall time bounded by completion fences; no asynchronous launch timing.
    struct Timings { double source_boundary=0., poisson=0., force=0.; };
    const Timings& timings() const;
    void add_sources_on_patch(std::vector<FluidVector>&, const FluidState&,
        const Grid&, double, void* = nullptr) const override;
    void add_flux_work_on_patch(std::vector<FluidVector>&, const std::vector<FluidVector>&,
        const FluidState&, const Grid&, double, int) const override;
private:
    struct Workspace;
    Workspace& workspace() const;
    GravityConfig config_;
    mutable std::unique_ptr<Workspace> work_;
    mutable std::shared_ptr<GravityExecution> execution_;
};
}
