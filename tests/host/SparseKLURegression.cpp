/**
 * @file SparseKLURegression.cpp
 * @brief Recover a manufactured solution with the CPU sparse linear solver.
 *
 * Assemble b = A x for a tridiagonal matrix with diagonal 4 and off-diagonal
 * entries -1. The known x_i = sin(0.17 (i + 1)) checks the solve independently
 * of a dense solver and also exercises sparse matrix indexing and storage.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "numerics/linalg/SparseWrap.h"
int main() {
    constexpr int n=161;
    SparseMatrixData<n> matrix;
    std::array<double,n> expected{};
    double rhs[n]{};
    for(int i=0;i<n;++i) expected[i]=std::sin(0.17*(i+1));
    for(int i=0;i<n;++i) {
        matrix.set(i+1,i+1,4.0); rhs[i]=4.0*expected[i];
        if(i>0) { matrix.set(i+1,i,-1.0); rhs[i]-=expected[i-1]; }
        if(i+1<n) { matrix.set(i+1,i+2,-1.0); rhs[i]-=expected[i+1]; }
    }
    if(!SparseKLUSolver::solve<n,n>(matrix,rhs))
        throw std::runtime_error("KLU factorization failed");
    double worst=0.0;
    for(int i=0;i<n;++i) worst=std::max(worst,std::abs(rhs[i]-expected[i]));
    std::cout<<"KLU 161x161 sparse solve max absolute error: "<<worst
             <<", structural nnz="<<matrix.values.size()<<std::endl;
    if(worst>1.0e-11) throw std::runtime_error("KLU regression exceeded tolerance");
}
