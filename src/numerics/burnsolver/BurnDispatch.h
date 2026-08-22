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

// Three-stage runtime-to-compile-time burn dispatcher.
struct BurnDispatcher
{

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

        if (config.physics.burn.use_nse)
        {
            std::cout << "[Burn Dispatch] Online Timmes NSE solver enabled above T="
                      << config.physics.burn.nseTempThreshold << " K and rho="
                      << config.physics.burn.nseDensThreshold
                      << " g/cm^3." << std::endl;
        }

        // Map the normalized runtime name to a compile-time network policy.
        if (net_type == "aprox19") {
            dispatch_ode<NetAprox19>(ode_type, lin_type, std::forward<Func>(func));
        } else if (net_type == "aprox21") {
            dispatch_ode<NetAprox21>(ode_type, lin_type, std::forward<Func>(func));
        } else if (net_type == "aprox13") {
            dispatch_ode<NetAprox13>(ode_type, lin_type, std::forward<Func>(func));
        } else if (net_type == "iso7") {
            dispatch_ode<NetIso7>(ode_type, lin_type, std::forward<Func>(func));
        } else {
            bool custom_dispatched = false;
#define ARCH_TRY_CUSTOM_NETWORK(runtime_name, network_type)                 \
            if (!custom_dispatched && net_type == runtime_name) {           \
                dispatch_ode<network_type>(                                 \
                    ode_type, lin_type, std::forward<Func>(func));           \
                custom_dispatched = true;                                   \
            }
            ARCH_FOR_EACH_CUSTOM_NETWORK(ARCH_TRY_CUSTOM_NETWORK)
#undef ARCH_TRY_CUSTOM_NETWORK
            if (!custom_dispatched)
                throw std::runtime_error(
                    "Unknown network_name in par file: " + net_type);
        }
    }

private:
    /**
     * @brief Resolve the ODE integration policy for a fixed network.
     */

    template <typename NetType, typename Func>
    static void dispatch_ode(const std::string &ode_type, const std::string &lin_type, Func &&func)
    {
        if (ode_type == "BE_NR" || ode_type == "be_nr")
        {
            // Forward the three-parameter solver template to linear dispatch.
            dispatch_linsolver<Solver_BE_NR, NetType>(lin_type, std::forward<Func>(func));
        }
        else if (ode_type == "ROS4" || ode_type == "ros4")
        {
            dispatch_linsolver<Solver_ROS4, NetType>(lin_type, std::forward<Func>(func));
        }
        else if (ode_type == "BD" || ode_type == "bd")
        {
            dispatch_linsolver<Solver_BD, NetType>(lin_type, std::forward<Func>(func));
        }
        else
        {
            throw std::runtime_error("Unknown ODE Solver Type: [" + ode_type + "]");
        }
    }

    /**
     * @brief Resolve matrix and linear-solver policies and invoke the callback.
     * @tparam ODESolverWrapper Template accepting NetType, MatrixType, and LinearSolver.
     */
    template <template <typename, typename, typename> class ODESolverWrapper, typename NetType, typename Func>
    static void dispatch_linsolver(const std::string &lin_type, Func &&func)
    {
        const bool automatic = lin_type == "Auto" || lin_type == "auto";
        if (lin_type == "DenseLU" || automatic)
        {
            if constexpr (NetType::NUM_SPECIES <= BurnLimits::MAX_SPECIES) {
                std::cout << "[Burn Dispatch] Matrix backend: DenseLU (N="
                          << NetType::ODE_NEQ << ")" << std::endl;
                using Matrix = DenseMatrixData<NetType::ODE_NEQ>;
                ODESolverWrapper<NetType, Matrix, DenseLUSolver> burner;
                func(burner);
                return;
            } else if (!automatic) {
                throw std::runtime_error(
                    "DenseLU is reserved for networks with at most " +
                    std::to_string(BurnLimits::MAX_SPECIES) +
                    " isotopes; select linear_solver = SparseKLU or Auto.");
            }
        }
        if (lin_type == "SparseKLU" || automatic)
        {
            std::cout << "[Burn Dispatch] Matrix backend: SparseKLU (N="
                      << NetType::ODE_NEQ << ")" << std::endl;
            using Matrix = SparseMatrixData<NetType::ODE_NEQ>;
            ODESolverWrapper<NetType, Matrix, SparseKLUSolver> burner;
            func(burner);
            return;
        }
        throw std::runtime_error("Unknown Linear Solver Type: " + lin_type);
    }
};
