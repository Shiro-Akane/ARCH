/**
 * @file test_cuda_regrid_migration.cu
 * @brief Compare production GPU regrid transfers with CPU results.
 *
 * Refinement and coarsening cases cover all supported coordinate systems,
 * dimensions and several species-storage sizes.
 */
#include "cuda/amr/RegridMigration.h"
#include "../fixtures/regrid_migration_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void check(cudaError_t result)
{
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class T> struct Buffer {
    T* pointer = nullptr;
    explicit Buffer(std::size_t count) { if (count) check(cudaMalloc(&pointer, count * sizeof(T))); }
    ~Buffer() { static_cast<void>(cudaFree(pointer)); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

struct DeviceBlock {
    Buffer<double> state;
    Buffer<double> volume;
    arch::cuda::DeviceRegridBlock binding{};
    std::vector<double> packed;
    explicit DeviceBlock(const amr::Block& host)
        : state(static_cast<std::size_t>(host.grid.GetTotalSize())
                * (6 + host.fluid_state.GetNumSpecies())),
          volume(host.grid.GetTotalSize())
    {
        const int cells = host.grid.GetTotalSize();
        const int species = host.fluid_state.GetNumSpecies();
        packed.resize(static_cast<std::size_t>(cells) * (6 + species));
        const auto fields = amr::test::regrid_fields(host.fluid_state);
        for (int field = 0; field < 6; ++field)
            std::copy_n(fields[field], cells, packed.data() + field * cells);
        std::copy(host.fluid_state.mass_fractions.begin(), host.fluid_state.mass_fractions.end(),
                  packed.data() + 6 * cells);
        check(cudaMemcpy(state.pointer, packed.data(), packed.size() * sizeof(double), cudaMemcpyHostToDevice));
        binding.state = {state.pointer, state.pointer + cells, state.pointer + 2 * cells,
                         state.pointer + 3 * cells, state.pointer + 4 * cells,
                         state.pointer + 5 * cells, state.pointer + 6 * cells, cells, species};
        binding.grid = arch::cuda::make_device_grid_view(host.grid);
        std::vector<double> volumes(cells, 0.0);
        for (int k = host.grid.Ks(); k < host.grid.Ke(); ++k)
            for (int j = host.grid.Js(); j < host.grid.Je(); ++j)
                for (int i = host.grid.Is(); i < host.grid.Ie(); ++i)
                    volumes[host.grid.GetIndex(i, j, k)] = GridMetrics::CellVolume(host.grid, i, j, k);
        check(cudaMemcpy(volume.pointer, volumes.data(), volumes.size() * sizeof(double), cudaMemcpyHostToDevice));
        binding.grid.cell_volume = volume.pointer;
    }
    void compare(const amr::Block& expected)
    {
        check(cudaMemcpy(packed.data(), state.pointer, packed.size() * sizeof(double), cudaMemcpyDeviceToHost));
        const int cells = expected.grid.GetTotalSize();
        const auto fields = amr::test::regrid_fields(expected.fluid_state);
        for (int cell = 0; cell < cells; ++cell) {
            for (int field = 0; field < 6 + expected.fluid_state.GetNumSpecies(); ++field) {
                const double actual = packed[field * cells + cell];
                const double reference = field < 6 ? fields[field][cell]
                    : expected.fluid_state.X(field - 6, cell);
                require(std::isfinite(actual)
                    && std::abs(actual - reference) <= 3.0e-14 * std::max(1.0, std::abs(reference)),
                    "device regrid migration differs from Host Block authority");
                if (field >= 6) require(actual >= 0.0, "device migration produced negative species");
            }
        }
    }
};

void run(int dimension, int species, const std::string& geometry)
{
    auto parent = amr::test::regrid_block(dimension, 0, 0, species, geometry);
    amr::test::seed_regrid_parent(parent);
    DeviceBlock source(parent);
    const int count = 1 << dimension;
    std::vector<amr::Block> children;
    std::vector<std::unique_ptr<DeviceBlock>> device_children;
    children.reserve(count);
    arch::cuda::DeviceRegridChildren bindings{};
    const auto max_work = static_cast<std::size_t>(source.binding.grid.active_cell_count())
        * amr::regrid_math::prolongation_workspace_per_species * species;
    Buffer<double> workspace(max_work);
    Buffer<int> status(1);
    check(cudaMemset(status.pointer, 0, sizeof(int)));
    for (int child = 0; child < count; ++child) {
        children.push_back(amr::test::regrid_block(dimension, 1, child, species, geometry));
        device_children.push_back(std::make_unique<DeviceBlock>(children.back()));
        children.back().InterpolateFromCoarse(parent, child, dimension, 1.0e-10, 1.0e-10);
        bindings.blocks[child] = device_children.back()->binding;
        check(arch::cuda::launch_cuda_regrid_prolongation(source.binding,
            bindings.blocks[child], child, 1.0e-10, 1.0e-10,
            workspace.pointer, max_work, status.pointer, nullptr));
    }
    check(cudaDeviceSynchronize());
    int result = -1;
    check(cudaMemcpy(&result, status.pointer, sizeof(int), cudaMemcpyDeviceToHost));
    require(result == 0, "device prolongation rejected valid family");
    source.compare(parent); // Old active sources must remain untouched.
    for (int child = 0; child < count; ++child) device_children[child]->compare(children[child]);
    auto coarse = amr::test::regrid_block(dimension, 0, 0, species, geometry);
    DeviceBlock destination(coarse);
    const amr::Block* pointers[8]{};
    for (int child = 0; child < count; ++child) pointers[child] = &children[child];
    coarse.AverageToCoarse(pointers, dimension, 1.0e-10, 1.0e-10);
    check(arch::cuda::launch_cuda_regrid_restriction(bindings, destination.binding,
        1.0e-10, 1.0e-10, workspace.pointer, max_work, status.pointer, nullptr));
    check(cudaDeviceSynchronize());
    check(cudaMemcpy(&result, status.pointer, sizeof(int), cudaMemcpyDeviceToHost));
    require(result == 0, "device restriction rejected valid family");
    destination.compare(coarse);
    for (int child = 0; child < count; ++child) device_children[child]->compare(children[child]);

    // Malformed public device views must fail before launching/indexing.
    auto malformed = source.binding;
    malformed.grid.stride_y = malformed.grid.total_x - 1;
    require(arch::cuda::launch_cuda_regrid_prolongation(malformed,
        bindings.blocks[0], 0, 1.0e-10, 1.0e-10,
        workspace.pointer, max_work, status.pointer, nullptr) == cudaErrorInvalidValue,
        "invalid row stride was accepted by migration launcher");
    malformed = destination.binding;
    malformed.grid.stride_z = 0;
    require(arch::cuda::launch_cuda_regrid_restriction(bindings, malformed,
        1.0e-10, 1.0e-10, workspace.pointer, max_work, status.pointer, nullptr)
        == cudaErrorInvalidValue, "invalid plane stride was accepted by migration launcher");
    destination.compare(coarse);

    // Invalid source density is reported without mutating the active source.
    auto bad = parent;
    std::fill(bad.fluid_state.rho.begin(), bad.fluid_state.rho.end(), -1.0);
    DeviceBlock bad_source(bad);
    check(cudaMemset(status.pointer, 0, sizeof(int)));
    check(arch::cuda::launch_cuda_regrid_prolongation(bad_source.binding,
        bindings.blocks[0], 0, 1.0e-10, 1.0e-10,
        workspace.pointer, max_work, status.pointer, nullptr));
    check(cudaDeviceSynchronize());
    check(cudaMemcpy(&result, status.pointer, sizeof(int), cudaMemcpyDeviceToHost));
    require(result == static_cast<int>(amr::regrid_math::Status::ParentFluid),
            "device migration failed to propagate shared fluid guard");
    bad_source.compare(bad);
    device_children[0]->compare(children[0]);
    std::cout << "CUDA_REGRID_MIGRATION_PASS dim=" << dimension
              << " species=" << species << " geometry=" << geometry << '\n';
}

} // namespace

int main()
{
    int count = 0;
    const auto probe = cudaGetDeviceCount(&count);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && count == 0)) return 77;
    try {
        check(probe);
        for (const char* geometry : {"cartesian", "cylindrical", "spherical"})
            for (int dimension = 1; dimension <= 3; ++dimension)
                run(dimension, 4, geometry);
        run(1, 0, "cartesian");
        run(2, 21, "cartesian");
        run(2, 41, "cartesian");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
