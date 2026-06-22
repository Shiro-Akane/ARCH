/**
 * @file Tabular4DEOS.h
 * @brief 4D Tabular Equation of State reading from HDF5 (rho, e, A_bar, Z_bar).
 * Designed specifically for Non-NSE astrophysical environments (e.g., Helmholtz EOS).
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

#ifndef EOS_INLINE
#define EOS_INLINE inline
#endif

// ====================================================================
// 1. Device View: 16 顶点四线性插值核心
// ====================================================================
struct Tabular4DEOSView
{
    // --- 表格维度与边界 (新增 A 和 Z) ---
    int n_rho, n_e, n_A, n_Z;
    double log_rho_min, log_rho_max, dlog_rho;
    double log_e_min, log_e_max, dlog_e;
    double A_min, A_max, dA;
    double Z_min, Z_max, dZ;

    // --- 数据裸指针 ---
    const double *table_P;
    const double *table_T;
    const double *table_cs;

    double *table_dP_drho;
    double *table_dP_de;

    const SpeciesManager *specs;

    // ========================================================
    // 核心：四线性插值 (Quadrilinear Interpolation)
    // ========================================================
    EOS_INLINE double interpolate_4d(const double *table, double rho, double e, double A, double Z) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;

        double x = log10(rho);
        double y = log10(e);
        double u = A;
        double v = Z;

        // 边界截断 (Clamping)
        x = fmax(log_rho_min, fmin(x, log_rho_max - 1e-6));
        y = fmax(log_e_min, fmin(y, log_e_max - 1e-6));
        u = fmax(A_min, fmin(u, A_max - 1e-6));
        v = fmax(Z_min, fmin(v, Z_max - 1e-6));

        // 计算基准索引
        int i = static_cast<int>((x - log_rho_min) / dlog_rho);
        int j = static_cast<int>((y - log_e_min) / dlog_e);
        int k = static_cast<int>((u - A_min) / dA);
        int l = static_cast<int>((v - Z_min) / dZ);

        // 计算局部偏移 [0, 1)
        double tx = (x - (log_rho_min + i * dlog_rho)) / dlog_rho;
        double ty = (y - (log_e_min + j * dlog_e)) / dlog_e;
        double tu = (u - (A_min + k * dA)) / dA;
        double tv = (v - (Z_min + l * dZ)) / dZ;

// 辅助宏：计算 4D 展平数组的 1D 索引 -> i*(Ne*Na*Nz) + j*(Na*Nz) + k*Nz + l
#define IDX(ii, jj, kk, ll) ((ii) * n_e * n_A * n_Z + (jj) * n_A * n_Z + (kk) * n_Z + (ll))

        // 降维折叠法：第一步，沿着 Z 轴插值，将 16 个顶点折叠为 8 个顶点
        double c000 = table[IDX(i, j, k, l)] * (1.0 - tv) + table[IDX(i, j, k, l + 1)] * tv;
        double c100 = table[IDX(i + 1, j, k, l)] * (1.0 - tv) + table[IDX(i + 1, j, k, l + 1)] * tv;
        double c010 = table[IDX(i, j + 1, k, l)] * (1.0 - tv) + table[IDX(i, j + 1, k, l + 1)] * tv;
        double c110 = table[IDX(i + 1, j + 1, k, l)] * (1.0 - tv) + table[IDX(i + 1, j + 1, k, l + 1)] * tv;
        double c001 = table[IDX(i, j, k + 1, l)] * (1.0 - tv) + table[IDX(i, j, k + 1, l + 1)] * tv;
        double c101 = table[IDX(i + 1, j, k + 1, l)] * (1.0 - tv) + table[IDX(i + 1, j, k + 1, l + 1)] * tv;
        double c011 = table[IDX(i, j + 1, k + 1, l)] * (1.0 - tv) + table[IDX(i, j + 1, k + 1, l + 1)] * tv;
        double c111 = table[IDX(i + 1, j + 1, k + 1, l)] * (1.0 - tv) + table[IDX(i + 1, j + 1, k + 1, l + 1)] * tv;
#undef IDX

        // 第二步：沿着 A 轴插值，将 8 个点折叠为 4 个点
        double c00 = c000 * (1.0 - tu) + c001 * tu;
        double c10 = c100 * (1.0 - tu) + c101 * tu;
        double c01 = c010 * (1.0 - tu) + c011 * tu;
        double c11 = c110 * (1.0 - tu) + c111 * tu;

        // 第三步：沿着 e 轴 (Y方向) 插值，将 4 个点折叠为 2 个点
        double c0 = c00 * (1.0 - ty) + c01 * ty;
        double c1 = c10 * (1.0 - ty) + c11 * ty;

        // 第四步：沿着 rho 轴 (X方向) 插值，得到最终 1 个结果
        return c0 * (1.0 - tx) + c1 * tx;
    }

    // ========================================================
    // 状态查询接口 (提取 A_bar 和 Z_bar)
    // ========================================================

    EOS_INLINE double get_Abar(const double *Yi) const
    {
        if (specs && specs->count() > 0)
            return specs->calc_Abar(Yi);
        return 14.0; // 兜底：假设纯氮
    }

    EOS_INLINE double get_Zbar(const double *Yi) const
    {
        if (specs && specs->count() > 0)
            return specs->calc_Zbar(Yi);
        return 7.0; // 兜底：假设纯氮
    }

    EOS_INLINE double get_pressure_from_rho_e(double rho, double e, const double *Yi) const
    {
        return interpolate_4d(table_P, rho, e, get_Abar(Yi), get_Zbar(Yi));
    }

    EOS_INLINE double get_temperature(double rho, double e, const double *Yi) const
    {
        return interpolate_4d(table_T, rho, e, get_Abar(Yi), get_Zbar(Yi));
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
        return interpolate_4d(table_cs, U.rho, e_int, get_Abar(Yi), get_Zbar(Yi));
    }

    EOS_INLINE double get_gamma(const double *Yi, double rho = 0.0, double e = 0.0) const
    {
        if (rho < 1e-12 || e < 1e-12)
            return 1.4;
        double p = interpolate_4d(table_P, rho, e, get_Abar(Yi), get_Zbar(Yi));
        double cs = interpolate_4d(table_cs, rho, e, get_Abar(Yi), get_Zbar(Yi));
        if (p < 1e-12)
            return 1.4;
        return (rho * cs * cs) / p;
    }

    EOS_INLINE double get_dp_drho_e(double rho, double e, const double *Yi) const
    {
        if (table_dP_drho)
            return interpolate_4d(table_dP_drho, rho, e, get_Abar(Yi), get_Zbar(Yi));

        double drho = rho * 0.001;
        return (interpolate_4d(table_P, rho + drho, e, get_Abar(Yi), get_Zbar(Yi)) -
                interpolate_4d(table_P, rho - drho, e, get_Abar(Yi), get_Zbar(Yi))) /
               (2.0 * drho);
    }

    EOS_INLINE double get_dp_de_rho(double rho, double e, const double *Yi) const
    {
        if (table_dP_de)
            return interpolate_4d(table_dP_de, rho, e, get_Abar(Yi), get_Zbar(Yi));

        double de = e * 0.001;
        return (interpolate_4d(table_P, rho, e + de, get_Abar(Yi), get_Zbar(Yi)) -
                interpolate_4d(table_P, rho, e - de, get_Abar(Yi), get_Zbar(Yi))) /
               (2.0 * de);
    }

    EOS_INLINE double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Yi) const
    {
        double e_guess = p / ((1.4 - 1.0) * rho);
        for (int iter = 0; iter < 20; ++iter)
        {
            double p_guess = interpolate_4d(table_P, rho, e_guess, get_Abar(Yi), get_Zbar(Yi));
            double dp_de = get_dp_de_rho(rho, e_guess, Yi);

            if (fabs(dp_de) < 1e-12)
                break;
            double delta_e = (p - p_guess) / dp_de;

            while (e_guess + delta_e <= 1e-12)
            {
                delta_e *= 0.5;
            }
            e_guess += delta_e;

            if (fabs(delta_e) < 1e-6 * e_guess)
                break;
        }
        return rho * e_guess + 0.5 * rho * (u * u + v * v + w * w);
    }
};

// ====================================================================
// 2. Host Manager: HDF5 4D 数据加载
// ====================================================================
struct Tabular4DEOS : public EOSBase
{
private:
    std::string table_path;
    std::vector<double> h_table_P;
    std::vector<double> h_table_T;
    std::vector<double> h_table_cs;
    std::vector<double> h_table_dP_drho;
    std::vector<double> h_table_dP_de;

    Tabular4DEOSView view;

public:
    Tabular4DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr)
        : table_path(h5_filename)
    {
        std::cout << "[Tabular4DEOS] Loading 4D HDF5 table: " << h5_filename << std::endl;
        HighFive::File file(h5_filename, HighFive::File::ReadOnly);

        file.getDataSet("n_rho").read(view.n_rho);
        file.getDataSet("n_e").read(view.n_e);
        file.getDataSet("n_A").read(view.n_A);
        file.getDataSet("n_Z").read(view.n_Z);

        file.getDataSet("log_rho_min").read(view.log_rho_min);
        file.getDataSet("log_rho_max").read(view.log_rho_max);
        file.getDataSet("log_e_min").read(view.log_e_min);
        file.getDataSet("log_e_max").read(view.log_e_max);
        file.getDataSet("A_min").read(view.A_min);
        file.getDataSet("A_max").read(view.A_max);
        file.getDataSet("Z_min").read(view.Z_min);
        file.getDataSet("Z_max").read(view.Z_max);

        view.dlog_rho = (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
        view.dlog_e = (view.log_e_max - view.log_e_min) / (view.n_e - 1);
        view.dA = (view.A_max - view.A_min) / (view.n_A - 1);
        view.dZ = (view.Z_max - view.Z_min) / (view.n_Z - 1);

        file.getDataSet("pressure").read(h_table_P);
        file.getDataSet("temperature").read(h_table_T);
        file.getDataSet("sound_speed").read(h_table_cs);

        try
        {
            file.getDataSet("dp_drho").read(h_table_dP_drho);
            view.table_dP_drho = h_table_dP_drho.data();
            file.getDataSet("dp_de").read(h_table_dP_de);
            view.table_dP_de = h_table_dP_de.data();
            std::cout << "[Tabular4DEOS] Loaded 4D analytical derivative tables." << std::endl;
        }
        catch (...)
        {
            view.table_dP_drho = nullptr;
            view.table_dP_de = nullptr;
            std::cout << "[Tabular4DEOS] No derivative tables found. Falling back to finite difference." << std::endl;
        }

        view.table_P = h_table_P.data();
        view.table_T = h_table_T.data();
        view.table_cs = h_table_cs.data();

        view.specs = specs_ptr;

        std::cout << "[Tabular4DEOS] 4D Table loaded successfully." << std::endl;
    }

    Tabular4DEOSView get_view() const { return view; }
};