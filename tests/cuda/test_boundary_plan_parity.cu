#include "amr/BoundaryPlan.h"
#include "cuda/hydro/Boundary.cuh"
#include "driver/DriverUtils.h"

#include <cuda_runtime.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
using namespace arch::boundary;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename T>
class DeviceArray
{
public:
    explicit DeviceArray(std::size_t count) : count_(count)
    {
        if (count_ != 0
            && cudaMalloc(reinterpret_cast<void**>(&data_), count_ * sizeof(T))
                != cudaSuccess)
            throw std::runtime_error("cudaMalloc failed");
    }

    ~DeviceArray() { cudaFree(data_); }
    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;

    T* get() const noexcept { return data_; }

    void upload(const std::vector<T>& values)
    {
        require(values.size() == count_, "DeviceArray upload size mismatch");
        if (count_ != 0)
            require(cudaMemcpy(data_, values.data(), count_ * sizeof(T),
                               cudaMemcpyHostToDevice) == cudaSuccess,
                    "cudaMemcpy upload failed");
    }

    std::vector<T> download() const
    {
        std::vector<T> values(count_);
        if (count_ != 0)
            require(cudaMemcpy(values.data(), data_, count_ * sizeof(T),
                               cudaMemcpyDeviceToHost) == cudaSuccess,
                    "cudaMemcpy download failed");
        return values;
    }

private:
    T* data_ = nullptr;
    std::size_t count_ = 0;
};

struct DeviceStateOwner
{
    DeviceArray<double> rho;
    DeviceArray<double> mom_u;
    DeviceArray<double> mom_v;
    DeviceArray<double> mom_w;
    DeviceArray<double> eng;
    DeviceArray<double> enuc;
    DeviceArray<double> species;
    arch::cuda::DeviceStateView view{};

    DeviceStateOwner(int total, int species_count)
        : rho(total), mom_u(total), mom_v(total), mom_w(total), eng(total),
          enuc(total), species(static_cast<std::size_t>(total) * species_count)
    {
        view = {rho.get(), mom_u.get(), mom_v.get(), mom_w.get(), eng.get(),
                enuc.get(), species.get(), total, species_count};
    }

    void upload(const FluidState& state)
    {
        rho.upload(state.rho);
        mom_u.upload(state.mom_u);
        mom_v.upload(state.mom_v);
        mom_w.upload(state.mom_w);
        eng.upload(state.eng);
        enuc.upload(state.enuc_rate);
        species.upload(state.mass_fractions);
    }
};

BoundaryPlan make_plan(int dimension)
{
    BoundaryPlanInput input{};
    input.dimension = dimension;
    input.active_extent = {
        16, dimension >= 2 ? 16 : 1, dimension == 3 ? 16 : 1};
    input.ghost_depth = 4;
    input.faces.fill(BoundaryType::Inactive);
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Lower)] =
        BoundaryType::Periodic;
    input.faces[face_index(BoundaryAxis::X1, BoundarySide::Upper)] =
        dimension == 2 ? BoundaryType::Outflow : BoundaryType::Reflecting;
    if (dimension >= 2) {
        input.faces[face_index(BoundaryAxis::X2, BoundarySide::Lower)] =
            BoundaryType::Reflecting;
        input.faces[face_index(BoundaryAxis::X2, BoundarySide::Upper)] =
            BoundaryType::Periodic;
    }
    if (dimension == 3) {
        input.faces[face_index(BoundaryAxis::X2, BoundarySide::Lower)] =
            BoundaryType::Outflow;
        input.faces[face_index(BoundaryAxis::X3, BoundarySide::Lower)] =
            BoundaryType::Reflecting;
        input.faces[face_index(BoundaryAxis::X3, BoundarySide::Upper)] =
            BoundaryType::Outflow;
    }
    return make_boundary_plan(input);
}

arch::cuda::DeviceGridView make_device_grid(int dimension)
{
    arch::cuda::DeviceGridView grid{};
    grid.dim = dimension;
    grid.ng = 4;
    grid.total_x = 29;
    grid.total_y = dimension >= 2 ? 27 : 1;
    grid.total_z = dimension == 3 ? 28 : 1;
    grid.stride_y = 37;
    grid.stride_z = dimension >= 2 ? 37 * 31 : 37;
    grid.total_size = grid.stride_z * grid.total_z;
    grid.is = 5;
    grid.ie = 21;
    grid.js = dimension >= 2 ? 6 : 0;
    grid.je = dimension >= 2 ? 22 : 1;
    grid.ks = dimension == 3 ? 7 : 0;
    grid.ke = dimension == 3 ? 23 : 1;
    return grid;
}

