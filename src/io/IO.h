/**
 * @file IO.h
 * @brief Handles data input/output operations for the simulation.
 * * Currently implements a simple CSV exporter for 1D flow data.
 * * It converts the internal conservative state (Density, Momentum, Total Energy)
 * * into human-readable primitive variables (Velocity, Pressure) during output.
 */

#pragma once

#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <vector>

#include <highfive/H5File.hpp>
#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>

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
using namespace HighFive;

// ======================================================================
// 辅助函数：获取计算域的 HDF5 维度 (Row-major: Z, Y, X)
// ======================================================================
inline std::vector<size_t> get_hdf5_dims(const Grid &grid)
{
    size_t nx = grid.nx;
    size_t ny = grid.dim >= 2 ? grid.ny : 1;
    size_t nz = grid.dim == 3 ? grid.nz : 1;
    if (grid.dim == 1)
        return {nx};
    if (grid.dim == 2)
        return {ny, nx};
    return {nz, ny, nx};
}

// ======================================================================
// 1. Plot 文件输出 (给人看/后处理)
// ======================================================================
template <typename EosType>
void write_plt(const FluidState &state, const EosType &eos,
               const Grid &grid, int file_index, double current_time,
               const SimConfig &config, const SpeciesManager &specs)
{
    // 确保输出目录存在
    if (!fs::exists(config.io.out_dir))
        fs::create_directories(config.io.out_dir);

    // 构造文件名 (例如: output/Sod_HLLC_plt_0001.h5)
    std::ostringstream oss;
    oss << config.io.out_dir << "/"
        << config.io.base_name << "_"
        << config.numerics.solver_name << "_plt_"
        << std::setw(4) << std::setfill('0') << file_index << ".h5";

    try
    {
        File file(oss.str(), File::ReadWrite | File::Create | File::Truncate);

        // 写表头属性
        file.createAttribute("time", current_time);
        file.createAttribute("dim", grid.dim);
        file.createAttribute("geometry", grid.geometry);

        Group grid_group = file.createGroup("Grid"); // 网格信息放在 "Grid" 组下
        Group data_group = file.createGroup("Data"); // 所有变量放在 "Data" 组下

        auto dims = get_hdf5_dims(grid);
        size_t total_cells = dims[0];
        if (dims.size() > 1)
            total_cells *= dims[1];
        if (dims.size() > 2)
            total_cells *= dims[2];

        std::vector<double> buffer(total_cells); // 用于暂存每个变量的一维数据

        std::vector<std::string> axis_names = grid.GetAxisNames(); // 例如 {"x"} 或 {"y", "x"} 或 {"z", "y", "x"}

        // 输出坐标轴数据
        std::vector<double> coord_x(grid.nx);
        for (int i = 0; i < grid.nx; ++i)
            coord_x[i] = grid.GetCellCenterX(grid.Is() + i);
        grid_group.createDataSet(axis_names[0], coord_x); // x 坐标

        // 只有在二维或三维时才输出 y 坐标
        if (grid.dim >= 2)
        {
            std::vector<double> coord_y(grid.ny);
            for (int j = 0; j < grid.ny; ++j)
                coord_y[j] = grid.GetCellCenterY(grid.Js() + j);
            grid_group.createDataSet(axis_names[1], coord_y); // y 坐标
        }

        // 只有在三维时才输出 z 坐标
        if (grid.dim == 3)
        {
            std::vector<double> coord_z(grid.nz);
            for (int k = 0; k < grid.nz; ++k)
                coord_z[k] = grid.GetCellCenterZ(grid.Ks() + k);
            grid_group.createDataSet(axis_names[2], coord_z); // z 坐标
        }

        // Lambda 表达式抽取数据
        auto write_var = [&](const std::string &name, auto extract_func)
        {
            size_t buf_idx = 0;
            for (int k = grid.Ks(); k < grid.Ke(); ++k)
            {
                for (int j = grid.Js(); j < grid.Je(); ++j)
                {
                    for (int i = grid.Is(); i < grid.Ie(); ++i)
                    {
                        buffer[buf_idx++] = extract_func(state, grid.GetIndex(i, j, k));
                    }
                }
            }
            DataSet ds = data_group.createDataSet<double>(name, DataSpace(dims));
            ds.write_raw(buffer.data());
        };

        const auto &vars = config.io.vars;
        if (vars.rho)
        {
            write_var("rho", [](const FluidState &s, int idx)
                      { return s.get(idx).rho; });
        }
        if (vars.p)
        {
            std::vector<double> Xi_temp(state.GetNumSpecies());
            write_var("p", [&](const FluidState &s, int idx)
                      {
                for(int k=0; k<s.GetNumSpecies(); ++k) Xi_temp[k] = s.X(k, idx);
                return eos.get_pressure(s.get(idx), Xi_temp.data()); });
        }
        if (vars.eng)
        {
            write_var("eng", [](const FluidState &s, int idx)
                      { return s.get(idx).eng; });
        }
        if (vars.u)
        {
            write_var("u", [](const FluidState &s, int idx)
                      {
                auto U = s.get(idx);
                return U.rho > 1e-12 ? U.mom_x / U.rho : 0.0; });
        }
        if (vars.v && grid.dim >= 2)
        {
            write_var("v", [](const FluidState &s, int idx)
                      {
                auto U = s.get(idx);
                return U.rho > 1e-12 ? U.mom_y / U.rho : 0.0; });
        }
        if (vars.w && grid.dim == 3)
        {
            write_var("w", [](const FluidState &s, int idx)
                      {
                auto U = s.get(idx);
                return U.rho > 1e-12 ? U.mom_z / U.rho : 0.0; });
        }
        if (vars.species)
        {
            std::vector<double> Xi_temp(state.GetNumSpecies());
            for (int k = 0; k < specs.count(); ++k)
            {
                std::string var_name = "X_" + specs.get_name(k);
                write_var(var_name, [k, &Xi_temp](const FluidState &s, int idx)
                          {
                    Xi_temp[k] = s.X(k, idx);
                    return Xi_temp[k]; });
            }
        }

        std::cout << "[IO] Saved PLT: " << oss.str() << " at t=" << current_time << std::endl;
    }
    catch (Exception &err)
    {
        std::cerr << "[IO Error] PLT write failed: " << err.what() << std::endl;
    }
}
// ======================================================================
// 2. Checkpoint 文件输出 (断点重启)
// ======================================================================
inline void write_chk(const FluidState &state, const Grid &grid,
                      int chk_file_index, int plt_file_index,
                      int step_count, double current_time,
                      const SimConfig &config)
{
    if (!fs::exists(config.io.out_dir))
        fs::create_directories(config.io.out_dir);

    std::ostringstream oss;
    oss << config.io.out_dir << "/" << config.io.base_name << "_chk_"
        << std::setw(4) << std::setfill('0') << chk_file_index << ".h5";

    try
    {
        File file(oss.str(), File::ReadWrite | File::Create | File::Truncate);
        // 1. 写入时间、步数、索引等元数据
        file.createAttribute("time", current_time);
        file.createAttribute("step", step_count);
        file.createAttribute("ng", grid.ng);
        file.createAttribute("total_size", grid.total_size);
        file.createAttribute("chk_index", chk_file_index);
        file.createAttribute("plt_index", plt_file_index);

        // 2. 极速写入 SoA 数组 (直接传入 std::vector，HighFive 会自动处理)
        file.createDataSet("rho", state.rho);
        file.createDataSet("mom_x", state.mom_x);
        file.createDataSet("mom_y", state.mom_y);
        file.createDataSet("mom_z", state.mom_z);
        file.createDataSet("eng", state.eng);

        if (state.GetNumSpecies() > 0)
        {
            file.createDataSet("mass_fractions", state.mass_fractions);
        }

        std::cout << "[IO] Saved CHK: " << oss.str() << " at step " << step_count << std::endl;
    }
    catch (Exception &err)
    {
        std::cerr << "[IO Error] CHK write failed: " << err.what() << std::endl;
    }
}

