/**
 * @file test_grid_metrics_cache.cu
 * @brief Compare cached device geometry with the shared grid measures.
 *
 * Verify metric parity and rejection of invalid geometry before kernels
 * consume the cache.
 */
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "amr/storage/Block.h"
#include "cuda/common/GridMetricsCache.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "grid/GridMetrics.h"

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

/** Native fixtures use the real Block generator, never copied face formulas.
 * Workflow: define an actual root; bind its periodic rule; set the actual
 * logical block; invoke the production InitGeometry owner. No fluid storage,
 * EOS, Runtime, boundary callback, or GPU allocation is needed for this task.
 */
void initialize_native_block(amr::Block& block, const Grid& root,
    int level, std::uint32_t radial, std::uint32_t axial)
{
    block.Reset();
    block.level = level;
    block.logical_x1 = radial;
    block.logical_x2 = axial;
    block.InitGeometry(root,
        (root.x1_max - root.x1_min) / (root.nblockx1 * amr::BLOCK_NX),
        (root.x2_max - root.x2_min) / (root.nblockx2 * amr::BLOCK_NY), 1.0,
        GridMetrics::GeometrySemantics::AxisymmetricRz);
    block.RequireNativeGeometryIdentity();
}

/** Root endpoints are deliberately nonbinary; the actual owner supplies faces. */
Grid native_root(bool axis, int radial_roots, int axial_roots, bool periodic)
{
    Grid root(amr::MAX_NG, axis ? 0.0 : 0.4, 1.3, -0.3, 0.7,
        0.0, 1.0, radial_roots, axial_roots, 1);
    root.geometry = "cylindrical";
    root.dim = 2;
    root.dyadic_identity.periodic_axial = periodic;
    return root;
}

/** Compare payload/coordinate bits, distinct from approximate metric parity. */
void require_equal_bits(double actual, double expected, const char* context)
{
    require(std::bit_cast<std::uint64_t>(actual)
                == std::bit_cast<std::uint64_t>(expected),
            std::string("native copied geometry changed coordinate bits: ") + context);
}

/** Exercise an old synthetic lowering shape without any native/root members. */
struct OrdinarySyntheticGrid {
    int dim, ng, stride_y, stride_z, total_size, total_x, total_y, total_z;
    int is, ie, js, je, ks, ke;
    std::string geometry = "cartesian";
    std::uint8_t amr_coarse_fine_face[6]{};
    double dx1, dx2, dx3, x1_min, x2_min, x3_min, x1_max, x2_max, x3_max;

    explicit OrdinarySyntheticGrid(const arch::cuda::DeviceGridView& grid)
        : dim(grid.dim), ng(grid.ng), stride_y(grid.stride_y),
          stride_z(grid.stride_z), total_size(grid.total_size),
          total_x(grid.total_x), total_y(grid.total_y), total_z(grid.total_z),
          is(grid.is), ie(grid.ie), js(grid.js), je(grid.je), ks(grid.ks), ke(grid.ke),
          dx1(grid.dx1), dx2(grid.dx2), dx3(grid.dx3),
          x1_min(grid.x1_min), x2_min(grid.x2_min), x3_min(grid.x3_min),
          x1_max(grid.x1_max), x2_max(grid.x2_max), x3_max(grid.x3_max) {}
    int GetTotalSize() const { return total_size; }
    int GetTotalX() const { return total_x; }
    int GetTotalY() const { return total_y; }
    int GetTotalZ() const { return total_z; }
    int Is() const { return is; }
    int Ie() const { return ie; }
    int Js() const { return js; }
    int Je() const { return je; }
    int Ks() const { return ks; }
    int Ke() const { return ke; }
};

/** Reject absent/stale native context before this owner allocates GPU storage.
 * These are real Grid mutations or copied-POD self-consistency checks, not a
 * claim that an arbitrary still-self-consistent root mutation can be detected
 * without the external topology owner. Every existing synthetic/ordinary
 * lowering remains legal; Native requires explicit real provenance.
 */
