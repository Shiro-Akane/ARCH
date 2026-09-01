/**
 * @file BurnDispatch.h
 * @brief Runtime dispatcher for Nuclear Networks and ODE Solvers.
 * Implements Cartesian Product Template Dispatching.
 */
#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "BurnerHandle.h"
#include "Networks.h"
#include "ode_bd.h"
#include "ode_be-nr.h"
#include "ode_ros4.h"

#include "../../data/GlobalDefs.h" // Defines SimConfig and its burn configuration.
#include "../../driver/dispatch/BackendCapabilities.h"
#include "../../driver/dispatch/PolicyDescriptor.h"

#include "../linalg/DenseWrap.h"  // Dense matrix and LU policy.
#include "../linalg/SparseWrap.h" // Reserved sparse-matrix policy.

// No-op policy used when burning is disabled. It keeps the driver pipeline
// branch-free after runtime dispatch.
struct DummyBurner
{
    template <typename EOSViewType>
    bool integrate(double * /*X_ODE*/, double /*rho*/, double /*dt_target*/,
                   const EOSViewType & /*eos*/, const BurnConfig & /*burn_cfg*/, double & /*dt_rec*/) const
    {
        return true; // The compiler removes this empty policy from specialized drivers.
    }
};

// Active CUDA burning must never enter the host burn loop.  The CUDA backend
// owns its kernel launch and reduction path, so this guard turns an accidental
// host invocation into an immediate control-flow error instead of silently
// skipping the source term.
struct CudaHostBurnGuard
{
    template <typename EOSViewType>
    [[noreturn]] bool integrate(
        double* /*X_ODE*/, double /*rho*/, double /*dt_target*/,
        const EOSViewType& /*eos*/, const BurnConfig& /*burn_cfg*/,
        double& /*dt_rec*/) const
    {
        throw std::logic_error(
            "active CUDA burn was invoked through the host burner handle");
    }
};

// Three-stage runtime-to-compile-time burn dispatcher.
struct BurnDispatcher
{

    /**
     * @brief Bind the host-side burner after the compute backend is resolved.
     *
     * CPU execution retains the concrete network/ODE/linear-solver handle.
     * CUDA execution uses a no-op only when burning is disabled; active CUDA
     * burning receives a fail-closed guard because the device backend owns the
     * burn operation.  A still-unresolved Auto backend is never accepted at
     * this construction boundary.
     */
    template <typename EosPolicy>
    static BurnerHandle<EosPolicy> make_host_handle(
        const SimConfig& config,
        const arch::dispatch::ResolvedExecutionPlan& plan,
        const arch::dispatch::BackendResolution& backend)
    {
        using namespace arch::dispatch;

        if (backend.resolved_backend == ComputeBackend::Auto) {
            throw std::logic_error(
                "host burner construction received an unresolved Auto backend");
        }

        const bool disabled_plan = plan.network == NetworkId::None
            && plan.ode_solver == OdeSolverId::None
            && plan.linear_solver == LinearSolverId::None;
        const bool complete_active_plan = plan.network != NetworkId::None
            && plan.ode_solver != OdeSolverId::None
            && plan.linear_solver != LinearSolverId::None;
        if ((!config.physics.burn.use_burn && !disabled_plan)
            || (config.physics.burn.use_burn && !complete_active_plan)) {
            throw std::logic_error(
                "host burner construction received an incomplete burn plan");
        }
        if (config.physics.burn.use_burn && config.physics.burn.use_nse
            && !network_supports_nse(plan.network)) {
            throw std::logic_error(
                "host burner construction received an NSE-incompatible network");
        }

        if (backend.resolved_backend == ComputeBackend::Cpu) {
            return make_handle<EosPolicy>(config, plan);
        }
        if (backend.resolved_backend != ComputeBackend::Cuda) {
            throw std::logic_error(
                "host burner construction received an invalid resolved backend");
        }
        if (!config.physics.burn.use_burn) {
            return BurnerHandle<EosPolicy>::template bind<DummyBurner>();
        }
        return BurnerHandle<EosPolicy>::template bind<CudaHostBurnGuard>();
    }

