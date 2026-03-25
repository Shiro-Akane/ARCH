/**
 * @file Sod.cpp
 * @brief User-defined Problem: Multi-dimensional Sod Shock Tube & Riemann Problems.
 */

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
#include <cmath>

namespace
{
    // ========================================================================
    // Geometry & Shape Control
    // shape_type: 0 = Planar (1D Shock), 1 = Corner (2D Quadrant), 2 = Circle
    // ========================================================================
    int g_shape_type;
    int g_shock_dir;   // Used only if shape_type == 0
    double g_x0, g_y0; // Center or corner coordinates
    double g_radius;   // Used only if shape_type == 2

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
    g_shape_type = config.Get<int>("shape_type", 0); // Default to 1D Planar
    g_shock_dir = config.Get<int>("shock_dir", 0);
    g_x0 = config.Get<double>("x_pos", 0.5);
    g_y0 = config.Get<double>("y_pos", 0.5);
    g_radius = config.Get<double>("radius", 0.2);

    // [2. Read Thermodynamics & Kinematics]
    // 默认高压区
    g_rho_L = config.Get<double>("rho_left", 1.0);
    g_p_L = config.Get<double>("p_left", 1.0);
    g_u_L = config.Get<double>("u_left", 0.0);
    g_v_L = config.Get<double>("v_left", 0.0); // 增加 Y 方向初速度扩展性

    // 默认低压区
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

    g_species_id_L = specs.add_species(name_L, gamma_L, Cv_L);
    g_species_id_R = specs.add_species(name_R, gamma_R, Cv_R);

    // Console Output for Validation
    std::cout << "[Problem] Setup complete. Type: ";
    if (g_shape_type == 0)
        std::cout << "1D Planar (Dir " << g_shock_dir << ", pos=" << g_x0 << ")\n";
    if (g_shape_type == 1)
        std::cout << "2D Corner (x<" << g_x0 << " & y<" << g_y0 << ")\n";
    if (g_shape_type == 2)
        std::cout << "2D Circular (r<" << g_radius << " at " << g_x0 << "," << g_y0 << ")\n";
}

void Sod_Init(double x, double y, double z, PrimitiveData &out)
{
    // Determine which state this geometric coordinate belongs to
    bool is_high_pressure = false;

    if (g_shape_type == 0) // 1D Planar
    {
        double coord = (g_shock_dir == 0) ? x : ((g_shock_dir == 1) ? y : z);
        is_high_pressure = (coord < g_x0);
    }
    else if (g_shape_type == 1) // 2D Corner (Quadrant)
    {
        // 只有当 x 和 y 都小于设定阈值时，才处于角落高压区
        is_high_pressure = (x <= g_x0 && y <= g_y0);
    }
    else if (g_shape_type == 2) // 2D Circular Explosion
    {
        double r2 = (x - g_x0) * (x - g_x0) + (y - g_y0) * (y - g_y0);
        is_high_pressure = (r2 <= g_radius * g_radius); // 避免使用 std::sqrt 提升性能
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