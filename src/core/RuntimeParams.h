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

#include "../data/GlobalDefs.h"
#include "../io/ConfigParser.h"

class RuntimeParams
{
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
        // 1. 搬运核心参数 (Grid)
        cfg.grid.nx = parser.GetInt("nx", 100);
        cfg.grid.ny = parser.GetInt("ny", 1);
        cfg.grid.nz = parser.GetInt("nz", 1);
        cfg.grid.x_min = parser.GetDouble("x_min", 0.0);
        cfg.grid.x_max = parser.GetDouble("x_max", 1.0);
        cfg.grid.geometry = parser.GetString("geometry", "cartesian");

        // 2. 搬运数值参数 (Numerics)
        cfg.numerics.solver_name = parser.GetString("solver", "SW");
        cfg.numerics.cfl = parser.GetDouble("cfl", 0.8);
        cfg.numerics.limiter = parser.GetString("limiter", "minmod");
        cfg.numerics.reconstruction = parser.GetString("reconstruct", "pcm");

        // 3. 搬运物理参数 (Physics)
        cfg.physics.eos_type = parser.GetString("eos", "ideal");
        cfg.physics.gamma = parser.GetDouble("gamma", 1.4);
        cfg.physics.use_burn = (parser.GetInt("use_burn", 0) != 0);

        // 4. 搬运IO参数 (IO)
        cfg.io.tmax = parser.GetDouble("tmax", 0.1);
        cfg.io.plt_interval = parser.GetDouble("plt_interval", 0.01);
        cfg.io.out_dir = parser.GetString("out_dir", "data");

        std::string plt_vars = parser.GetString("plt_variables", "all");

        if (plt_vars == "all")
        {
            // 如果是 all，全部开启
            cfg.io.vars = {true, true, true, true, true};
        }
        else
        {
            // 否则，先全部重置为 false
            cfg.io.vars = {false, false, false, false, false};
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