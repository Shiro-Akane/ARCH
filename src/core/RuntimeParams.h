/**
 * @file RuntimeParams.h
 * @brief Global static interface for accessing runtime parameters via type deduction.
 * * Wraps the ConfigParser in a Singleton to provide seamless access across the codebase.
 */

#pragma once

#include <string>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <cmath>

#include "../data/GlobalDefs.h"
#include "../io/ConfigParser.h"

class RuntimeParams
{
private:
    /**
     * @brief 轻量级表达式解析：支持 "3.14", "pi", "2.0*pi", "pi/2" 等基础输入
     */
    static double ParseMathExpr(std::string str, double default_val = 0.0)
    {
        if (str.empty())
            return default_val;

        // 1. 转小写并去除所有空格
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end());

        // 2. 查找是否包含 "pi"
        size_t pi_pos = str.find("pi");
        if (pi_pos == std::string::npos)
        {
            // 不包含 pi，直接走常规 stod
            try
            {
                return std::stod(str);
            }
            catch (...)
            {
                return default_val;
            }
        }

        // 3. 包含 pi 的简单计算逻辑
        double pi_val = M_PI; // 3.141592653589793...

        if (str == "pi")
            return pi_val;
        if (str == "-pi")
            return -pi_val;

        // 处理 "系数 * pi" 或 "pi * 系数"
        size_t star_pos = str.find('*');
        if (star_pos != std::string::npos)
        {
            std::string coeff_str = (pi_pos > star_pos) ? str.substr(0, star_pos) : str.substr(star_pos + 1);
            try
            {
                return std::stod(coeff_str) * pi_val;
            }
            catch (...)
            {
                return pi_val;
            }
        }

        // 处理 "pi / 系数"
        size_t slash_pos = str.find('/');
        if (slash_pos != std::string::npos && pi_pos < slash_pos)
        {
            std::string coeff_str = str.substr(slash_pos + 1);
            try
            {
                return pi_val / std::stod(coeff_str);
            }
            catch (...)
            {
                return pi_val;
            }
        }

        return default_val; // 无法解析则返回默认值
    }

