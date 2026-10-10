/**
 * @file test_cuda_reduction_contract.cu
 * @brief Check reduction contracts on an actual CUDA device.
 *
 * Edge cases test candidate ordering and invalid states; production hydro
 * and diffusion owners verify that runtime reductions use those contracts.
 */
#include <cuda_runtime.h>

#include "cuda/diffusion/DiffusionKernels.cuh"
#include "cuda/hydro/kernels/HydroStateKernels.cuh"
#include "cuda/runtime/gravity/CudaGravityExecution.h"
#include "driver/schedule/ReductionSpec.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/FiniteRingBoundaryMath.h"
#include "physics/constant/PhysicalConstants.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
using arch::reduction::ReductionCandidate;
using arch::reduction::ReductionResult;
using arch::reduction::ReductionSpec;
using arch::reduction::ReductionStatus;

int failures = 0;
int edge_cases = 0;

void fail(const std::string& message)
{
    std::cerr << "FAIL " << message << '\n';
    ++failures;
}

void require_cuda(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess)
        fail(std::string(operation) + ": " + cudaGetErrorString(error));
}

template<class T>
struct DeviceArray
{
    T* pointer = nullptr;
    std::size_t count = 0;

    explicit DeviceArray(std::size_t size) : count(size)
    {
        if (count > 0)
            require_cuda(cudaMalloc(
                reinterpret_cast<void**>(&pointer), count * sizeof(T)),
                "cudaMalloc");
    }
    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;
    ~DeviceArray() { if (pointer) cudaFree(pointer); }

    void upload(const T* source, std::size_t size)
    {
        if (size > 0) require_cuda(cudaMemcpy(
            pointer, source, size * sizeof(T), cudaMemcpyHostToDevice),
            "cudaMemcpy upload");
    }

    void download(T* destination, std::size_t size) const
    {
        if (size > 0) require_cuda(cudaMemcpy(
            destination, pointer, size * sizeof(T), cudaMemcpyDeviceToHost),
            "cudaMemcpy download");
    }
};

struct DeviceStateOwner
{
    DeviceArray<double> rho, mom_u, mom_v, mom_w, eng, enuc, species;
    arch::cuda::DeviceStateView view{};

    DeviceStateOwner(int total, int species_count)
        : rho(total), mom_u(total), mom_v(total), mom_w(total), eng(total),
          enuc(total), species(static_cast<std::size_t>(total) * species_count)
    {
        view = {rho.pointer, mom_u.pointer, mom_v.pointer, mom_w.pointer,
                eng.pointer, enuc.pointer, species.pointer, total,
                species_count};
    }

    void upload(const FluidState& state)
    {
        rho.upload(state.rho.data(), state.rho.size());
        mom_u.upload(state.mom_u.data(), state.mom_u.size());
        mom_v.upload(state.mom_v.data(), state.mom_v.size());
        mom_w.upload(state.mom_w.data(), state.mom_w.size());
        eng.upload(state.eng.data(), state.eng.size());
        enuc.upload(state.enuc_rate.data(), state.enuc_rate.size());
        species.upload(
            state.mass_fractions.data(), state.mass_fractions.size());
    }
};

amr::CellLogicalKey key(
    std::int64_t root_i, std::int64_t root_j, std::int64_t root_k,
    int level, std::uint64_t morton, int i, int j, int k, int component)
{
    return {{root_i, root_j, root_k}, level, morton, i, j, k, component};
}

ReductionCandidate candidate(double value, amr::CellLogicalKey logical_key,
                             bool active = true)
{
    return {value, logical_key, active};
}

bool same_key(const amr::CellLogicalKey& lhs, const amr::CellLogicalKey& rhs)
{
    return lhs.root.root_i == rhs.root.root_i
        && lhs.root.root_j == rhs.root.root_j
        && lhs.root.root_k == rhs.root.root_k
        && lhs.level == rhs.level && lhs.morton == rhs.morton
        && lhs.logical_i == rhs.logical_i && lhs.logical_j == rhs.logical_j
        && lhs.logical_k == rhs.logical_k && lhs.component == rhs.component;
}

__global__ void reduction_executor_kernel(
    ReductionSpec spec, const ReductionCandidate* candidates,
    std::size_t count, ReductionResult* result)
{
    if (blockIdx.x == 0 && threadIdx.x == 0)
        *result = arch::reduction::execute_ordered_reduction(
            spec, candidates, count);
}

__global__ void partial_state_executor_kernel(
    ReductionSpec spec, const ReductionCandidate* candidates,
    ReductionResult* result)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    auto left = arch::reduction::begin_reduction(spec);
    auto right = arch::reduction::begin_reduction(spec);
    arch::reduction::combine_candidate(spec, left, candidates[0]);
    arch::reduction::combine_candidate(spec, left, candidates[1]);
    arch::reduction::combine_candidate(spec, right, candidates[2]);
    arch::reduction::combine_candidate(spec, right, candidates[3]);
    auto merged = arch::reduction::begin_reduction(spec);
    arch::reduction::combine_state(spec, merged, left);
    arch::reduction::combine_state(spec, merged, right);
    *result = arch::reduction::finalize_reduction(spec, merged);
}

void run_device_case(
    const char* name, ReductionSpec spec,
    std::vector<ReductionCandidate> candidates, ReductionStatus status,
    std::uint64_t bits, std::uint64_t count,
    const amr::CellLogicalKey* winner = nullptr,
    bool sort_input = true)
{
    ++edge_cases;
    if (sort_input) {
        std::sort(candidates.begin(), candidates.end(),
            [](const ReductionCandidate& lhs, const ReductionCandidate& rhs) {
                return arch::reduction::cell_logical_key_less(
                    lhs.key, rhs.key);
            });
    }
    DeviceArray<ReductionCandidate> device_candidates(
        std::max<std::size_t>(1, candidates.size()));
    DeviceArray<ReductionResult> device_result(1);
    device_candidates.upload(candidates.data(), candidates.size());
    reduction_executor_kernel<<<1, 1>>>(
        spec, device_candidates.pointer, candidates.size(),
        device_result.pointer);
    require_cuda(cudaGetLastError(), "reduction executor launch");
    require_cuda(cudaDeviceSynchronize(), "reduction executor sync");
    ReductionResult result{};
    device_result.download(&result, 1);
    if (result.status != status) fail(std::string(name) + ".status");
    if (std::bit_cast<std::uint64_t>(result.value) != bits)
        fail(std::string(name) + ".value");
    if (result.accepted_count != count) fail(std::string(name) + ".count");
    if (winner && !same_key(result.key, *winner))
        fail(std::string(name) + ".key");
}

void run_device_partial_state_case(
    const ReductionSpec& spec,
    const std::vector<ReductionCandidate>& candidates)
{
    ++edge_cases;
    DeviceArray<ReductionCandidate> device_candidates(candidates.size());
    DeviceArray<ReductionResult> device_result(1);
    device_candidates.upload(candidates.data(), candidates.size());
    partial_state_executor_kernel<<<1, 1>>>(
        spec, device_candidates.pointer, device_result.pointer);
    require_cuda(cudaGetLastError(), "partial state executor launch");
    require_cuda(cudaDeviceSynchronize(), "partial state executor sync");
    ReductionResult result{};
    device_result.download(&result, 1);
    if (result.status != ReductionStatus::PartialStateRejected)
        fail("36.sum.partial-state-rejected.status");
    if (std::bit_cast<std::uint64_t>(result.value)
        != 0x0000000000000000ULL)
        fail("36.sum.partial-state-rejected.value");
    if (result.accepted_count != 0)
        fail("36.sum.partial-state-rejected.count");
}