arch::boundary::host::HostBoundaryLayout host_layout(
    const arch::cuda::DeviceGridView& grid)
{
    return {
        grid.dim,
        grid.ie - grid.is, grid.je - grid.js, grid.ke - grid.ks,
        grid.ng,
        grid.is, grid.js, grid.ks,
        grid.total_x, grid.total_y, grid.total_z,
        grid.stride_y, grid.stride_z, grid.total_size};
}

void seed_state(FluidState& state, int total, int species_count)
{
    state.Preallocate(total);
    state.InitSpecies(species_count);
    for (int cell = 0; cell < total; ++cell) {
        const auto value = static_cast<std::uint64_t>(cell);
        state.rho[cell] = std::bit_cast<double>(UINT64_C(0x3ff0000000000000) + value);
        state.mom_u[cell] = std::bit_cast<double>(UINT64_C(0x4000000000000000) + value);
        state.mom_v[cell] = std::bit_cast<double>(UINT64_C(0x4010000000000000) + value);
        state.mom_w[cell] = std::bit_cast<double>(UINT64_C(0x4020000000000000) + value);
        state.eng[cell] = std::bit_cast<double>(UINT64_C(0x4030000000000000) + value);
        state.enuc_rate[cell] = std::bit_cast<double>(UINT64_C(0x4040000000000000) + value);
        for (int species = 0; species < species_count; ++species) {
            state.X(species, cell) = std::bit_cast<double>(
                UINT64_C(0x3f80000000000000)
                + (static_cast<std::uint64_t>(species) << 40) + value);
        }
    }
}

int independent_flatten(
    const arch::cuda::DeviceGridView& grid, LogicalCellRef logical)
{
    const std::int64_t i = static_cast<std::int64_t>(grid.is) + logical.i;
    const std::int64_t j = static_cast<std::int64_t>(grid.js) + logical.j;
    const std::int64_t k = static_cast<std::int64_t>(grid.ks) + logical.k;
    const std::int64_t index = k * grid.stride_z + j * grid.stride_y + i;
    require(i >= 0 && i < grid.total_x && j >= 0 && j < grid.total_y
                && k >= 0 && k < grid.total_z
                && index >= 0 && index < grid.total_size,
            "independent logical-cell fixture is outside the device layout");
    return static_cast<int>(index);
}

std::vector<double>& field_values(FluidState& state, BoundaryFieldClass field)
{
    switch (field) {
    case BoundaryFieldClass::Density: return state.rho;
    case BoundaryFieldClass::MomentumX: return state.mom_u;
    case BoundaryFieldClass::MomentumY: return state.mom_v;
    case BoundaryFieldClass::MomentumZ: return state.mom_w;
    case BoundaryFieldClass::Energy: return state.eng;
    case BoundaryFieldClass::AllSpecies: break;
    }
    throw std::logic_error("species is not a conserved-array fixture field");
}

BoundaryFieldClass normal_momentum(BoundaryAxis axis)
{
    if (axis == BoundaryAxis::X1) return BoundaryFieldClass::MomentumX;
    if (axis == BoundaryAxis::X2) return BoundaryFieldClass::MomentumY;
    return BoundaryFieldClass::MomentumZ;
}

BoundaryFieldClass tangential_momentum(BoundaryAxis axis)
{
    return axis == BoundaryAxis::X1
        ? BoundaryFieldClass::MomentumY : BoundaryFieldClass::MomentumX;
}

struct IeeeWitness {
    int first_source;
    int first_destination;
    int second_source;
    int second_destination;
    BoundaryFieldClass normal;
    BoundaryFieldClass tangential;
};

