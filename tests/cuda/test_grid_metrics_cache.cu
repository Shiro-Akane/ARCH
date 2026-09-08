#include "cuda/common/GridMetricsCache.h"
#include "grid/GridMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

struct CacheOwner {
    static constexpr double poison = -137.25;
    double* values = nullptr;
    cudaStream_t stream = nullptr;
    const std::size_t pitch;
    arch::cuda::GridMetricsCacheView view{};
    std::vector<double> host_values;

    explicit CacheOwner(int count)
        : pitch(static_cast<std::size_t>(count) + 13), host_values(7 * pitch)
    {
        check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
        const auto allocated = cudaMalloc(&values, 7 * pitch * sizeof(double));
        if (allocated != cudaSuccess) {
            static_cast<void>(cudaStreamDestroy(stream));
            check(allocated);
        }
        view.cell_volume = values;
        for (int axis = 0; axis < 3; ++axis) {
            view.face_area_lower[axis] = values + (axis + 1) * pitch;
            view.face_area_upper[axis] = values + (axis + 4) * pitch;
        }
        view.capacity = pitch;
    }

    ~CacheOwner()
    {
        // Failure paths must not retire arrays that a queued test still owns.
        if (cudaStreamSynchronize(stream) != cudaSuccess) std::terminate();
        static_cast<void>(cudaFree(values));
        static_cast<void>(cudaStreamDestroy(stream));
    }
    CacheOwner(const CacheOwner&) = delete;
    CacheOwner& operator=(const CacheOwner&) = delete;

    void reset()
    {
        // Member ownership lets the destructor fence even a failed transfer
        // before its Host source/destination is destroyed.
        std::fill(host_values.begin(), host_values.end(), poison);
        check(cudaMemcpyAsync(values, host_values.data(), host_values.size() * sizeof(double),
                              cudaMemcpyHostToDevice, stream));
        check(cudaStreamSynchronize(stream));
    }

    std::vector<double> download()
    {
        check(cudaMemcpyAsync(host_values.data(), values, host_values.size() * sizeof(double),
                              cudaMemcpyDeviceToHost, stream));
        check(cudaStreamSynchronize(stream));
        return host_values;
    }
};

Grid make_grid(int dim, const std::string& geometry, int ng)
{
    const bool cartesian = geometry == "cartesian";
    Grid grid(ng, cartesian ? -1.75 : 1.125, cartesian ? 2.125 : 2.875,
              geometry == "spherical" ? 0.37 : -2.25,
              geometry == "spherical" ? 2.43 : 1.5, -0.625, 1.875);
    grid.dim = dim;
    grid.geometry = geometry;
    grid.InitializeTopology();
    return grid;
}

void run_parity(int dim, const std::string& geometry, int variant)
{
    const Grid host = make_grid(dim, geometry, variant == 1 ? 0 : amr::MAX_NG);
    auto grid = arch::cuda::make_device_grid_view(host);
    if (variant == 2) {
        // Exercise row padding, plane padding, and unused tail capacity in
        // addition to the canonical allocation's ghost/inactive dimensions.
        grid.stride_y += 5;
        grid.stride_z = grid.stride_y * grid.total_y + 7;
        grid.total_size = grid.stride_z * grid.total_z + 11;
    }
    if (dim < 3) {
        grid.dx3 = std::numeric_limits<double>::quiet_NaN();
        grid.x3_min = std::numeric_limits<double>::infinity();
        grid.x3_max = -std::numeric_limits<double>::infinity();
    }
    if (dim == 1) {
        grid.dx2 = -1.0;
        grid.x2_min = std::numeric_limits<double>::quiet_NaN();
        grid.x2_max = std::numeric_limits<double>::quiet_NaN();
    }
    CacheOwner cache(grid.total_size);
    cache.reset();
    // Existing const caches are not inputs to geometry initialization.
    grid.cell_volume = cache.view.face_area_upper[2];
    check(arch::cuda::launch_cuda_grid_metrics_cache(grid, cache.view, cache.stream));
    const auto actual = cache.download();
    std::vector<double> expected(actual.size(), CacheOwner::poison);
    for (int array = 0; array < 7; ++array)
        std::fill_n(expected.data() + array * cache.pitch, grid.total_size, 0.0);
    for (int k = grid.ks; k < grid.ke; ++k) {
        for (int j = grid.js; j < grid.je; ++j) {
            for (int i = grid.is; i < grid.ie; ++i) {
                const auto cell = static_cast<std::size_t>(k) * grid.stride_z
                    + static_cast<std::size_t>(j) * grid.stride_y + i;
                expected[cell] = GridMetrics::CellVolume(host, i, j, k);
                for (int axis = 0; axis < dim; ++axis) {
                    expected[(axis + 1) * cache.pitch + cell] =
                        GridMetrics::FaceArea(host, axis, i, j, k, false);
                    expected[(axis + 4) * cache.pitch + cell] =
                        GridMetrics::FaceArea(host, axis, i, j, k, true);
                }
            }
        }
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto context = geometry + " dim=" + std::to_string(dim)
            + " variant=" + std::to_string(variant) + " offset=" + std::to_string(index);
        if (expected[index] == 0.0) {
            require(actual[index] == 0.0 && !std::signbit(actual[index]),
                    "ghost/padding/inactive cache must be positive zero: " + context);
        } else if (expected[index] == CacheOwner::poison) {
            require(actual[index] == CacheOwner::poison,
                    "cache capacity guard was overwritten: " + context);
        } else {
            const double tolerance = 64.0 * std::numeric_limits<double>::epsilon()
                * std::max(1.0, std::abs(expected[index]));
            require(std::isfinite(actual[index])
                        && std::abs(actual[index] - expected[index]) <= tolerance,
                    "cache differs from shared Host GridMetrics: " + context);
        }
    }
}