void verify_device_edges()
{
    const auto k0 = key(-1, 0, 0, 0, 0, 0, 0, 0, 0);
    const auto k1 = key(0, 0, 0, 0, 1, 0, 0, 0, 0);
    const auto k2 = key(0, 0, 0, 0, 2, 0, 0, 0, 0);
    const auto k3 = key(1, 0, 0, 0, 3, 0, 0, 0, 0);
    const double nan = std::bit_cast<double>(0x7ff8000000000042ULL);
    const double pinf = std::numeric_limits<double>::infinity();
    const double ninf = -std::numeric_limits<double>::infinity();
    const auto min_spec = arch::reduction::minimum_spec(99.0);
    const auto max_spec = arch::reduction::maximum_spec(-99.0);
    const auto sum_spec = arch::reduction::conservation_sum_spec();

    run_device_case("01.min", min_spec, {
        candidate(4.0,k2), candidate(2.0,k1), candidate(3.0,k3)},
        ReductionStatus::Ok, 0x4000000000000000ULL, 3, &k1);
    run_device_case("02.max", max_spec, {
        candidate(4.0,k2), candidate(2.0,k1), candidate(3.0,k3)},
        ReductionStatus::Ok, 0x4010000000000000ULL, 3, &k2);
    run_device_case("03.sum", sum_spec, {
        candidate(1.0e16,k0), candidate(-1.0e16,k2), candidate(1.0,k1)},
        ReductionStatus::Ok, 0x0000000000000000ULL, 3, &k0);
    run_device_case("04.nan.first", min_spec, {
        candidate(nan,k0),candidate(2.0,k1),candidate(3.0,k2)},
        ReductionStatus::Ok, 0x4000000000000000ULL, 2, &k1);
    run_device_case("05.nan.middle", min_spec, {
        candidate(2.0,k1),candidate(nan,k0),candidate(3.0,k2)},
        ReductionStatus::Ok, 0x4000000000000000ULL, 2, &k1);
    run_device_case("06.nan.last", min_spec, {
        candidate(2.0,k1),candidate(3.0,k2),candidate(nan,k0)},
        ReductionStatus::Ok, 0x4000000000000000ULL, 2, &k1);
    run_device_case("07.max.nan", max_spec, {
        candidate(nan,k0),candidate(2.0,k1)},
        ReductionStatus::Ok, 0x4000000000000000ULL, 1, &k1);
    run_device_case("08.sum.nan.first", sum_spec, {
        candidate(nan,k0),candidate(2.0,k1)},
        ReductionStatus::NanRejected, 0x0000000000000000ULL, 0);
    run_device_case("09.sum.nan.middle", sum_spec, {
        candidate(1.0,k0),candidate(nan,k1),candidate(2.0,k2)},
        ReductionStatus::NanRejected, 0x3ff0000000000000ULL, 1);
    run_device_case("10.sum.nan.last", sum_spec, {
        candidate(1.0,k0),candidate(2.0,k1),candidate(nan,k2)},
        ReductionStatus::NanRejected, 0x4008000000000000ULL, 2);
    run_device_case("11.min.neginf", min_spec, {
        candidate(2.0,k1),candidate(ninf,k2)}, ReductionStatus::Ok,
        0xfff0000000000000ULL, 2, &k2);
    run_device_case("12.min.posinf", min_spec, {candidate(pinf,k1)},
        ReductionStatus::Ok, 0x7ff0000000000000ULL, 1, &k1);
    run_device_case("13.max.neginf", max_spec, {candidate(ninf,k1)},
        ReductionStatus::Ok, 0xfff0000000000000ULL, 1, &k1);
    run_device_case("14.max.posinf", max_spec, {
        candidate(2.0,k1),candidate(pinf,k2)}, ReductionStatus::Ok,
        0x7ff0000000000000ULL, 2, &k2);
    run_device_case("15.sum.neginf", sum_spec, {candidate(ninf,k1)},
        ReductionStatus::InfiniteRejected, 0x0000000000000000ULL, 0);
    run_device_case("16.sum.posinf", sum_spec, {candidate(pinf,k1)},
        ReductionStatus::InfiniteRejected, 0x0000000000000000ULL, 0);
    run_device_case("17.min.zero.plus", min_spec, {
        candidate(-0.0,k2),candidate(+0.0,k1)}, ReductionStatus::Ok,
        0x0000000000000000ULL, 2, &k1);
    run_device_case("18.min.zero.minus", min_spec, {
        candidate(+0.0,k2),candidate(-0.0,k1)}, ReductionStatus::Ok,
        0x8000000000000000ULL, 2, &k1);
    run_device_case("19.max.zero.plus", max_spec, {
        candidate(-0.0,k2),candidate(+0.0,k1)}, ReductionStatus::Ok,
        0x0000000000000000ULL, 2, &k1);
    run_device_case("20.max.zero.minus", max_spec, {
        candidate(+0.0,k2),candidate(-0.0,k1)}, ReductionStatus::Ok,
        0x8000000000000000ULL, 2, &k1);
    run_device_case("21.min.empty", min_spec, {}, ReductionStatus::Empty,
        std::bit_cast<std::uint64_t>(99.0), 0);
    run_device_case("22.max.empty", max_spec, {}, ReductionStatus::Empty,
        std::bit_cast<std::uint64_t>(-99.0), 0);
    run_device_case("23.sum.empty", sum_spec, {}, ReductionStatus::Empty,
        0x0000000000000000ULL, 0);
    run_device_case("24.inactive", min_spec, {
        candidate(1.0,k0,false),candidate(2.0,k1)}, ReductionStatus::Ok,
        0x4000000000000000ULL, 1, &k1);
    run_device_case("25.all.inactive", min_spec, {
        candidate(1.0,k0,false),candidate(2.0,k1,false)},
        ReductionStatus::Empty, std::bit_cast<std::uint64_t>(99.0), 0);
    run_device_case("26.duplicate.min", min_spec, {
        candidate(1.0,k1),candidate(2.0,k1),candidate(3.0,k2)},
        ReductionStatus::InvalidKeyOrder,
        std::bit_cast<std::uint64_t>(99.0), 0);
    run_device_case("27.duplicate.max", max_spec, {
        candidate(1.0,k1),candidate(2.0,k1),candidate(3.0,k2)},
        ReductionStatus::InvalidKeyOrder,
        std::bit_cast<std::uint64_t>(-99.0), 0);
    run_device_case("28.duplicate.sum", sum_spec, {
        candidate(1.0,k1),candidate(2.0,k1)},
        ReductionStatus::InvalidKeyOrder, 0x0000000000000000ULL, 0);
    run_device_case("29.equal", min_spec, {
        candidate(2.0,k2),candidate(2.0,k0),candidate(2.0,k1)},
        ReductionStatus::Ok, 0x4000000000000000ULL, 3, &k0);
    run_device_case("30.permutation", min_spec, {
        candidate(3.0,k3),candidate(2.0,k2),candidate(2.0,k0)},
        ReductionStatus::Ok, 0x4000000000000000ULL, 3, &k0);
    const auto low_root = key(-1,0,0,4,99,9,9,9,9);
    const auto high_root = key(1,0,0,0,0,0,0,0,0);
    run_device_case("31.roots", min_spec, {
        candidate(-0.0,high_root),candidate(+0.0,low_root)},
        ReductionStatus::Ok, 0x0000000000000000ULL, 2, &low_root);
    const auto root = amr::root_logical_key_from_leaf(3, 17, 9, 25);
    const auto reconstructed = key(
        root->root_i, root->root_j, root->root_k, 3, 0x1234,
        17, 9, 25, 7);
    run_device_case("32.checkpoint", min_spec, {
        candidate(5.0,reconstructed),candidate(5.0,k3)},
        ReductionStatus::Ok, 0x4014000000000000ULL, 2, &k3);
    run_device_case("33.min.invalid-key-order", min_spec, {
        candidate(3.0,k1),candidate(2.0,k2),candidate(1.0,k1)},
        ReductionStatus::InvalidKeyOrder,
        std::bit_cast<std::uint64_t>(99.0), 0, nullptr, false);
    run_device_case("34.max.invalid-key-order", max_spec, {
        candidate(1.0,k1),candidate(2.0,k2),candidate(3.0,k1)},
        ReductionStatus::InvalidKeyOrder,
        std::bit_cast<std::uint64_t>(-99.0), 0, nullptr, false);
    const std::vector partial_counterexample = {
        candidate(1.0e16,k0),candidate(1.0,k1),
        candidate(-1.0e16,k2),candidate(1.0,k3)};
    run_device_case("35.sum.partial-counterexample.full", sum_spec,
        partial_counterexample, ReductionStatus::Ok,
        0x3ff0000000000000ULL, 4, &k0);
    run_device_partial_state_case(sum_spec, partial_counterexample);
}

