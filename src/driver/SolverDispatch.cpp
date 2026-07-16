/**
 * @file SolverDispatch.cpp
 * @brief The "Switchboard" for the simulation.
 * * Responsibilities:
 * * 1. Read config strings (Solver, Reconstruction, Limiter).
 * * 2. Instantiate Grid, EOS, and FluidState.
 * * 3. Call ProblemGenerator to set initial conditions (t=0).
 * * 4. Instantiate the template chain (TimeIntegrator < Flux < Recon < Limiter > >).
 * * 5. Hand over to Driver::run_simulation().
 */

#include "SolverDispatch.h"

#include <string>
#include <iostream>
#include <stdexcept>

// 1. Core Data Structures
#include "../data/FluidState.h"
#include "../grid/Grid.h"
#include "../core/RuntimeParams.h"
#include "../interface/ProblemGenerator.h"
#include "../io/IO.h"

// 2. Physics & Solvers
#include "../physics/eos/eosdispatch.h"
#include "../physics/gravity/GravityDispatch.h"
#include "../numerics/burnsolver/BurnDispatch.h"

#include "../numerics/flux/FluxVL.h"
#include "../numerics/flux/FluxSW.h"
#include "../numerics/flux/FluxRoe.h"
#include "../numerics/flux/FluxHLL.h"
#include "../numerics/flux/FluxHLLC.h"

#include "../numerics/reconstruction/Reconstruction.h"
#include "../numerics/reconstruction/Limiters.h"

#include "../numerics/integrator/TimeIntegratorEuler.h"
#include "../numerics/integrator/TimeIntegratorRK2.h"
#include "../numerics/integrator/TimeIntegratorRK3.h"

// 3. The Main Loop
#include "Driver.h"

// =========================================================
// Helper: Determine Ghost Cells based on Config
// =========================================================
int determine_required_ng(const SimConfig &config)
{
    // 1. 获取重构方法
    std::string recon = config.numerics.reconstruction;

    int ng_recon = 1; // 默认 PCM

    if (recon == "pcm" || recon == "PCM")
    {
        ng_recon = 1;
    }
    else if (recon == "muscl" || recon == "MUSCL")
    {
        ng_recon = 2; // MUSCL 需要 i-1, i, i+1, i+2，所以单侧需要 2 层
    }
    else if (recon == "ppm" || recon == "PPM" || recon == "weno5")
    {
        ng_recon = 3; // PPM/WENO 通常需要更宽的模板
    }
    else
    {
        // 未知方法，为了安全起见，给予较大的 ghost cell
        // 或者抛出异常，这里假设默认用 MUSCL
        ng_recon = 2;
    }

    // 2. 检查是否有特殊的通量格式需要更多 NG (一般很少见，但为了扩展性)
    // 比如某些高阶通量可能本身就需要宽模板
    int ng_flux = 1;
    if (config.numerics.solver_name == "SomeHighOrderFlux")
    {
        ng_flux = 3;
    }

    // 3. 返回最大值
    return std::max(ng_recon, ng_flux);
}

// =========================================================
// Template Instantiation Helpers (Private Helpers)
// =========================================================

// Level 4: Execute the simulation with the fully assembled type
template <typename SolverType, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void launch_run(FluidState &state, const EosPolicy &eos, GravityPolicy &gravity, BurnerPolicy &burn,
                const Grid &grid, const SimConfig &config,
                const SpeciesManager &specs, const RunState &run_state)
{
    // 调用 Driver.h 中的主循环
    run_simulation<SolverType>(state, eos, gravity, burn, grid, config, specs, run_state);
}

