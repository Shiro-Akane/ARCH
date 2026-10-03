/**
 * @file test_ppm_limiter.cu
 * @brief Shared PPM Host/device parity and roundoff sensitivity witnesses.
 *
 * Both executions call the production scalar reconstruction. Independent
 * analytic profile checks belong to the Host PPM test; this test checks that
 * CUDA uses the same map and that one/two-ULP stencil changes remain within the
 * existing 64-epsilon witness budget. Device errors are failures, not skips.
 */
#include "numerics/reconstruction/Reconstruction.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

struct Stencil { double values[6]; };
struct Faces { double left; double right; };
constexpr double kWitnessTolerance = 64.0 * std::numeric_limits<double>::epsilon();
constexpr int kThreads = 128;

void checked(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}

__global__ void reconstruct(const Stencil* inputs, Faces* outputs, int count)
{
    const int index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count)
        PPMReconstruction::reconstruct_scalar_ppm(
            inputs[index].values, outputs[index].left, outputs[index].right);
}

// Each group contains one original stencil and every single-entry one/two-ULP
// perturbation in both directions. Sign and magnitude changes are diagnostic
// inputs, never backend-dependent parameters of the production method.
std::vector<Stencil> make_stencils(std::vector<std::size_t>& groups)
{
    const double a = 1.0 / 32.0;
    const double witnesses[][6] = {
        {1.0 - 2.5*a, 1.0 - a, 1.0, 1.0 - a, 1.0 - 4.0*a, 1.0 - 9.0*a},
        {0.7, 0.5, 1.0, 1.2, 0.5, 0.9},
        {0.25, 0.25, 0.25, 4.0, 4.0, 0.25}
    };
    std::vector<Stencil> result;
    for (const auto& witness : witnesses)
        for (double sign : {-1.0, 1.0})
            for (double scale : {1e-100, 1.0, 1e100}) {
                Stencil base{};
                for (int k = 0; k < 6; ++k) base.values[k] = witness[k] * sign * scale;
                groups.push_back(result.size());
                result.push_back(base);
                for (int k = 0; k < 6; ++k)
                    for (double direction : {-std::numeric_limits<double>::infinity(),
                                              std::numeric_limits<double>::infinity()})
                        for (int steps : {1, 2}) {
                            Stencil perturbed = base;
                            for (int step = 0; step < steps; ++step)
                                perturbed.values[k] = std::nextafter(perturbed.values[k], direction);
                            result.push_back(perturbed);
                        }
            }
    return result;
}

void check_faces(const std::vector<Stencil>& inputs, const std::vector<Faces>& actual,
                 const std::vector<std::size_t>& groups)
{
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        Faces expected{};
        PPMReconstruction::reconstruct_scalar_ppm(
            inputs[i].values, expected.left, expected.right);
        if (!std::isfinite(actual[i].left) || !std::isfinite(actual[i].right)
            || std::bit_cast<std::uint64_t>(actual[i].left) != std::bit_cast<std::uint64_t>(expected.left)
            || std::bit_cast<std::uint64_t>(actual[i].right) != std::bit_cast<std::uint64_t>(expected.right))
            throw std::runtime_error("Shared PPM Host/device bits differ at stencil " + std::to_string(i));
    }
    for (std::size_t group = 0; group < groups.size(); ++group) {
        const std::size_t first = groups[group];
        const std::size_t last = group + 1 < groups.size() ? groups[group + 1] : inputs.size();
        double scale = 0.0;
        for (double value : inputs[first].values) scale = std::max(scale, std::abs(value));
        for (std::size_t i = first + 1; i < last; ++i)
            if (std::abs(actual[i].left - actual[first].left) > kWitnessTolerance * scale
                || std::abs(actual[i].right - actual[first].right) > kWitnessTolerance * scale)
                throw std::runtime_error("PPM perturbation exceeds the witness budget at stencil "
                                         + std::to_string(i));
    }
}

} // namespace

int main()
{
    Stencil* device_inputs = nullptr;
    Faces* device_outputs = nullptr;
    try {
        int devices = 0;
        checked(cudaGetDeviceCount(&devices));
        if (devices == 0) throw std::runtime_error("No CUDA device available for PPM test");
        std::vector<std::size_t> groups;
        const auto inputs = make_stencils(groups);
        std::vector<Faces> outputs(inputs.size());
        checked(cudaMalloc(&device_inputs, inputs.size() * sizeof(Stencil)));
        checked(cudaMalloc(&device_outputs, outputs.size() * sizeof(Faces)));
        checked(cudaMemcpy(device_inputs, inputs.data(), inputs.size() * sizeof(Stencil),
                           cudaMemcpyHostToDevice));
        const int count = static_cast<int>(inputs.size());
        reconstruct<<<(count + kThreads - 1) / kThreads, kThreads>>>(device_inputs, device_outputs, count);
        checked(cudaGetLastError());
        checked(cudaDeviceSynchronize());
        checked(cudaMemcpy(outputs.data(), device_outputs, outputs.size() * sizeof(Faces),
                           cudaMemcpyDeviceToHost));
        check_faces(inputs, outputs, groups);
        checked(cudaFree(device_inputs));
        device_inputs = nullptr;
        checked(cudaFree(device_outputs));
        device_outputs = nullptr;
        std::cout << "Shared PPM CUDA witnesses passed: " << inputs.size()
                  << " stencils, bitwise Host/device parity and64eps sensitivity\n";
        return 0;
    } catch (const std::exception& error) {
        if (device_inputs) cudaFree(device_inputs);
        if (device_outputs) cudaFree(device_outputs);
        std::cerr << "PPM CUDA test failed: " << error.what() << '\n';
        return 1;
    }
}