struct HydroEos
{
    ARCH_INLINE double get_pressure(
        const FluidVector& state, const double*) const
    {
        const double kinetic = 0.5
            * (state.mom_u * state.mom_u + state.mom_v * state.mom_v
               + state.mom_w * state.mom_w) / state.rho;
        return 0.4 * (state.eng - kinetic);
    }
    ARCH_INLINE double get_sound_speed(
        const FluidVector& state, double pressure, const double*) const
    {
        return sqrt(1.4 * pressure / state.rho);
    }
};

Grid make_diffusion_grid(double cell_width = 0.01)
{
    Grid grid(2,
              0.0, amr::BLOCK_NX * cell_width,
              0.0, amr::BLOCK_NY * cell_width,
              0.0, amr::BLOCK_NZ * cell_width);
    grid.dim = 1;
    grid.geometry = "cartesian";
    grid.InitializeTopology();
    return grid;
}

FluidState make_diffusion_state(const Grid& grid)
{
    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(2);
    for (int k = 0; k < grid.GetTotalZ(); ++k) {
        for (int j = 0; j < grid.GetTotalY(); ++j) {
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                const double q = 0.013 * i + 0.021 * j + 0.034 * k;
                const double rho = 1.1 + 0.07 * q;
                const double u = 0.2 + 0.03 * q;
                const double v = -0.1 + 0.02 * q;
                const double w = 0.05 - 0.01 * q;
                const double x0 = 0.35 + 0.01 * q;
                const double x1 = 1.0 - x0;
                const double cv = x0 * 3.5 + x1 * 7.25;
                const double temperature = 2.0 + 0.4 * q;
                state.rho[cell] = rho;
                state.mom_u[cell] = rho * u;
                state.mom_v[cell] = rho * v;
                state.mom_w[cell] = rho * w;
                state.eng[cell] = rho * cv * temperature
                    + 0.5 * rho * (u * u + v * v + w * w);
                state.X(0, cell) = x0;
                state.X(1, cell) = x1;
            }
        }
    }
    return state;
}

void verify_real_hydro_owner()
{
    Grid grid(3, 0.0, 16.0);
    grid.dim = 1;
    grid.InitializeTopology();
    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(0);
    for (int cell = 0; cell < grid.GetTotalSize(); ++cell)
        state.set(cell, {2.0, 2.0, 2.5, 2.0, 10.0});
    state.set(grid.GetIndex(grid.Is() + 3),
              {1.5, 3.0, -0.5, 0.2, 8.0});
    DeviceStateOwner device_state(grid.GetTotalSize(), 0);
    device_state.upload(state);
    const int count = grid.Ie() - grid.Is();
    DeviceArray<double> candidates(count);
    DeviceArray<double> result(1);
    DeviceArray<int> status(1);
    arch::cuda::CudaHydroWorkspaceView workspace{
        {}, {}, candidates.pointer, result.pointer, status.pointer};
    const auto launch = arch::cuda::launch_compute_hydro_dt(
        device_state.view, arch::cuda::make_device_grid_view(grid),
        HydroEos{}, 0.8, workspace, nullptr);
    require_cuda(launch, "real hydro reduction launch");
    require_cuda(cudaDeviceSynchronize(), "real hydro reduction sync");
    double value = 0.0;
    int device_status = -1;
    result.download(&value, 1);
    status.download(&device_status, 1);
    if (device_status != static_cast<int>(ReductionStatus::Ok))
        fail("real hydro reduction status");
    // Keep exact reduction parity; the retired snapshot used the one-face CFL.
    std::vector<double> hydro_candidates(count);
    candidates.download(hydro_candidates.data(), hydro_candidates.size());
    const double hydro_minimum = *std::min_element(hydro_candidates.begin(), hydro_candidates.end());
    if (std::bit_cast<std::uint64_t>(value) != std::bit_cast<std::uint64_t>(0.8 * hydro_minimum))
        fail("real hydro reduction differs from serial candidate minimum");

    const double signed_zero_candidates[2] = {+0.0, -0.0};
    candidates.upload(signed_zero_candidates, 2);
    arch::cuda::detail::hydro_cfl_reduce_kernel<<<1, 1>>>(
        candidates.pointer, 2, 1.0, result.pointer, status.pointer);
    require_cuda(cudaGetLastError(), "signed-zero hydro reduce launch");
    require_cuda(cudaDeviceSynchronize(), "signed-zero hydro reduce sync");
    result.download(&value, 1);
    status.download(&device_status, 1);
    if (std::bit_cast<std::uint64_t>(value) != 0x0000000000000000ULL)
        fail("signed-zero hydro reduce keyed authority");
    if (device_status != static_cast<int>(ReductionStatus::Ok))
        fail("signed-zero hydro reduce status");

    const double invalid_candidates[2] = {
        3.0, std::numeric_limits<double>::quiet_NaN()};
    candidates.upload(invalid_candidates, 2);
    arch::cuda::detail::hydro_cfl_reduce_kernel<<<1, 1>>>(
        candidates.pointer, 2, 1.0, result.pointer, status.pointer);
    require_cuda(cudaGetLastError(), "invalid hydro reduce launch");
    require_cuda(cudaDeviceSynchronize(), "invalid hydro reduce sync");
    result.download(&value, 1);
    status.download(&device_status, 1);
    if (!std::isnan(value)
        || device_status != static_cast<int>(ReductionStatus::NanRejected))
        fail("invalid hydro candidate was not rejected");
}