// Level 3: Select Limiter (For MUSCL)
template <template <typename> class TimeIntegrator, template <typename> class FluxScheme, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void select_limiter(FluidState &state, const EosPolicy &eos, GravityPolicy &gravity, BurnerPolicy &burn, const Grid &grid,
                    const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string lim = config.numerics.limiter;

    if (lim == "minmod" || lim == "MinMod")
    {
        using MyRecon = MusclReconstruction<MinMod>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (lim == "superbee" || lim == "SuperBee")
    {
        using MyRecon = MusclReconstruction<SuperBee>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (lim == "vanleer" || lim == "VanLeer")
    {
        using MyRecon = MusclReconstruction<VanLeer>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (lim == "mc" || lim == "MC")
    {
        using MyRecon = MusclReconstruction<McLimiter>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else
    {
        // 默认或未识别，回退到 MinMod
        std::cerr << "[Warning] Unknown limiter '" << lim << "', defaulting to MinMod." << std::endl;
        using MyRecon = MusclReconstruction<MinMod>;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
}

// Level 2: Select Reconstruction Scheme
template <template <typename> class TimeIntegrator, template <typename> class FluxScheme, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void select_reconstruction(FluidState &state, const EosPolicy &eos, GravityPolicy &gravity, BurnerPolicy &burn, const Grid &grid,
                           const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string recon = config.numerics.reconstruction;
    if (recon == "pcm" || recon == "PCM")
    {
        // PCM (一阶) 不需要限制器，直接组装
        using MyRecon = PCMReconstruction;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (recon == "muscl" || recon == "MUSCL")
    {
        // MUSCL (二阶) 需要进一步选择限制器 -> 进入 Level 4
        select_limiter<TimeIntegrator, FluxScheme>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (recon == "ppm" || recon == "PPM")
    {
        // PPM (三阶) 通常自带逻辑，或者有单独的限制参数
        using MyRecon = PPMReconstruction;
        using MySolver = TimeIntegrator<FluxScheme<MyRecon>>;
        launch_run<MySolver>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else
    {
        throw std::runtime_error("Unknown Reconstruction method: " + recon);
    }
}

// Level 1: Select Flux Scheme
template <template <typename> class TimeIntegrator, typename EosPolicy, typename GravityPolicy, typename BurnerPolicy>
void select_flux(FluidState &state, const EosPolicy &eos, GravityPolicy &gravity, BurnerPolicy &burn, const Grid &grid,
                 const SimConfig &config, const SpeciesManager &specs, const RunState &run_state)
{
    std::string flux = config.numerics.solver_name; // e.g., "VL", "HLLC"

    if (flux == "VL" || flux == "VanLeer")
    {
        // 选定 FluxVL，进入 Level 3 选择重构
        select_reconstruction<TimeIntegrator, FluxVL>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (flux == "SW" || flux == "StegerWarming")
    {
        // dispatch_reconstruction<TimeIntegrator, FluxSW>(state, eos, grid, config, specs);
        select_reconstruction<TimeIntegrator, FluxSW>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (flux == "Roe" || flux == "roe")
    {
        // dispatch_reconstruction<TimeIntegrator, FluxSW>(state, eos, grid, config, specs);
        select_reconstruction<TimeIntegrator, FluxRoe>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (flux == "HLL" || flux == "hll")
    {
        // dispatch_reconstruction<TimeIntegrator, FluxSW>(state, eos, grid, config, specs);
        select_reconstruction<TimeIntegrator, FluxHLL>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (flux == "HLLC")
    {
        // dispatch_reconstruction<TimeIntegrator, FluxHLLC>(state, eos, grid, config, specs);
        select_reconstruction<TimeIntegrator, FluxHLLC>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else
    {
        throw std::runtime_error("Unknown Flux Solver: " + flux);
    }
}

// =========================================================
// The Public Dispatch Function
// =========================================================

/**
 * @brief The entry point called by main.cpp
 * * 1. Initializes Grid & EOS.
 * * 2. Allocates FluidState.
 * * 3. Calls Problem->Initialize() to set t=0 data.
 * * 4. Dispatches to the correct template instantiation.
 */
void DispatchSolver(const std::string &solver_name,
                    ProblemGenerator &problem,
                    const SimConfig &config,
                    const SpeciesManager &specs)
{
    std::cout << "[Dispatch] Initializing System..." << std::endl;

    // --- 关键修改点 ---
    // 1. 动态计算需要的 Ghost Cell 数量
    int required_ng = determine_required_ng(config);
    std::cout << "[Dispatch] Determined Ghost Cell count: " << required_ng << std::endl;

    // 2. 使用计算出的 ng 创建 Grid
    Grid grid(config.grid, required_ng);

    std::cout << "[Dispatch] Grid Topology: " << grid.dim << "D "
              << config.grid.geometry << " ("
              << grid.nx << " x " << grid.ny << " x " << grid.nz << ")" << std::endl;

    // 目前写死 IdealGas，未来可以根据 config.physics.eos_type 做工厂模式
    IdealGas eos(config.physics.gamma, specs);

    // 2. 分配全局状态内存
    FluidState state(grid, specs.count());

    // 3. 应用初始条件 (t=0)
    RunState run_state;

    if (config.io.restart && !config.io.restart_file.empty())
    {
        std::cout << "[Dispatch] Restarting from checkpoint: " << config.io.restart_file << std::endl;
        // 把 run_state 传进去读取
        read_chk(config.io.restart_file, state, grid, run_state);
    }
    else
    {
        std::cout << "[Dispatch] Initializing Data via Problem Generator..." << std::endl;
        problem.InitializeData(state, grid, config, specs);

        int center_idx = grid.GetIndex(grid.Is(), grid.Js(), grid.Ks());
        std::cout << "[Dispatch Debug] After InitializeData, state.X(0, center_idx) = " << state.X(0, center_idx) << std::endl;
    }


    // 4. [Level 1] 选择时间积分器 (Time Integrator)
    // RuntimeParams 已将新旧参数名归一化到 numerics.time_integrator。

    // 5. 选择重力策略
    std::cout << "[Dispatch] Resolving Gravity Policy..." << std::endl;
    std::string grav_type = config.physics.gravity.type;
    std::cout << "           -> Type: " << grav_type;

    if (grav_type == "external" || grav_type == "External" || grav_type == "EXTERNAL")
    {
        std::cout << " | g = (" << config.physics.gravity.g_x << ", "
                  << config.physics.gravity.g_y << ", "
                  << config.physics.gravity.g_z << ")";
    }
    else if (grav_type == "self" || grav_type == "Self" || grav_type == "SELF")
    {
        std::cout << " | G_const = " << config.physics.gravity.G_const;
    }
    std::cout << std::endl;

    // 6. 分发到EOS和重力模块，最后进入时间积分器和数值格式的选择
    std::cout << "[Dispatch] Resolving Physics Policies..." << std::endl;
    EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos)
                                {
                                    using EosPolicy = std::remove_cvref_t<decltype(eos)>;
                                    auto burn = BurnDispatcher::make_handle<EosPolicy>(config);
                                    Physical::Gravity::dispatch_gravity(config, [&](auto &&gravity)
                                                                        {
    const std::string &time_int = config.numerics.time_integrator;

    std::cout << "[Dispatch] Strategy: "
              << time_int << " + "
              << config.numerics.solver_name << " + "
              << config.numerics.reconstruction
              << " (" << config.numerics.limiter << ")" << std::endl;

    if (time_int == "RK2" || time_int == "SSPRK2")
    {
        select_flux<SolverRK2>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (time_int == "RK3" || time_int == "SSPRK3")
    {
        select_flux<SolverRK3>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else if (time_int == "Euler" || time_int == "RK1")
    {
        select_flux<SolverEuler>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
    else
    {
        std::cerr << "[Warning] Unknown time integrator '" << time_int << "', defaulting to SSPRK2." << std::endl;
        select_flux<SolverRK2>(state, eos, gravity, burn, grid, config, specs, run_state);
    }
                                                                        }); // Gravity lambda end
                                }); // EOS lambda end
}