IeeeWitness seed_touched_ieee_values(
    FluidState& state, const BoundaryPlan& plan,
    const arch::cuda::DeviceGridView& grid, int species_count)
{
    const BoundaryOperation* first = nullptr;
    const BoundaryOperation* second = nullptr;
    int first_source = -1;
    for (const auto& operation : plan.operations()) {
        if (operation.type != BoundaryType::Reflecting)
            continue;
        const auto& extent = plan.input().active_extent;
        if (operation.source.i < 0 || operation.source.i >= extent[0]
            || operation.source.j < 0 || operation.source.j >= extent[1]
            || operation.source.k < 0 || operation.source.k >= extent[2])
            continue;
        const int source = independent_flatten(grid, operation.source);
        if (first == nullptr) {
            first = &operation;
            first_source = source;
        } else if (source != first_source) {
            second = &operation;
            break;
        }
    }
    require(first != nullptr && second != nullptr,
            "IEEE witness requires two distinct reflecting sources");
    const int second_source = independent_flatten(grid, second->source);
    const int first_destination = independent_flatten(grid, first->destination);
    const int second_destination = independent_flatten(grid, second->destination);
    const auto normal = normal_momentum(first->axis);
    const auto tangential = tangential_momentum(first->axis);

    state.rho[first_source] =
        std::bit_cast<double>(UINT64_C(0x7ff8000000000042));
    state.eng[first_source] = std::numeric_limits<double>::infinity();
    field_values(state, normal)[first_source] =
        std::bit_cast<double>(UINT64_C(0x0000000000000000));
    field_values(state, tangential)[first_source] =
        std::bit_cast<double>(UINT64_C(0x8000000000000000));
    field_values(state, normal)[second_source] =
        std::numeric_limits<double>::infinity();
    if (species_count > 0) {
        state.X(0, first_source) =
            std::bit_cast<double>(UINT64_C(0x7ff8000000001234));
    }
    return {first_source, first_destination, second_source, second_destination,
            normal, tangential};
}

void require_raw_equal(
    const std::vector<double>& actual, const std::vector<double>& expected,
    std::string_view field)
{
    require(actual.size() == expected.size(), "field size mismatch");
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (std::bit_cast<std::uint64_t>(actual[index])
            != std::bit_cast<std::uint64_t>(expected[index])) {
            std::cerr << "raw mismatch field=" << field << " index=" << index
                      << " actual=0x" << std::hex
                      << std::bit_cast<std::uint64_t>(actual[index])
                      << " expected=0x"
                      << std::bit_cast<std::uint64_t>(expected[index])
                      << std::dec << '\n';
            throw std::runtime_error("CUDA boundary raw parity drifted");
        }
    }
}