public:
    /**
     * @brief Parses the parameter file and populates the SimConfig struct.
     * @param filename Path to the .par file.
     * @return A fully initialized SimConfig object.
     */
    static SimConfig Load(const std::string &filename)
    {
        ConfigParser parser;
        if (!parser.Load(filename))
        {
            throw std::runtime_error("RuntimeParams::Load failed: Could not open " + filename);
        }

        SimConfig cfg;

        cfg.grid.geometry = parser.GetString("geometry", "cartesian");

        // 1. 搬运核心参数 (Grid)
        cfg.grid.nx = parser.GetInt("nx", 100);
        cfg.grid.ny = parser.GetInt("ny", 1);
        cfg.grid.nz = parser.GetInt("nz", 1);

        cfg.grid.x_min = ParseMathExpr(parser.GetString("x_min", "0.0"));
        cfg.grid.x_max = ParseMathExpr(parser.GetString("x_max", "1.0"));
        cfg.grid.y_min = ParseMathExpr(parser.GetString("y_min", "0.0"));
        cfg.grid.y_max = ParseMathExpr(parser.GetString("y_max", "1.0"));
        cfg.grid.z_min = ParseMathExpr(parser.GetString("z_min", "0.0"));
        cfg.grid.z_max = ParseMathExpr(parser.GetString("z_max", "1.0"));

        cfg.grid.xl_boundary_type = parser.GetString("xl_boundary_type", "outflow");
        cfg.grid.xr_boundary_type = parser.GetString("xr_boundary_type", "outflow");
        cfg.grid.yl_boundary_type = parser.GetString("yl_boundary_type", "outflow");
        cfg.grid.yr_boundary_type = parser.GetString("yr_boundary_type", "outflow");
        cfg.grid.zl_boundary_type = parser.GetString("zl_boundary_type", "outflow");
        cfg.grid.zr_boundary_type = parser.GetString("zr_boundary_type", "outflow");

        // 2. 搬运数值参数 (Numerics)
        cfg.numerics.solver_name = parser.GetString("solver", "SW");
        cfg.numerics.cfl = parser.GetDouble("cfl", 0.8);
        cfg.numerics.limiter = parser.GetString("limiter", "minmod");
        cfg.numerics.reconstruction = parser.GetString("reconstruct", "pcm");
        // Prefer the canonical snake_case key while preserving compatibility
        // with existing parameter files that use the legacy spelling.
        cfg.numerics.time_integrator = parser.GetString(
            "time_integrator", parser.GetString("timeintegrator", "RK2"));
        std::string fix_switch = parser.GetString("EntropyFix", "On"); // 默认开启
        if (fix_switch == "Off" || fix_switch == "False")
        {
            cfg.numerics.entropy_fix_coeff = 0.0; // 0.0 代表关闭
        }
        else
        {
            // 如果开启，读取系数，默认为 0.1
            cfg.numerics.entropy_fix_coeff = parser.GetDouble("EntropyFixCoefficient", 0.1);
        }

        cfg.numerics.sml_rho = parser.GetDouble("sml_rho", 1e-12);
        cfg.numerics.max_eint = parser.GetDouble("max_eint", 1e21);

        // Execution backend.  This is independent of the time integrator:
        // a CUDA-enabled fat binary can still execute the CPU path at runtime.
        cfg.execution.compute_backend = parser.GetString("compute_backend", "cpu");
        std::transform(cfg.execution.compute_backend.begin(),
                       cfg.execution.compute_backend.end(),
                       cfg.execution.compute_backend.begin(), ::tolower);
        cfg.execution.cuda_device = parser.GetInt("cuda_device", 0);

        // 3. 搬运物理参数 (Physics)
        cfg.physics.eos_type = parser.GetString("eos_type", "ideal");
        cfg.physics.eos_table_path = parser.GetString("eos_table_path", "");
        cfg.physics.gamma = parser.GetDouble("gamma", 1.4);

        // --- 核反应燃烧模块 (Burn) ---
        cfg.physics.burn.use_burn = (parser.GetInt("use_burn", 0) != 0);
        cfg.physics.burn.network_name = parser.GetString("network_name", "aprox19");
        cfg.physics.burn.nuclearTempMin = parser.GetDouble("nuclearTempMin", 1e9);
        cfg.physics.burn.nuclearDensMin = parser.GetDouble("nuclearDensMin", 1e-10);
        cfg.physics.burn.smallt = parser.GetDouble("smallt", 1e5);
        cfg.physics.burn.smallx = parser.GetDouble("smallx", 1e-20);
        
        cfg.physics.burn.enucDtFactor = parser.GetDouble("enucDtFactor", 1e30);
        cfg.physics.burn.use_nse = (parser.GetInt("use_nse", 1) != 0);
        cfg.physics.burn.nseTempThreshold = parser.GetDouble("nseTempThreshold", 4.5e9);
        cfg.physics.burn.nseDensThreshold = parser.GetDouble("nseDensThreshold", 1.0e6);
        
        cfg.physics.burn.enforce_mass_conservation = (parser.GetInt("enforce_mass_conservation", 1) != 0); // 默认开启
        cfg.physics.burn.verbose_level = parser.GetInt("burn_verbose_level", 0);

        // --- ODE 求解器配置 (ODE) ---
        cfg.physics.burn.odeconfig.ode_solver = parser.GetString("ode_solver", "BE_NR");
        cfg.physics.burn.odeconfig.linear_solver = parser.GetString("linear_solver", "DenseLU");

        cfg.physics.burn.odeconfig.rtol = parser.GetDouble("ode_rtol", 1e-4);
        cfg.physics.burn.odeconfig.atol = parser.GetDouble("ode_atol", 1e-8);
        cfg.physics.burn.odeconfig.max_newton_iter = parser.GetInt("ode_max_newton_iter", 50);
        cfg.physics.burn.odeconfig.max_substeps = parser.GetInt("ode_max_substeps", 10000);

        cfg.physics.burn.odeconfig.dt_safe_factor = parser.GetDouble("ode_dt_safe_fac", 0.9);
        cfg.physics.burn.odeconfig.dt_fac_max = parser.GetDouble("ode_dt_fac_max", 2.0);
        cfg.physics.burn.odeconfig.dt_fac_min = parser.GetDouble("ode_dt_fac_min", 0.1);
        cfg.physics.burn.odeconfig.initial_dt_frac = parser.GetDouble("ode_initial_dt_frac", 1e-3);

        cfg.physics.burn.odeconfig.use_numerical_jacobian = (parser.GetInt("ode_use_numerical_jac", 0) != 0);
        cfg.physics.burn.odeconfig.freeze_jacobian = (parser.GetInt("ode_freeze_jacobian", 0) != 0);

        // --- 扩散模块 (Diffusion) ---
        cfg.physics.diffusion.use_diffusion = (parser.GetInt("use_diffusion", 0) != 0);
        cfg.physics.diffusion.integrator = parser.GetString("diff_integrator", "RKL2");
        cfg.physics.diffusion.diff_cfl = parser.GetDouble("diff_cfl", 0.8);
        cfg.physics.diffusion.max_stages = parser.GetInt("diff_max_stages", 256);

        cfg.physics.diffusion.use_thermal_diffusion = (parser.GetInt("use_thermal_diff", 0) != 0);
        cfg.physics.diffusion.use_viscous_diffusion = (parser.GetInt("use_viscous_diff", 0) != 0);
        cfg.physics.diffusion.use_species_diffusion = (parser.GetInt("use_species_diff", 0) != 0);
        cfg.physics.diffusion.nu_visc = parser.GetDouble("nu_visc", 0.0);
        cfg.physics.diffusion.alpha_therm = parser.GetDouble("alpha_therm", 0.0);
        cfg.physics.diffusion.D_spec = parser.GetDouble("D_spec", 0.0);

        // --- 引力模块 (Gravity) ---
        std::string grav_type = parser.GetString("gravity_type", "none");
        std::transform(grav_type.begin(), grav_type.end(), grav_type.begin(), ::tolower);
        cfg.physics.gravity.type = grav_type;

        if (grav_type == "external")
        {
            cfg.physics.gravity.g_x = ParseMathExpr(parser.GetString("gravity_g_x", "0.0"));
            cfg.physics.gravity.g_y = ParseMathExpr(parser.GetString("gravity_g_y", "0.0"));
            cfg.physics.gravity.g_z = ParseMathExpr(parser.GetString("gravity_g_z", "0.0"));
        }
        else if (grav_type == "self")
        {
            cfg.physics.gravity.G_const = ParseMathExpr(parser.GetString("gravity_G", "6.6743e-8"));
        }

        // 4. 搬运IO参数 (IO)
        cfg.io.tmax = parser.GetDouble("tmax", 0.1);
        cfg.io.max_steps = parser.GetInt("max_steps", -1);

        cfg.io.out_dir = parser.GetString("out_dir", "data");
        cfg.io.base_name = parser.GetString("base_name", "arch");

        cfg.io.plt_dt = parser.GetDouble("plt_dt", -1.0);
        cfg.io.plt_dstep = parser.GetInt("plt_dstep", -1);

        cfg.io.chk_dt = parser.GetDouble("chk_dt", -1.0);
        cfg.io.chk_dstep = parser.GetInt("chk_dstep", -1);

        std::string restart_str = parser.GetString("restart", "false");
        std::transform(restart_str.begin(), restart_str.end(), restart_str.begin(), ::tolower);

        cfg.io.restart = (restart_str.find("true") != std::string::npos ||
                          restart_str.find("1") != std::string::npos ||
                          restart_str.find("yes") != std::string::npos ||
                          restart_str.find("on") != std::string::npos);

        std::string r_file = parser.GetString("restart_file", "");
        r_file.erase(0, r_file.find_first_not_of(" \t\r\n"));
        r_file.erase(r_file.find_last_not_of(" \t\r\n") + 1);
        cfg.io.restart_file = r_file;

        if (cfg.io.restart)
        {
            std::cout << "[RuntimeParams] Restart Enabled. Target file: '"
                      << cfg.io.restart_file << "'" << std::endl;
        }

        std::string plt_vars = parser.GetString("plt_variables", "all");

        std::string plt_vars_lower = plt_vars;

        if (plt_vars_lower == "all" || plt_vars_lower == "conserved")
        {
            // 如果是 all，全部开启
            cfg.io.vars = {true, true, true, true, true, true, true};

            // 如果是 conserved，开启 rho, u, p, eng，关闭 v, w
            if (plt_vars_lower == "conserved")
            {
                cfg.io.vars.p = false;
            }
        }
        else
        {
            // 否则，先全部重置为 false
            cfg.io.vars = {false, false, false, false, false, false, false};
            std::stringstream ss(plt_vars);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                // 简单的去除前后空格 (Trim)
                size_t first = token.find_first_not_of(" \t");
                size_t last = token.find_last_not_of(" \t");
                if (first != std::string::npos && last != std::string::npos)
                {
                    token = token.substr(first, last - first + 1);
                }

                // 字符串匹配 -> 开启开关
                if (token == "rho")
                    cfg.io.vars.rho = true;
                else if (token == "u")
                    cfg.io.vars.u = true;
                else if (token == "v")
                    cfg.io.vars.v = true;
                else if (token == "w")
                    cfg.io.vars.w = true;
                else if (token == "p")
                    cfg.io.vars.p = true;
                else if (token == "eng")
                    cfg.io.vars.eng = true;
                else if (token == "species")
                    cfg.io.vars.species = true;
                // 未来扩展点：
                // else if (token == "enuc") cfg.io.vars.enuc = true;
            }
        }

        // 5. 自动搬运剩余参数到 Custom Params
        // 这是最关键的一步，保证了未来的扩展性
        for (const auto &[key, val_str] : parser.GetAllParams())
        {
            try
            {
                // 尝试转成 double 存起来
                double val = std::stod(val_str);
                cfg.custom_params[key] = val;
            }
            catch (...)
            {
                // 如果转不成 double (比如 "solver=VL")，忽略即可，
                // 因为核心字符串参数已经在上面处理过了。
                cfg.custom_string_params[key] = val_str;
            }
        }

        return cfg;
    }
};
