#pragma once

#include <cuda_runtime.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cuda/diffusion/DiffusionKernels.cuh"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"

// Test-private shared surface for the split diffusion RKL parity test. The
// fail/require_cuda/expect_close/make_grid/make_state/make_config definitions
// and the single failure counter stay in the core TU
// (test_diffusion_rkl_parity.cu); the tabular TU
// (test_diffusion_rkl_tabular.cu) links the same definitions and reports into
// the same counter, so both TUs keep one failure owner and one main().
namespace diffusion_rkl_test {

void fail(const std::string& message);

void require_cuda(cudaError_t error, const char* operation);

void expect_close(const char* label, double actual, double expected,
                  double relative_limit = 5.0e-12);

Grid make_grid(int dimension, double cell_width = 0.01);

FluidState make_state(const Grid& grid, int species_count);

SimConfig make_config(bool thermal, bool viscous, bool species);

// Defined in test_diffusion_rkl_tabular.cu; core main() calls it at its
// original position so the unchanged executable keeps all ten entry points.
void verify_tabular_diffusion_failures();

template <class T>
struct DeviceArray
{
    T* pointer = nullptr;
    std::size_t count = 0;

    DeviceArray() = default;
    explicit DeviceArray(std::size_t size) : count(size)
    {
        if (count > 0) require_cuda(
            cudaMalloc(reinterpret_cast<void**>(&pointer), count * sizeof(T)),
            "cudaMalloc");
    }
    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;
    DeviceArray(DeviceArray&& other) noexcept
        : pointer(std::exchange(other.pointer, nullptr)), count(other.count) {}
    ~DeviceArray() { if (pointer) cudaFree(pointer); }

    void upload(const T* source, std::size_t size)
    {
        require_cuda(cudaMemcpy(pointer, source, size * sizeof(T),
                                cudaMemcpyHostToDevice), "upload");
    }
    void download(T* destination, std::size_t size) const
    {
        require_cuda(cudaMemcpy(destination, pointer, size * sizeof(T),
                                cudaMemcpyDeviceToHost), "download");
    }
};

struct DeviceStateOwner
{
    DeviceArray<double> rho, mom_u, mom_v, mom_w, eng, enuc, species;
    arch::cuda::DeviceStateView view{};

    DeviceStateOwner(int total, int n_species)
        : rho(total), mom_u(total), mom_v(total), mom_w(total), eng(total),
          enuc(total), species(static_cast<std::size_t>(total) * n_species)
    {
        view = {rho.pointer, mom_u.pointer, mom_v.pointer, mom_w.pointer,
                eng.pointer, enuc.pointer, species.pointer, total, n_species};
    }

    void upload(const FluidState& state)
    {
        const std::size_t total = state.rho.size();
        rho.upload(state.rho.data(), total);
        mom_u.upload(state.mom_u.data(), total);
        mom_v.upload(state.mom_v.data(), total);
        mom_w.upload(state.mom_w.data(), total);
        eng.upload(state.eng.data(), total);
        enuc.upload(state.enuc_rate.data(), total);
        if (!state.mass_fractions.empty())
            species.upload(state.mass_fractions.data(), state.mass_fractions.size());
    }

    FluidState download() const
    {
        FluidState state;
        state.Preallocate(view.total_size);
        state.InitSpecies(view.n_species);
        rho.download(state.rho.data(), state.rho.size());
        mom_u.download(state.mom_u.data(), state.mom_u.size());
        mom_v.download(state.mom_v.data(), state.mom_v.size());
        mom_w.download(state.mom_w.data(), state.mom_w.size());
        eng.download(state.eng.data(), state.eng.size());
        enuc.download(state.enuc_rate.data(), state.enuc_rate.size());
        if (!state.mass_fractions.empty())
            species.download(state.mass_fractions.data(), state.mass_fractions.size());
        return state;
    }
};

struct DeviceFixture
{
    Grid grid = make_grid(1);
    FluidState host_state = make_state(grid, 2);
    DeviceStateOwner state{grid.GetTotalSize(), 2};
    DeviceStateOwner output{grid.GetTotalSize(), 2};
    DeviceStateOwner flux{grid.GetTotalSize(), 2};
    DeviceArray<double> candidates{static_cast<std::size_t>(
        (grid.Ie() - grid.Is()) * (grid.Je() - grid.Js()) *
        (grid.Ke() - grid.Ks()))};
    DeviceArray<double> result{1};
    DeviceArray<int> status{1};
    DeviceArray<double> volume{static_cast<std::size_t>(grid.GetTotalSize())};
    DeviceArray<double> lower{static_cast<std::size_t>(grid.GetTotalSize())};
    DeviceArray<double> upper{static_cast<std::size_t>(grid.GetTotalSize())};
    DeviceArray<double> A{2}, Z{2}, gamma{2}, cv{2};
    IdealGasView eos{};
    arch::cuda::DeviceGridView device_grid{};
    arch::cuda::DiffusionWorkspaceView workspace{};

    DeviceFixture()
    {
        state.upload(host_state);
        FluidState sentinel = make_state(grid, 2);
        std::fill(sentinel.rho.begin(), sentinel.rho.end(), -101.0);
        std::fill(sentinel.mom_u.begin(), sentinel.mom_u.end(), -102.0);
        std::fill(sentinel.mom_v.begin(), sentinel.mom_v.end(), -103.0);
        std::fill(sentinel.mom_w.begin(), sentinel.mom_w.end(), -104.0);
        std::fill(sentinel.eng.begin(), sentinel.eng.end(), -105.0);
        std::fill(sentinel.enuc_rate.begin(), sentinel.enuc_rate.end(), -777.0);
        std::fill(sentinel.mass_fractions.begin(), sentinel.mass_fractions.end(), -106.0);
        output.upload(sentinel);
        flux.upload(sentinel);

        std::vector<double> metric(grid.GetTotalSize(), grid.dx1);
        std::vector<double> area(grid.GetTotalSize(), 1.0);
        volume.upload(metric.data(), metric.size());
        lower.upload(area.data(), area.size());
        upper.upload(area.data(), area.size());
        device_grid = arch::cuda::make_device_grid_view(grid);
        device_grid.cell_volume = volume.pointer;
        device_grid.face_area_lower[0] = lower.pointer;
        device_grid.face_area_upper[0] = upper.pointer;

        const double hA[2] = {1.0, 4.0};
        const double hZ[2] = {1.0, 2.0};
        const double hgamma[2] = {1.4, 1.5};
        const double hcv[2] = {3.5, 7.25};
        A.upload(hA, 2); Z.upload(hZ, 2); gamma.upload(hgamma, 2); cv.upload(hcv, 2);
        eos.species = {A.pointer, Z.pointer, gamma.pointer, cv.pointer, 2};
        eos.global_gamma = 1.37;
        workspace = {flux.view, candidates.pointer, result.pointer, status.pointer};
    }
};

} // namespace diffusion_rkl_test
