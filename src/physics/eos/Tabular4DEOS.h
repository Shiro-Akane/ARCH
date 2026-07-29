/**
 * @file Tabular4DEOS.h
 * @brief 4D Tabular Equation of State reading from HDF5 (rho, e, A_bar, Z_bar).
 * Designed specifically for Non-NSE astrophysical environments (e.g., Helmholtz EOS).
 */
#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "highfive/H5File.hpp"

#include "eos_Utils.h"
#include "eos.h"

#include "../species/Species.h"

// ====================================================================
// 1. Device View: 16 顶点四线性插值核心
// ====================================================================
struct Tabular4DEOSView
{
    // --- 表格维度与边界 (新增 A 和 Z) ---
    int n_rho, n_T, n_A, n_Z;
    double log_rho_min, log_rho_max, dlog_rho;
    double log_T_min, log_T_max, dlog_T;
    double A_min, A_max, dA;
    double Z_min, Z_max, dZ;

    // --- 数据裸指针 ---
    const double *table_P;
    const double *table_E;
    const double *table_cs;
    const double *table_cv;

    double *table_dP_drho;
    double *table_dP_dT;

    const SpeciesManager *specs;

    static constexpr double k_B_cgs = 1.380649e-16; // erg/K
    static constexpr double m_u_cgs = 1.660539e-24; // g

    // ========================================================
    // 边界检测与解析回退 (Ideal Gas Fallback)
    // ========================================================

    // 检查是否超出插值表范围
    bool is_out_of_bounds(double log_rho, double log_T, double A, double Z) const
    {
        // 允许边界内极小误差 (1e-6)
        return (log_rho < log_rho_min || log_rho >= log_rho_max - 1e-6 ||
                log_T < log_T_min || log_T >= log_T_max - 1e-6 ||
                A < A_min || A >= A_max - 1e-6 ||
                Z < Z_min || Z >= Z_max - 1e-6);
    }

    // 解析推导: 获取等效 Gamma (单原子理想气体通常为 5/3)
    double fallback_gamma() const { return 5.0 / 3.0; }

    double fallback_pressure(double rho, double e) const
    {
        return rho * e * (fallback_gamma() - 1.0);
    }

    double fallback_temperature(double e, double Abar) const
    {
        double R_spec = k_B_cgs / (Abar * m_u_cgs);
        return e * (fallback_gamma() - 1.0) / R_spec;
    }

    double fallback_sound_speed(double rho, double e) const
    {
        double p = fallback_pressure(rho, e);
        return std::sqrt(fallback_gamma() * p / rho);
    }

    // ========================================================
    // 核心：四线性插值 (Quadrilinear Interpolation)
    // ========================================================
    double interpolate_4d(const double *table, double rho, double T, double A, double Z) const
    {
        if (rho <= 1e-12 || T <= 1e-12)
            return 0.0;

        double x = log10(rho);
        double y = log10(T);
        double u = A;
        double v = Z;

        // 边界检查交给上层调用函数处理
        // 此处严格要求传入的 (x, y, u, v) 已在界内
        int i = static_cast<int>((x - log_rho_min) / dlog_rho);
        int j = static_cast<int>((y - log_T_min) / dlog_T);
        int k = static_cast<int>((u - A_min) / dA);
        int l = static_cast<int>((v - Z_min) / dZ);

        // 防御性越界保护 (防止浮点精度导致的下标溢出)
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_A - 2));
        l = std::max(0, std::min(l, n_Z - 2));

        // 计算局部偏移 [0, 1)
        double tx = (x - (log_rho_min + i * dlog_rho)) / dlog_rho;
        double ty = (y - (log_T_min + j * dlog_T)) / dlog_T;
        double tu = (u - (A_min + k * dA)) / dA;
        double tv = (v - (Z_min + l * dZ)) / dZ;