void run_cuda_lowering_and_execution(
    int dimension, int species_count,
    arch::cuda::DeviceGeometry geometry = arch::cuda::DeviceGeometry::Cartesian)
{
    static_assert(std::is_standard_layout_v<arch::cuda::DeviceBoundaryTransfer>);
    static_assert(std::is_trivially_copyable_v<arch::cuda::DeviceBoundaryTransfer>);

    constexpr std::array<std::uint64_t, 3> fingerprints{
        UINT64_C(0x47898461178fb7ca),
        UINT64_C(0x900448e8727693a0),
        UINT64_C(0x559c7d610a5606b6)};
    constexpr std::array<std::size_t, 3> operation_counts{8, 320, 9728};
    const auto plan = make_plan(dimension);
    auto grid = make_device_grid(dimension);
    grid.geometry = static_cast<int>(geometry);
    const auto device_compiled = arch::cuda::compile_boundary_plan(plan, grid);
    const auto host_compiled = arch::boundary::host::compile(
        plan, host_layout(grid));
    require(device_compiled.logical_fingerprint
                == fingerprints[dimension - 1],
            "CUDA lowerer changed frozen logical fingerprint");
    require(device_compiled.transfers.size() == operation_counts[dimension - 1],
            "CUDA lowerer changed operation count");
    require(device_compiled.phases == host_compiled.phases,
            "CUDA lowerer changed phase partition");
    for (std::size_t index = 0; index < device_compiled.transfers.size(); ++index) {
        const auto& device = device_compiled.transfers[index];
        const auto& host = host_compiled.transfers[index];
        bool signs_match = true;
        for (std::size_t field = 0; field < host.conserved_signs.size(); ++field)
            signs_match = signs_match
                && device.conserved_signs[field] == host.conserved_signs[field];
        require(device.logical_ordinal == host.logical_ordinal
                && device.source_index
                    == independent_flatten(grid, plan.operations()[index].source)
                && device.destination_index
                    == independent_flatten(
                        grid, plan.operations()[index].destination)
                && device.source_index == host.source_index
                && device.destination_index == host.destination_index
                && signs_match
                && device.species_sign == host.species_sign,
                "Host and CUDA lowering disagree");
    }

    FluidState initial;
    seed_state(initial, grid.total_size, species_count);
    const auto ieee = seed_touched_ieee_values(
        initial, plan, grid, species_count);
    FluidState expected = initial;
    arch::boundary::host::execute(host_compiled, expected);

    DeviceStateOwner device_state(grid.total_size, species_count);
    device_state.upload(initial);
    DeviceArray<arch::cuda::DeviceBoundaryTransfer> transfers(
        device_compiled.transfers.size());
    transfers.upload(device_compiled.transfers);
    cudaStream_t stream = nullptr;
    require(cudaStreamCreate(&stream) == cudaSuccess, "cudaStreamCreate failed");
    const auto launch = arch::cuda::launch_boundary_plan(
        device_state.view, transfers.get(), device_compiled, stream);
    require(launch == cudaSuccess, "boundary kernel launch failed");
    require(cudaStreamSynchronize(stream) == cudaSuccess,
            "boundary kernel execution failed");
    require(cudaStreamDestroy(stream) == cudaSuccess, "cudaStreamDestroy failed");

    const auto actual_rho = device_state.rho.download();
    const auto actual_u = device_state.mom_u.download();
    const auto actual_v = device_state.mom_v.download();
    const auto actual_w = device_state.mom_w.download();
    const auto actual_eng = device_state.eng.download();
    const auto actual_enuc = device_state.enuc.download();
    const auto actual_species = device_state.species.download();
    require_raw_equal(actual_rho, expected.rho, "rho");
    require_raw_equal(actual_u, expected.mom_u, "mom_u");
    require_raw_equal(actual_v, expected.mom_v, "mom_v");
    require_raw_equal(actual_w, expected.mom_w, "mom_w");
    require_raw_equal(actual_eng, expected.eng, "eng");
    require_raw_equal(actual_enuc, expected.enuc_rate, "enuc_rate");
    require_raw_equal(actual_species, expected.mass_fractions, "species");

    const auto& normal_values = ieee.normal == BoundaryFieldClass::MomentumX
        ? actual_u : (ieee.normal == BoundaryFieldClass::MomentumY
            ? actual_v : actual_w);
    const auto& tangential_values =
        ieee.tangential == BoundaryFieldClass::MomentumX ? actual_u : actual_v;
    require(std::bit_cast<std::uint64_t>(actual_rho[ieee.first_destination])
                == UINT64_C(0x7ff8000000000042),
            "CUDA positive copy changed a touched NaN payload");
    require(std::bit_cast<std::uint64_t>(
                actual_eng[ieee.first_destination])
                == UINT64_C(0x7ff0000000000000),
            "CUDA positive copy changed touched positive infinity");
    require(std::bit_cast<std::uint64_t>(
                normal_values[ieee.first_destination])
                == UINT64_C(0x8000000000000000),
            "CUDA reflection did not negate touched positive zero");
    require(std::bit_cast<std::uint64_t>(
                tangential_values[ieee.first_destination])
                == UINT64_C(0x8000000000000000),
            "CUDA tangential copy canonicalized touched negative zero");
    require(std::bit_cast<std::uint64_t>(
                normal_values[ieee.second_destination])
                == UINT64_C(0xfff0000000000000),
            "CUDA reflection did not negate touched positive infinity");
    if (species_count > 0) {
        const auto destination = static_cast<std::size_t>(ieee.first_destination);
        require(std::bit_cast<std::uint64_t>(actual_species[destination])
                    == UINT64_C(0x7ff8000000001234),
                "CUDA species copy changed a touched NaN payload");
    }
}

void test_cuda_lowering_and_execution()
{
    constexpr std::array species_counts{
        0, 2, arch::cuda::kLocalSpeciesScratchCapacity};
    int cases = 0;
    unsigned species_mask = 0;
    for (int dimension = 1; dimension <= 3; ++dimension) {
        for (const int species_count : species_counts) {
            run_cuda_lowering_and_execution(dimension, species_count);
            species_mask |= species_count == 0 ? 1U
                : (species_count == 2 ? 2U
                                      : (species_count
                                                == arch::cuda::kLocalSpeciesScratchCapacity
                                            ? 4U : 0U));
            ++cases;
        }
    }
    require(cases == 9 && species_mask == 7U,
            "CUDA boundary dimension/species matrix is incomplete");
    for (const auto geometry : {arch::cuda::DeviceGeometry::Cylindrical,
                                arch::cuda::DeviceGeometry::Spherical})
        for (int dimension = 1; dimension <= 3; ++dimension)
            run_cuda_lowering_and_execution(dimension, 2, geometry);
}

