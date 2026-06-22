/**
 * @file Tabular3DEOS.h
 * @brief Tabular Equation of State reading from HDF5.
 * Implements Host-Device View pattern for CUDA compatibility
 * and provides thermodynamic states (T, Ye) for nuclear reaction networks.
 */
#pragma once

#include "eos.h"
#include <string>
#include <vector>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "highfive/H5File.hpp"
#include "../species/Species.h"

// 跨平台宏定义：在纯 CPU 环境下展开为 inline
// 未来引入 CUDA 时，在 CMake 中注入 #define EOS_INLINE __host__ __device__ inline
#ifndef EOS_INLINE
#define EOS_INLINE inline
#endif

// ====================================================================
// 1. Device View: 零拷贝、纯裸指针计算核心 (天然支持 GPU 核函数)
// ====================================================================
struct Tabular3DEOSView
{
    // --- 表格维度与边界 ---
    int n_rho, n_e, n_Y;
    double log_rho_min, log_rho_max, dlog_rho;
    double log_e_min, log_e_max, dlog_e;
    double Y_min, Y_max, dY; // Range of Mass fraction Y

    // --- 数据裸指针 (GPU 可见内存) ---
    const double *table_P;
    const double *table_T;
    const double *table_cs;

    double *table_dP_drho; // 可选的预计算偏导数表
    double *table_dP_de;   // 可选的预计算偏导数表

    const SpeciesManager *specs;

    int target_species_id;

    EOS_INLINE double interpolate_3d(const double *table, double rho, double e, double Y) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;

        double x = log10(rho);
        double y = log10(e);
        double z = Y;

        // 边界截断 (Clamping)
        x = fmax(log_rho_min, fmin(x, log_rho_max - 1e-6));
        y = fmax(log_e_min, fmin(y, log_e_max - 1e-6));
        z = fmax(Y_min, fmin(z, Y_max - 1e-6));

        // 计算索引
        int i = static_cast<int>((x - log_rho_min) / dlog_rho);
        int j = static_cast<int>((y - log_e_min) / dlog_e);
        int k = static_cast<int>((z - Y_min) / dY);

        // 计算局部偏移 [0, 1)
        double tx = (x - (log_rho_min + i * dlog_rho)) / dlog_rho;
        double ty = (y - (log_e_min + j * dlog_e)) / dlog_e;
        double tz = (z - (Y_min + k * dY)) / dY;

// 辅助宏：计算 1D 展平数组的索引 (i, j, k) -> i * (n_e * n_Y) + j * n_Y + k
#define IDX(ii, jj, kk) ((ii) * n_e * n_Y + (jj) * n_Y + (kk))

        // 获取 8 个顶点的函数值
        double c000 = table[IDX(i, j, k)];
        double c100 = table[IDX(i + 1, j, k)];
        double c010 = table[IDX(i, j + 1, k)];
        double c110 = table[IDX(i + 1, j + 1, k)];
        double c001 = table[IDX(i, j, k + 1)];
        double c101 = table[IDX(i + 1, j, k + 1)];
        double c011 = table[IDX(i, j + 1, k + 1)];
        double c111 = table[IDX(i + 1, j + 1, k + 1)];
#undef IDX

        // 沿 X 轴插值
        double c00 = c000 * (1.0 - tx) + c100 * tx;
        double c01 = c001 * (1.0 - tx) + c101 * tx;
        double c10 = c010 * (1.0 - tx) + c110 * tx;
        double c11 = c011 * (1.0 - tx) + c111 * tx;

        // 沿 Y 轴插值
        double c0 = c00 * (1.0 - ty) + c10 * ty;
        double c1 = c01 * (1.0 - ty) + c11 * ty;

