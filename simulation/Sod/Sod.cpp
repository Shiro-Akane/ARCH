/**
 * @file Sod.cpp
 * @brief User-defined Problem: Multi-dimensional Sod Shock Tube & Riemann Problems.
 */

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
#include <cmath>
#include <iostream>

class SodProblem
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
    double g_angle_min, g_angle_max;

    // ========================================================================
    // Fluid States (Left/Inner: High Pressure)
    // ========================================================================
    double g_rho_L, g_p_L, g_u_L, g_v_L;
    int g_species_id_L;

    // ========================================================================
    // Fluid States (Right/Outer: Low Pressure Background)
    // ========================================================================
    double g_rho_R, g_p_R, g_u_R, g_v_R;
    int g_species_id_R;

public:
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        // [1. Read Geometry Configuration]
        g_shape_type = config.Get<int>("shape_type", 0);
        g_shock_dir = config.Get<int>("shock_dir", 0);
        g_x0 = config.Get<double>("x_pos", 0.5);
        g_y0 = config.Get<double>("y_pos", 0.5);
        g_radius = config.Get<double>("radius", 0.15);

        g_angle_min = config.Get<double>("angle_min", 0.0);
        g_angle_max = config.Get<double>("angle_max", 90.0);

        // [2. Read Fluid States (Left/Inner)]
        g_rho_L = config.Get<double>("prob_rho_L", 1.0);
        g_p_L = config.Get<double>("prob_p_L", 1.0);
        g_u_L = config.Get<double>("prob_u_L", 0.0);
        g_v_L = config.Get<double>("prob_v_L", 0.0);

        // [3. Read Fluid States (Right/Outer)]
        g_rho_R = config.Get<double>("prob_rho_R", 0.125);
        g_p_R = config.Get<double>("prob_p_R", 0.1);
        g_u_R = config.Get<double>("prob_u_R", 0.0);
        g_v_R = config.Get<double>("prob_v_R", 0.0);

        // [4. Register Multi-Species (Using Ideal Gas with arbitrary properties)]
        // Using cv = 717.5 J/kgK for air (gamma = 1.4)
        double cv = 717.5;
        g_species_id_L = specs.add_species("DriverGas", 1.0, 1.0, config.physics.gamma, cv);
        g_species_id_R = specs.add_species("DrivenGas", 1.0, 1.0, config.physics.gamma, cv);

        // [5. Console Output]
        std::cout << "[Problem] Sod Setup Complete. Shape Type: " << g_shape_type << "\n"
                  << "          Left State  : Rho=" << g_rho_L << ", P=" << g_p_L << "\n"
                  << "          Right State : Rho=" << g_rho_R << ", P=" << g_p_R << "\n";
    }

    void Init(const PointCoords &p, PrimitiveData &out) const
    {
        bool is_left = false;

        if (g_shape_type == 0) // Planar (1D Shock Tube)
        {
            if (g_shock_dir == 0) // X-direction
                is_left = (p.x < g_x0);
            else if (g_shock_dir == 1) // Y-direction
                is_left = (p.y < g_x0);
            else if (g_shock_dir == 2) // Z-direction
                is_left = (p.z < g_x0);
        }
        else if (g_shape_type == 1) // Corner (2D Quadrant Explosion)
        {
            is_left = (p.x < g_x0 && p.y < g_y0);
        }
        else if (g_shape_type == 2) // Cartesian Circle
        {
            double r = std::sqrt((p.x - g_x0) * (p.x - g_x0) + (p.y - g_y0) * (p.y - g_y0));
            is_left = (r < g_radius);
        }
        else if (g_shape_type == 3) // True Cylindrical/Radial (using angular bounds)
        {
            double r = std::sqrt((p.x - g_x0) * (p.x - g_x0) + (p.y - g_y0) * (p.y - g_y0));

            // 计算角度 (0 到 360 度)
            double angle = std::atan2(p.y - g_y0, p.x - g_x0) * 180.0 / M_PI;
            if (angle < 0.0)
                angle += 360.0;

            // 判断是否在指定的半径和角度范围内
            is_left = (r < g_radius) && (angle >= g_angle_min) && (angle <= g_angle_max);
        }

        // Apply state based on geometry determination
        if (is_left)
        {
            out.rho = g_rho_L;
            out.p = g_p_L;
            out.u = g_u_L;
            out.v = g_v_L;
            out.w = 0.0;
            out.SetMassFraction(g_species_id_L, 1.0);
            out.SetMassFraction(g_species_id_R, 0.0);
        }
        else
        {
            out.rho = g_rho_R;
            out.p = g_p_R;
            out.u = g_u_R;
            out.v = g_v_R;
            out.w = 0.0;
            out.SetMassFraction(g_species_id_L, 0.0);
            out.SetMassFraction(g_species_id_R, 1.0);
        }
    }
};

REGISTER_PROBLEM_CLASS("Sod", SodProblem);