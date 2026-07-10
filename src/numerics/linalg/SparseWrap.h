/**
 * @file linalg/SparseWrap.h
 * @brief 稀疏矩阵求解器的包装器占位符 (COO格式)
 */
#pragma once
#include <vector>
#include <tuple>
#include <iostream>
#include <stdexcept>

namespace SparseLimits
{
    static constexpr int MAX_NNZ = BurnLimits::MAX_ODE_NEQ * 10;
}
// 稀疏矩阵的数据容器占位
struct SparseMatrixData
{
    int rows[SparseLimits::MAX_NNZ];
    int cols[SparseLimits::MAX_NNZ];
    double values[SparseLimits::MAX_NNZ];
    int nnz = 0;

    // 存储非零元素：<row, col, value>
    void zero() { nnz = 0; }

    double &operator()(int i, int j)
    {
        // 稀疏矩阵不推荐随机读，但 pynucastro 生成的代码如果是 J(i,j) = val
        // 我们可以拦截。如果 nnz 溢出则忽略或报错
        if (nnz < SparseLimits::MAX_NNZ)
        {
            rows[nnz] = i - 1;
            cols[nnz] = j - 1;
            values[nnz] = 0.0; // 先给引用
            return values[nnz++];
        }
        static double dummy = 0.0;
        return dummy;
    }

    void set(int i, int j, double val)
    {
        if (std::abs(val) > 1e-30 && nnz < SparseLimits::MAX_NNZ)
        {
            rows[nnz] = i - 1;
            cols[nnz] = j - 1;
            values[nnz++] = val;
        }
    }
};

// 稀疏求解器包装器
struct SparseSolverWrap
{
    template <int ACTIVE_N, int MAX_N>
    static bool solve(SparseMatrixData &A, double b[MAX_N])
    {
        std::cout << "[SparseWrap] Triggered sparse solve for " << ACTIVE_N << "x" << ACTIVE_N << std::endl;
        std::cout << "[SparseWrap] Non-zero elements collected: " << A.nnz << std::endl;

        // ======================================================
        // 【未来的接口对接区】
        // 1. 在这里，将 A.non_zeros 转换为 CSR 格式 (row_ptr, col_ind, values)
        // 2. 调用 cuSPARSE 或者 SUNDIALS SUNMatrix 的接口
        // 3. 将结果写回 b
        // ======================================================

        // 占位符：目前直接抛出异常，防止实际进行数学计算导致错误
        throw std::runtime_error("Real Sparse Solver Not Yet Integrated!");
        return true;
    }
};