// ======================================================================
// 3. Checkpoint 文件读取 (断点重启载入)
// ======================================================================
inline void read_chk(const std::string &filepath, FluidState &state, const Grid &grid,
                     RunState &run_state)
{
    try
    {
        File file(filepath, File::ReadOnly);

        // 1. 验证网格大小是否匹配 (防止用 400x400 的文件去重启 800x800 的网格)
        int file_total_size = 0;
        file.getAttribute("total_size").read(file_total_size);
        if (file_total_size != grid.total_size)
        {
            throw std::runtime_error("Restart grid total_size mismatch!");
        }

        // 2. 恢复元数据
        file.getAttribute("time").read(run_state.time);
        file.getAttribute("step").read(run_state.step);
        file.getAttribute("chk_index").read(run_state.chk_idx);
        file.getAttribute("plt_index").read(run_state.plt_idx);

        // 3. 恢复 SoA 数组数据
        file.getDataSet("rho").read(state.rho);
        file.getDataSet("mom_x").read(state.mom_x);
        file.getDataSet("mom_y").read(state.mom_y);
        file.getDataSet("mom_z").read(state.mom_z);
        file.getDataSet("eng").read(state.eng);

        if (state.GetNumSpecies() > 0 && file.exist("mass_fractions"))
        {
            file.getDataSet("mass_fractions").read(state.mass_fractions);
        }

        std::cout << "[IO] Successfully restarted from: " << filepath
                  << " | t = " << run_state.time << ", step = " << run_state.step << std::endl;
    }
    catch (Exception &err)
    {
        std::cerr << "[IO Fatal] Failed to read restart file: " << err.what() << std::endl;
        exit(1);
    }
}