void run_native_lowering_preflight()
{
    using GridMetrics::GeometrySemantics;
    const auto ordinary = make_grid(2, "cartesian", amr::MAX_NG);
    const OrdinarySyntheticGrid synthetic(arch::cuda::make_device_grid_view(ordinary));
    require(arch::cuda::valid_hydro_grid(arch::cuda::make_device_grid_view(synthetic)),
        "old synthetic ordinary lowering lost compatibility");
    const auto rejects = [](const auto& operation, const char* label) {
        bool rejected = false;
        try { operation(); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, std::string("native lowering failed to reject before allocation: ") + label);
    };
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(
        synthetic, GeometrySemantics::AxisymmetricRz)); }, "synthetic has no actual root context");

    const auto root = native_root(false, 3, 5, true);
    amr::Block block;
    initialize_native_block(block, root, 3, 2, 7);
    const auto& host = block.grid;
    const auto device = arch::cuda::make_device_grid_view(host, GeometrySemantics::AxisymmetricRz);
    require(arch::cuda::valid_hydro_grid(device), "real Native lowering was rejected");
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(host)); }, "implicit Existing on bound Native");
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(
        host, GeometrySemantics::Existing)); }, "explicit Existing on bound Native");
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(
        host, static_cast<GeometrySemantics>(99))); }, "unknown semantics");
    auto bad_host = host;
    bad_host.dyadic_identity.bound = false;
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(
        bad_host, GeometrySemantics::AxisymmetricRz)); }, "unbound Native context");
    bad_host = host;
    ++bad_host.nblockx1;
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(
        bad_host, GeometrySemantics::AxisymmetricRz)); }, "actual root count differs");
    bad_host = host;
    bad_host.x1_max = std::nextafter(host.x1_max, std::numeric_limits<double>::infinity());
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(
        bad_host, GeometrySemantics::AxisymmetricRz)); }, "one-ULP actual upper differs");
    bad_host = host;
    bad_host.dx2 = std::nextafter(host.dx2, std::numeric_limits<double>::infinity());
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(
        bad_host, GeometrySemantics::AxisymmetricRz)); }, "one-ULP representative spacing differs");
    bad_host = host;
    ++bad_host.dyadic_identity.logical[0];
    rejects([&] { static_cast<void>(arch::cuda::make_device_grid_view(
        bad_host, GeometrySemantics::AxisymmetricRz)); }, "stale logical block identity");

    auto bad_device = device;
    bad_device.dyadic_identity.level = amr::kMaxRefinementLevel + 1;
    require(!arch::cuda::valid_hydro_grid(bad_device), "invalid copied Native level accepted");
    bad_device = device;
    bad_device.x2_max = std::nextafter(device.x2_max, std::numeric_limits<double>::infinity());
    require(!arch::cuda::valid_hydro_grid(bad_device), "stale copied Native endpoint accepted");
    bad_device = device;
    bad_device.semantics = GeometrySemantics::Existing;
    require(!arch::cuda::valid_hydro_grid(bad_device), "copied Native chart relabel accepted");
    bad_device = device;
    bad_device.dyadic_identity.bound = false;
    require(!arch::cuda::valid_hydro_grid(bad_device), "copied Native without provenance accepted");
    std::cout << "Native geometry lowering preflight passed before GPU allocation\n";
}

/** Export the actual device canonical coordinate leaf, including real ghosts.
 * Formula ownership stays in GeometryView. Eight value planes carry radial
 * and axial center/lower/upper/width, so a correct active metric cache cannot
 * conceal a dropped periodic-ghost or deep-coordinate identity.
 */
__global__ void native_coordinate_payload(arch::cuda::DeviceGridView grid,
    int columns, double* output)
{
    const auto geometry = arch::cuda::make_grid_geometry_view(grid);
    for (int cell = static_cast<int>(threadIdx.x); cell < columns;
         cell += static_cast<int>(blockDim.x)) {
        const bool radial = cell < grid.total_x;
        const bool axial = cell < grid.total_y;
        output[cell] = radial ? geometry.GetCellCenterX(cell) : 0.0;
        output[columns + cell] = radial ? geometry.GetFacePosL(cell) : 0.0;
        output[2 * columns + cell] = radial ? geometry.GetFacePosR(cell) : 0.0;
        output[3 * columns + cell] = radial ? geometry.CellWidth(0, cell) : 0.0;
        output[4 * columns + cell] = axial ? geometry.GetCellCenterY(cell) : 0.0;
        output[5 * columns + cell] = axial ? geometry.GetAxialFacePosL(cell) : 0.0;
        output[6 * columns + cell] = axial ? geometry.GetAxialFacePosR(cell) : 0.0;
        output[7 * columns + cell] = axial ? geometry.CellWidth(1, cell) : 0.0;
    }
}

/** Compare genuine device payload bits with the actual Host Grid generator.
 * An owned sentinel arena and fence preserve failure lifetime. This helper
 * touches no fluid/EOS/source arrays and does not exercise Native evolution.
 */
