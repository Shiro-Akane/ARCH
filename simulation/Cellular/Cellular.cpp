/**
 * @file Cellular.cpp
 * @brief User-defined Problem: Cellular Detonation with Helmholtz EOS & Aprox19.
 */

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
#include "../../src/physics/eos/HelmEos.h"

#include <cmath>
#include <iostream>
#include <vector>
#include <stdexcept>

namespace
{
    // ========================================================================
    // Global State for the Problem (Populated once in Setup)
    // ========================================================================
    double g_rho_amb, g_p_amb, g_T_amb;
    double g_rho_vn, g_p_vn, g_T_vn, g_u_vn;

    // Geometry & Perturbation
    double g_distance;
    double g_noise_amp;
    int g_shock_dir;

    // Species & Pre-calculated Mass Fractions
    int g_sp_fuel;
    std::vector<double> g_X_init; // 缓存背景组分，极大地提升 Init 的赋值速度
}

void Cellular_Setup(SimConfig &config, SpeciesManager &specs)
{
    // [1. Read Thermodynamics & Kinematics (Temperature-based)]
    g_rho_amb = config.Get<double>("rho_ambient", 1.0e7);
    g_T_amb = config.Get<double>("T_ambient", 2.0e8);

    g_rho_vn = config.Get<double>("rho_vn", 4.0e7);
    g_T_vn = config.Get<double>("T_vn", 3.0e9);
    g_u_vn = config.Get<double>("u_vn", 1.0e9);

    // [2. Read Geometry Configuration]
    g_distance = config.Get<double>("distance", 0.5);
    g_noise_amp = config.Get<double>("noiseAmplitude", 0.0);
    g_shock_dir = config.Get<int>("shock_dir", 0);

    // [3. Setup Species & Mass Fractions]
    // [3. Setup Species & Mass Fractions]

    // 【核心修复】如果检测到当前 Species Count 为 0，主动注册 Aprox19 的 19 种组分！
    if (specs.count() == 0)
    {
        std::cout << "[CellularSetup] Registering pynucastro Aprox19 isotopes to SpeciesManager...\n";
        // 严格按照 network_properties.H 定义的顺序与成分注册 16 种组分
        specs.add_species("n", 1.0, 0.0, 1.6667, 0.0);
        specs.add_species("h1", 1.0, 1.0, 1.6667, 0.0);
        specs.add_species("he3", 3.0, 2.0, 1.6667, 0.0);
        specs.add_species("he4", 4.0, 2.0, 1.6667, 0.0);
        specs.add_species("c12", 12.0, 6.0, 1.6667, 0.0);
        specs.add_species("o16", 16.0, 8.0, 1.6667, 0.0);
        specs.add_species("ne20", 20.0, 10.0, 1.6667, 0.0);
        specs.add_species("mg24", 24.0, 12.0, 1.6667, 0.0);
        specs.add_species("si28", 28.0, 14.0, 1.6667, 0.0);
        specs.add_species("s32", 32.0, 16.0, 1.6667, 0.0);
        specs.add_species("ar36", 36.0, 18.0, 1.6667, 0.0);
        specs.add_species("ca40", 40.0, 20.0, 1.6667, 0.0);
        specs.add_species("ti44", 44.0, 22.0, 1.6667, 0.0);
        specs.add_species("cr48", 48.0, 24.0, 1.6667, 0.0);
        specs.add_species("fe52", 52.0, 26.0, 1.6667, 0.0);
        specs.add_species("ni56", 56.0, 28.0, 1.6667, 0.0);
    }

    g_sp_fuel = specs.GetSpeciesID("c12");
    if (g_sp_fuel < 0)
    {
        std::cerr << "[Warning] C12 not found. Defaulting to index 3.\n";
        g_sp_fuel = 3;
    }

    int num_species = specs.count(); // 现在应该是 19 了！
    g_X_init.resize(num_species, 1e-20);

    double sum_x = 0.0;
    g_X_init[g_sp_fuel] = 1.0;
    for (int i = 0; i < num_species; ++i)
        sum_x += g_X_init[i];
    for (int i = 0; i < num_species; ++i)
        g_X_init[i] /= sum_x;

    // [4. Thermodynamically Consistent Pressure Calculation]
    std::string table_path = config.Get<std::string>("eos_table_path", "");
    if (table_path.empty())
    {
        throw std::runtime_error("[CellularSetup] Error: eos_table_path is empty!");
    }

    // 临时挂载 EOS 获取 View 以计算准确初压
    HelmEos init_eos(table_path, &specs);

    double e_amb = init_eos.get_eint_from_T(g_rho_amb, g_T_amb, g_X_init.data());
    g_p_amb = init_eos.get_pressure_from_rho_T(g_rho_amb, g_T_amb, g_X_init.data());

    double e_vn = init_eos.get_eint_from_T(g_rho_vn, g_T_vn, g_X_init.data());
    g_p_vn = init_eos.get_pressure_from_rho_T(g_rho_vn, g_T_vn, g_X_init.data());

    // [5. Console Output]
    std::cout << "[Problem] Cellular Detonation Setup Complete.\n"
              << "          Shock Dir  : " << g_shock_dir << " (0=X, 1=Y, 2=Z)\n"
              << "          Spike Dist : " << g_distance << "\n"
              << "          Amb Press  : " << g_p_amb << " erg/cm^3\n"
              << "          VN Press   : " << g_p_vn << " erg/cm^3\n";
}

void Cellular_Init(const PointCoords &p, PrimitiveData &out)
{
    // [1. Coordinate & Perturbation Calculation]
    double coord = p.x;
    if (g_shock_dir == 1)
        coord = p.y;
    if (g_shock_dir == 2)
        coord = p.z;

    // 无状态哈希微扰 (多线程/CUDA 安全)
    double noise = 0.0;
    if (g_noise_amp > 0.0)
    {
        double pseudo_rand = std::sin(p.x * 12.9898 + p.y * 78.233 + p.z * 37.719) * 43758.5453;
        pseudo_rand = pseudo_rand - std::floor(pseudo_rand);
        noise = g_noise_amp * (2.0 * pseudo_rand - 1.0);
    }

    // [2. Assign Hydrodynamic State]
    if (coord < g_distance)
    {
        out.rho = g_rho_vn * (1.0 + noise);
        out.p = g_p_vn * (1.0 + noise * 1.5); // 有效 gamma 近似 1.5 用于压力微扰
        out.u = (g_shock_dir == 0) ? g_u_vn : 0.0;
        out.v = (g_shock_dir == 1) ? g_u_vn : 0.0;
        out.w = (g_shock_dir == 2) ? g_u_vn : 0.0;
    }
    else
    {
        out.rho = g_rho_amb;
        out.p = g_p_amb;
        out.u = 0.0;
        out.v = 0.0;
        out.w = 0.0;
    }

    if (p.x < 1e-5)
        std::cout << "Init checking cell 0..." << std::endl; // 只在第一个网格打印

    // [3. Apply Pre-calculated Mass Fractions]
    for (size_t i = 0; i < g_X_init.size(); ++i)
    {
        // 加强安全保护
        try
        {
            out.SetMassFraction(static_cast<int>(i), g_X_init[i]);
        }
        catch (...)
        {
            std::cerr << "CRASH at Setting Mass Fraction for species " << i << std::endl;
            exit(1);
        }
    }
}

// Registration Macro
REGISTER_PROBLEM("CellularDet", Cellular_Setup, Cellular_Init);