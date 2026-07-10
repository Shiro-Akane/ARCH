/**
 * @file NetAprox19.h
 * @brief Aprox19 网络的静态包装器 (Adapter for pynucastro)
 */
#pragma once

#include <string>
#include "../../data/GlobalDefs.h"
#include "../../numerics/linalg/DenseWrap.h"
#include "../../numerics/linalg/SparseWrap.h"

struct NetAprox19
{
    // =======================================================
    // 1. 编译期常量
    // =======================================================
    static constexpr int ODE_NEQ = 19;
    static constexpr int NUM_SPECIES = 16;

    // =======================================================
    // 2. 信息接口
    // =======================================================
    static std::string get_network_name() { return "Aprox19"; }

    // =======================================================
    // 3. 物理计算接口声明 (实现见 .cpp)
    // =======================================================
    static void eval_rhs(const double *Y, double rho, double *RHS, double &enuc);

    static void eval_jacobian(const double *Y, double rho, DenseMatrixData &J_dense);

    static void eval_jacobian(const double *Y, double rho, SparseMatrixData &J_sparse);
};