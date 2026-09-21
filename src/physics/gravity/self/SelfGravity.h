/** @file SelfGravity.h
 * CPU domain-field owner. Prepare is serial; patch consumers only read a fully
 * published field. No solve is hidden in patch callbacks or output routines.
 */
#pragma once
#include "physics/gravity/IGravityPolicy.h"
#include <array>
#include <memory>
namespace amr { struct EllipticMeshBinding; }
namespace arch::state { struct CompletionToken; }
namespace arch::multigrid { struct SolveReport; }
namespace Physical::Gravity {
struct GravitySolveRequest;
class SelfGravity final : public IGravityPolicy {
public:
    explicit SelfGravity(GravityConfig config);
    ~SelfGravity();
    void bind(amr::EllipticMeshBinding binding) const;
    arch::state::CompletionToken prepare(const GravitySolveRequest&) const;
    void invalidate() const noexcept;
    double timestep(double cfl) const;
    const std::vector<double>& potential() const;
    const std::array<std::vector<double>,3>& acceleration() const;
    const arch::multigrid::SolveReport& report() const;
    double density_mean() const;
    void add_sources_on_patch(std::vector<FluidVector>&, const FluidState&,
        const Grid&, double, void* = nullptr) const override;
    void add_flux_work_on_patch(std::vector<FluidVector>&, const std::vector<FluidVector>&,
        const FluidState&, const Grid&, double, int) const override;
private:
    struct Workspace;
    Workspace& workspace() const;
    GravityConfig config_;
    mutable std::unique_ptr<Workspace> work_;
};
}
