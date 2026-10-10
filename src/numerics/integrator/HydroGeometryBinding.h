/**
 * @file HydroGeometryBinding.h
 * @brief Preflight and shared chart selection for Host Hydro scheduler lanes.
 *
 * Workflow:
 * 1. Match the explicit Hydro chart to the physical boundary owner.
 * 2. For native RZ, require a Host stage binding, the actual domain extent and
 *    a configured post-boundary gate before Clear() or any patch output.
 * 3. Preflight each real grid and boundary plan, then select its seam chart.
 * A nonempty callable establishes presence, not authenticated Runtime identity;
 * production binds the actual Runtime EOS gate and retains its owner checks.
 */
#pragma once

#include <cmath>
#include <stdexcept>

#include "driver/schedule/StageScheduler.h"
#include "numerics/integrator/IHydroSolver.h"
#include "physics/gravity/IGravityPolicy.h"
#include "physics/gravity/NativeExternalStage.h"
#include "physics/gravity/NativeSelfStage.h"

namespace TimeIntegration {
struct HydroGeometryBinding {
    GridMetrics::GeometrySemantics semantics;
    amr::CoordinateSeamGeometry exchange_chart;
    bool deferred_native_source=false;
};

/** Borrow the actual prepared source's original boundary domain. Workflow:
 * discover the declared origin; require exactly its one private live frame;
 * borrow that frame's existing domain before register/output/cache mutation.
 * A descriptor alone cannot construct authority, and no new BC owner is made.
 */
inline const arch::boundary::HostHydroBoundaryDomainAuthority&
prepared_native_boundary_domain(const Physical::Gravity::IGravityPolicy& gravity)
{
    using Physical::Gravity::GravitySourceOrigin;
    const auto* external=gravity.prepared_native_external();
    const auto* self=gravity.prepared_native_self();
    if(external&&self)
        throw std::logic_error("Native source supplied two private stage frames");
    switch(gravity.source_descriptor().origin) {
    case GravitySourceOrigin::NativeExternalOrthonormal:
        if(!external||self)throw std::logic_error("Native source preparation supplied no actual frame");
        return external->boundary_domain();
    case GravitySourceOrigin::NativeSelfComposite:
        if(!self||external)throw std::logic_error("Native self preparation supplied no matching frame");
        return self->boundary_domain();
    default:
        throw std::logic_error("Native source preparation has an unknown origin");
    }
}

/** Resolve a shared chart and reject incomplete native Host execution before mutation. */
template<typename BCPolicy>
HydroGeometryBinding bind_hydro_geometry(
    const amr::AMRControl& control, const BCPolicy& boundary,
    const Numerics::IHydroSolver* hydro,
    const Physical::Gravity::IGravityPolicy* gravity)
{
    if (!hydro) throw std::invalid_argument("Host Hydro policy is missing");
    const auto semantics=hydro->geometry_semantics();
    const bool rz=semantics==GridMetrics::GeometrySemantics::AxisymmetricRz;
    if (!rz && semantics!=GridMetrics::GeometrySemantics::Existing)
        throw std::invalid_argument("Unknown Host Hydro chart");
    if constexpr (requires { boundary.geometry_semantics(); }) {
        if (boundary.geometry_semantics()!=semantics)
            throw std::invalid_argument("Hydro and boundary chart mismatch");
    } else {
        if (rz) throw std::invalid_argument("RZ boundary chart identity is missing");
    }
    if (rz && gravity) {
        const auto source=gravity->source_descriptor();
        const auto& binding=arch::scheduler::current_stage_binding();
        // Origin discovers ordering only; private real source publication is
        // borrowed by the first executor after actual preparation succeeds.
        const bool external=source.origin==Physical::Gravity::GravitySourceOrigin::NativeExternalOrthonormal;
        const bool self=source.origin==Physical::Gravity::GravitySourceOrigin::NativeSelfComposite;
        if((!external&&!self)
            ||(external&&(!source.external.enabled||!std::isfinite(source.external.g_x)
                ||!std::isfinite(source.external.g_y)||!std::isfinite(source.external.g_z)))
            ||!binding.context.hydro_preparation
            ||!binding.context.hydro_preparation->supports_macro_step_journal(arch::state::ExecutionSide::Host))
            throw std::invalid_argument("RZ gravity requires its actual prepared source contract");
        // Discovery selects ordering only. The first executor must borrow the
        // private real frame before clearing registers or evaluating patches.
    }
    if(rz) {
        const auto& binding=arch::scheduler::current_stage_binding();
        if(binding.context.side!=arch::state::ExecutionSide::Host)
            throw std::logic_error("Native RZ Hydro requires a Host stage binding");
        if(!binding.context.post_boundary_acceptance)
            throw std::logic_error("Native RZ Hydro requires post-boundary acceptance");
        const auto& active=control.tree->GetActiveBlocks();
        if(active.empty()||binding.handles.size()!=active.size())
            throw std::logic_error("Native RZ Hydro stage domain extent mismatch");
        // Presence cannot identify a std::function's owner. The actual Runtime
        // callback checks its ledger/clock/handles/grid/BC frame after real
        // exchange and applies the bound EOS before any ghost readiness.
    }
    // Validate all patches before Clear(), any stage output, or publication.
    for (int id:control.tree->GetActiveBlocks()) {
        const auto& grid=control.pool->GetBlock(id).grid;
        (void)GridMetrics::make_geometry_view(grid,semantics);
        if constexpr (requires { boundary.logical_plan(grid); })
            (void)boundary.logical_plan(grid);
        else if (rz)
            throw std::invalid_argument("RZ boundary preflight is missing");
    }
    return {semantics,rz ? amr::CoordinateSeamGeometry::RzAxisymmetric
                        : amr::CoordinateSeamGeometry::ExistingChart,rz&&gravity};
}
} // namespace TimeIntegration