    /**
     * @brief Resolve the burner once without instantiating the hydro matrix for
     * every network/ODE combination.
     */
    template <typename EosPolicy>
    static BurnerHandle<EosPolicy> make_handle(const SimConfig &config)
    {
        BurnerHandle<EosPolicy> result;
        dispatch(config, [&](auto &&burner)
                 {
                     using BurnerPolicy = std::remove_cvref_t<decltype(burner)>;
                     result = BurnerHandle<EosPolicy>::template bind<BurnerPolicy>();
                 });
        return result;
    }

    template <typename EosPolicy>
    static BurnerHandle<EosPolicy> make_handle(
        const SimConfig& config,
        const arch::dispatch::ResolvedExecutionPlan& plan)
    {
        BurnerHandle<EosPolicy> result;
        dispatch(config, plan.network, plan.ode_solver, plan.linear_solver,
                 [&](auto&& burner) {
                     using BurnerPolicy =
                         std::remove_cvref_t<decltype(burner)>;
                     result = BurnerHandle<EosPolicy>::template bind<
                         BurnerPolicy>();
                 });
        return result;
    }

    /**
     * @brief Resolve the network type.
     */
    template <typename Func>
    static void dispatch(const SimConfig &config, Func &&func)
    {

        // Resolve the no-burn policy before parsing any network-specific option.
        if (!config.physics.burn.use_burn)
        {
            std::cout << "[Burn Dispatch] Burning is DISABLED. Using DummyBurner." << std::endl;
            DummyBurner dummy;
            func(dummy);
            return;
        }

        std::string net_type = config.physics.burn.network_name;
        std::string ode_type = config.physics.burn.odeconfig.ode_solver;
        std::string lin_type = config.physics.burn.odeconfig.linear_solver;
        // ODE solver selection defaults to BE_NR.

        std::cout << "[Burn Dispatch] Resolving Network: " << net_type
                  << " | ODE Solver: " << ode_type << " | Linear Solver: " << lin_type << std::endl;

        using namespace arch::dispatch;
        const auto selected = parse_registered_policy<NetworkPolicies>(net_type);
        if (selected.ok && selected.value != NetworkId::None) {
            visit_policy<NetworkPolicies>(selected.value, [&]<class Registration> {
                using Binding = typename PolicyRegistration<Registration>::CpuBinding;
                if constexpr (!std::is_same_v<Binding, CpuNoNetworkBinding>
                              && !std::is_same_v<Binding, AbsentBinding>) {
                    using Network = typename CpuNetworkType<Binding>::type;
                    validate_and_report_nse<Registration, Network>(config);
                    dispatch_ode<Network>(
                        ode_type, lin_type, std::forward<Func>(func));
                }
            });
            return;
        }

        throw std::runtime_error("Unknown network_name in par file: " + net_type);
    }