void run_invalid_inputs()
{
    const Grid host = make_grid(3, "spherical", amr::MAX_NG);
    const auto grid = arch::cuda::make_device_grid_view(host);
    CacheOwner cache(grid.total_size);
    cache.reset();
    const auto reject = [&](arch::cuda::DeviceGridView bad_grid,
                            arch::cuda::GridMetricsCacheView bad_cache,
                            const char* label) {
        require(arch::cuda::launch_cuda_grid_metrics_cache(
                    bad_grid, bad_cache, cache.stream) == cudaErrorInvalidValue,
                std::string("malformed cache input was accepted: ") + label);
        const auto contents = cache.download();
        require(std::all_of(contents.begin(), contents.end(),
                            [](double value) { return value == CacheOwner::poison; }),
                std::string("rejected input modified an output: ") + label);
    };
    auto bad_cache = cache.view;
    bad_cache.capacity = static_cast<std::size_t>(grid.total_size - 1);
    reject(grid, bad_cache, "short capacity");
    bad_cache = cache.view;
    bad_cache.face_area_upper[1] = nullptr;
    reject(grid, bad_cache, "null array");
    bad_cache = cache.view;
    bad_cache.face_area_lower[0] = bad_cache.cell_volume;
    reject(grid, bad_cache, "aliased arrays");
    bad_cache = cache.view;
    bad_cache.face_area_upper[2] = bad_cache.cell_volume + 1;
    reject(grid, bad_cache, "partially overlapping arrays");
    bad_cache = cache.view;
    bad_cache.cell_volume = reinterpret_cast<double*>(
        reinterpret_cast<std::uintptr_t>(cache.values) + 1);
    reject(grid, bad_cache, "misaligned array");
    bad_cache = cache.view;
    bad_cache.cell_volume = reinterpret_cast<double*>(
        std::numeric_limits<std::uintptr_t>::max() & ~(alignof(double) - 1U));
    reject(grid, bad_cache, "overflowing address range");

    auto bad_grid = grid;
    bad_grid.geometry = -1;
    reject(bad_grid, cache.view, "unsupported geometry");
    bad_grid = grid;
    bad_grid.dim = 4;
    reject(bad_grid, cache.view, "unsupported dimension");
    bad_grid = grid;
    bad_grid.stride_y = grid.total_x - 1;
    reject(bad_grid, cache.view, "short row stride");
    bad_grid = grid;
    bad_grid.stride_z = grid.stride_y * grid.total_y - 1;
    reject(bad_grid, cache.view, "short plane stride");
    bad_grid = grid;
    bad_grid.total_size = 1;
    reject(bad_grid, cache.view, "out-of-allocation logical grid");
    bad_grid = grid;
    bad_grid.ie = grid.total_x + 1;
    reject(bad_grid, cache.view, "invalid active range");
    bad_grid = grid;
    bad_grid.dx1 = 0.0;
    reject(bad_grid, cache.view, "zero active spacing");
    bad_grid = grid;
    bad_grid.dx2 = std::numeric_limits<double>::quiet_NaN();
    reject(bad_grid, cache.view, "nonfinite active spacing");
    bad_grid = grid;
    bad_grid.x3_min = std::numeric_limits<double>::infinity();
    reject(bad_grid, cache.view, "nonfinite active origin");
    bad_grid = grid;
    bad_grid.x1_max = grid.x1_min;
    reject(bad_grid, cache.view, "nonpositive active extent");
}

} // namespace

int main()
{
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) {
        std::cout << "SKIP: CUDA device/driver unavailable\n";
        return 77;
    }
    try {
        check(probe);
        check(cudaSetDevice(0));
        for (const std::string geometry : {"cartesian", "cylindrical", "spherical"})
            for (int dim = 1; dim <= 3; ++dim)
                for (int variant = 0; variant < 3; ++variant)
                    run_parity(dim, geometry, variant);
        run_invalid_inputs();
        std::cout << "Grid metric cache: 27 shared-math parity layouts and invalid-input guards passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Grid metric cache failure: " << error.what() << '\n';
        return 1;
    }
}