void verify_real_diffusion_owner()
{
    const Grid grid = make_diffusion_grid();
    const FluidState state = make_diffusion_state(grid);
    DeviceStateOwner device_state(grid.GetTotalSize(), 2);
    DeviceStateOwner flux(grid.GetTotalSize(), 2);
    device_state.upload(state);
    flux.upload(state);
    const int count = grid.Ie() - grid.Is();
    DeviceArray<double> candidates(count), result(1);
    DeviceArray<int> status(1);
    DeviceArray<double> A(2), Z(2), gamma(2), cv(2);
    const double host_A[2] = {1.0, 4.0};
    const double host_Z[2] = {1.0, 2.0};
    const double host_gamma[2] = {1.4, 1.5};
    const double host_cv[2] = {3.5, 7.25};
    A.upload(host_A, 2); Z.upload(host_Z, 2);
    gamma.upload(host_gamma, 2); cv.upload(host_cv, 2);
    IdealGasView eos{{A.pointer, Z.pointer, gamma.pointer, cv.pointer, 2}, 1.37};
    DiffFlux::DiffusionConfigView config{
        true, true, true, true, 0.19, 0.37, 0.11};
    arch::cuda::DiffusionWorkspaceView workspace{
        flux.view, candidates.pointer, result.pointer, status.pointer};
    const auto launch = arch::cuda::launch_raw_diffusion_dt(
        device_state.view, eos, eos.species,
        arch::cuda::make_device_grid_view(grid), config, workspace, nullptr);
    require_cuda(launch.error, "real diffusion reduction launch");
    require_cuda(cudaDeviceSynchronize(), "real diffusion reduction sync");
    double value = 0.0;
    int device_status = -1;
    result.download(&value, 1);
    status.download(&device_status, 1);
    if (device_status != 0) fail("real diffusion candidate status");
    // Verify the reduction itself against a serial host minimum of the actual
    // device candidates. The old snapshot encoded the retired cell-only
    // diffusion limit. Independent operator/convergence and cell parity tests
    // own the physics, rather than blessing another observed bit pattern here.
    std::vector<double> host_candidates(count);
    candidates.download(host_candidates.data(), host_candidates.size());
    double expected = DiffFlux::diffusion_dt_sentinel();
    for (const double item : host_candidates) {
        if (!std::isfinite(item) || item <= 0.0)
            fail("real diffusion candidate is not finite and positive");
        expected = std::min(expected, item);
    }
    if (std::bit_cast<std::uint64_t>(value) != std::bit_cast<std::uint64_t>(expected))
        fail("real diffusion reduction differs from serial candidate minimum");

    const Grid finite_seed_grid = make_diffusion_grid(1.0);
    const FluidState finite_seed_state = make_diffusion_state(finite_seed_grid);
    DeviceStateOwner finite_seed_device(finite_seed_grid.GetTotalSize(), 2);
    finite_seed_device.upload(finite_seed_state);
    const int finite_seed_count =
        finite_seed_grid.Ie() - finite_seed_grid.Is();
    DeviceArray<double> finite_seed_candidates(finite_seed_count);
    DeviceArray<double> finite_seed_result(1);
    DeviceArray<int> finite_seed_status(1);
    DiffFlux::DiffusionConfigView finite_seed_config{
        true, false, true, false, 2.0e-12, 0.0, 0.0};
    arch::cuda::DiffusionWorkspaceView finite_seed_workspace{
        {}, finite_seed_candidates.pointer, finite_seed_result.pointer,
        finite_seed_status.pointer};
    const auto finite_seed_launch = arch::cuda::launch_raw_diffusion_dt(
        finite_seed_device.view, eos, eos.species,
        arch::cuda::make_device_grid_view(finite_seed_grid),
        finite_seed_config, finite_seed_workspace, nullptr);
    require_cuda(finite_seed_launch.error,
                 "finite-seed diffusion reduction launch");
    require_cuda(cudaDeviceSynchronize(),
                 "finite-seed diffusion reduction sync");
    finite_seed_result.download(&value, 1);
    finite_seed_status.download(&device_status, 1);
    if (device_status != 0) fail("finite-seed diffusion candidate status");
    // Tiny positive viscosity still constrains the timestep; no dimensional cap.
    std::vector<double> small_candidates(finite_seed_count);
    finite_seed_candidates.download(small_candidates.data(), small_candidates.size());
    const double small_minimum = *std::min_element(small_candidates.begin(), small_candidates.end());
    if (!std::isfinite(value) || value <= 1.0e10
        || std::bit_cast<std::uint64_t>(value) != std::bit_cast<std::uint64_t>(small_minimum))
        fail("small-viscosity diffusion candidate minimum");

    config.use_diffusion = false;
    const auto disabled = arch::cuda::launch_raw_diffusion_dt(
        device_state.view, eos, eos.species,
        arch::cuda::make_device_grid_view(grid), config, workspace, nullptr);
    require_cuda(disabled.error, "disabled diffusion reduction launch");
    require_cuda(cudaDeviceSynchronize(), "disabled diffusion reduction sync");
    result.download(&value, 1);
    if (value != std::numeric_limits<double>::max())
        fail("disabled diffusion sentinel authority");

    const double mixed_candidates[2] = {
        std::bit_cast<double>(0x3f21b661f8cde833ULL),
        std::bit_cast<double>(0x3f41b661f8cde833ULL)};
    candidates.upload(mixed_candidates, 2);
    arch::cuda::detail::diffusion_dt_reduce_kernel<<<1, 1>>>(
        candidates.pointer, 2, result.pointer);
    require_cuda(cudaGetLastError(), "mixed diffusion reduce launch");
    require_cuda(cudaDeviceSynchronize(), "mixed diffusion reduce sync");
    result.download(&value, 1);
    if (std::bit_cast<std::uint64_t>(value) != 0x3f21b661f8cde833ULL)
        fail("mixed diffusion reduce body raw authority");

    const double signed_zero_candidates[2] = {+0.0, -0.0};
    candidates.upload(signed_zero_candidates, 2);
    arch::cuda::detail::diffusion_dt_reduce_kernel<<<1, 1>>>(
        candidates.pointer, 2, result.pointer);
    require_cuda(cudaGetLastError(), "signed-zero diffusion reduce launch");
    require_cuda(cudaDeviceSynchronize(), "signed-zero diffusion reduce sync");
    result.download(&value, 1);
    if (std::bit_cast<std::uint64_t>(value) != 0x0000000000000000ULL)
        fail("signed-zero diffusion reduce keyed authority");
}
} // namespace

// Use the production executor and the unchanged host-callable leaves. Sizes
// straddle both compensated chunks and the one-block execution boundary.
void verify_composite_reductions()
{
    using namespace arch::multigrid;
    auto owner = arch::cuda::make_cuda_gravity_execution(nullptr, 0, {});
    auto execution = owner->numeric();
    const auto equal = [](double a, double b) {
        return (std::isnan(a) && std::isnan(b))
            || std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
    };
    const auto host_reduce = [](const Reduction& task) {
        const int count = (task.size + Reduction::chunk - 1)/Reduction::chunk;
        std::vector<double> partials(count);
        for (int i = 0; i < count; ++i) partials[i] = task.partial(i);
        return task.finish(partials.data(), count);
    };
    for (const int size : {1,63,64,65,8192,8193}) {
        std::vector<double> weights(size);
        double total = 0.;
        for (int i = 0; i < size; ++i) total += weights[i] = 1. + i%5;
        for (auto& weight : weights) weight /= total;
        auto device_weights = execution->upload(weights);
        for (int fixture = 0; fixture < 5; ++fixture) {
            const double factor = fixture == 0 ? 0. : fixture == 2 ? 1.e150
                : fixture == 3 ? 1.e-250 : 1.;
            std::vector<double> input(size);
            for (int i = 0; i < size; ++i) input[i] = factor * (i%11 - 5);
            if (fixture == 4) input[size/2] = std::numeric_limits<double>::quiet_NaN();
            auto device = execution->upload(input);
            for (const auto kind : {ReductionKind::Maximum, ReductionKind::Product}) {
                const Reduction reference{input.data(),nullptr,weights.data(),size,kind};
                const double actual = execution->reduce({device.data,nullptr,device_weights.data,size,kind});
                if (!equal(actual,host_reduce(reference))) fail("composite scalar reduction changed shared bits");
            }
            const double scale = host_reduce({input.data(),nullptr,nullptr,size,ReductionKind::Maximum});
            const double sum = host_reduce({input.data(),nullptr,weights.data(),size,
                ReductionKind::Product,1.,1.,&scale});
            const ProjectWork reference{size,input.data(),&scale,&sum};
            for (int i = 0; i < size; ++i) reference(i);
            execution->project(device,device_weights);
            const auto actual = execution->download(device);
            for (int i = 0; i < size; ++i)
                if (!equal(actual[i],input[i])) { fail("composite projection changed shared bits"); break; }
        }
    }
}

/** Execute the shared affine face rows on the real device with independent
 *  Neumann and near-Neumann references; include irrelevant overflowing field
 *  differences to verify that zero stencil weights do not consume them. */
