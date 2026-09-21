/**
 * @file test_generated_network_reference.cpp
 * @brief Emit network values for comparison across adapter implementations.
 *
 * Supply the selected header and ARCH_TEST_NETWORK_TYPE at compilation. The
 * output includes the RHS, energy terms, temperature derivatives and Jacobian
 * in hexadecimal floating-point notation, preserving their binary64 values.
 */
#include "numerics/linalg/DenseWrap.h"
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    using Network = ARCH_TEST_NETWORK_TYPE;
    std::vector<double> state(Network::ODE_NEQ, 1.0 / Network::NUM_SPECIES);
    std::vector<double> rhs(Network::NUM_SPECIES), energy(rhs.size()), temperature(rhs.size());
    DenseMatrixData<Network::ODE_NEQ> matrix;
    for (double thermal : {3.e9, 3.01e9}) {
        state.back() = thermal;
        matrix.zero();
        double enuc = 0.0, derivative = 0.0;
        Network::eval_rhs(state.data(), 1.e7, 0.0, rhs.data(), enuc);
        Network::eval_jacobian(state.data(), 1.e7, 0.0, matrix, energy.data());
        Network::eval_temperature_derivative(state.data(), 1.e7, 0.0, temperature.data(), derivative);
        std::cout << std::hexfloat << enuc << '\n' << derivative << '\n';
        for (const auto* values : {&rhs, &energy, &temperature})
            for (double value : *values) std::cout << value << '\n';
        for (int row = 0; row < Network::ODE_NEQ; ++row)
            for (int column = 0; column < Network::ODE_NEQ; ++column)
                std::cout << matrix.data[row][column] << '\n';
    }
}
