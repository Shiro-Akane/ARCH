/**
 * @file test_cuda_reduction_contract.cu
 * @brief Check reduction contracts on an actual CUDA device.
 *
 * Edge cases test candidate ordering and invalid states; production hydro
 * and diffusion owners verify that runtime reductions use those contracts.
 */
#include <cuda_runtime.h>

#include "cuda/diffusion/DiffusionKernels.cuh"
#include "cuda/hydro/HydroStateKernels.cuh"
#include "driver/ReductionSpec.h"
#include "physics/eos/IdealGas.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
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
    if (std::bit_cast<std::uint64_t>(value) != 0x3fce8a38358aef78ULL)
        fail("real hydro reduction raw authority");

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
    if (std::bit_cast<std::uint64_t>(value) != 0x4202a05f20000000ULL)
        fail("finite-seed diffusion raw authority");

    config.use_diffusion = false;
    const auto disabled = arch::cuda::launch_raw_diffusion_dt(
        device_state.view, eos, eos.species,
        arch::cuda::make_device_grid_view(grid), config, workspace, nullptr);
    require_cuda(disabled.error, "disabled diffusion reduction launch");
    require_cuda(cudaDeviceSynchronize(), "disabled diffusion reduction sync");
    result.download(&value, 1);
    if (std::bit_cast<std::uint64_t>(value) != 0x4202a05f20000000ULL)
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
    if (edge_cases != 36) fail("device edge case count");
    if (failures == 0)
        std::cout << "D2_CUDA_REDUCTION_CONTRACT_PASS edge_cases="
                  << edge_cases << '\n';
    return failures == 0 ? 0 : 1;
}