void verify_composite_flux_extremes()
{
    using namespace arch::multigrid;
    auto owner=arch::cuda::make_cuda_gravity_execution(nullptr,0,{});
    auto execution=owner->numeric();
    const double a=std::ldexp(1.,-60),qB=2.,denominator=a+qB;
    const std::vector<double> potential={std::ldexp(1.,54),-std::ldexp(1.,54),
        std::numeric_limits<double>::max(),-std::numeric_limits<double>::max()};
    SparseStorage rows;
    const std::vector<int> first={0,1},second={2,3};
    const std::vector<double> zero={0.,0.},robin={-a*qB/denominator,0.};
    rows.row(first,zero);rows.row(first,robin);rows.row(second,zero);
    SparseArray stencil(*execution,rows);
    auto anchors=execution->upload(std::vector<int>{0,0,2});
    auto weights=execution->upload(std::vector<double>{0.,-a*qB/denominator,0.});
    auto factors=execution->upload(std::vector<double>{1.,qB/denominator,1.});
    auto kinds=execution->upload(std::vector<unsigned char>{1,1,1});
    auto values=execution->upload(std::vector<double>{1.,1.,1.});
    auto field=execution->upload(potential);
    auto gradients=execution->array<double>(3);
    execution->run(GradientWork{3,{stencil.view(),anchors.data,factors.data,weights.data,kinds.data},
        field.data,values.data,gradients.data});
    const auto actual=execution->download(gradients);
    const double expected[]={1.,1.-std::ldexp(1.,-6),1.};
    for(int i=0;i<3;++i)
        if(actual[i]!=expected[i])fail("composite prescribed flux lost an extreme-value datum");
}

/** Actual Device raw controller: one lane owns the same real scratch spans
 * through contact/separated/repeat/short/recovery calls. No Host pointer, typed
 * gravity provider, source identity or production certificate is substituted.
 */
__global__ void finite_ring_raw_storage_kernel(
    Physical::Gravity::finite_ring_detail::RingBox* boxes,
    Physical::Gravity::finite_ring_detail::RingBoxReductionView::Node* nodes,
    Physical::Gravity::RingPotentialEnclosure* output)
{
    if(blockIdx.x||threadIdx.x)return;
    using namespace Physical::Gravity;using namespace finite_ring_detail;
    constexpr std::size_t maximum=17,node_count=64;
    for(std::size_t n=0;n<maximum;++n)
        boxes[n]={-4.,-3.,-2.,-1.,{1.e100,2.e100},RingIntervalStatus::InvalidInput,999,999};
    for(std::size_t n=0;n<node_count;++n){nodes[n].integral={1.e100,2.e100};
        nodes[n].status=RingIntervalStatus::InvalidInput;nodes[n].largest_width=1.e100;nodes[n].worst_index=maximum-1;}
    RingEnclosureControl control;control.maximum_boxes=maximum;
    for(int lane=0;lane<6;++lane)
        output[lane]=finite_ring_potential_enclosure_raw(.5,1.,-.375,.375,1.,lane==1?2.:1.,0.,1.,control,
            boxes,lane==3?maximum-1:maximum,nodes,lane==4?node_count-1:node_count);
    control.relative_target=1.e-10;
    output[6]=finite_ring_potential_enclosure_raw(.5,1.,-.375,.375,0.,1.,0.,1.,control,nullptr,0,nullptr,0);
    output[7]=finite_ring_potential_enclosure_raw(.5,1.,-.375,.375,1.,0.,0.,1.,control,nullptr,0,nullptr,0);
    output[8]=finite_ring_potential_enclosure_raw(.5,1.,-.375,.375,1.,1.e6,0.,1.,control,nullptr,0,nullptr,0);
}

/** Independent full-azimuth product quadrature; a reference, not a certified near bound. */
long double independent_ring_potential(double lo,double hi,double zlo,double zhi,
    const std::array<double,3>& point,int order) {
    std::vector<long double> nodes(order),weights(order);
    constexpr long double pi_l=3.141592653589793238462643383279502884L;
    for(int i=0;i<order;++i) {
        long double x=std::cos(pi_l*(i+.75L)/(order+.5L));
        for(int it=0;it<30;++it) {
            long double prev=1.,p=x;
            for(int n=2;n<=order;++n) {const long double next=((2*n-1)*x*p-(n-1)*prev)/n;prev=p;p=next;}
            const long double derivative=order*(x*p-prev)/(x*x-1.);
            const long double next=x-p/derivative;
            if(std::abs(next-x)<4*std::numeric_limits<long double>::epsilon()) {x=next;break;}
            x=next;
        }
        long double prev=1.,p=x;
        for(int n=2;n<=order;++n) {const long double next=((2*n-1)*x*p-(n-1)*prev)/n;prev=p;p=next;}
        const long double derivative=order*(x*p-prev)/(x*x-1.);
        nodes[i]=x;weights[i]=2/((1-x*x)*derivative*derivative);
    }
    constexpr int azimuths=256;
    long double value=0.;
    for(int i=0;i<order;++i)for(int j=0;j<order;++j) {
        const long double radius=.5L*(lo+hi)+.5L*(hi-lo)*nodes[i];
        const long double z=.5L*(zlo+zhi)+.5L*(zhi-zlo)*nodes[j];
        long double angular=0.;
        for(int k=0;k<azimuths;++k) {
            const long double angle=2*pi_l*(k+.5L)/azimuths;
            const long double x=point[0]-radius*std::cos(angle);
            const long double y=point[1]-radius*std::sin(angle);
            const long double dz=point[2]-z;
            angular+=1/std::sqrt(x*x+y*y+dz*dz);
        }
        value-=radius*weights[i]*weights[j]*.25L*(hi-lo)*(zhi-zlo)
            *2*pi_l/azimuths*angular;
    }
    return value;
}

/** Compare actual Device intervals/diagnostics to the original Host controller.
 * Certificates constrain the same source integral; cross-compiler endpoints
 * need overlap, not an invented bit-exact or tighter error budget.
 */
