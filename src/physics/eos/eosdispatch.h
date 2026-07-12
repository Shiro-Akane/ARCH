#pragma once

#include <string>
#include <stdexcept>
#include <iostream>

#include "eos.h"
#include "IdealGas.h"
#include "Tabular3DEOS.h"
#include "Tabular4DEOS.h"
#include "HelmEos.h"

#include "../../core/RuntimeParams.h"
#include "../species/Species.h"

#include "highfive/H5File.hpp"

struct EOSDispatcher
{
    /**
     * @brief 零开销静态分发器
     * 通过泛型 Lambda 将具体的 EOS 类型在编译期注入到求解器模板中
     */
    template <typename Func>
    static void dispatch_eos(const SimConfig &config, const SpeciesManager &specs, Func &&func)
    {
        std::string eos_type = config.physics.eos_type;
        std::cout << "[EOS Dispatch] Resolving EOS Policy: " << eos_type << std::endl;

        if (eos_type == "ideal" || eos_type == "Ideal")
        {
            // 对于 IdealGas，它本身既是 Manager 也是计算核心，直接传
            IdealGas eos(config.physics.gamma, specs);
            func(eos);
        }
        else if (eos_type == "tabular" || eos_type == "Tabular")
        {
            // 1. 获取路径
            std::string path = config.physics.eos_table_path;
            path.erase(std::remove(path.begin(), path.end(), '\"'), path.end());
            path.erase(std::remove(path.begin(), path.end(), '\''), path.end());
            if (path.empty())
            {
                throw std::runtime_error("Tabular EOS requires 'eos_table_path' in .par file!");
            }

            std::cout << "[EOS Dispatch] Inspecting HDF5 Metadata..." << std::endl;

            HighFive::File file(path, HighFive::File::ReadOnly);

            bool is_4d = file.exist("n_A") && file.exist("n_Z");

            if (is_4d)
            {
                std::cout << "[EOS Dispatch] 4D Helmholtz EOS format." << std::endl;

                // 实例化 4D 管理器并分发其 View
                Tabular4DEOS eos_manager(path, &specs);
                func(eos_manager.get_view());
            }
            else
            {
                std::cout << "[EOS Dispatch] 3D Tabular EOS format." << std::endl;

                // 实例化 3D 管理器并分发其 View
                Tabular3DEOS eos_manager(path, &specs);
                func(eos_manager.get_view());
            }
        }
        else if (eos_type == "helmholtz" || eos_type == "Helmholtz")
        {
            std::string path = config.physics.eos_table_path;
            path.erase(std::remove(path.begin(), path.end(), '\"'), path.end());
            path.erase(std::remove(path.begin(), path.end(), '\''), path.end());
            if (path.empty())
            {
                throw std::runtime_error("Helmholtz EOS requires 'eos_table_path' in .par file!");
            }
            HelmEos eos_manager(path, &specs);
            func(eos_manager);
        }
        else
        {
            throw std::runtime_error("Unknown EOS Type: " + eos_type);
        }
    }
};