        // 沿 Z 轴插值，得到最终结果
        return c0 * (1.0 - tz) + c1 * tz;
    }

    // ========================================================
    // 状态查询接口 (含 pynucastro 预留的 Yi)
    // ========================================================

    EOS_INLINE double get_target_Y(const double *Yi) const
    {
        // 1. 最高优先级：如果是普通的双组分测试，直接提取目标质量分数
        if (target_species_id >= 0)
        {
            return Yi[target_species_id];
        }

        // 2. 次优先级：如果没指定特定组分，且挂载了 specs，则计算天体物理的 Ye
        if (specs && specs->count() > 0)
        {
            return specs->calc_Ye(Yi);
        }

        // 3. 兜底
        return 0.5;
    }

    EOS_INLINE double get_pressure_from_rho_e(double rho, double e, const double *Yi) const
    {
        // 直接从表格插值压力，确保与 get_pressure() 和 get_sound_speed() 的一致性
        return interpolate_3d(table_P, rho, e, get_target_Y(Yi));
    }

    // 提取温度，用于驱动 Alpha-chain 等核反应网络
    EOS_INLINE double get_temperature(double rho, double e, const double *Yi) const
    {
        return interpolate_3d(table_T, rho, e, get_target_Y(Yi));
    }

    EOS_INLINE double get_pressure(const FluidVector &U, const double *Yi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        double e_int = (U.eng - 0.5 * (U.mom_x * U.mom_x + U.mom_y * U.mom_y + U.mom_z * U.mom_z) / U.rho) / U.rho;
        return get_pressure_from_rho_e(U.rho, e_int, Yi);
    }

    EOS_INLINE double get_sound_speed(const FluidVector &U, double p, const double *Yi) const
    {
        if (U.rho < 1e-12)
            return 0.0;
        double e_int = (U.eng - 0.5 * (U.mom_x * U.mom_x + U.mom_y * U.mom_y + U.mom_z * U.mom_z) / U.rho) / U.rho;
        return interpolate_3d(table_cs, U.rho, e_int, get_target_Y(Yi));
    }

    EOS_INLINE double get_gamma(const double *Yi, double rho = 0.0, double e = 0.0) const
    {
        if (rho < 1e-12 || e < 1e-12)
            return 1.4;
        double p = interpolate_3d(table_P, rho, e, get_target_Y(Yi));
        double cs = interpolate_3d(table_cs, rho, e, get_target_Y(Yi));
        if (p < 1e-12)
            return 1.4;
        return (rho * cs * cs) / p;
    }

    // ========================================================
    // 导数接口 (支持读取真实导数表或回退有限差分)
    // ========================================================
    EOS_INLINE double get_dp_drho_e(double rho, double e, const double *Yi) const
    {
        if (table_dP_drho)
        {
            return interpolate_3d(table_dP_drho, rho, e, get_target_Y(Yi));
        }
        double drho = rho * 0.001;
        return (interpolate_3d(table_P, rho + drho, e, get_target_Y(Yi)) - interpolate_3d(table_P, rho - drho, e, get_target_Y(Yi))) / (2.0 * drho);
    }

    EOS_INLINE double get_dp_de_rho(double rho, double e, const double *Yi) const
    {
        if (table_dP_de)
        {
            return interpolate_3d(table_dP_de, rho, e, get_target_Y(Yi));
        }
        double de = e * 0.001;
        return (interpolate_3d(table_P, rho, e + de, get_target_Y(Yi)) - interpolate_3d(table_P, rho, e - de, get_target_Y(Yi))) / (2.0 * de);
    }

    // ========================================================
    // 鲁棒的阻尼牛顿法反推总能
    // ========================================================
    EOS_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Yi) const
    {
        double e_guess = p / ((1.4 - 1.0) * rho);

        for (int iter = 0; iter < 20; ++iter)
        {
            double p_guess = interpolate_3d(table_P, rho, e_guess, get_target_Y(Yi));
            double dp_de = get_dp_de_rho(rho, e_guess, Yi);

            if (fabs(dp_de) < 1e-12)
                break;

            double delta_e = (p - p_guess) / dp_de;

            // 阻尼处理：防止极端压力梯度导致内能变为非物理的负数
            while (e_guess + delta_e <= 1e-12)
            {
                delta_e *= 0.5;
            }

            e_guess += delta_e;

            if (fabs(delta_e) < 1e-6 * e_guess)
                break;
        }

        double e_kinetic = 0.5 * rho * (u * u + v * v + w * w);
        return rho * e_guess + e_kinetic;
    }
};

// ====================================================================
// 2. Host Manager: 负责 HDF5 IO 和内存生命周期管理 (绝不进入内层循环)
// ====================================================================
struct Tabular3DEOS : public EOSBase
{
private:
    std::string table_path;
    std::vector<double> h_table_P;
    std::vector<double> h_table_T;
    std::vector<double> h_table_cs;

    // 预留
    std::vector<double> h_table_dP_drho;
    std::vector<double> h_table_dP_de;

    Tabular3DEOSView view;

public:
    Tabular3DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr)
        : table_path(h5_filename)
    {
        std::cout << "[Tabular3DEOS] Loading HDF5 table: " << h5_filename << std::endl;
        HighFive::File file(h5_filename, HighFive::File::ReadOnly);

        file.getDataSet("n_rho").read(view.n_rho);
        file.getDataSet("n_e").read(view.n_e);
        file.getDataSet("n_Y").read(view.n_Y);

        file.getDataSet("log_rho_min").read(view.log_rho_min);
        file.getDataSet("log_rho_max").read(view.log_rho_max);
        file.getDataSet("log_e_min").read(view.log_e_min);
        file.getDataSet("log_e_max").read(view.log_e_max);
        file.getDataSet("Y_min").read(view.Y_min);
        file.getDataSet("Y_max").read(view.Y_max);

        view.dlog_rho = (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
        view.dlog_e = (view.log_e_max - view.log_e_min) / (view.n_e - 1);
        view.dY = (view.Y_max - view.Y_min) / (view.n_Y - 1);

        file.getDataSet("pressure").read(h_table_P);
        file.getDataSet("temperature").read(h_table_T);
        file.getDataSet("sound_speed").read(h_table_cs);

        // 尝试加载偏导数表（如果不存在则捕获异常，保持为 nullptr 回退到有限差分）
        try
        {
            file.getDataSet("dp_drho").read(h_table_dP_drho);
            view.table_dP_drho = h_table_dP_drho.data();
            file.getDataSet("dp_de").read(h_table_dP_de);
            view.table_dP_de = h_table_dP_de.data();
            std::cout << "[Tabular3DEOS] Loaded EOS tables." << std::endl;
        }
        catch (...)
        {
            view.table_dP_drho = nullptr;
            view.table_dP_de = nullptr;
            std::cout << "[Tabular3DEOS] No tables found. Falling back to finite difference." << std::endl;
        }

        // 挂载指针到底层 Vector
        view.table_P = h_table_P.data();
        view.table_T = h_table_T.data();
        view.table_cs = h_table_cs.data();

        view.specs = specs_ptr;
        view.target_species_id = 1;

        std::cout << "[Tabular3DEOS] Table loaded successfully." << std::endl;
    }

    // CFD 求解器分发时，只获取 View
    Tabular3DEOSView get_view() const { return view; }
};