void verify_finite_ring_raw_storage_owner()
{
    using namespace Physical::Gravity;using namespace finite_ring_detail;
    const int failures_before=failures;
    constexpr std::size_t maximum=17,node_count=64;
    DeviceArray<RingBox> boxes(maximum);
    DeviceArray<RingBoxReductionView::Node> nodes(node_count);
    DeviceArray<RingPotentialEnclosure> output(9);
    if(!boxes.pointer||!nodes.pointer||!output.pointer)return;
    finite_ring_raw_storage_kernel<<<1,1>>>(boxes.pointer,nodes.pointer,output.pointer);
    require_cuda(cudaGetLastError(),"finite-ring raw controller launch");
    require_cuda(cudaDeviceSynchronize(),"finite-ring raw controller sync");
    std::vector<RingPotentialEnclosure> actual(9);output.download(actual.data(),actual.size());
    RingEnclosureControl control;control.maximum_boxes=maximum;
    for(int lane=0;lane<9;++lane) {
        const bool short_capacity=lane==3||lane==4;
        auto policy=control;if(lane>=6)policy.relative_target=1.e-10;
        const double density=lane==6?0.:1.;const double radius=lane==1?2.:lane==7?0.:lane==8?1.e6:1.;
        const auto expected=finite_ring_potential_enclosure(.5,1.,-.375,.375,density,radius,0.,1.,policy);
        const auto& value=actual[lane];
        if(short_capacity) {
            if(value.status!=RingIntervalStatus::WorkLimit||value.bound_valid||value.leaf_boxes!=0)
                fail("short actual Device raw capacity retained a certificate");
            continue;
        }
        if(value.status!=expected.status||value.bound_valid!=expected.bound_valid
            ||value.leaf_boxes!=expected.leaf_boxes||value.range_evaluations!=expected.range_evaluations
            ||value.kernel_enclosures!=expected.kernel_enclosures||value.agm_iterations!=expected.agm_iterations)
            fail("actual Device raw controller changed original status or work diagnostics lane="+std::to_string(lane));
        if(!value.bound_valid||!std::isfinite(value.lower)||!std::isfinite(value.upper)
            ||!std::isfinite(value.value)||!std::isfinite(value.absolute_error)
            ||value.lower>value.value||value.value>value.upper
            ||value.lower>expected.upper||expected.lower>value.upper
            ||std::abs(value.value-expected.value)>value.absolute_error+expected.absolute_error)
            fail("actual Device raw interval is invalid or disjoint from original Host lane="+std::to_string(lane));
        if(lane<6) {
            if(value.status!=RingIntervalStatus::WorkLimit||value.leaf_boxes!=maximum
                ||value.range_evaluations!=2*maximum-1)
                fail("actual Device fixture bypassed original adaptive controller");
        } else if(value.status!=RingIntervalStatus::Bounded||value.leaf_boxes!=0||value.range_evaluations!=0
            ||value.absolute_error>policy.relative_target*std::abs(value.value))
            fail("actual Device physical fast path required scratch or missed original target");
        if(lane==0||lane==8) {
            const std::array<double,3> point{radius,0.,0.};
            const auto reference8=independent_ring_potential(.5,1.,-.375,.375,point,8);
            const auto reference12=independent_ring_potential(.5,1.,-.375,.375,point,12);
            if(!(value.lower<=reference8&&reference8<=value.upper
                &&value.lower<=reference12&&reference12<=value.upper))
                fail("actual Device raw contact/far escaped original independent Newton references");
        }
        if(lane==7) {
            // Same original long-double axis primitive and 2e-10 reference budget.
            const auto primitive=[](long double r,long double z){return .5L*(z*std::sqrt(r*r+z*z)+r*r*std::asinh(z/r));};
            const long double reference=-2*3.141592653589793238462643383279502884L*
                (primitive(1.,.375L)-primitive(1.,-.375L)-primitive(.5L,.375L)+primitive(.5L,-.375L));
            if(!(std::abs(value.value-reference)<2.e-10L*std::abs(reference)))
                fail("actual Device raw axis differs from original independent Newton primitive");
        }
    }
    if(failures==failures_before)std::cout<<"FINITE_RING_RAW_STORAGE_OWNER_PASS side=Device maximum=17 nodes=64 dirty_reuse=1 short_unbounded=1 fastpaths=3 kernels=1 synchronizations=1 scratch_h2d=0 receipt_d2h="
        <<9*sizeof(RingPotentialEnclosure)<<" consumer_qualified=0\n";
}

// The typed provider must reject incomplete ring work before touching
// borrowed owners, launching a kernel, or retaining an old Host certificate.
void verify_typed_ring_device_gates()
{
    using namespace Physical::Gravity;
    auto owner=arch::cuda::make_cuda_gravity_execution(nullptr,0,{});
    const auto before=owner->numeric()->counters();
    RingBoundaryEvaluation result;result.status=RingBoundaryStatus::Bounded;
    result.source_generation=99;result.values={1.};
    bool rejected=false;
    try {owner->run(EvaluateRingBoundary{nullptr,nullptr,nullptr,nullptr,&result});}
    catch(const std::logic_error& e) {
        rejected=std::string_view(e.what())=="Incomplete ring work descriptor";
    }
    if(!rejected || result.status==RingBoundaryStatus::Bounded
        || result.source_generation!=0 || !result.values.empty())
        fail("CUDA typed ring decline retained a certificate or touched missing owner");
    EvaluateBoundary legacy{};
    legacy.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    rejected=false;
    try {owner->run(legacy);}
    catch(const std::logic_error& e) {
        rejected=std::string_view(e.what())=="RZ boundary requires the typed finite-ring consumer";
    }
    if(!rejected)fail("CUDA RZ chart reached legacy logarithmic work");
    const auto after=owner->numeric()->counters();
    if(before.kernels!=after.kernels || before.bytes_h2d!=after.bytes_h2d
        || before.bytes_d2h!=after.bytes_d2h || before.synchronizations!=after.synchronizations)
        fail("CUDA typed ring decline launched or transferred work");
    std::cout<<"TYPED_RING_DEVICE_GATES_PASS legacy_log_rejected=1 result_retired=1 kernels=0 transfers=0\n";
}

/** Actual typed owner receipts against one cold Host controller. This uses
 * the supported public 4x4 operator with only four nonzero source leaves;
 * two-dimensional boundary interpolation needs more than four stored cells.
 * No solved field/Runtime/source journal is qualified by this fixture.
 */
void ring_require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
Physical::Gravity::RingBoundaryEvaluation cold_ring_boundary(
    const Physical::Gravity::GravityBoundary& tree,const arch::elliptic::CompositePoisson& op,
    const Physical::Gravity::GravitySolveIdentity& identity,const Physical::Gravity::RingBoundaryControl& control) {
    using namespace Physical::Gravity;using namespace ring_boundary_detail;using namespace finite_ring_detail;
    const auto packet=tree.prepare_ring_boundary_inputs(op,identity,control);
    RingBoundaryEvaluation result;result.source=packet.source;
    const auto count=packet.faces.size();
    result.values.assign(count,0.);result.lower.assign(count,0.);result.upper.assign(count,0.);
    result.far_truncation_upper.assign(count,0.);result.far_evaluation_width_upper.assign(count,0.);result.errors.resize(count);
    std::vector<RingBox> boxes(control.maximum_boxes_per_leaf);
    std::vector<RingBoxReductionView::Node> nodes(2*ring_reduction_capacity(control.maximum_boxes_per_leaf));
    ColdRingLeafEvaluator leaf{boxes.data(),boxes.size(),nodes.data(),nodes.size()};
    const RingBoundaryOutputView output{result.values.data(),result.lower.data(),result.upper.data(),
        result.far_truncation_upper.data(),result.far_evaluation_width_upper.data(),result.errors.data(),count};
    ring_boundary_shared(packet.view(),packet.control,output,result,leaf);return result;
}
/** Compare the complete ordered source identity without NVCC 12.3's missing
 * implicit C++20 equality symbols. Scalar == retains the original floating-point
 * semantics; this is neither a tolerant nor a partial provenance comparison.
 */
