/**
 * @file DispatchImpl.h
 * @brief Bind registered numerical policies to a typed simulation driver.
 *
 * Each integrator translation unit supplies its selected time integrator.
 * Registry visitors bind reconstruction and flux types to that driver while
 * erased EOS, gravity and burn interfaces retain their own shared authorities.
 */

#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>

// Core runtime types.
#include "../../core/RuntimeParams.h"
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"

// Flux and reconstruction policies.
#include "../../numerics/flux/FluxHLL.h"
#include "../../numerics/flux/FluxHLLC.h"
#include "../../numerics/flux/FluxRoe.h"
#include "../../numerics/flux/FluxSW.h"
#include "../../numerics/flux/FluxVL.h"
#include "../../numerics/reconstruction/Limiters.h"
#include "../../numerics/reconstruction/Reconstruction.h"

// Each Dispatch_*.cpp includes only its selected time integrator. Keeping those
// headers out of this shared template factory prevents all integrator variants
// from being instantiated in every translation unit and limits compiler memory.

// Driver and erased policy interfaces.
#include "../../numerics/burnsolver/BurnerHandle.h"
#include "../../numerics/integrator/HydroSolverImpl.h"
#include "../../physics/gravity/IGravityPolicy.h"
#include "../Driver.h"
#include "BackendCapabilities.h"
#include "PolicyDescriptor.h"

namespace DispatchImpl {

template <class Binding>
struct CpuLimiterType;

template <> struct CpuLimiterType<arch::dispatch::CpuMinModBinding> { using type = MinMod; };
template <> struct CpuLimiterType<arch::dispatch::CpuMcBinding> { using type = McLimiter; };
template <> struct CpuLimiterType<arch::dispatch::CpuSuperBeeBinding> { using type = SuperBee; };
template <> struct CpuLimiterType<arch::dispatch::CpuVanLeerLimiterBinding> { using type = VanLeer; };

template <class Binding>
struct CpuReconstructionType;

template <> struct CpuReconstructionType<arch::dispatch::CpuPcmBinding> { using type = PCMReconstruction; };
template <> struct CpuReconstructionType<arch::dispatch::CpuPpmBinding> { using type = PPMReconstruction; };

template <class Binding, class Reconstruction>
struct CpuFluxType;

template <class Reconstruction>
struct CpuFluxType<arch::dispatch::CpuVlBinding, Reconstruction> { using type = FluxVL<Reconstruction>; };
template <class Reconstruction>
struct CpuFluxType<arch::dispatch::CpuSwBinding, Reconstruction> { using type = FluxSW<Reconstruction>; };
template <class Reconstruction>
struct CpuFluxType<arch::dispatch::CpuRoeBinding, Reconstruction> { using type = FluxRoe<Reconstruction>; };
template <class Reconstruction>
struct CpuFluxType<arch::dispatch::CpuHllBinding, Reconstruction> { using type = FluxHLL<Reconstruction>; };
template <class Reconstruction>
struct CpuFluxType<arch::dispatch::CpuHllcBinding, Reconstruction> { using type = FluxHLLC<Reconstruction>; };

template <class Function>
bool visit_hydro_cpu_route(
    const arch::dispatch::ResolvedExecutionPlan& plan, Function&& function)
{
    using namespace arch::dispatch;
    bool invoked = false;
    const bool flux_found = visit_policy<FluxPolicies>(plan.flux, [&]<class FluxRegistration> {
        using FluxBinding = typename PolicyRegistration<FluxRegistration>::CpuBinding;
        const bool reconstruction_found = visit_policy<ReconstructionPolicies>(
            plan.reconstruction, [&]<class ReconstructionRegistration> {
                using ReconstructionBinding =
                    typename PolicyRegistration<ReconstructionRegistration>::CpuBinding;
                if constexpr (std::is_same_v<ReconstructionBinding, CpuMusclBinding>) {
                    visit_policy<LimiterPolicies>(plan.limiter, [&]<class LimiterRegistration> {
                        using LimiterBinding =
                            typename PolicyRegistration<LimiterRegistration>::CpuBinding;
                        using Limiter = typename CpuLimiterType<LimiterBinding>::type;
                        using Reconstruction = MusclReconstruction<Limiter>;
                        using Flux = typename CpuFluxType<FluxBinding, Reconstruction>::type;
                        function.template operator()<Flux, Reconstruction>();
                        invoked = true;
                    });
                } else {
                    using Reconstruction =
                        typename CpuReconstructionType<ReconstructionBinding>::type;
                    using Flux = typename CpuFluxType<FluxBinding, Reconstruction>::type;
                    function.template operator()<Flux, Reconstruction>();
                    invoked = true;
                }
            });
        invoked = invoked && reconstruction_found;
    });
    return flux_found && invoked;
}

// Execute a fully assembled route with its required resolved context.
template <typename TimeIntegrator, typename FluxSchemePolicy, typename EosPolicy>
void launch_run(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                const Physical::Gravity::IGravityPolicy* gravity,
                const BurnerHandle<EosPolicy> &burn,
                const SimConfig &config,
                const SpeciesManager &specs, const RunState &run_state,
                const arch::dispatch::ResolvedExecutionPlan& resolved_plan,
                const arch::dispatch::ExecutionRequirements& requirements,
                const arch::dispatch::BackendResolution& backend,
                arch::dispatch::StartupOrder& startup_order,
                const io::CheckpointProvenance& checkpoint_provenance)
{
    // Bind the selected EOS and flux policy behind the hydrodynamics interface.
    Numerics::HydroSolverImpl<EosPolicy, FluxSchemePolicy> hydro_solver(eos);

    std::string integrator_name = TimeIntegrator::name() + " + " + FluxSchemePolicy::name();

    // Pass the integrator entry point and the same context to the shared driver.
    run_simulation<EosPolicy>(amr_ctrl, eos, gravity, burn, &hydro_solver,
                              &TimeIntegrator::template solve<BCHandler>,
                              integrator_name, config, specs, run_state,
                              checkpoint_provenance,
                              &resolved_plan, &requirements, &backend,
                              &startup_order);
}

template <typename TimeIntegrator, typename EosPolicy>
void launch_resolved_run(
    amr::AMRControl& amr_ctrl, const EosPolicy& eos,
    const Physical::Gravity::IGravityPolicy* gravity,
    const BurnerHandle<EosPolicy>& burn, const SimConfig& config,
    const SpeciesManager& specs, const RunState& run_state,
    const io::CheckpointProvenance& checkpoint_provenance,
    const arch::dispatch::ResolvedExecutionPlan& plan,
    const arch::dispatch::ExecutionRequirements& requirements,
    const arch::dispatch::BackendResolution& backend,
    arch::dispatch::StartupOrder& startup_order)
{
    const bool launched = visit_hydro_cpu_route(
        plan, [&]<typename Flux, typename Reconstruction>() {
            (void)sizeof(Reconstruction);
            launch_run<TimeIntegrator, Flux>(
                amr_ctrl, eos, gravity, burn, config, specs, run_state,
                plan, requirements, backend, startup_order,
                checkpoint_provenance);
        });
    if (!launched)
        throw std::logic_error("resolved Hydro route has no CPU binding");
}

} // namespace DispatchImpl
