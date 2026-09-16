#include "numerics/linalg/SparseResidual.h"
#include "numerics/linalg/CsrMatrixView.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    using namespace arch::linalg;
    const auto require = [](bool valid) { if (!valid) throw std::runtime_error("sparse residual contract"); };
    try {
        const int columns[]{0, 1, 2}, offsets[]{0, 1, 2, 3};
        double values[]{1, 1, 1}, x[]{1, 2, 3}, rhs[]{1, 2, 3};
        CsrMatrixView<3> matrix{offsets, columns, values, 3, true};
        require(matrix.solution_accurate(rhs, x));
        for (int row = 0; row < 3; ++row) {
            auto r = sparse_residual_row(3, row, row + 1, columns, values, x, rhs[row]);
            require(r.state == SparseResidualState::Accurate && r.correction_rhs == 0);
        }
        x[1] += 1e-7;
        auto r = sparse_residual_row(3, 1, 2, columns, values, x, rhs[1]);
        require(!matrix.solution_accurate(rhs, x) && r.state == SparseResidualState::Correctable);
        x[1] += r.correction_rhs;
        require(matrix.solution_accurate(rhs, x));
        values[0] = 1e16; values[1] = 1; values[2] = -1e16;
        x[0] = x[1] = x[2] = 1;
        r = sparse_residual_row(3, 0, 3, columns, values, x, 0);
        require(r.correction_rhs == -1); // Naive summation loses this residual.
        const double a = std::nextafter(1.0, 2.0), b = std::nextafter(1.0, 0.0);
        values[0] = a; x[0] = b;
        r = sparse_residual_row(3, 0, 1, columns, values, x, a * b);
        require(r.correction_rhs == -std::fma(a, b, -(a * b)));
        x[0] = std::numeric_limits<double>::infinity();
        require(sparse_residual_row(3, 0, 1, columns, values, x, 1).state
                == SparseResidualState::Unrefinable);
        require(sparse_residual_row(3, 0, 1, nullptr, values, x, 1).state
                == SparseResidualState::Unrefinable);
        std::cout << "Shared sparse residual contracts passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
