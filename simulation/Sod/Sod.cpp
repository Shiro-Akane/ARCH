/**
 * @file Sod.cpp
 * @brief User-defined Problem: Sod Shock Tube.
 * * This file demonstrates how to define a specific simulation case in ARCH.
 * It implements the "ProblemGenerator" interface using two callbacks:
 * 1. Setup(): Reads configuration and defines materials.
 * 2. Init() : Sets the initial values (rho, u, p) for each point in space.
 */

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h" // Required to read .par file

// ============================================================================
// Internal Static Storage (Performance Optimization)
// ============================================================================
// [关键优化]
// Init() 函数会在循环中被调用成千上万次。
// 如果我们在 Init() 里写 config.Get<double>("rho_left")，意味着每一步都要做字符串哈希查找，
// 这会严重拖慢程序。
// 解决方案：在 Setup() 里读一次，存到这里的 static 变量里。Init() 直接读变量，速度最快。
// ============================================================================
namespace
{
    // Geometry
    double g_shock_pos;

    // Left State
    double g_rho_L, g_p_L, g_u_L;
    int g_species_id_L;

    // Right State
    double g_rho_R, g_p_R, g_u_R;
    int g_species_id_R;
}

// ============================================================================
// 1. Setup Phase
// Responsibility: Define Materials and Cache Parameters.
// Note: Core params (nx, cfl, etc.) are ALREADY loaded in 'config' by main.cpp.
// ============================================================================
void Sod_Setup(SimConfig &config, SpeciesManager &specs)
{
    // [1. Optional] Override Core Config if necessary
    // 虽然 main 已经读了 par 文件，但如果这个 case 强制要求特定的边界条件，可以在这里覆盖
    // config.grid.nx = 1000; // 例如强制覆盖 nx

    // [2. Read Custom Parameters from Config]
    // 使用我们在 SimConfig 里新加的模板函数 Get<T>
    g_shock_pos = config.Get<double>("shock_position", 0.5);

    g_rho_L = config.Get<double>("rho_left", 1.0);
    g_p_L = config.Get<double>("p_left", 1.0);
    g_u_L = config.Get<double>("u_left", 0.0);

    g_rho_R = config.Get<double>("rho_right", 0.125);
    g_p_R = config.Get<double>("p_right", 0.1);
    g_u_R = config.Get<double>("u_right", 0.0);

    // [3. Setup Species]
    std::string name_L = config.Get<std::string>("name_left", "Helium");
    double gamma_L = config.Get<double>("gamma_left", 1.67);

    std::string name_R = config.Get<std::string>("name_right", "Air");
    double gamma_R = config.Get<double>("gamma_right", 1.4);

    // Register and cache IDs
    g_species_id_L = specs.add_species(name_L, gamma_L);
    g_species_id_R = specs.add_species(name_R, gamma_R);

    // 打印信息确认
    std::cout << "[Sod] Setup complete. Shock at x=" << g_shock_pos << std::endl;
}

// ============================================================================
// 2. Initialization Phase
// Responsibility: Set initial primitive variables.
// Performance: This function is called Nx*Ny*Nz times! Keep it fast!
// ============================================================================
void Sod_Init(double x, double y, double z, PrimitiveData &out)
{
    // 直接使用 static 变量进行比较和赋值，无 Map 查找开销
    if (x < g_shock_pos)
    {
        // --- Left State ---
        out.rho = g_rho_L;
        out.p = g_p_L;
        out.u = g_u_L;
        out.SetMassFraction(g_species_id_L, 1.0); // 100% Species L
    }
    else
    {
        // --- Right State ---
        out.rho = g_rho_R;
        out.p = g_p_R;
        out.u = g_u_R;
        out.SetMassFraction(g_species_id_R, 1.0); // 100% Species R
    }
}

// Registration Macro
REGISTER_PROBLEM("Sod", Sod_Setup, Sod_Init);