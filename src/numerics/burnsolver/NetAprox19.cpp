#include "NetAprox19.h"

#include "data/GlobalDefs.h"
#include "../../numerics/linalg/DenseWrap.h"
#include "../../numerics/linalg/SparseWrap.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

// 直接在全局引入 pynucastro 头文件，不再使用 namespace 强行包裹
#include "amrex_bridge.H"
#include "network_properties.H"
#include "burn_type.H"
#include "actual_network.H"
#include "actual_rhs.H"

void NetAprox19::eval_rhs(const double *Y, double rho, double *RHS, double &enuc)
{
    burn_t state;
    state.rho = rho;
    state.T = Y[ODE_NEQ - 1]; // 温度在向量末尾

    for (int i = 0; i < NUM_SPECIES; ++i)
    {
        state.xn[i] = Y[i];
    }

    compute_ye(state);
    Array1D<Real, 1, NUM_SPECIES> ydot_arr;
    Real enu_weak = 0.0;

    // 调用 pynucastro 右端项
    actual_rhs(state, ydot_arr, enu_weak);

    for (int i = 0; i < NUM_SPECIES; ++i)
    {
        RHS[i] = ydot_arr(i + 1);
    }

    // 能量生成率评估
    Real enuc_local = 0.0;
    ener_gener_rate(ydot_arr, enuc_local);
    enuc = static_cast<double>(enuc_local);
}

void NetAprox19::eval_jacobian(const double *Y, double rho, DenseMatrixData &J_dense)
{
    burn_t state;
    state.rho = rho;
    state.T = Y[ODE_NEQ - 1];

    for (int i = 0; i < NUM_SPECIES; ++i)
        state.xn[i] = Y[i];
    compute_ye(state);

    actual_jac(state, J_dense);
}

void NetAprox19::eval_jacobian(const double *Y, double rho, SparseMatrixData &J_sparse)
{
    burn_t state;
    state.rho = rho;
    state.T = Y[ODE_NEQ - 1];

    for (int i = 0; i < NUM_SPECIES; ++i)
        state.xn[i] = Y[i];
    compute_ye(state);

    actual_jac(state, J_sparse);
}