// 辅助宏：计算 4D 展平数组的 1D 索引 -> i*(NT*Na*Nz) + j*(Na*Nz) + k*Nz + l
#define IDX(ii, jj, kk, ll) ((ii) * n_T * n_A * n_Z + (jj) * n_A * n_Z + (kk) * n_Z + (ll))

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

    double get_Abar(const double *Xi) const
    {
        if (specs && specs->count() > 0)
            return specs->calc_Abar(Xi);
        return 14.0; // 兜底：假设纯氮
    }

    double get_Zbar(const double *Xi) const
    {
        if (specs && specs->count() > 0)
            return specs->calc_Zbar(Xi);
        return 7.0; // 兜底：假设纯氮
    }

    double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    {
        if (rho <= 1e-12 || T <= 1e-12)
            return 0.0;
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_pressure(rho, e);
        }
        return interpolate_4d(table_P, rho, T, A, Z);
    }

    double get_pressure_from_rho_e(double rho, double e, const double *Xi) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;
        double T = get_temperature(rho, e, Xi);
        return get_pressure_from_rho_T(rho, T, Xi);
    }

    double get_eint_from_T(double rho, double T_target, const double *Xi) const
    {
        if (rho <= 1e-12 || T_target <= 1e-12)
            return 0.0;

        double A = get_Abar(Xi);
        double Z = get_Zbar(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), A, Z))
        {
            double R_spec = k_B_cgs / (A * m_u_cgs);
            return T_target * R_spec / (fallback_gamma() - 1.0);
        }

        return interpolate_4d(table_E, rho, T_target, A, Z);
    }

    double get_cv(double rho, double T_target, const double *Xi) const
    {
        if (rho <= 1e-12 || T_target <= 1e-12)
            return 0.0;

        double A = get_Abar(Xi);
        double Z = get_Zbar(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), A, Z))
        {
            double R_spec = k_B_cgs / (A * m_u_cgs);
            return R_spec / (fallback_gamma() - 1.0);
        }

        return interpolate_4d(table_cv, rho, T_target, A, Z);
    }

    double get_temperature(double rho, double e, const double *Xi) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;
            
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T_min = std::pow(10, log_T_min);
        double T_max = std::pow(10, log_T_max);
        
        // Out of bounds check for density or composition
        if (std::log10(rho) < log_rho_min || std::log10(rho) >= log_rho_max ||
            A < A_min || A >= A_max || Z < Z_min || Z >= Z_max)
        {
            return fallback_temperature(e, A);
        }
        
        // Fast boundary check: if e is below the minimum table energy, return T_min
        double e_min_table = interpolate_4d(table_E, rho, T_min, A, Z);
        if (e <= e_min_table) {
            return T_min;
        }
        
        // Newton-Raphson iteration
        double T_guess = 1e8; // reasonable astrophysics start
        const int max_iters = 20;
        const double tol = 1e-6;
        
        for (int i = 0; i < max_iters; ++i) {
            T_guess = std::max(T_min, std::min(T_guess, T_max));
            
            double e_eval = interpolate_4d(table_E, rho, T_guess, A, Z);
            double cv_eval = interpolate_4d(table_cv, rho, T_guess, A, Z);
            
            if (cv_eval <= 0.0) {
                // Finite difference fallback
                double dT_fd = T_guess * 0.01;
                double e_plus = interpolate_4d(table_E, rho, T_guess + dT_fd, A, Z);
                cv_eval = (e_plus - e_eval) / dT_fd;
                if (cv_eval <= 0.0) cv_eval = e_eval / T_guess;
            }
            
            double f = e_eval - e;
            double dT = -f / cv_eval;
            
            // Limit step size to avoid divergence (max 50% change)
            if (dT > 0.5 * T_guess) dT = 0.5 * T_guess;
            if (dT < -0.5 * T_guess) dT = -0.5 * T_guess;
            
            T_guess += dT;
            
            if (std::abs(dT) / T_guess < tol) break;
        }
        
        return T_guess;
    }

    double get_pressure(const FluidVector &U, const double *Xi) const
    {
        double e_int = eos_utils::extract_specific_internal_energy(U);
        return get_pressure_from_rho_e(U.rho, e_int, Xi);
    }

    double get_sound_speed(const FluidVector &U, double p, const double *Xi) const
    {
        double e_int = eos_utils::extract_specific_internal_energy(U);
        if (U.rho <= 1e-12 || e_int <= 1e-12)
            return 0.0;

        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T = get_temperature(U.rho, e_int, Xi);
        if (is_out_of_bounds(std::log10(U.rho), std::log10(T), A, Z))
        {
            return fallback_sound_speed(U.rho, e_int);
        }
        return interpolate_4d(table_cs, U.rho, T, A, Z);
    }

    double get_gamma(const double *Xi, double rho = 0.0, double e = 0.0) const
    {
        if (rho < 1e-12 || e < 1e-12)
            return fallback_gamma();
        double p = get_pressure_from_rho_e(rho, e, Xi);
        if (p < 1e-12)
            return fallback_gamma();

        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T = get_temperature(rho, e, Xi);
        return get_sound_speed_from_rho_T(rho, T, Xi);
    }
    
    double get_sound_speed_from_rho_T(double rho, double T, const double *Xi) const
    {
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_sound_speed(rho, e);
        }
        return interpolate_4d(table_cs, rho, T, A, Z);
    }

    double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        double T = get_temperature(rho, e, Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), A, Z))
        {
            return e * (fallback_gamma() - 1.0); // 解析偏导数 dP/drho
        }

        if (table_dP_drho)
            return interpolate_4d(table_dP_drho, rho, T, A, Z);

        double drho = rho * 0.001;
        return (interpolate_4d(table_P, rho + drho, T, A, Z) -
                interpolate_4d(table_P, rho - drho, T, A, Z)) /
               (2.0 * drho);
    }

    double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        double A = get_Abar(Xi), Z = get_Zbar(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(e), A, Z))
        {
            return rho * (fallback_gamma() - 1.0); // 解析偏导数 dP/de
        }

        double T = get_temperature(rho, e, Xi);
        if (table_dP_dT && table_cv) {
            double dp_dT = interpolate_4d(table_dP_dT, rho, T, A, Z);
            double cv = interpolate_4d(table_cv, rho, T, A, Z);
            if (cv > 0.0) return dp_dT / cv;
        }

        double de = e * 0.001;
        double T_plus = get_temperature(rho, e + de, Xi);
        double T_minus = get_temperature(rho, e - de, Xi);
        return (interpolate_4d(table_P, rho, T_plus, A, Z) -
                interpolate_4d(table_P, rho, T_minus, A, Z)) /
               (2.0 * de);
    }

    double get_total_energy_primitive(double rho, double u, double v, double w, double p, const double *Xi) const
    {
        return eos_utils::solve_total_energy(*this, rho, u, v, w, p, Xi);
    }

    double get_eta(double rho, double T, const double* Xi) const { return 0.0; }

    // =========================================================
    // Pipeline: evaluate_state
    // =========================================================
    void evaluate_state(eos_state_t& state) const {
        // =========================================================
        // 1. Core Thermodynamics (P, E, cv)
        // =========================================================
        state.P = get_pressure_from_rho_T(state.rho, state.T, state.Xi);
        state.E = get_eint_from_T(state.rho, state.T, state.Xi);
        state.cv = get_cv(state.rho, state.T, state.Xi);
        
        // =========================================================
        // 2. Derivatives and Sound Speed
        // =========================================================
        state.sound_speed = get_sound_speed_from_rho_T(state.rho, state.T, state.Xi);
        state.dp_drho = get_dp_drho_e(state.rho, state.E, state.Xi);
        state.dp_dT = 0.0; 
        if (table_dP_dT) {
            double A = get_Abar(state.Xi), Z = get_Zbar(state.Xi);
            state.dp_dT = interpolate_4d(table_dP_dT, state.rho, state.T, A, Z);
        }
        
        // =========================================================
        // 3. Deep Physical Variables (Unused in Tabular)
        // =========================================================
        state.pele = 0.0;
        state.xne = 0.0;
        state.eta = 0.0;
    }

    const SpeciesManager* get_species_manager() const { return specs; }
};