bool same_ring_source_identity(const Physical::Gravity::GravitySolveIdentity& a,
    const Physical::Gravity::GravitySolveIdentity& b)
{
    if(a.topology.value!=b.topology.value || a.inputs.size()!=b.inputs.size()
        || a.input_time!=b.input_time || a.gravitational_constant!=b.gravitational_constant
        || a.operator_revision!=b.operator_revision || a.boundary_revision!=b.boundary_revision
        || a.accuracy_revision!=b.accuracy_revision) return false;
    for(std::size_t i=0;i<a.inputs.size();++i) {
        const auto& x=a.inputs[i];const auto& y=b.inputs[i];
        if(x.block.uid.value!=y.block.uid.value || x.block.epoch.value!=y.block.epoch.value
            || x.slot!=y.slot || x.version.value!=y.version.value
            || x.storage_generation!=y.storage_generation) return false;
    }
    return true;
}
void compare_cold_ring_receipts(const Physical::Gravity::RingBoundaryEvaluation& actual,
    const Physical::Gravity::RingBoundaryEvaluation& expected,const arch::elliptic::CompositePoisson& op,
    const Physical::Gravity::RingBoundaryControl& control) {
    using namespace Physical::Gravity;using namespace arch::elliptic;
    const auto count=op.faces().size();
    ring_require(actual.status==expected.status&&same_ring_source_identity(actual.source,expected.source)
        &&actual.source_generation==expected.source_generation,"typed Device ring lost cold source/status provenance");
    ring_require(actual.values.size()==count&&actual.lower.size()==count&&actual.upper.size()==count
        &&actual.errors.size()==count&&actual.far_truncation_upper.size()==count
        &&actual.far_evaluation_width_upper.size()==count,"typed Device ring omitted full original output arrays");
    ring_require(actual.leaf_evaluations==expected.leaf_evaluations&&actual.parent_evaluations==expected.parent_evaluations
        &&actual.parent_acceptances==expected.parent_acceptances&&actual.represented_leaf_evaluations==expected.represented_leaf_evaluations
        &&actual.coalesced_parent_attempts==expected.coalesced_parent_attempts
        &&actual.coalesced_parent_acceptances==expected.coalesced_parent_acceptances
        &&actual.coalesced_native_leaves==expected.coalesced_native_leaves
        &&actual.range_evaluations==expected.range_evaluations&&actual.kernel_enclosures==expected.kernel_enclosures
        &&actual.agm_iterations==expected.agm_iterations,"typed Device ring changed cold DFS/controller work diagnostics");
    ring_require(actual.memo_hits==0&&actual.memo_admissions==0&&actual.memo_misses==expected.memo_misses
        &&actual.memo_misses==actual.leaf_evaluations+actual.coalesced_parent_attempts,
        "typed Device cold history did not retain every original leaf/quartet attempt");
    for(std::size_t face=0;face<count;++face) {
        ring_require(actual.errors[face].quality==expected.errors[face].quality,"typed Device ring changed all-or-nothing face quality");
        if(op.faces()[face].boundary_side<0) {
            ring_require(actual.values[face]==0.&&actual.lower[face]==0.&&actual.upper[face]==0.
                &&actual.errors[face].absolute_error==0.&&actual.far_truncation_upper[face]==0.
                &&actual.far_evaluation_width_upper[face]==0.,"typed Device ring gave an interior face boundary data");
            continue;
        }
        ring_require(std::isfinite(actual.values[face])&&std::isfinite(actual.lower[face])&&std::isfinite(actual.upper[face])
            &&actual.lower[face]<=actual.values[face]&&actual.values[face]<=actual.upper[face]
            &&std::isfinite(actual.errors[face].absolute_error)
            &&actual.lower[face]<=expected.upper[face]&&expected.lower[face]<=actual.upper[face]
            &&std::abs(actual.values[face]-expected.values[face])<=actual.errors[face].absolute_error+expected.errors[face].absolute_error,
            "typed Device ring interval is invalid or disjoint from original cold Host enclosure");
        ring_require(std::isfinite(actual.far_truncation_upper[face])&&actual.far_truncation_upper[face]>=0.
            &&std::isfinite(actual.far_evaluation_width_upper[face])&&actual.far_evaluation_width_upper[face]>=0.,
            "typed Device ring lost finite nonnegative far decomposition");
        if(actual.status==RingBoundaryStatus::Bounded)
            ring_require(actual.errors[face].quality==BoundaryErrorQuality::CertifiedAbsolute
                &&actual.errors[face].absolute_error<=control.face_absolute_target,"typed Device ring missed original certified absolute budget");
        else ring_require(actual.errors[face].quality==BoundaryErrorQuality::Unknown,"typed Device partial ring retained certification");
    }
}
void verify_typed_ring_device_owner() {
    using namespace Physical::Gravity;using namespace ring_boundary_detail;using namespace arch;
    const auto start_failures=failures;
    elliptic::CartesianMesh base;base.dimension=2;base.cells={4,4,1};base.spacing={.25,.25,1.};base.origin={0.,-.5,0.};
    base.geometry=elliptic::Geometry::Cylindrical;base.semantics=GridMetrics::GeometrySemantics::AxisymmetricRz;
    base.native_canonical_domain=true;base.root_upper={1.,.5,0.};
    std::vector<elliptic::CompositeCell> cells;
    for(int j=0;j<4;++j)for(int i=0;i<4;++i)cells.push_back({0,{i,j,0}});
    elliptic::CompositePoisson op(base,cells,elliptic::BoundaryKind::CurvilinearIsolated);
    GravityBoundary tree(op,{9});GravitySolveIdentity identity;identity.topology={9};
    identity.gravitational_constant=constants::gravity::cgs::gravitational_constant;
    identity.operator_revision=identity.boundary_revision=identity.accuracy_revision=1;
    identity.inputs.push_back({{{1},{9}},state::StateSlot::Current,{1},1});
    std::vector<double> density(op.size(),0.);
    for(int cell=0;cell<op.size();++cell)if(op.cells()[cell].index[0]<2&&op.cells()[cell].index[1]>=2)density[cell]=1.;
    ring_require(op.size()==16&&std::count(density.begin(),density.end(),1.)==4,"typed sparse native source shape changed");
    tree.update(density,identity);
    RingBoundaryControl control;control.face_absolute_target=1.e-5;control.maximum_boxes_per_leaf=17;
    control.maximum_leaf_evaluations=tree.full_ring_traversal_work_bound(op);
    auto owner=arch::cuda::make_cuda_gravity_execution(nullptr,0,{});
    ring_require(owner->numeric()->device(),"typed ring fixture selected a Host execution provider");
    const auto packet=tree.prepare_ring_boundary_inputs(op,identity,control);
    const auto count=packet.faces.size(),surface=packet.surface_faces.size();
    ring_require(surface==12&&control.maximum_leaf_evaluations==312,"typed owner lost original actual surface/work ceiling");
    const auto h2d=tree.nodes().size()*(sizeof(BoundaryTreeNode)+sizeof(RingMomentEnclosure)+sizeof(UniformRingQuartet))
        +static_cast<std::size_t>(op.size())*(sizeof(RingLeafEdges)+sizeof(double))+count*sizeof(RingBoundaryFace);
    const auto same_counters=[](const auto& a,const auto& b) {return a.kernels==b.kernels&&a.bytes_h2d==b.bytes_h2d
        &&a.bytes_d2h==b.bytes_d2h&&a.synchronizations==b.synchronizations;};
    RingBoundaryEvaluation actual;
    const auto run=[&](const RingBoundaryControl& budget) {
        const auto expected=cold_ring_boundary(tree,op,identity,budget);
        const auto before=owner->numeric()->counters();
        owner->run(EvaluateRingBoundary{&tree,&op,&identity,&budget,&actual});
        const auto after=owner->numeric()->counters();
        ring_require(after.kernels-before.kernels==1&&after.synchronizations-before.synchronizations==8
            &&after.bytes_h2d-before.bytes_h2d==h2d,"typed owner hid a real launch/input transfer/join");
        const auto receipt_bytes=after.bytes_d2h-before.bytes_d2h;
        ring_require(receipt_bytes>sizeof(RingBoundaryScalars)
            &&receipt_bytes<sizeof(RingBoundaryScalars)+count*(5*sizeof(double)+sizeof(elliptic::BoundaryPotentialError)),
            "typed owner downloaded complete interior arrays or omitted surface receipts");
        compare_cold_ring_receipts(actual,expected,op,budget);
        return receipt_bytes;
    };
    const auto receipt_bytes=run(control);const auto first=actual;
    ring_require(first.status==RingBoundaryStatus::Bounded&&first.represented_leaf_evaluations==surface*op.size()
        &&first.coalesced_parent_acceptances>0&&first.range_evaluations>0,"typed current source did not complete actual finite-ring work");
    tree.require_current_ring(op,first);
    for(const int face:packet.surface_faces) {
        const auto& f=op.faces()[face];
        ring_require(packet.faces[face].r==f.center[0]&&packet.faces[face].z==f.center[1],"typed packet lost native (r,z) observer words");
    }
    const int probe=packet.surface_faces.front();
    ring_require(op.faces()[probe].center[1]!=0.,"typed coordinate fixture did not exercise a nonzero axial observer");
    int contact=-1;
    for(const int face:packet.surface_faces)if(op.faces()[face].boundary_side==3&&op.faces()[face].center[0]<.5) {contact=face;break;}
    ring_require(contact>=0,"typed fixture omitted the actual positive-source contact observer");
    for(const int observer:{probe,contact}) {
        long double reference8=0.,reference12=0.;
        const std::array<double,3> point{op.faces()[observer].center[0],0.,op.faces()[observer].center[1]};
        for(int cell=0;cell<op.size();++cell)if(density[cell]>0.) {
            const auto factor=static_cast<long double>(identity.gravitational_constant)*density[cell];
            reference8+=factor*independent_ring_potential(op.lower(cell,0),op.upper(cell,0),op.lower(cell,1),op.upper(cell,1),point,8);
            reference12+=factor*independent_ring_potential(op.lower(cell,0),op.upper(cell,0),op.lower(cell,1),op.upper(cell,1),point,12);
        }
        ring_require(first.lower[observer]<=reference8&&reference8<=first.upper[observer]
            &&first.lower[observer]<=reference12&&reference12<=first.upper[observer],
            "typed native contact/separated face escaped original independent Newton reference orders");
    }
    ring_require(run(control)==receipt_bytes&&actual.values==first.values&&actual.lower==first.lower&&actual.upper==first.upper
        &&actual.source_generation==first.source_generation,"typed owner dirty reuse changed same-input Device receipts");
    auto prefix=control;prefix.maximum_leaf_evaluations=5;run(prefix);
    ring_require(actual.status==RingBoundaryStatus::WorkLimit&&actual.parent_evaluations==5&&actual.leaf_evaluations==0
        &&actual.coalesced_parent_acceptances==4&&actual.represented_leaf_evaluations==16&&actual.values[probe]<0.,
        "typed global cap lost the original completed first-face prefix");
    for(std::size_t f=0;f<count;++f) {
        ring_require(actual.errors[f].quality==elliptic::BoundaryErrorQuality::Unknown,"typed partial prefix certified a face");
        if(static_cast<int>(f)!=probe)ring_require(actual.values[f]==0.,"typed global cap evaluated a later face");
    }
    bool partial_rejected=false;try {tree.require_current_ring(op,actual);}catch(const std::exception&) {partial_rejected=true;}
    ring_require(partial_rejected,"typed partial prefix became a current certified source receipt");
    auto fallback=control;fallback.face_absolute_target=0.;fallback.maximum_boxes_per_leaf=1;fallback.maximum_leaf_evaluations=4;run(fallback);
    ring_require(actual.status==RingBoundaryStatus::WorkLimit&&actual.parent_evaluations==4&&actual.leaf_evaluations==0
        &&actual.coalesced_parent_attempts==3&&actual.coalesced_parent_acceptances==2&&actual.represented_leaf_evaluations==8,
        "typed failed quartet fallback was not charged before evaluation");
    auto zero_target=control;zero_target.face_absolute_target=0.;run(zero_target);
    ring_require(actual.status!=RingBoundaryStatus::Bounded,"typed nonzero source acquired a hidden zero-target floor");
    const auto reject=[&](const EvaluateRingBoundary& request) {
        actual=first;const auto before=owner->numeric()->counters();bool rejected=false;
        try {owner->run(request);}catch(const std::exception&) {rejected=true;}
        ring_require(rejected&&same_counters(before,owner->numeric()->counters()),"typed rejected request did CUDA work");
        if(request.result)ring_require(actual.source_generation==0&&actual.source.inputs.empty()&&actual.values.empty()
            &&actual.lower.empty()&&actual.upper.empty()&&actual.errors.empty()
            &&actual.status!=RingBoundaryStatus::Bounded,"typed rejected request retained previous certification");
    };
    auto stale=identity;stale.inputs.front().version={2};reject({&tree,&op,&stale,&control,&actual});
    auto bad_control=control;bad_control.maximum_leaf_evaluations=0;reject({&tree,&op,&identity,&bad_control,&actual});
    bad_control=control;bad_control.maximum_boxes_per_leaf=0;reject({&tree,&op,&identity,&bad_control,&actual});
    bad_control=control;bad_control.maximum_boxes_per_leaf=65537;reject({&tree,&op,&identity,&bad_control,&actual});
    bad_control=control;bad_control.face_absolute_target=std::numeric_limits<double>::quiet_NaN();reject({&tree,&op,&identity,&bad_control,&actual});
    reject({nullptr,&op,&identity,&control,&actual});reject({&tree,&op,&identity,&control,nullptr});
    auto moved_base=base;moved_base.origin[1]+=.25;moved_base.root_upper[1]+=.25;
    elliptic::CompositePoisson moved_op(moved_base,cells,elliptic::BoundaryKind::CurvilinearIsolated);
    reject({&tree,&moved_op,&identity,&control,&actual});
    auto invalid=density;invalid[8]=-1.;bool update_rejected=false;
    try {tree.update(invalid,identity);}catch(const std::exception&) {update_rejected=true;}
    ring_require(update_rejected,"typed source fixture accepted a negative input update");
    reject({&tree,&op,&identity,&control,&actual});
    tree.update(density,identity);run(control);
    ring_require(actual.status==RingBoundaryStatus::Bounded&&actual.source_generation>first.source_generation,
        "typed actual owner did not recover with a new materialized source generation");
    bool old_rejected=false;try {tree.require_current_ring(op,first);}catch(const std::exception&) {old_rejected=true;}
    ring_require(old_rejected,"typed original receipt gained authority over replacement source generation");
    // Same four positive leaves, now nonuniform: exact coalescence must decline,
    // and accepted general parents retain separate tail/evaluation outputs.
    const auto recovered_generation=actual.source_generation;
    density[8]=.5;density[9]=1.;density[12]=1.5;density[13]=2.;identity.inputs.front().version={2};
    tree.update(density,identity);run(control);
    ring_require(actual.status==RingBoundaryStatus::Bounded&&actual.source_generation>recovered_generation
        &&actual.parent_acceptances>0&&*std::max_element(actual.far_truncation_upper.begin(),actual.far_truncation_upper.end())>0.
        &&*std::max_element(actual.far_evaluation_width_upper.begin(),actual.far_evaluation_width_upper.end())>0.,
        "typed nonuniform current source omitted accepted far-parent error decomposition");
    tree.require_current_ring(op,actual);
    if(failures==start_failures)std::cout<<"TYPED_RING_DEVICE_OWNER_PASS stored_leaves=16 nonzero_leaves=4 surface_faces="<<surface
        <<" global_cap_order=1 fallback_charge=1 reuse=1 invalid_zero_work=1 kernels_per_call=1 joins_per_call=8 receipt_d2h="
        <<receipt_bytes<<" field_qualified=0 runtime_qualified=0\n";
}

int main()
{
    int device_count = 0;
    require_cuda(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount");
    if (device_count <= 0) {
        fail("CUDA device unavailable");
        return 1;
    }
    // The actual launches below validate the compiled image. These reductions
    // need no device-model or exact compute-capability restriction.
    verify_device_edges();
    verify_real_hydro_owner();
    verify_real_diffusion_owner();
    verify_composite_reductions();
    verify_composite_flux_extremes();
    verify_finite_ring_raw_storage_owner();
    verify_typed_ring_device_gates();
    try {verify_typed_ring_device_owner();}catch(const std::exception& e){fail(std::string("typed ring owner: ")+e.what());}
    if (edge_cases != 36) fail("device edge case count");
    if (failures == 0)
        std::cout << "D2_CUDA_REDUCTION_CONTRACT_PASS edge_cases="
                  << edge_cases << '\n';
    return failures == 0 ? 0 : 1;
}