    template <typename Func>
    static void dispatch(const SimConfig& config,
                         arch::dispatch::NetworkId network,
                         arch::dispatch::OdeSolverId ode,
                         arch::dispatch::LinearSolverId linear,
                         Func&& func)
    {
        using namespace arch::dispatch;
        if (!config.physics.burn.use_burn) {
            if (network != NetworkId::None || ode != OdeSolverId::None
                || linear != LinearSolverId::None) {
                throw std::logic_error("disabled burn received an active plan");
            }
            std::cout << "[Burn Dispatch] Burning is DISABLED. Using DummyBurner."
                      << std::endl;
            DummyBurner dummy;
            func(dummy);
            return;
        }
        if (network == NetworkId::None || ode == OdeSolverId::None
            || linear == LinearSolverId::None) {
            throw std::logic_error("enabled burn received an incomplete plan");
        }
        std::cout << "[Burn Dispatch] Resolving Network: "
                  << config.physics.burn.network_name
                  << " | ODE Solver: "
                  << config.physics.burn.odeconfig.ode_solver
                  << " | Linear Solver: "
                  << config.physics.burn.odeconfig.linear_solver << std::endl;
        if (!visit_policy<NetworkPolicies>(network,
                [&]<class Registration> {
                    using Binding = typename PolicyRegistration<
                        Registration>::CpuBinding;
                    if constexpr (!std::is_same_v<Binding,
                                                  CpuNoNetworkBinding>) {
                        using Network = typename CpuNetworkType<Binding>::type;
                        validate_and_report_nse<Registration, Network>(config);
                        dispatch_ode<Network>(ode, linear,
                                              std::forward<Func>(func));
                    }
                })) {
            throw std::logic_error("resolved network has no CPU binding");
        }
    }

private:
    template <class Registration, class Network>
    static void validate_and_report_nse(const SimConfig& config)
    {
        static_assert(
            Network::SUPPORTS_NSE
                == arch::dispatch::NetworkPolicyMetadata<
                    Registration>::supports_nse,
            "network NSE metadata must match the concrete network type");
        if (!config.physics.burn.use_nse) return;
        if constexpr (!Network::SUPPORTS_NSE) {
            throw std::runtime_error(
                "NSE is unavailable for burn network: "
                + std::string(
                    arch::dispatch::PolicyRegistration<Registration>::names[0]));
        } else {
            std::cout << "[Burn Dispatch] Online Timmes NSE solver enabled above T="
                      << config.physics.burn.nseTempThreshold << " K and rho="
                      << config.physics.burn.nseDensThreshold
                      << " g/cm^3." << std::endl;
        }
    }

    /**
     * @brief Resolve the ODE integration policy for a fixed network.
     */

    template <typename NetType, typename Func>
    static void dispatch_ode(const std::string &ode_type, const std::string &lin_type, Func &&func)
    {
        using namespace arch::dispatch;
        const auto selected = parse_registered_policy<OdeSolverPolicies>(ode_type);
        if (!selected.ok || selected.value == OdeSolverId::None) {
            throw std::runtime_error("Unknown ODE Solver Type: [" + ode_type + "]");
        }
        visit_policy<OdeSolverPolicies>(selected.value, [&]<class Registration> {
            using Binding = typename PolicyRegistration<Registration>::CpuBinding;
            if constexpr (std::is_same_v<Binding, CpuBeNrBinding>) {
                dispatch_linsolver<Solver_BE_NR, NetType>(lin_type, std::forward<Func>(func));
            } else if constexpr (std::is_same_v<Binding, CpuBdBinding>) {
                dispatch_linsolver<Solver_BD, NetType>(lin_type, std::forward<Func>(func));
            } else if constexpr (std::is_same_v<Binding, CpuRos4Binding>) {
                dispatch_linsolver<Solver_ROS4, NetType>(lin_type, std::forward<Func>(func));
            }
        });
    }

    template <typename NetType, typename Func>
    static void dispatch_ode(arch::dispatch::OdeSolverId ode,
                             arch::dispatch::LinearSolverId linear,
                             Func&& func)
    {
        using namespace arch::dispatch;
        if (!visit_policy<OdeSolverPolicies>(ode,
                [&]<class Registration> {
                    using Binding = typename PolicyRegistration<
                        Registration>::CpuBinding;
                    if constexpr (std::is_same_v<Binding, CpuBeNrBinding>) {
                        dispatch_linsolver<Solver_BE_NR, NetType>(
                            linear, std::forward<Func>(func));
                    } else if constexpr (std::is_same_v<Binding, CpuBdBinding>) {
                        dispatch_linsolver<Solver_BD, NetType>(
                            linear, std::forward<Func>(func));
                    } else if constexpr (std::is_same_v<Binding, CpuRos4Binding>) {
                        dispatch_linsolver<Solver_ROS4, NetType>(
                            linear, std::forward<Func>(func));
                    }
                })) {
            throw std::logic_error("resolved ODE has no CPU binding");
        }
    }