// ====================================================================
// 2. Host Manager: HDF5 4D 数据加载
// ====================================================================
struct Tabular4DEOS : public EOSBase
{
private:
    std::string table_path;
    std::vector<double> h_table_P;
    std::vector<double> h_table_E;
    std::vector<double> h_table_cs;
    std::vector<double> h_table_cv;
    std::vector<double> h_table_dP_drho;
    std::vector<double> h_table_dP_dT;

    Tabular4DEOSView view;

public:
    Tabular4DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr)
        : table_path(h5_filename)
    {
        std::cout << "[Tabular4DEOS] Loading 4D HDF5 table: " << h5_filename << std::endl;
        HighFive::File file(h5_filename, HighFive::File::ReadOnly);

        file.getDataSet("n_rho").read(view.n_rho);
        file.getDataSet("n_T").read(view.n_T);
        file.getDataSet("n_A").read(view.n_A);
        file.getDataSet("n_Z").read(view.n_Z);

        file.getDataSet("log_rho_min").read(view.log_rho_min);
        file.getDataSet("log_rho_max").read(view.log_rho_max);
        file.getDataSet("log_T_min").read(view.log_T_min);
        file.getDataSet("log_T_max").read(view.log_T_max);
        file.getDataSet("A_min").read(view.A_min);
        file.getDataSet("A_max").read(view.A_max);
        file.getDataSet("Z_min").read(view.Z_min);
        file.getDataSet("Z_max").read(view.Z_max);

        view.dlog_rho = (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
        view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
        view.dA = (view.A_max - view.A_min) / (view.n_A - 1);
        view.dZ = (view.Z_max - view.Z_min) / (view.n_Z - 1);

        file.getDataSet("pressure").read(h_table_P);
        file.getDataSet("energy").read(h_table_E);
        file.getDataSet("sound_speed").read(h_table_cs);
        file.getDataSet("cv").read(h_table_cv);

        try
        {
            file.getDataSet("dp_drho").read(h_table_dP_drho);
            view.table_dP_drho = h_table_dP_drho.data();
            file.getDataSet("dp_dT").read(h_table_dP_dT);
            view.table_dP_dT = h_table_dP_dT.data();
            std::cout << "[Tabular4DEOS] Loaded 4D analytical derivative tables." << std::endl;
        }
        catch (...)
        {
            view.table_dP_drho = nullptr;
            view.table_dP_dT = nullptr;
            std::cout << "[Tabular4DEOS] No derivative tables found. Falling back to finite difference." << std::endl;
        }

        view.table_P = h_table_P.data();
        view.table_E = h_table_E.data();
        view.table_cs = h_table_cs.data();
        view.table_cv = h_table_cv.data();

        view.specs = specs_ptr;

        std::cout << "[Tabular4DEOS] 4D Table loaded successfully." << std::endl;
    }

    Tabular4DEOSView get_view() const { return view; }
    const SpeciesManager* get_species_manager() const { return view.specs; }
};