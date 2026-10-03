/**
 * @file test_flux_limiter.cu
 * @brief Device parity for the shared conservative flux trace constraint.
 *
 * Workflow:
 * 1. Construct admissible fluid means and normalized trial face compositions.
 * 2. Execute the production flux limiter on Host and device with identical inputs.
 * 3. Compare every fluid/species result bitwise; device failures never skip.
 * Independent cone inequalities and continuity checks belong to the Host test.
 */
#include "physics/eos/IdealGas.h"
#include "numerics/flux/FluxHLLC.h"

#include <cuda_runtime.h>

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
struct Input {
    FluidVector left, right, high;
    double x_left[2], x_right[2], species_flux[2];
    double p_left, c_left, p_right, c_right;
    int direction;
};
struct Output { FluidVector flux; double species[2]; };

void checked(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}

ARCH_INLINE Output evaluate(const Input& input)
{
    Output output{input.high, {input.species_flux[0], input.species_flux[1]}};
    FluxAdmissibility::limit_face_with_thermo(input.left, input.right,
        input.x_left, input.x_right, 2, input.p_left, input.c_left,
        input.p_right, input.c_right, input.direction, output.flux, output.species);
    return output;
}

__global__ void limit_fluxes(const Input* inputs, Output* outputs, int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count) outputs[index] = evaluate(inputs[index]);
}

FluidVector conserved(double density, double velocity, double pressure, int direction)
{
    FluidVector state{density, density * velocity, 0.0, 0.0,
                      pressure / 0.4 + 0.5 * density * velocity * velocity};
    if (direction == 1) std::swap(state.mom_u, state.mom_v);
    if (direction == 2) std::swap(state.mom_u, state.mom_w);
    return state;
}

std::vector<Input> make_inputs()
{
    const IdealGasView eos{};
    std::vector<Input> result;
    for (int direction : {0, 1, 2})
        for (bool reverse : {false, true})
            for (double scale : {1.0, 1e-12, 1e-100})
                for (double trace : {0.0, 1e-16, 1e-30, 1e-100, 1e-12, 1e-6}) {
                    Input input{};
                    input.direction = direction;
                    input.left = scale * conserved(1.0, reverse ? -0.3 : 0.3, 1.0, direction);
                    input.right = scale * conserved(0.125, reverse ? 0.1 : -0.1, 0.1, direction);
                    if (reverse) std::swap(input.left, input.right);
                    input.x_left[0] = input.x_right[0] = 1.0;
                    input.x_left[1] = input.x_right[1] = 0.0;
                    const double face_x[]{1.0 - trace, trace};
                    FluxHLLC<PCMReconstruction>::compute_face_flux(input.left, input.right,
                        face_x, face_x, 2, eos, direction, 0.0, input.high, input.species_flux);
                    FluxAdmissibility::required_mean_thermo(input.left, input.x_left, eos,
                        input.p_left, input.c_left);
                    FluxAdmissibility::required_mean_thermo(input.right, input.x_right, eos,
                        input.p_right, input.c_right);
                    result.push_back(input);
                }
    return result;
}

bool equal_bits(double left, double right)
{
    return std::isfinite(right)
        && std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}
} // namespace

int main()
{
    Input* device_inputs = nullptr;
    Output* device_outputs = nullptr;
    try {
        int devices = 0;
        checked(cudaGetDeviceCount(&devices));
        if (devices == 0) throw std::runtime_error("No CUDA device available for flux limiter test");
        const auto inputs = make_inputs();
        std::vector<Output> actual(inputs.size());
        checked(cudaMalloc(&device_inputs, inputs.size() * sizeof(Input)));
        checked(cudaMalloc(&device_outputs, inputs.size() * sizeof(Output)));
        checked(cudaMemcpy(device_inputs, inputs.data(), inputs.size() * sizeof(Input), cudaMemcpyHostToDevice));
        constexpr int threads = 128;
        const int count = static_cast<int>(inputs.size());
        limit_fluxes<<<(count + threads - 1) / threads, threads>>>(device_inputs, device_outputs, count);
        checked(cudaGetLastError());
        checked(cudaDeviceSynchronize());
        checked(cudaMemcpy(actual.data(), device_outputs, actual.size() * sizeof(Output), cudaMemcpyDeviceToHost));
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto expected = evaluate(inputs[i]);
            const auto& observed = actual[i];
            if (!equal_bits(expected.flux.rho, observed.flux.rho)
                || !equal_bits(expected.flux.mom_u, observed.flux.mom_u)
                || !equal_bits(expected.flux.mom_v, observed.flux.mom_v)
                || !equal_bits(expected.flux.mom_w, observed.flux.mom_w)
                || !equal_bits(expected.flux.eng, observed.flux.eng)
                || !equal_bits(expected.species[0], observed.species[0])
                || !equal_bits(expected.species[1], observed.species[1]))
                throw std::runtime_error("Flux limiter Host/device bits differ at case " + std::to_string(i));
        }
        checked(cudaFree(device_inputs)); device_inputs = nullptr;
        checked(cudaFree(device_outputs)); device_outputs = nullptr;
        std::cout << "Shared flux limiter CUDA parity passed: " << count << " cases\n";
        return 0;
    } catch (const std::exception& error) {
        if (device_inputs) cudaFree(device_inputs);
        if (device_outputs) cudaFree(device_outputs);
        std::cerr << "Flux limiter CUDA test failed: " << error.what() << '\n';
        return 1;
    }
}
