/**
 * @file Tabular3DEOS.h
 * @brief Tabular Equation of State reading from HDF5.
 * Implements Host-Device View pattern for CUDA compatibility
 * and provides thermodynamic states (T, Ye) for nuclear reaction networks.
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
// 1. Device View: 零拷贝、纯裸指针计算核心 (天然支持 GPU 核函数)
// ====================================================================
struct Tabular3DEOSView
{
    // --- 表格维度与边界 ---
    int n_rho, n_T, n_X;
    double log_rho_min, log_rho_max, dlog_rho;
    double log_T_min, log_T_max, dlog_T;
    double X_min, X_max, dX; // Range of Mass fraction X

    // --- 数据裸指针 (GPU 可见内存) ---
    const double *table_P;
    const double *table_E;
    const double *table_cs;
    const double *table_cv;

    double *table_dP_drho; // 可选的预计算偏导数表
    double *table_dP_dT;   // 可选的预计算偏导数表

    const SpeciesManager *specs;

    int target_species_id;

    // 物理常数 (CGS 单位制)
    static constexpr double k_B_cgs = 1.380649e-16; // erg/K
    static constexpr double m_u_cgs = 1.660539e-24; // g

    // ========================================================
    // 边界检测与解析回退 (Ideal Gas Fallback)
    // ========================================================

    bool is_out_of_bounds(double log_rho, double log_T, double X) const
    {
        return (log_rho < log_rho_min || log_rho >= log_rho_max - 1e-6 ||
                log_T < log_T_min || log_T >= log_T_max - 1e-6 ||
                X < X_min || X >= X_max - 1e-6);
    }

    double fallback_gamma() const { return 5.0 / 3.0; } // 假设单原子理想气体

    double fallback_pressure(double rho, double e) const
    {
        return rho * e * (fallback_gamma() - 1.0);
    }

    double fallback_temperature(double e, const double *Xi) const
    {
        // 尝试获取 Abar，如果失败则给一个合理的默认值 (例如 1.0 代表纯氢)
        double Abar = (specs && specs->count() > 0) ? specs->calc_Abar(Xi) : 1.0;
        double R_spec = k_B_cgs / (Abar * m_u_cgs);
        return e * (fallback_gamma() - 1.0) / R_spec;
    }

    double fallback_sound_speed(double rho, double e) const
    {
        double p = fallback_pressure(rho, e);
        return std::sqrt(fallback_gamma() * p / rho);
    }

    // ========================================================
    // 核心：三线性插值 (Trilinear Interpolation)
    // ========================================================
    double interpolate_3d(const double *table, double rho, double T, double X) const
    {
        if (rho <= 1e-12 || T <= 1e-12)
            return 0.0;

        double x = log10(rho);
        double y = log10(T);
        double z = X;

        // 边界截断 (Clamping)
        int i = static_cast<int>((x - log_rho_min) / dlog_rho);
        int j = static_cast<int>((y - log_T_min) / dlog_T);
        int k = static_cast<int>((z - X_min) / dX);

        // 计算索引
        i = std::max(0, std::min(i, n_rho - 2));
        j = std::max(0, std::min(j, n_T - 2));
        k = std::max(0, std::min(k, n_X - 2));

        // 计算局部偏移 [0, 1)
        double tx = (x - (log_rho_min + i * dlog_rho)) / dlog_rho;
        double ty = (y - (log_T_min + j * dlog_T)) / dlog_T;
        double tz = (z - (X_min + k * dX)) / dX;

// 辅助宏：计算 1D 展平数组的索引 (i, j, k) -> i * (n_T * n_X) + j * n_X + k
#define IDX(ii, jj, kk) ((ii) * n_T * n_X + (jj) * n_X + (kk))

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
    // 状态查询接口 (含 pynucastro 预留的 Xi)
    // ========================================================

    double get_target_X(const double *Xi) const
    {
        // 1. 最高优先级：如果是普通的双组分测试，直接提取目标质量分数
        if (target_species_id >= 0)
        {
            return Xi[target_species_id];
        }

        // 2. 次优先级：如果没指定特定组分，且挂载了 specs，则计算天体物理的 Ye
        if (specs && specs->count() > 0)
        {
            return specs->calc_Ye(Xi);
        }

        // 3. 兜底
        return 0.5;
    }

    double get_pressure_from_rho_T(double rho, double T, const double *Xi) const
    {
        if (rho <= 1e-12 || T <= 1e-12)
            return 0.0;
        double X = get_target_X(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), X))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_pressure(rho, e);
        }
        return interpolate_3d(table_P, rho, T, X);
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

        double X = get_target_X(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), X))
        {
            double Abar = (specs && specs->count() > 0) ? specs->calc_Abar(Xi) : 1.0;
            double R_spec = k_B_cgs / (Abar * m_u_cgs);
            return T_target * R_spec / (fallback_gamma() - 1.0);
        }

        return interpolate_3d(table_E, rho, T_target, X);
    }

    double get_cv(double rho, double T_target, const double *Xi) const
    {
        if (rho <= 1e-12 || T_target <= 1e-12)
            return 0.0;

        double X = get_target_X(Xi);

        if (is_out_of_bounds(std::log10(rho), std::log10(T_target), X))
        {
            double Abar = (specs && specs->count() > 0) ? specs->calc_Abar(Xi) : 1.0;
            double R_spec = k_B_cgs / (Abar * m_u_cgs);
            return R_spec / (fallback_gamma() - 1.0);
        }

        return interpolate_3d(table_cv, rho, T_target, X);
    }

    double get_temperature(double rho, double e, const double *Xi) const
    {
        if (rho <= 1e-12 || e <= 1e-12)
            return 0.0;
            
        double X = get_target_X(Xi);
        double T_min = std::pow(10, log_T_min);
        double T_max = std::pow(10, log_T_max);
        
        // Out of bounds check for density or composition
        if (std::log10(rho) < log_rho_min || std::log10(rho) >= log_rho_max ||
            X < X_min || X >= X_max)
        {
            return fallback_temperature(e, Xi);
        }
        
        // Fast boundary check: if e is below the minimum table energy, return T_min
        double e_min_table = interpolate_3d(table_E, rho, T_min, X);
        if (e <= e_min_table) {
            return T_min;
        }
        
        // Newton-Raphson iteration
        double T_guess = 1e8; // reasonable astrophysics start
        const int max_iters = 20;
        const double tol = 1e-6;
        
        for (int i = 0; i < max_iters; ++i) {
            T_guess = std::max(T_min, std::min(T_guess, T_max));
            
            double e_eval = interpolate_3d(table_E, rho, T_guess, X);
            double cv_eval = interpolate_3d(table_cv, rho, T_guess, X);
            
            if (cv_eval <= 0.0) {
                // Finite difference fallback
                double dT_fd = T_guess * 0.01;
                double e_plus = interpolate_3d(table_E, rho, T_guess + dT_fd, X);
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

        double X = get_target_X(Xi);
        double T = get_temperature(U.rho, e_int, Xi);
        if (is_out_of_bounds(std::log10(U.rho), std::log10(T), X))
        {
            return fallback_sound_speed(U.rho, e_int);
        }
        return interpolate_3d(table_cs, U.rho, T, X);
    }

    double get_gamma(const double *Xi, double rho = 0.0, double e = 0.0) const
    {
        if (rho < 1e-12 || e < 1e-12)
            return fallback_gamma();
        double p = get_pressure_from_rho_e(rho, e, Xi);
        if (p < 1e-12)
            return fallback_gamma();

        double X = get_target_X(Xi);
        double T = get_temperature(rho, e, Xi);
        return get_sound_speed_from_rho_T(rho, T, Xi);
    }
    
    double get_sound_speed_from_rho_T(double rho, double T, const double *Xi) const
    {
        double X = get_target_X(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), X))
        {
            double e = get_eint_from_T(rho, T, Xi);
            return fallback_sound_speed(rho, e);
        }
        return interpolate_3d(table_cs, rho, T, X);
    }

    // ========================================================
    // 导数接口 (支持读取真实导数表或回退有限差分)
    // ========================================================
    double get_dp_drho_e(double rho, double e, const double *Xi) const
    {
        double X = get_target_X(Xi);
        double T = get_temperature(rho, e, Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(T), X))
        {
            return e * (fallback_gamma() - 1.0); // 理想气体 dP/drho
        }

        if (table_dP_drho)
            return interpolate_3d(table_dP_drho, rho, T, X);

        double drho = rho * 0.001;
        return (interpolate_3d(table_P, rho + drho, T, X) -
                interpolate_3d(table_P, rho - drho, T, X)) /
               (2.0 * drho);
    }

    double get_dp_de_rho(double rho, double e, const double *Xi) const
    {
        double X = get_target_X(Xi);
        if (is_out_of_bounds(std::log10(rho), std::log10(e), X))
        {
            return rho * (fallback_gamma() - 1.0); // 理想气体 dP/de
        }

        double T = get_temperature(rho, e, Xi);
        if (table_dP_dT && table_cv) {
            double dp_dT = interpolate_3d(table_dP_dT, rho, T, X);
            double cv = interpolate_3d(table_cv, rho, T, X);
            if (cv > 0.0) return dp_dT / cv;
        }

        double de = e * 0.001;
        double T_plus = get_temperature(rho, e + de, Xi);
        double T_minus = get_temperature(rho, e - de, Xi);
        return (interpolate_3d(table_P, rho, T_plus, X) -
                interpolate_3d(table_P, rho, T_minus, X)) /
               (2.0 * de);
    }
    // ========================================================
    // 鲁棒的阻尼牛顿法反推总能
    // ========================================================
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
            double X = get_target_X(state.Xi);
            state.dp_dT = interpolate_3d(table_dP_dT, state.rho, state.T, X);
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
// 2. Host Manager: 负责 HDF5 IO 和内存生命周期管理 (绝不进入内层循环)
// ====================================================================
struct Tabular3DEOS : public EOSBase
{
private:
    std::string table_path;
    std::vector<double> h_table_P;
    std::vector<double> h_table_E;
    std::vector<double> h_table_cs;
    std::vector<double> h_table_cv;

    // 预留
    std::vector<double> h_table_dP_drho;
    std::vector<double> h_table_dP_dT;

    Tabular3DEOSView view;

public:
    Tabular3DEOS(const std::string &h5_filename, const SpeciesManager *specs_ptr = nullptr)
        : table_path(h5_filename)
    {
        std::cout << "[Tabular3DEOS] Loading HDF5 table: " << h5_filename << std::endl;
        HighFive::File file(h5_filename, HighFive::File::ReadOnly);

        file.getDataSet("n_rho").read(view.n_rho);
        file.getDataSet("n_T").read(view.n_T);
        file.getDataSet("n_X").read(view.n_X);

        file.getDataSet("log_rho_min").read(view.log_rho_min);
        file.getDataSet("log_rho_max").read(view.log_rho_max);
        file.getDataSet("log_T_min").read(view.log_T_min);
        file.getDataSet("log_T_max").read(view.log_T_max);
        file.getDataSet("X_min").read(view.X_min);
        file.getDataSet("X_max").read(view.X_max);

        view.dlog_rho = (view.log_rho_max - view.log_rho_min) / (view.n_rho - 1);
        view.dlog_T = (view.log_T_max - view.log_T_min) / (view.n_T - 1);
        view.dX = (view.X_max - view.X_min) / (view.n_X - 1);

        file.getDataSet("pressure").read(h_table_P);
        file.getDataSet("energy").read(h_table_E);
        file.getDataSet("sound_speed").read(h_table_cs);
        file.getDataSet("cv").read(h_table_cv);

        // 尝试加载偏导数表（如果不存在则捕获异常，保持为 nullptr 回退到有限差分）
        try
        {
            file.getDataSet("dp_drho").read(h_table_dP_drho);
            view.table_dP_drho = h_table_dP_drho.data();
            file.getDataSet("dp_dT").read(h_table_dP_dT);
            view.table_dP_dT = h_table_dP_dT.data();
            std::cout << "[Tabular3DEOS] Loaded EOS tables." << std::endl;
        }
        catch (...)
        {
            view.table_dP_drho = nullptr;
            view.table_dP_dT = nullptr;
            std::cout << "[Tabular3DEOS] No tables found. Falling back to finite difference." << std::endl;
        }

        // 挂载指针到底层 Vector
        view.table_P = h_table_P.data();
        view.table_E = h_table_E.data();
        view.table_cs = h_table_cs.data();
        view.table_cv = h_table_cv.data();

        view.specs = specs_ptr;
        view.target_species_id = 1;

        std::cout << "[Tabular3DEOS] Table loaded successfully." << std::endl;
    }

    // CFD 求解器分发时，只获取 View
    Tabular3DEOSView get_view() const { return view; }
    
    const SpeciesManager* get_species_manager() const { return view.specs; }
};