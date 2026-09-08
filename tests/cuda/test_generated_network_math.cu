// Compile with the selected generated network header and ARCH_TEST_NETWORK_TYPE.
// No network identity or equation extent is embedded in the test driver.
#include "numerics/linalg/CsrPattern.h"
#include "numerics/burnsolver/OdeContinuation.h"
#include "cuda/microphysics/device_network_owner.h"
#include <cuda_runtime.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <memory>

using Network = ARCH_TEST_NETWORK_TYPE;
using Matrix = CsrMatrixView<Network::ODE_NEQ>;
namespace {
void check(cudaError_t result) {
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
template<class T> struct Buffer {
    T* data = nullptr;
    explicit Buffer(std::size_t count) { check(cudaMalloc(reinterpret_cast<void**>(&data), count * sizeof(T))); }
    ~Buffer() { cudaFree(data); }
};
__global__ void evaluate(const double* state, Matrix matrix, double* rhs,
                          double* energy, double* temperature, double* scalars,
                          Network network, double rho) {
    if (blockIdx.x || threadIdx.x) return;
    matrix.zero();
    network.eval_rhs(state, rho, 0.0, rhs, scalars[0]);
    network.eval_jacobian(state, rho, 0.0, matrix, energy);
    network.eval_temperature_derivative(state, rho, 0.0, temperature, scalars[1]);
    scalars[2] = matrix.pattern_valid ? 1.0 : 0.0;
}
void compare(const char* field, const std::vector<double>& expected, const std::vector<double>& actual) {
    if (expected.size() != actual.size()) throw std::runtime_error("generated comparison shape mismatch");
    for (std::size_t index = 0; index < actual.size(); ++index)
        if (!std::isfinite(expected[index]) || !std::isfinite(actual[index])
            || std::abs(actual[index] - expected[index]) > 2.e-10 * std::max(1.0, std::abs(expected[index])))
        {
            std::ostringstream reason;
            reason << std::setprecision(17) << "generated Host/Device mismatch in " << field
                << '[' << index << "] host=" << expected[index] << " device=" << actual[index]
                << " budget=" << 2.e-10 * std::max(1.0, std::abs(expected[index]));
            throw std::runtime_error(reason.str());
        }
}
}
int main(int argc, char** argv) {
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) return 77;
    if (probe != cudaSuccess) {
        std::cerr << cudaGetErrorString(probe) << '\n';
        return 1;
    }
    try {
        if (argc != 1 && argc != 3) throw std::invalid_argument("usage: network_math [rho temperature]");
        const double rho = argc == 3 ? std::stod(argv[1]) : 1.e7;
        const double initial_temperature = argc == 3 ? std::stod(argv[2]) : 3.e9;
        if (!std::isfinite(rho) || rho <= 0.0 || !std::isfinite(initial_temperature)
            || initial_temperature < 1.0) throw std::invalid_argument("invalid network test state");
        const auto host_network = make_host_burn_network<Network>();
        std::unique_ptr<arch::cuda::DeviceNetworkOwner> owner;
        const auto device_network = [&]<class Net>() {
            if constexpr (requires { Net::host_table_storage(); }) {
                owner = std::make_unique<arch::cuda::DeviceNetworkOwner>(Net::host_table_storage(), nullptr);
                return Net{owner->view()};
            } else return Net{};
        }.template operator()<Network>();
        std::vector<double> state(Network::ODE_NEQ, 1.0 / Network::NUM_SPECIES);
        state[Network::NUM_SPECIES] = initial_temperature;
        for (int index = Network::NUM_SPECIES + 1; index < Network::ODE_NEQ; ++index) state[index] = 0.0;
        arch::linalg::CsrPatternBuilder builder(Network::ODE_NEQ);
        Network::enumerate_jacobian_structure(builder);
        builder.include_dense_row_column(Network::NUM_SPECIES + 1);
        const auto pattern = builder.finish();
        const auto nnz = pattern.column_indices.size();
        std::vector<double> values(nnz), rhs(Network::NUM_SPECIES), energy(rhs.size()), temperature(rhs.size());
        double energy_value = 0.0, temperature_value = 0.0;
        auto matrix = pattern.view<Network::ODE_NEQ>(values.data());
        host_network.eval_rhs(state.data(), rho, 0.0, rhs.data(), energy_value);
        host_network.eval_jacobian(state.data(), rho, 0.0, matrix, energy.data());
        host_network.eval_temperature_derivative(state.data(), rho, 0.0, temperature.data(), temperature_value);
        Buffer<double> device_state(state.size()), device_values(nnz), device_rhs(rhs.size()),
            device_energy(rhs.size()), device_temperature(rhs.size()), scalars(3);
        Buffer<int> rows(pattern.row_offsets.size()), columns(nnz);
        check(cudaMemcpy(device_state.data, state.data(), state.size() * sizeof(double), cudaMemcpyHostToDevice));
        check(cudaMemcpy(rows.data, pattern.row_offsets.data(), pattern.row_offsets.size() * sizeof(int), cudaMemcpyHostToDevice));
        check(cudaMemcpy(columns.data, pattern.column_indices.data(), nnz * sizeof(int), cudaMemcpyHostToDevice));
        Matrix device_matrix{rows.data, columns.data, device_values.data, static_cast<int>(nnz), true};
        evaluate<<<1, 1>>>(device_state.data, device_matrix, device_rhs.data,
            device_energy.data, device_temperature.data, scalars.data, device_network, rho);
        check(cudaDeviceSynchronize());
        const auto download = [&](const double* pointer, std::size_t count) {
            std::vector<double> result(count);
            check(cudaMemcpy(result.data(), pointer, count * sizeof(double), cudaMemcpyDeviceToHost));
            return result;
        };
        compare("jacobian", values, download(device_values.data, nnz));
        compare("rhs", rhs, download(device_rhs.data, rhs.size()));
        compare("energy_gradient", energy, download(device_energy.data, energy.size()));
        compare("temperature_gradient", temperature, download(device_temperature.data, temperature.size()));
        compare("energy/temperature_energy/pattern", {energy_value, temperature_value, 1.0}, download(scalars.data, 3));
        std::cout << "GENERATED_NETWORK_MATH_PASS neq=" << Network::ODE_NEQ << " nnz=" << nnz
            << " rho=" << rho << " temperature=" << initial_temperature << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
