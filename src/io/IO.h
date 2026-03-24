/**
 * @file IO.h
 * @brief Handles data input/output operations for the simulation.
 * * Currently implements a simple CSV exporter for 1D flow data.
 * * It converts the internal conservative state (Density, Momentum, Total Energy)
 * * into human-readable primitive variables (Velocity, Pressure) during output.
 */

#pragma once

#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

#include "../data/GlobalDefs.h"
#include "../data/FluidState.h"

#include "../physics/species/Species.h"

#include "../grid/Grid.h"

/**
 * @brief Exports the current fluid state to a CSV file.
 * * Generates a filename with a zero-padded index (e.g., output_0001.csv)
 * and writes column-based data.
 * * @tparam EosType The Equation of State type used to recover Pressure from Energy.
 * @param state       The container holding conservative variables.
 * @param eos         Equation of State object.
 * @param grid        Grid topology (for coordinates).
 * @param file_index  The step/frame index for file naming (e.g., 10 -> "0010").
 * @param solver_name A string tag for the filename (e.g., "Sod").
 * @param specs       Species manager to retrieve component names.
 */

namespace fs = std::filesystem;

/**
 * @brief Exports the current fluid state to a CSV file.
 */
template <typename EosType>
void save_data(const FluidState &state, const EosType &eos,
               const Grid &grid, int file_index,
               const SimConfig &config,
               const SpeciesManager &specs)
{
    const auto &vars = config.io.vars;
    std::string dir_path = config.io.out_dir;
    // Auto mkdir for output path
    try
    {
        if (!fs::exists(dir_path))
        {
            fs::create_directories(dir_path);
        }
    }
    catch (const fs::filesystem_error &e)
    {
        std::cerr << "[IO Error] Could not create directory: " << dir_path
                  << ". Reason: " << e.what() << std::endl;
        return;
    }

    fs::path full_path = fs::path(dir_path) /
                         (config.io.base_name + "_" +
                          config.numerics.solver_name + "_" +
                          "");

    // 1. 构造文件名
    std::ostringstream oss;
    oss << config.io.base_name << "_" // 使用配置里的 base_name
        << config.numerics.solver_name << "_"
        << std::setw(4) << std::setfill('0') << file_index
        << ".csv";

    fs::path file_path = fs::path(dir_path) / oss.str();

    std::ofstream outFile(file_path);
    if (!outFile.is_open())
    {
        std::cerr << "Error: Could not open file " << file_path << std::endl;
        return;
    }

    // =========================================================
    // 2. Write Header (动态适配维度)
    // =========================================================
    outFile << "x";
    if (grid.dim >= 2)
        outFile << ",y";
    if (grid.dim == 3)
        outFile << ",z";

    if (vars.rho)
        outFile << ",rho";
    if (vars.u)
        outFile << ",u";
    if (vars.v && grid.dim >= 2)
        outFile << ",v";
    if (vars.w && grid.dim == 3)
        outFile << ",w";
    if (vars.p)
        outFile << ",p";
    if (vars.eng)
        outFile << ",eng";

    if (vars.species)
    {
        for (int k = 0; k < specs.count(); ++k)
            outFile << ",Y_" << specs.get_name(k);
    }
    outFile << "\n";

    // =========================================================
    // 3. Iterate Domain (3D 遍历)
    // =========================================================
    std::vector<double> Yi_temp(state.GetNumSpecies());

    for (int k = grid.Ks(); k < grid.Ke(); ++k)
    {
        for (int j = grid.Js(); j < grid.Je(); ++j)
        {
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {

                int idx = grid.GetIndex(i, j, k);
                FluidVector U = state.get(idx);

                // 坐标输出
                outFile << grid.GetCellCenterX(i);
                if (grid.dim >= 2)
                    outFile << "," << grid.GetCellCenterY(j);
                if (grid.dim == 3)
                    outFile << "," << grid.GetCellCenterZ(k);

                // 密度
                if (vars.rho)
                    outFile << "," << U.rho;

                // 速度计算
                double u_local = 0.0, v_local = 0.0, w_local = 0.0;
                if (U.rho > 1e-12)
                {
                    u_local = U.mom_x / U.rho;
                    v_local = U.mom_y / U.rho;
                    w_local = U.mom_z / U.rho;
                }

                if (vars.u)
                    outFile << "," << u_local;
                if (vars.v && grid.dim >= 2)
                    outFile << "," << v_local;
                if (vars.w && grid.dim == 3)
                    outFile << "," << w_local;

                // 压力与 EOS
                if (vars.p)
                {
                    for (int s = 0; s < state.GetNumSpecies(); ++s)
                        Yi_temp[s] = state.Y(s, idx);
                    // 使用更新后的 FluidVector 接口，内部自动扣除 3D 动能
                    double p = eos.get_pressure(U, Yi_temp.data());
                    outFile << "," << p;
                }

                // 能量与组分
                if (vars.eng)
                    outFile << "," << U.eng;
                if (vars.species)
                {
                    for (int s = 0; s < state.GetNumSpecies(); ++s)
                        outFile << "," << state.Y(s, idx);
                }

                outFile << "\n";
            }
        }
    }

    outFile.close();
    std::cout << "Saved: " << file_path.string() << std::endl;
}