void test_invalid_cuda_inputs()
{
    const auto plan = make_plan(3);
    auto grid = make_device_grid(3);
    grid.stride_y = grid.total_x - 1;
    bool rejected = false;
    try {
        (void)arch::cuda::compile_boundary_plan(plan, grid);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "CUDA lowerer accepted an invalid grid");

    grid = make_device_grid(3);
    grid.is = 3;
    rejected = false;
    try {
        (void)arch::cuda::compile_boundary_plan(plan, grid);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "CUDA lowerer accepted an insufficient ghost origin");
    arch::boundary::BoundaryPlanInput overflow_input{
        1, {16, 1, 1}, 4,
        {arch::boundary::BoundaryType::Outflow,
         arch::boundary::BoundaryType::Outflow,
         arch::boundary::BoundaryType::Inactive,
         arch::boundary::BoundaryType::Inactive,
         arch::boundary::BoundaryType::Inactive,
         arch::boundary::BoundaryType::Inactive}};
    const auto overflow_plan = arch::boundary::make_boundary_plan(overflow_input);
    grid = make_device_grid(3);
    grid.dim = 1;
    grid.ng = 4;
    grid.total_x = std::numeric_limits<int>::max();
    grid.total_y = 1;
    grid.total_z = 1;
    grid.stride_y = std::numeric_limits<int>::max();
    grid.stride_z = std::numeric_limits<int>::max();
    grid.total_size = std::numeric_limits<int>::max();
    grid.is = std::numeric_limits<int>::max() - 16;
    grid.ie = std::numeric_limits<int>::max();
    grid.js = 0;
    grid.je = 1;
    grid.ks = 0;
    grid.ke = 1;
    volatile int overflow_origin = std::numeric_limits<int>::max() - 16;
    require(!arch::boundary::detail::contains_active_region(
                overflow_origin, 16, 4, std::numeric_limits<int>::max()),
            "CUDA host lowerer active-region check overflowed before reject");
    rejected = false;
    try {
        (void)arch::cuda::compile_boundary_plan(overflow_plan, grid);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected,
            "CUDA lowerer accepted an overflowing active-region bound");
    rejected = false;
    try {
        (void)arch::cuda::detail::flatten_boundary_checked(
            make_device_grid(3), {-6, 0, 0});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "CUDA lowerer accepted an out-of-range logical coordinate");

    const auto valid_grid = make_device_grid(3);
    auto compiled = arch::cuda::compile_boundary_plan(plan, valid_grid);
    arch::cuda::DeviceStateView invalid{};
    require(arch::cuda::launch_boundary_plan(invalid, nullptr, compiled, nullptr)
                == cudaErrorInvalidValue,
            "CUDA launcher accepted an invalid state/transfer view");

    FluidState initial;
    seed_state(initial, valid_grid.total_size, 2);
    DeviceStateOwner state(valid_grid.total_size, 2);
    state.upload(initial);
    DeviceArray<arch::cuda::DeviceBoundaryTransfer> transfers(
        compiled.transfers.size());
    transfers.upload(compiled.transfers);
    auto bad_state = state.view;
    bad_state.total_size -= 1;
    require(arch::cuda::launch_boundary_plan(
                bad_state, transfers.get(), compiled, nullptr)
                == cudaErrorInvalidValue,
            "CUDA launcher accepted a mismatched state view");
    compiled.phases[1].first += 1;
    require(arch::cuda::launch_boundary_plan(
                state.view, transfers.get(), compiled, nullptr)
                == cudaErrorInvalidValue,
            "CUDA launcher accepted a broken phase range");
    require_raw_equal(state.rho.download(), initial.rho,
                      "invalid-launch zero-write witness");

    compiled = arch::cuda::compile_boundary_plan(plan, valid_grid);
    compiled.transfers.front().source_index = compiled.total_size;
    require(arch::cuda::launch_boundary_plan(
                state.view, transfers.get(), compiled, nullptr)
                == cudaErrorInvalidValue,
            "CUDA launcher accepted a corrupt lowered source index");
    require_raw_equal(state.rho.download(), initial.rho,
                      "invalid-transfer zero-write witness");
    compiled = arch::cuda::compile_boundary_plan(plan, valid_grid);
    compiled.transfers.front().conserved_signs[0] = 0;
    require(arch::cuda::launch_boundary_plan(
                state.view, transfers.get(), compiled, nullptr)
                == cudaErrorInvalidValue,
            "CUDA launcher accepted a non-unit compiled sign");
    require_raw_equal(state.rho.download(), initial.rho,
                      "invalid-sign zero-write witness");
}
} // namespace

int main()
{
    try {
        test_cuda_lowering_and_execution();
        test_invalid_cuda_inputs();
        std::cout << "boundary plan CUDA contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
