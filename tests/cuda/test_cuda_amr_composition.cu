/**
 * @file test_cuda_amr_composition.cu
 * @brief Check composition conservation during CUDA AMR transfers.
 *
 * Exercise shared prolongation cases in every dimension, including trace
 * species, closure roundoff and invalid parent-density controls.
 */
#include "cuda/amr/CoarseFineExchangeKernels.cuh"
#include "../fixtures/amr_composition_test_cases.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void check(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}

template <class T>
class DeviceBuffer {
public:
    explicit DeviceBuffer(std::size_t count)
    {
        check(cudaMalloc(reinterpret_cast<void**>(&pointer_), count * sizeof(T)));
    }
    ~DeviceBuffer() { static_cast<void>(cudaFree(pointer_)); }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    T* get() const { return pointer_; }
private:
    T* pointer_ = nullptr;
};

arch::cuda::DeviceStateView view(double* data, int cells, int species)
{
    return {data, data + cells, data + 2 * cells, data + 3 * cells,
            data + 4 * cells, data + 5 * cells, data + 6 * cells,
            cells, species};
}

void run_case(const amr::test::CompositionCase& example, int dimension,
              bool invalid = false, double invalid_density = 0.0,
              int species_count = amr::test::CompositionCase::species)
{
    using namespace amr::prolongation_math;
    constexpr int cells = amr::test::CompositionCase::cells;
    const int fields = 6 + species_count;
    const int children = 1 << dimension;
    std::vector<double> source(fields * cells);
    for (int field = 0; field < 6; ++field)
        for (int cell = 0; cell < cells; ++cell)
            source[field * cells + cell] = field == 0
                ? example.density[cell] : 1.0 + field + 0.01 * cell;
    std::copy_n(example.fractions.begin(), species_count * cells,
                source.begin() + 6 * cells);
    auto bad_source = source;
    std::fill_n(bad_source.begin(), cells, invalid_density);
    constexpr double untouched = -42.0;
    std::vector<double> destination(fields * children, untouched);
    DeviceBuffer<double> source_device(source.size());
    DeviceBuffer<double> bad_source_device(source.size());
    DeviceBuffer<double> destination_device(destination.size());
    DeviceBuffer<double> scratch(destination.size());
    DeviceBuffer<int> status_device(1);
    check(cudaMemcpy(source_device.get(), source.data(), source.size() * sizeof(double),
                     cudaMemcpyHostToDevice));
    check(cudaMemcpy(bad_source_device.get(), bad_source.data(), source.size() * sizeof(double),
                     cudaMemcpyHostToDevice));
    check(cudaMemcpy(destination_device.get(), destination.data(),
                     destination.size() * sizeof(double), cudaMemcpyHostToDevice));

    std::array<arch::cuda::DeviceExchangeBlock, 3> blocks{};
    blocks[0].state = view(source_device.get(), cells, species_count);
    blocks[1].state = view(destination_device.get(), children, species_count);
    blocks[2].state = view(bad_source_device.get(), cells, species_count);
    DeviceBuffer<arch::cuda::DeviceExchangeBlock> block_device(blocks.size());
    check(cudaMemcpy(block_device.get(), blocks.data(), sizeof(blocks), cudaMemcpyHostToDevice));
    std::vector<arch::cuda::DeviceCoarseFineTransfer> transfers(children);
    for (int child = 0; child < children; ++child) {
        auto& transfer = transfers[child];
        // Mix valid and invalid sources to prove that no valid transfer is
        // scattered when any member of the same exchange plan is rejected.
        transfer.source_block = invalid && child == 0 ? 2 : 0;
        transfer.destination_block = 1;
        transfer.destination_cell = child;
        transfer.source_count = 1;
        transfer.source_cells[0] = 0;
        transfer.prolongation_dimension = dimension;
        std::copy(example.slopes.begin(), example.slopes.end(), transfer.slope_cells);
        for (int axis = 0; axis < dimension; ++axis)
            transfer.fine_position[axis] = (child & (1 << axis)) != 0 ? 0.25 : -0.25;
    }
    DeviceBuffer<arch::cuda::DeviceCoarseFineTransfer> transfer_device(transfers.size());
    check(cudaMemcpy(transfer_device.get(), transfers.data(),
                     transfers.size() * sizeof(transfers[0]), cudaMemcpyHostToDevice));
    check(arch::cuda::launch_coarse_fine_exchange(
        block_device.get(), transfer_device.get(), children, fields,
        scratch.get(), status_device.get(), nullptr));
    check(cudaDeviceSynchronize());
    int status = -1;
    check(cudaMemcpy(&status, status_device.get(), sizeof(int), cudaMemcpyDeviceToHost));
    check(cudaMemcpy(destination.data(), destination_device.get(),
                     destination.size() * sizeof(double), cudaMemcpyDeviceToHost));
    if (invalid) {
        require(status == static_cast<int>(CompositionFamily::InvalidDensity),
                "CUDA invalid density was not rejected by the shared math");
        for (const double value : destination)
            require(value == untouched, "invalid CUDA AMR plan partially scattered");
        return;
    }

    require(status == 0, "valid CUDA AMR plan was rejected");
    auto stencil = example.stencil(dimension);
    stencil.species_count = species_count;
    const auto family = classify_composition_family(stencil);
    require(family == example.family, "CPU family differs from independent fixture expectation");
    std::array<double, amr::test::CompositionCase::species> integrals{};
    for (int child = 0; child < children; ++child) {
        const double* position = transfers[child].fine_position;
        const double density = reconstruct_field(stencil, source.data(), position);
        double composition_sum = 0.0;
        for (int field = 0; field < fields; ++field) {
            const double actual = destination[field * children + child];
            const double expected = field < 6
                ? reconstruct_field(stencil, source.data() + field * cells, position)
                : reconstruct_mass_fraction(stencil, family, density, field - 6, position);
            require(std::isfinite(actual)
                        && std::abs(actual - expected) <= 2.0e-15 * std::max(1.0, std::abs(expected)),
                    "production CUDA AMR kernel differs from shared CPU math");
            if (field >= 6) {
                // Deliberately no tolerance on physical admissibility: the
                // old -2.77556e-17 last species must fail this regression.
                require(actual >= 0.0 && actual <= 1.0,
                        "production CUDA AMR kernel produced negative/invalid X");
                composition_sum += actual;
                integrals[field - 6] += density * actual;
            }
        }
        if (species_count > 0)
            require(std::abs(composition_sum - 1.0) <= 4.0e-16,
                    "production CUDA AMR kernel failed composition closure");
    }
    for (int species = 0; species < species_count; ++species) {
        const double parent = example.density[0] * example.fractions[species * cells];
        require(std::abs(integrals[species] / children - parent) <= 4.0e-16,
                "production CUDA AMR family failed rhoX conservation");
    }
}

} // namespace

int main()
{
    int devices = 0;
    const cudaError_t probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) {
        static_cast<void>(cudaGetLastError());
        std::cout << "SKIP: CUDA runtime device unavailable\n";
        return 77;
    }
    try {
        check(probe);
        const auto cases = amr::test::composition_cases();
        for (int dimension = 1; dimension <= 3; ++dimension) {
            for (const auto& example : cases) run_case(example, dimension);
            for (const double density : {0.0, -1.0,
                                        std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::infinity()}) {
                run_case(cases[0], dimension, true, density);
                run_case(cases[0], dimension, true, density, 0);
            }
        }
        std::cout << "CUDA_AMR_COMPOSITION_PASS valid=12 invalid_no_scatter=24\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