void require_device_coordinate_payload(const Grid& host,
    const arch::cuda::DeviceGridView& grid)
{
    const int columns = std::max(host.GetTotalX(), host.GetTotalY());
    const int count = 8 * columns;
    CacheOwner arena(count);
    arena.reset();
    native_coordinate_payload<<<1, 32, 0, arena.stream>>>(grid, columns, arena.values);
    check(cudaGetLastError());
    const auto actual = arena.download();
    for (int cell = 0; cell < columns; ++cell) {
        const bool radial = cell < host.GetTotalX();
        const bool axial = cell < host.GetTotalY();
        const double expected[]{
            radial ? host.GetCellCenterX(cell) : 0.0,
            radial ? host.GetFacePosL(cell) : 0.0,
            radial ? host.GetFacePosR(cell) : 0.0,
            radial ? host.CellWidth(0, cell) : 0.0,
            axial ? host.GetCellCenterY(cell) : 0.0,
            axial ? host.GetAxialFacePosL(cell) : 0.0,
            axial ? host.GetAxialFacePosR(cell) : 0.0,
            axial ? host.CellWidth(1, cell) : 0.0};
        for (int component = 0; component < 8; ++component)
            require_equal_bits(actual[component * columns + cell], expected[component],
                "actual GPU canonical coordinate payload");
    }
    require(std::all_of(actual.begin() + count, actual.end(),
        [](double value) { return value == CacheOwner::poison; }),
        "Native coordinate payload wrote beyond its owned output range");
}

/** Full copied geometry and actual device caches borrow one common math owner.
 * Original metric error budget remains 64*epsilon*max(1,abs(reference)). The
 * coordinate payload and periodic represented-source aliases are bit checked.
 * This qualifies geometry metadata/cache only, not any Native solver backend.
 */
void run_native_parity(bool axis, int radial_roots, int axial_roots,
    int level, std::uint32_t radial, std::uint32_t axial, bool periodic)
{
    using GridMetrics::GeometrySemantics;
    const auto root = native_root(axis, radial_roots, axial_roots, periodic);
    amr::Block block;
    initialize_native_block(block, root, level, radial, axial);
    const auto& host = block.grid;
    const auto grid = arch::cuda::make_device_grid_view(host, GeometrySemantics::AxisymmetricRz);
    const auto actual_geometry = arch::cuda::make_grid_geometry_view(grid);
    const auto expected_geometry = GridMetrics::make_geometry_view(host, GeometrySemantics::AxisymmetricRz);
    require(actual_geometry.semantics == expected_geometry.semantics
        && GridMetrics::equal_identity(actual_geometry.dyadic_identity, host.dyadic_identity),
        "Native adapter dropped the owned chart/root identity");
    require_equal_bits(actual_geometry.actual_block_upper[0], host.x1_max, "actual radial upper");
    require_equal_bits(actual_geometry.actual_block_upper[1], host.x2_max, "actual axial upper");
    for (int i = 0; i < host.GetTotalX(); ++i) {
        require_equal_bits(actual_geometry.GetCellCenterX(i), host.GetCellCenterX(i), "radial center");
        require_equal_bits(actual_geometry.GetFacePosL(i), host.GetFacePosL(i), "radial lower");
        require_equal_bits(actual_geometry.GetFacePosR(i), host.GetFacePosR(i), "radial upper");
        require_equal_bits(actual_geometry.CellWidth(0, i), host.CellWidth(0, i), "radial width");
    }
    for (int j = 0; j < host.GetTotalY(); ++j) {
        require_equal_bits(actual_geometry.GetCellCenterY(j), host.GetCellCenterY(j), "axial center");
        require_equal_bits(actual_geometry.GetAxialFacePosL(j), host.GetAxialFacePosL(j), "axial lower");
        require_equal_bits(actual_geometry.GetAxialFacePosR(j), host.GetAxialFacePosR(j), "axial upper");
        require_equal_bits(actual_geometry.CellWidth(1, j), host.CellWidth(1, j), "axial width");
    }
    const auto axial_blocks = static_cast<std::uint32_t>(axial_roots) << level;
    if (periodic && (axial == 0 || axial + 1 == axial_blocks)) {
        amr::Block represented;
        const bool lower = axial == 0;
        initialize_native_block(represented, root, level, radial,
            lower ? axial_blocks - 1 : 0);
        const int ghost = lower ? host.Js() - 1 : host.Je();
        const int source = lower ? represented.grid.Je() - 1 : represented.grid.Js();
        require_equal_bits(actual_geometry.GetAxialFacePosL(ghost),
            represented.grid.GetAxialFacePosL(source), "periodic alias lower endpoint");
        require_equal_bits(actual_geometry.GetAxialFacePosR(ghost),
            represented.grid.GetAxialFacePosR(source), "periodic alias upper endpoint");
        require_equal_bits(actual_geometry.GetCellCenterY(ghost),
            represented.grid.GetCellCenterY(source), "periodic alias center");
        require_equal_bits(actual_geometry.CellWidth(1, ghost),
            represented.grid.CellWidth(1, source), "periodic alias width");
    }

    require_device_coordinate_payload(host, grid);
    CacheOwner cache(grid.total_size);
    cache.reset();
    check(arch::cuda::launch_cuda_grid_metrics_cache(grid, cache.view, cache.stream));
    const auto values = cache.download();
    for (int array = 0; array < 7; ++array) {
        for (std::size_t index = 0; index < cache.pitch; ++index) {
            double expected = CacheOwner::poison;
            if (index < static_cast<std::size_t>(grid.total_size)) {
                expected = 0.0;
                const int k = static_cast<int>(index / grid.stride_z);
                const int j = static_cast<int>((index % grid.stride_z) / grid.stride_y);
                const int i = static_cast<int>((index % grid.stride_z) % grid.stride_y);
                if (i >= grid.is && i < grid.ie && j >= grid.js && j < grid.je
                    && k >= grid.ks && k < grid.ke) {
                    if (array == 0) expected = GridMetrics::CellVolume(expected_geometry, i, j, k);
                    else {
                        const int direction = array <= 3 ? array - 1 : array - 4;
                        if (direction < grid.dim) expected = GridMetrics::FaceArea(
                            expected_geometry, direction, i, j, k, array >= 4);
                    }
                }
            }
            const double actual = values[array * cache.pitch + index];
            if (expected == 0.0)
                require(actual == 0.0 && !std::signbit(actual), "Native inactive cache is not positive zero");
            else if (expected == CacheOwner::poison)
                require(actual == expected, "Native cache capacity guard was overwritten");
            else
                require(std::isfinite(actual) && std::abs(actual - expected)
                    <= 64.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(expected)),
                    "Native device metric differs from shared actual Host measure");
        }
    }
}

