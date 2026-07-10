/**
 * @file Sod.cpp
 * @brief User-defined Problem: Multi-dimensional Sod Shock Tube & Riemann Problems.
 */

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
#include <cmath>
#include <iostream>

namespace
{
    // ========================================================================
    // Geometry & Shape Control
    // shape_type: 0 = Planar (1D Shock), 1 = Corner (2D Quadrant),
    //             2 = Cartesian Circle, 3 = True Cylindrical/Radial Explosion
    // ========================================================================
    int g_shape_type;
    int g_shock_dir;
    double g_x0, g_y0;
    double g_radius;
    double g_angle_min, g_angle_max; // 新增：用于扇形/柱坐标角度控制

    // Left/Inner State (High Pressure)
    double g_rho_L, g_p_L, g_u_L, g_v_L;
    int g_species_id_L;

    // Right/Outer State (Low Pressure)
    double g_rho_R, g_p_R, g_u_R, g_v_R;
    int g_species_id_R;
}

void Sod_Setup(SimConfig &config, SpeciesManager &specs)
{
    // [1. Read Geometry Configuration]
    g_shape_type = config.Get<int>("shape_type", 0);
    g_shock_dir = config.Get<int>("shock_dir", 0);
    g_x0 = config.Get<double>("x_pos", 0.5);
    g_y0 = config.Get<double>("y_pos", 0.5);
    g_radius = config.Get<double>("radius", 0.2);

    // 新增：允许用户在 .par 中输入扇形爆炸的角度范围 (默认 0 到 2*pi)
    g_angle_min = config.Get<double>("angle_min", 0.0);
    g_angle_max = config.Get<double>("angle_max", 6.2831853);

    // [2. Read Thermodynamics & Kinematics] (被找回来的代码)
    g_rho_L = config.Get<double>("rho_left", 1.0);
    g_p_L = config.Get<double>("p_left", 1.0);
    g_u_L = config.Get<double>("u_left", 0.0);
    g_v_L = config.Get<double>("v_left", 0.0); // 增加 Y 方向初速度扩展性

    g_rho_R = config.Get<double>("rho_right", 0.125);
    g_p_R = config.Get<double>("p_right", 0.1);
    g_u_R = config.Get<double>("u_right", 0.0);
    g_v_R = config.Get<double>("v_right", 0.0);

    // [3. Setup Species]
    std::string name_L = config.Get<std::string>("name_left", "Air");
    double gamma_L = config.Get<double>("gamma_left", 1.4);
    double Cv_L = config.Get<double>("cv_left", 717.5);

    std::string name_R = config.Get<std::string>("name_right", "Helium");
    double gamma_R = config.Get<double>("gamma_right", 1.67);
    double Cv_R = config.Get<double>("cv_right", 3113.9);

    g_species_id_L = specs.add_species(name_L, 2.0, 1.0, gamma_L, Cv_L);
    g_species_id_R = specs.add_species(name_R, 4.0, 2.0, gamma_R, Cv_R);

    // Console Output for Validation
    std::cout << "[Problem] Setup complete. Type: ";
    if (g_shape_type == 0)
        std::cout << "1D Planar (Dir " << g_shock_dir << ", pos=" << g_x0 << ")\n";
    else if (g_shape_type == 1)
        std::cout << "2D Corner (x<" << g_x0 << " & y<" << g_y0 << ")\n";
    else if (g_shape_type == 2)
        std::cout << "2D Circular (shifted to " << g_x0 << "," << g_y0 << ")\n";
    else if (g_shape_type == 3)
        std::cout << "Cylindrical Sector (r_cy < " << g_radius << ", angle in [" << g_angle_min << "," << g_angle_max << "])\n";
}

// [核心修改点] 接收 PointCoords 字典
void Sod_Init(const PointCoords &p, PrimitiveData &out)
{
    // Determine which state this geometric coordinate belongs to
    bool is_high_pressure = false;

    if (g_shape_type == 0) // 1D Planar
    {
        // 依然可以使用笛卡尔坐标系的 x, y, z
        double coord = (g_shock_dir == 0) ? p.x : ((g_shock_dir == 1) ? p.y : p.z);
        is_high_pressure = (coord < g_x0);
    }
    else if (g_shape_type == 1) // 2D Corner (Quadrant)
    {
        is_high_pressure = (p.x <= g_x0 && p.y <= g_y0);
    }
    else if (g_shape_type == 2) // 2D Shifted Circle (笛卡尔位移圆)
    {
        double dx = p.x - g_x0;
        double dy = p.y - g_y0;
        is_high_pressure = (dx * dx + dy * dy <= g_radius * g_radius);
    }
    else if (g_shape_type == 3) // [新增] 原点柱坐标爆炸 / 扇形激波管
    {
        // 直接爽快地调用字典中的柱坐标分量 r_cy 和 phi_cy！
        // 无需再做 sqrt 或 atan2 运算，因为底层的 GetPhysicalCoords 已经全算好了
        if (p.r_cy <= g_radius && p.phi_cy >= g_angle_min && p.phi_cy <= g_angle_max)
        {
            is_high_pressure = true;
        }
    }

    // Apply the chosen state
    if (is_high_pressure)
    {
        out.rho = g_rho_L;
        out.p = g_p_L;
        out.u = g_u_L;
        out.v = g_v_L;
        out.w = 0.0;
        out.SetMassFraction(g_species_id_L, 1.0);
    }
    else
    {
        out.rho = g_rho_R;
        out.p = g_p_R;
        out.u = g_u_R;
        out.v = g_v_R;
        out.w = 0.0;
        out.SetMassFraction(g_species_id_R, 1.0);
    }
}

// Registration Macro
REGISTER_PROBLEM("Sod", Sod_Setup, Sod_Init);