    /**
     * @brief Resolve matrix and linear-solver policies and invoke the callback.
     * @tparam ODESolverWrapper Template accepting NetType, MatrixType, and LinearSolver.
     */
    template <template <typename, typename, typename> class ODESolverWrapper, typename NetType, typename Func>
    static void dispatch_linsolver(const std::string &lin_type, Func &&func)
    {
        using namespace arch::dispatch;
        const bool automatic = ascii_iequals(lin_type, "auto");
        LinearSolverId selected_id = LinearSolverId::None;
        if (automatic) {
            selected_id = NetType::NUM_SPECIES <= BurnLimits::MAX_SPECIES
                ? LinearSolverId::DenseLu : LinearSolverId::SparseKlu;
        } else {
            const auto selected =
                parse_registered_policy<LinearSolverPolicies>(lin_type);
            if (!selected.ok || selected.value == LinearSolverId::None) {
                throw std::runtime_error(
                    "Unknown Linear Solver Type: " + lin_type);
            }
            selected_id = selected.value;
        }

        if (selected_id == LinearSolverId::DenseLu)
        {
            if constexpr (NetType::NUM_SPECIES <= BurnLimits::MAX_SPECIES) {
                std::cout << "[Burn Dispatch] Matrix backend: DenseLU (N="
                          << NetType::ODE_NEQ << ")" << std::endl;
                using Matrix = DenseMatrixData<NetType::ODE_NEQ>;
                ODESolverWrapper<NetType, Matrix, DenseLUSolver> burner;
                func(burner);
                return;
            } else {
                throw std::runtime_error(
                    "DenseLU is reserved for networks with at most " +
                    std::to_string(BurnLimits::MAX_SPECIES) +
                    " isotopes; select linear_solver = SparseKLU or Auto.");
            }
        }
        if (selected_id == LinearSolverId::SparseKlu)
        {
#if !ARCH_HAS_KLU
            throw std::runtime_error(
                "SparseKLU was selected, but this ARCH build has KLU disabled.");
#else
            std::cout << "[Burn Dispatch] Matrix backend: SparseKLU (N="
                      << NetType::ODE_NEQ << ")" << std::endl;
            using Matrix = SparseMatrixData<NetType::ODE_NEQ>;
            ODESolverWrapper<NetType, Matrix, SparseKLUSolver> burner;
            func(burner);
            return;
#endif
        }
        throw std::runtime_error("Unknown Linear Solver Type: " + lin_type);
    }

    template <template <typename, typename, typename> class ODESolverWrapper,
              typename NetType, typename Func>
    static void dispatch_linsolver(
        arch::dispatch::LinearSolverId linear, Func&& func)
    {
        using namespace arch::dispatch;
        if (linear == LinearSolverId::DenseLu) {
            if constexpr (NetType::NUM_SPECIES <= BurnLimits::MAX_SPECIES) {
                std::cout << "[Burn Dispatch] Matrix backend: DenseLU (N="
                          << NetType::ODE_NEQ << ")" << std::endl;
                using Matrix = DenseMatrixData<NetType::ODE_NEQ>;
                ODESolverWrapper<NetType, Matrix, DenseLUSolver> burner;
                func(burner);
                return;
            }
            throw std::runtime_error(
                "DenseLU is reserved for networks with at most "
                + std::to_string(BurnLimits::MAX_SPECIES)
                + " isotopes; select linear_solver = SparseKLU or Auto.");
        }
        if (linear == LinearSolverId::SparseKlu) {
#if !ARCH_HAS_KLU
            throw std::runtime_error(
                "SparseKLU was selected, but this ARCH build has KLU disabled.");
#else
            std::cout << "[Burn Dispatch] Matrix backend: SparseKLU (N="
                      << NetType::ODE_NEQ << ")" << std::endl;
            using Matrix = SparseMatrixData<NetType::ODE_NEQ>;
            ODESolverWrapper<NetType, Matrix, SparseKLUSolver> burner;
            func(burner);
            return;
#endif
        }
        throw std::logic_error("resolved linear solver has no CPU binding");
    }
};