/** Actual cache-launch rejection occurs before occupancy/kernel and any writes.
 * GPU storage here is an already-owned sentinel, so this is distinct from the
 * preallocation Host lowering checks above and never claims allocation rollback.
 */
void run_native_cache_rejection()
{
    using GridMetrics::GeometrySemantics;
    const auto root = native_root(false, 3, 5, true);
    amr::Block block;
    initialize_native_block(block, root, 3, 2, 7);
    const auto grid = arch::cuda::make_device_grid_view(block.grid, GeometrySemantics::AxisymmetricRz);
    CacheOwner cache(grid.total_size);
    cache.reset();
    const auto reject = [&](arch::cuda::DeviceGridView invalid, const char* reason) {
        require(arch::cuda::launch_cuda_grid_metrics_cache(invalid, cache.view, cache.stream)
            == cudaErrorInvalidValue, std::string("Native cache accepted invalid context: ") + reason);
        const auto unchanged = cache.download();
        require(std::all_of(unchanged.begin(), unchanged.end(),
            [](double value) { return value == CacheOwner::poison; }),
            std::string("Native rejection modified cache storage: ") + reason);
    };
    auto bad = grid;
    bad.x1_max = std::nextafter(grid.x1_max, std::numeric_limits<double>::infinity());
    reject(bad, "actual upper endpoint");
    bad = grid;
    ++bad.dyadic_identity.logical[1];
    reject(bad, "stale axial block");
    bad = grid;
    bad.semantics = GeometrySemantics::Existing;
    reject(bad, "relabelled chart");
    bad = grid;
    bad.dyadic_identity.bound = false;
    reject(bad, "missing bound root");
    bad = grid;
    bad.semantics = static_cast<GeometrySemantics>(99);
    reject(bad, "unknown chart");
}

} // namespace

int main()
{
    try {
        run_native_lowering_preflight();
    } catch (const std::exception& error) {
        std::cerr << "Native lowering preflight failure: " << error.what() << '\n';
        return 1;
    }
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
        run_native_parity(true, 1, 1, 0, 0, 0, false);
        run_native_parity(false, 3, 5, 3, 2, 7, false);
        run_native_parity(true, 1, 1, 3, 0, 0, true);
        run_native_parity(false, 3, 5, 14, 3U * (1U << 14) - 1,
            5U * (1U << 14) - 1, true);
        run_native_parity(false, 3, 5, 15, 3U * (1U << 15) - 1,
            5U * (1U << 15) - 1, true);
        run_native_cache_rejection();
        std::cout << "Native geometry: five real canonical payload/cache layouts passed; backend qualification unchanged\n";
        std::cout << "Grid metric cache: 27 shared-math parity layouts and invalid-input guards passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Grid metric cache failure: " << error.what() << '\n';
        return 1;
    }
}
