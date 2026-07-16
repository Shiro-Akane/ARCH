/*
 * @file DenseWrap.h
 * @brief 稠密矩阵 LU 分解求解器 (带有列主元消去)
 */
#pragma once
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include "../../data/GlobalDefs.h"

struct DenseMatrixData
{
    double data[BurnLimits::MAX_ODE_NEQ][BurnLimits::MAX_ODE_NEQ];

    // 提供给 Pynucastro 的 1-based 索引接口
    double &operator()(int i, int j)
    {
        return data[i - 1][j - 1];
    }

    const double &operator()(int i, int j) const
    {
        return data[i - 1][j - 1];
    }

    void set(int i, int j, double val)
    {
        data[i - 1][j - 1] = val;
    }

    void zero()
    {
        // 实际开发中可以通过 ACTIVE_N 来优化清零范围
        for (int i = 0; i < BurnLimits::MAX_ODE_NEQ; ++i)
#pragma omp simd
            for (int j = 0; j < BurnLimits::MAX_ODE_NEQ; ++j)
                data[i][j] = 0.0;
    }
};

// =======================================================
// 稠密矩阵 LU 分解求解器 (带有列主元消去)
// =======================================================
struct DenseLUSolver
{
    /**
     * @brief 求解 Ax = b
     * @tparam ACTIVE_N 当前网络实际活跃的方程组维度 (如 Aprox19 为 20)
     * @tparam MAX_N    内存中实际分配的最大维度 (BurnLimits::MAX_ODE_NEQ)
     * @param A         输入矩阵 (会被原地修改为 LU 矩阵)
     * @param b         输入右端项 (会被原地修改为解向量 x)
     * @return bool     如果矩阵奇异则返回 false
     */
    template <int ACTIVE_N, int MAX_N>
    static bool solve(DenseMatrixData &A, double b[MAX_N])
    {
        int p[ACTIVE_N]; // 行置换记录数组 (栈上分配，极快)
#pragma omp simd
        for (int i = 0; i < ACTIVE_N; ++i)
            p[i] = i;

        // 1. LU 分解 (带有列主元选主)
        for (int i = 0; i < ACTIVE_N; ++i)
        {
            // 寻找当前列的最大主元
            double max_val = 0.0;
            int pivot_row = i;
            for (int j = i; j < ACTIVE_N; ++j)
            {
                double val = std::abs(A.data[p[j]][i]);
                if (val > max_val)
                {
                    max_val = val;
                    pivot_row = j;
                }
            }

            if (max_val < 1e-20)
                return false; // 矩阵接近奇异，直接返回失败让外层自适应缩减 dt

            // 虚拟交换行 (只交换索引，不移动内存数据)
            std::swap(p[i], p[pivot_row]);

            // 消元过程
            double pivot_inv = 1.0 / A.data[p[i]][i];
            for (int j = i + 1; j < ACTIVE_N; ++j)
            {
                A.data[p[j]][i] *= pivot_inv; // 存储 L 的乘子
#pragma omp simd
                for (int k = i + 1; k < ACTIVE_N; ++k)
                {
                    A.data[p[j]][k] -= A.data[p[j]][i] * A.data[p[i]][k]; // 更新 U
                }
            }
        }

        // 2. 前向代入 (Forward Substitution): 解 Ly = Pb
        double y[ACTIVE_N]; // 栈上临时数组
        for (int i = 0; i < ACTIVE_N; ++i)
        {
            y[i] = b[p[i]];
            for (int j = 0; j < i; ++j)
            {
                y[i] -= A.data[p[i]][j] * y[j];
            }
        }

        // 3. 后向代入 (Backward Substitution): 解 Ux = y (结果直接写回 b)
        for (int i = ACTIVE_N - 1; i >= 0; --i)
        {
            b[i] = y[i];
            for (int j = i + 1; j < ACTIVE_N; ++j)
            {
                b[i] -= A.data[p[i]][j] * b[j];
            }
            b[i] /= A.data[p[i]][i];
        }

        return true;
    }

    /**
     * @brief  仅执行 LU 分解 (O(N^3))
     */
    template <int ACTIVE_N, int MAX_N>
    static bool factorize(DenseMatrixData &A, int p[MAX_N])
    {
#pragma omp simd
        for (int i = 0; i < ACTIVE_N; ++i)
            p[i] = i;

        for (int i = 0; i < ACTIVE_N; ++i)
        {
            double max_val = 0.0;
            int pivot_row = i;
            for (int j = i; j < ACTIVE_N; ++j)
            {
                double val = std::abs(A.data[p[j]][i]);
                if (val > max_val)
                {
                    max_val = val;
                    pivot_row = j;
                }
            }

            if (max_val < 1e-20) return false;

            std::swap(p[i], p[pivot_row]);

            double pivot_inv = 1.0 / A.data[p[i]][i];
            for (int j = i + 1; j < ACTIVE_N; ++j)
            {
                A.data[p[j]][i] *= pivot_inv;
#pragma omp simd
                for (int k = i + 1; k < ACTIVE_N; ++k)
                {
                    A.data[p[j]][k] -= A.data[p[j]][i] * A.data[p[i]][k];
                }
            }
        }
        return true;
    }

    /**
     * @brief 使用已分解的 LU 矩阵进行极速回代求解 (O(N^2))
     */
    template <int ACTIVE_N, int MAX_N>
    static void solve_with_factors(const DenseMatrixData &A, const int p[MAX_N], double b[MAX_N])
    {
        double y[ACTIVE_N];
        for (int i = 0; i < ACTIVE_N; ++i)
        {
            y[i] = b[p[i]];
#pragma omp simd
            for (int j = 0; j < i; ++j)
            {
                y[i] -= A.data[p[i]][j] * y[j];
            }
        }

        for (int i = ACTIVE_N - 1; i >= 0; --i)
        {
            b[i] = y[i];
#pragma omp simd
            for (int j = i + 1; j < ACTIVE_N; ++j)
            {
                b[i] -= A.data[p[i]][j] * b[j];
            }
            b[i] /= A.data[p[i]][i];
        }
    }
};
