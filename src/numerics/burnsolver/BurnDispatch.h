/**
 * @file BurnDispatch.h
 * @brief Runtime dispatcher for Nuclear Networks and ODE Solvers.
 * Implements Cartesian Product Template Dispatching.
 */
#pragma once

#include <string>
#include <iostream>
#include <stdexcept>

#include "Networks.h"
#include "ode_be-nr.h"
#include "ode_bd.h"
#include "ode_ros4.h"
#include "BurnerHandle.h"

#include "../../data/GlobalDefs.h" // 包含 SimConfig

#include "../../numerics/linalg/DenseWrap.h"  // 包含 DenseLU
#include "../../numerics/linalg/SparseWrap.h" // 包含 SparseKLU

// =======================================================
// 空燃烧器 (当 use_burn == false 时使用)
// 保证主循环不需要写 if (use_burn) 这种破坏流水线的判断
// =======================================================
struct DummyBurner
{
    template <typename EOSViewType>
    bool integrate(double * /*X_ODE*/, double /*rho*/, double /*dt_target*/,
                   const EOSViewType & /*eos*/, const BurnConfig & /*burn_cfg*/, double & /*dt_rec*/) const
    {
        return true; // 什么都不做。编译器会将其完全优化剔除。
    }
};

// =======================================================
// 主分发器
// =======================================================
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
     * @brief 第一层分发：解析并锁定网络类型
     */
    template <typename Func>
    static void dispatch(const SimConfig &config, Func &&func)
    {

        // 1. 检查是否开启了燃烧模块
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
        // 假设 ODE 求解器类型配置在 catch-all 字典中，默认使用 BE_NR

        std::cout << "[Burn Dispatch] Resolving Network: " << net_type
                  << " | ODE Solver: " << ode_type << " | Linear Solver: " << lin_type << std::endl;

        if (config.physics.burn.use_nse)
        {
            std::cout << "[Burn Dispatch] Online Timmes NSE solver enabled above T="
                      << config.physics.burn.nseTempThreshold << " K and rho="
                      << config.physics.burn.nseDensThreshold
                      << " g/cm^3." << std::endl;
        }

        // 2. 将字符串转换为编译期的强类型 Network
        if (net_type == "aprox19") {
            dispatch_ode<NetAprox19>(ode_type, lin_type, std::forward<Func>(func));
        } else if (net_type == "aprox21") {
            dispatch_ode<NetAprox21>(ode_type, lin_type, std::forward<Func>(func));
        } else if (net_type == "aprox13") {
            dispatch_ode<NetAprox13>(ode_type, lin_type, std::forward<Func>(func));
        } else if (net_type == "iso7") {
            dispatch_ode<NetIso7>(ode_type, lin_type, std::forward<Func>(func));
        } else {
            throw std::runtime_error("Unknown network_name in par file: " + net_type);
        }
    }

private:
    /**
     * @brief 第二层分发：锁定 ODE 求解器，最终发射
     */

    template <typename NetType, typename Func>
    static void dispatch_ode(const std::string &ode_type, const std::string &lin_type, Func &&func)
    {
        if (ode_type == "BE_NR" || ode_type == "be_nr")
        {
            // 将 Solver_BE_NR 作为接受 3 个参数的模板传给下一层
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
     * @brief 第三层分发：锁定线性求解器与矩阵，完成全编译期类型拼装并最终发射 (func)
     * @tparam ODESolverWrapper 期待接收三个参数的模板类：<NetType, MatrixType, LinearSolver>
     */
    template <template <typename, typename, typename> class ODESolverWrapper, typename NetType, typename Func>
    static void dispatch_linsolver(const std::string &lin_type, Func &&func)
    {
        if (lin_type == "DenseLU")
        {
            ODESolverWrapper<NetType, DenseMatrixData, DenseLUSolver> burner;
            func(burner);
        }
        else if (lin_type == "SparseKLU")
        {
            // ODESolverWrapper<NetType, SparseMatrixData, SparseSolverWrap> burner;
            // func(burner);
            throw std::runtime_error("SparseKLU not fully implemented.");
        }
        else
        {
            throw std::runtime_error("Unknown Linear Solver Type: " + lin_type);
        }
    }
};
