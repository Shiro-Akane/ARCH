/**
 * @file DispatchImpl.h
 * @brief Template matrix factory for dispatching physical and numerical solver policies.
 *
 * Workflow:
 * 1. Instantiates the physics models (EOS, Gravity, Reaction Networks).
 * 2. Instantiates the hydrodynamics components (Flux solvers, Limiters).
 * 3. Binds them into a concrete template sequence to call run_simulation().
 * 4. Used heavily to avoid bloated compilation objects by keeping template instantiations segregated.
 */

#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

// Core runtime types.
#include "../../core/RuntimeParams.h"
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include "../../interface/ProblemGenerator.h"

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

inline auto parse_flux_selection(const SimConfig& config) noexcept
{
    return arch::dispatch::parse_registered_policy<arch::dispatch::FluxPolicies>(
        config.numerics.solver_name);
}

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

// Level 4: Execute the simulation with the fully assembled type
template <typename TimeIntegrator, typename FluxSchemePolicy, typename EosPolicy>
void launch_run(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                const Physical::Gravity::IGravityPolicy* gravity,
                const BurnerHandle<EosPolicy> &burn,
                const SimConfig &config,
                const SpeciesManager &specs, const RunState &run_state,
                const arch::dispatch::ResolvedExecutionPlan* resolved_plan = nullptr,
                const arch::dispatch::ExecutionRequirements* requirements = nullptr,
                const arch::dispatch::BackendResolution* backend = nullptr,
                arch::dispatch::StartupOrder* startup_order = nullptr)
{
    // Bind the selected EOS and flux policy behind the hydrodynamics interface.
    Numerics::HydroSolverImpl<EosPolicy, FluxSchemePolicy> hydro_solver(eos);

    std::string integrator_name = TimeIntegrator::name() + " + " + FluxSchemePolicy::name();

    // Pass the integrator entry point to the non-templated driver loop.
    run_simulation<EosPolicy>(amr_ctrl, eos, gravity, burn, &hydro_solver,
                              &TimeIntegrator::template solve<BCHandler>,
                              integrator_name, config, specs, run_state,
                              resolved_plan, requirements, backend,
                              startup_order);
}

template <typename TimeIntegrator, typename EosPolicy>
void launch_resolved_run(
    amr::AMRControl& amr_ctrl, const EosPolicy& eos,
    const Physical::Gravity::IGravityPolicy* gravity,
    const BurnerHandle<EosPolicy>& burn, const SimConfig& config,
    const SpeciesManager& specs, const RunState& run_state,
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
                &plan, &requirements, &backend, &startup_order);
        });
    if (!launched)
        throw std::logic_error("resolved Hydro route has no CPU binding");
}

// Level 3: Select Limiter (For MUSCL)
template <typename TimeIntegrator, class FluxBinding, typename EosPolicy>
void select_limiter(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                    const Physical::Gravity::IGravityPolicy* gravity,
                    const BurnerHandle<EosPolicy> &burn,
                    const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    using namespace arch::dispatch;
    const auto selected = parse_registered_policy<LimiterPolicies>(config.numerics.limiter);
    if (selected.defaulted)
        std::cerr << "[Warning] Unknown limiter '" << config.numerics.limiter
                  << "', defaulting to MinMod." << std::endl;
    visit_policy<LimiterPolicies>(selected.value, [&]<class Registration> {
        using LimiterBinding = typename PolicyRegistration<Registration>::CpuBinding;
        using Limiter = typename CpuLimiterType<LimiterBinding>::type;
        using Reconstruction = MusclReconstruction<Limiter>;
        using Flux = typename CpuFluxType<FluxBinding, Reconstruction>::type;
        launch_run<TimeIntegrator, Flux>(amr_ctrl, eos, gravity, burn,
                                         config, specs, run_state);
    });
}

// Level 2: Select Reconstruction Scheme
template <typename TimeIntegrator, class FluxBinding, typename EosPolicy>
void select_reconstruction(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                           const Physical::Gravity::IGravityPolicy* gravity,
                           const BurnerHandle<EosPolicy> &burn,
                           const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    using namespace arch::dispatch;
    const auto selected = parse_registered_policy<ReconstructionPolicies>(
        config.numerics.reconstruction);
    if (selected.defaulted)
        std::cerr << "[Warning] Unknown reconstruction '" << config.numerics.reconstruction
                  << "', defaulting to PCM." << std::endl;
    visit_policy<ReconstructionPolicies>(selected.value, [&]<class Registration> {
        using Binding = typename PolicyRegistration<Registration>::CpuBinding;
        if constexpr (std::is_same_v<Binding, CpuMusclBinding>) {
            select_limiter<TimeIntegrator, FluxBinding>(
                amr_ctrl, eos, gravity, burn, config, specs, run_state);
        } else {
            using Reconstruction = typename CpuReconstructionType<Binding>::type;
            using Flux = typename CpuFluxType<FluxBinding, Reconstruction>::type;
            launch_run<TimeIntegrator, Flux>(amr_ctrl, eos, gravity, burn,
                                             config, specs, run_state);
        }
    });
}

// Level 1: Select Flux Scheme
template <typename TimeIntegrator, typename EosPolicy>
void select_flux(amr::AMRControl &amr_ctrl, const EosPolicy &eos,
                 const Physical::Gravity::IGravityPolicy* gravity,
                 const BurnerHandle<EosPolicy> &burn,
                 const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    using namespace arch::dispatch;
    const auto selected = parse_flux_selection(config);
    if (selected.defaulted)
        std::cerr << "[Warning] Unknown solver '" << config.numerics.solver_name
                  << "', defaulting to HLLC." << std::endl;
    visit_policy<FluxPolicies>(selected.value, [&]<class Registration> {
        using Binding = typename PolicyRegistration<Registration>::CpuBinding;
        select_reconstruction<TimeIntegrator, Binding>(
            amr_ctrl, eos, gravity, burn, config, specs, run_state);
    });
}

} // namespace DispatchImpl

void Dispatch_Euler(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK2(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
void Dispatch_RK3(amr::AMRControl &amr_ctrl, const SimConfig &config, const SpeciesManager &specs, const RunState &run_state);
