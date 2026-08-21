#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#if defined(__CUDACC__)
#include <cuda_runtime.h>
#endif

#include "driver/DriverUtils.h"
#include "numerics/flux/FluxFunctions.h"
#include "numerics/flux/FluxHLL.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/FluxRoe.h"
#include "numerics/flux/FluxSW.h"
#include "numerics/flux/FluxVL.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "numerics/reconstruction/Reconstruction.h"

#if defined(__CUDACC__)
#include "cuda/hydro/HydroStageKernels.cuh"
#endif

namespace
{
struct TestIdealGas
{
    ARCH_INLINE double get_gamma(const double*) const { return 1.4; }

    ARCH_INLINE double get_pressure(const FluidVector& state, const double*) const
    {
        const double kinetic = 0.5
            * (state.mom_u * state.mom_u + state.mom_v * state.mom_v
               + state.mom_w * state.mom_w)
            / state.rho;
        return 0.4 * (state.eng - kinetic);
    }

    ARCH_INLINE double get_sound_speed(
        const FluidVector& state, double pressure, const double*) const
    {
        return std::sqrt(1.4 * pressure / state.rho);
    }

    ARCH_INLINE double get_pressure_from_rho_e(double rho, double energy, const double*) const
    {
        return 0.4 * rho * energy;
    }

    ARCH_INLINE double get_dp_drho_e(double, double energy, const double*) const
    {
        return 0.4 * energy;
    }

    ARCH_INLINE double get_dp_de_rho(double rho, double, const double*) const
    {
        return 0.4 * rho;
    }

    ARCH_INLINE double get_total_energy_primitive(
        double rho, double u, double v, double w, double pressure,
        const double*) const
    {
        return pressure / 0.4 + 0.5 * rho * (u * u + v * v + w * w);
    }
};

struct FaceResult
{
    FluidVector flux;
    double species[2];
};

struct DeviceLeafResult
{
    FluidVector hll;
    FluidVector hllc;
    FluidVector roe;
    FluidVector sw;
    FluidVector vl;
    FluidVector pcm_left;
    FluidVector pcm_right;
    FluidVector muscl_left;
    FluidVector muscl_right;
    double ppm_left;
    double ppm_right;
    double limiter;
    double limiter_negzero;
    double slope;
    double slope_below;
    double cfl_nan_minimum;
    double cfl_infinite_minimum;
    double species[10];
};

ARCH_INLINE void evaluate_device_leaves(DeviceLeafResult* output)
{
    const FluidVector left{1.0, 0.75, -0.2, 0.1, 2.80625};
    const FluidVector right{0.8, -0.2, 0.24, -0.12, 1.82};
    const double species_left[2] = {0.4, 0.6};
    const double species_right[2] = {0.7, 0.3};
    const TestIdealGas eos;
    output->limiter = MinMod::calc(0.5);
    output->limiter_negzero = MinMod::calc(-0.0);
    output->slope = compute_limited_slope<MinMod>(0.0, 1.0, 3.0);
    output->slope_below = compute_limited_slope<MinMod>(
        0.0, 1.0, 1.0 + 0.5e-12);
    output->cfl_nan_minimum = combine_cfl_minimum(
        3.0, std::numeric_limits<double>::quiet_NaN());
    output->cfl_infinite_minimum = combine_cfl_minimum(
        3.0, std::numeric_limits<double>::infinity());

    FluidVector pcm_left;
    FluidVector pcm_right;
    PCMReconstruction::reconstruct(left, right, pcm_left, pcm_right);
    output->pcm_left = pcm_left;
    output->pcm_right = pcm_right;
    FluidVector muscl_left;
    FluidVector muscl_right;
    MusclReconstruction<MinMod>::reconstruct(
        {0.5, -2.0, 2.0, -1.0, 4.0}, {1.0, -1.0, 3.0, 0.0, 5.0},
        {3.0, 2.0, 4.0, 1.0, 9.0}, {6.0, 4.0, 8.0, 3.0, 12.0},
        muscl_left, muscl_right);
    output->muscl_left = muscl_left;
    output->muscl_right = muscl_right;
    const double ppm_values[6] = {0.0, 0.0, 0.0, 10.0, 10.0, 10.0};
    double ppm_left;
    double ppm_right;
    PPMReconstruction::reconstruct_scalar_ppm(
        ppm_values, ppm_left, ppm_right);
    output->ppm_left = ppm_left;
    output->ppm_right = ppm_right;
    FluidVector flux;
    double species_flux[2];
    FluxHLL<PCMReconstruction>::compute_face_flux(
        left, right, species_left, species_right, 2, eos, 0, 0.0,
        flux, species_flux);
    output->hll = flux;
    output->species[0] = species_flux[0];
    output->species[1] = species_flux[1];
    FluxHLLC<PCMReconstruction>::compute_face_flux(
        left, right, species_left, species_right, 2, eos, 0, 0.0,
        flux, species_flux);
    output->hllc = flux;
    output->species[2] = species_flux[0];
    output->species[3] = species_flux[1];
    FluxRoe<PCMReconstruction>::compute_face_flux(
        left, right, species_left, species_right, 2, eos, 0, 0.1,
        flux, species_flux);
    output->roe = flux;
    output->species[4] = species_flux[0];
    output->species[5] = species_flux[1];
    FluxSW<PCMReconstruction>::compute_face_flux(
        left, right, species_left, species_right, 2, eos, 0, 0.1,
        flux, species_flux);
    output->sw = flux;
    output->species[6] = species_flux[0];
    output->species[7] = species_flux[1];
    FluxVL<PCMReconstruction>::compute_face_flux(
        left, right, species_left, species_right, 2, eos, 0, 0.0,
        flux, species_flux);
    output->vl = flux;
    output->species[8] = species_flux[0];
    output->species[9] = species_flux[1];
}

#if defined(__CUDACC__)
__global__ void evaluate_device_leaves_kernel(DeviceLeafResult* result)
{
    evaluate_device_leaves(result);
}
#endif

template <template <typename> class Flux>
FaceResult characterize_face(double coefficient)
{
    Grid grid(3, 0.0, 16.0);
    grid.dim = 1;
    grid.InitializeTopology();

    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(2);
    const FluidVector left{1.0, 0.75, -0.2, 0.1, 2.80625};
    const FluidVector right{0.8, -0.2, 0.24, -0.12, 1.82};
    const int left_cell = grid.Is() + 4;
    for (int cell = 0; cell < grid.GetTotalSize(); ++cell) {
        const bool is_left = cell <= left_cell;
        state.set(cell, is_left ? left : right);
        state.X(0, cell) = is_left ? 0.4 : 0.7;
        state.X(1, cell) = is_left ? 0.6 : 0.3;
    }

    std::vector<FluidVector> flux(grid.GetTotalSize());
    std::vector<double> species_flux(2 * grid.GetTotalSize());
    Flux<PCMReconstruction>::compute_fluxes(
        state, TestIdealGas{}, grid, flux, species_flux, 0, coefficient);
    const int face = left_cell + 1;
    return {flux[face], {species_flux[face],
                         species_flux[grid.GetTotalSize() + face]}};
}

template <typename Reconstruction>
FaceResult characterize_hll_reconstruction()
{
    Grid grid(3, 0.0, 16.0);
    grid.dim = 1;
    grid.InitializeTopology();
    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(2);
    const FluidVector left{1.0, 0.75, -0.2, 0.1, 2.80625};
    const FluidVector right{0.8, -0.2, 0.24, -0.12, 1.82};
    const int left_cell = grid.Is() + 4;
    for (int cell = 0; cell < grid.GetTotalSize(); ++cell) {
        const bool is_left = cell <= left_cell;
        state.set(cell, is_left ? left : right);
        state.X(0, cell) = is_left ? 0.4 : 0.7;
        state.X(1, cell) = is_left ? 0.6 : 0.3;
    }
    std::vector<FluidVector> flux(grid.GetTotalSize());
    std::vector<double> species_flux(2 * grid.GetTotalSize());
    FluxHLL<Reconstruction>::compute_fluxes(
        state, TestIdealGas{}, grid, flux, species_flux, 0, 0.0);
    const int face = left_cell + 1;
    return {flux[face], {species_flux[face],
                         species_flux[grid.GetTotalSize() + face]}};
}

bool exact_bits(double actual, std::uint64_t expected)
{
    return std::bit_cast<std::uint64_t>(actual) == expected;
}

bool vector_bits(
    const FluidVector& actual, std::uint64_t rho, std::uint64_t mom_u,
    std::uint64_t mom_v, std::uint64_t mom_w, std::uint64_t eng)
{
    return exact_bits(actual.rho, rho) && exact_bits(actual.mom_u, mom_u)
        && exact_bits(actual.mom_v, mom_v) && exact_bits(actual.mom_w, mom_w)
        && exact_bits(actual.eng, eng);
}

bool vector_near(const FluidVector& actual, const FluidVector& expected)
{
    const auto near = [](double a, double b) {
        return std::abs(a - b) <= 3e-14 * std::max(1.0, std::abs(b));
    };
    return near(actual.rho, expected.rho) && near(actual.mom_u, expected.mom_u)
        && near(actual.mom_v, expected.mom_v) && near(actual.mom_w, expected.mom_w)
        && near(actual.eng, expected.eng);
}

bool validate_characterized_host_paths()
{
    if (!exact_bits(MinMod::calc(-0.0), 0x0000000000000000ULL)
        || !exact_bits(MinMod::calc(0.5), 0x3fe0000000000000ULL)
        || !exact_bits(SuperBee::calc(0.5), 0x3ff0000000000000ULL)
        || !exact_bits(VanLeer::calc(std::numeric_limits<double>::infinity()),
                       0xfff8000000000000ULL)
        || !exact_bits(McLimiter::calc(std::numeric_limits<double>::quiet_NaN()),
                       0x4000000000000000ULL)
        || !exact_bits(compute_limited_slope<MinMod>(0.0, 1.0, 1.0 + 0.5e-12),
                       0x0000000000000000ULL)
        || !exact_bits(compute_limited_slope<MinMod>(0.0, 1.0, 1.0 + 1.0e-12),
                       0x3d61980000000000ULL))
        return false;

    const FluidVector state{2.0, 6.0, 8.0, 10.0, 100.0};
    if (!vector_bits(get_flux(state, 7.0, 0),
                     0x4018000000000000ULL, 0x4039000000000000ULL,
                     0x4038000000000000ULL, 0x403e000000000000ULL,
                     0x4074100000000000ULL)
        || !vector_bits(get_flux({-0.0, 1.0, 2.0, 3.0, 4.0}, 7.0, 0),
                        0, 0, 0, 0, 0)
        || !exact_bits(entropy_fix(-0.0, 1.0), 0x3fe0000000000000ULL))
        return false;

    const auto ppm = PPMReconstruction::apply(
        {1, 0, 0, 0, 2}, {1, 0, 0, 0, 2}, {1, 0, 0, 0, 2},
        {10, 0, 0, 0, 20}, {10, 0, 0, 0, 20}, {10, 0, 0, 0, 20});
    if (!vector_bits(ppm.first,
                     0x4004000000000000ULL, 0, 0, 0, 0x4014000000000000ULL)
        || !vector_bits(ppm.second,
                        0x4020fffffffffffeULL, 0, 0, 0, 0x4030fffffffffffeULL))
        return false;

    const FaceResult hll = characterize_face<FluxHLL>(0.0);
    const FaceResult hllc = characterize_face<FluxHLLC>(0.0);
    const FaceResult roe = characterize_face<FluxRoe>(0.1);
    const FaceResult sw = characterize_face<FluxSW>(0.1);
    const FaceResult vl = characterize_face<FluxVL>(0.0);
    return vector_bits(hll.flux,
                       0x3fdfe0fa91402e76ULL, 0x3ffc59d9bcfbe631ULL,
                       0xbfd6c55f025c1539ULL, 0x3fc6c55f025c1539ULL,
                       0x40008c51d5581401ULL)
        && vector_bits(hllc.flux,
                       0x3fe03e732de72443ULL, 0x3ffc695d1bdcbc59ULL,
                       0xbfb9fd85163ea06cULL, 0x3fa9fd85163ea06cULL,
                       0x4000b6f9c9e9bf2cULL)
        && vector_bits(roe.flux,
                       0x3fe0468a0283dcb1ULL, 0x3ffc65ce3bd35266ULL,
                       0xbfc4507df7b36b44ULL, 0x3fb4507df7b36b44ULL,
                       0x4000bdaf3bd85012ULL)
        && vector_bits(sw.flux,
                       0x3fdd6f579bdceca1ULL, 0x4000492c964085f2ULL,
                       0xbfd41edd0ad3fc16ULL, 0x3fc41edd0ad3fc16ULL,
                       0x3fff91085fc8c1d9ULL)
        && vector_bits(vl.flux,
                       0x3fdd3f7f1d3b7d98ULL, 0x40006dfb578715e0ULL,
                       0xbfd07e985324a906ULL, 0x3fc07e985324a906ULL,
                       0x3ffff6d62a7013d3ULL)
        && exact_bits(hll.species[0], 0x3fc980c87433585fULL)
        && exact_bits(hllc.species[0], 0x3fc9fd85163ea06cULL)
        && exact_bits(roe.species[0], 0x3fca0a766a6c944fULL)
        && exact_bits(sw.species[0], 0x3fa9dee10cbc3d20ULL)
        && exact_bits(vl.species[0], 0x3fb53fc3c72dfef2ULL);
}

void print_double(std::string_view name, double value)
{
    std::cout << name << " 0x" << std::hex << std::setw(16)
              << std::setfill('0') << std::bit_cast<std::uint64_t>(value)
              << std::dec << '\n';
}

void print_vector(std::string_view name, const FluidVector& value)
{
    const std::string prefix(name);
    print_double(prefix + ".rho", value.rho);
    print_double(prefix + ".mom_u", value.mom_u);
    print_double(prefix + ".mom_v", value.mom_v);
    print_double(prefix + ".mom_w", value.mom_w);
    print_double(prefix + ".eng", value.eng);
}

#if defined(__CUDACC__)
struct DeviceStateFixture
{
    arch::cuda::DeviceStateView view{};
    bool valid = false;
    std::vector<double> rho;
    std::vector<double> mom_u;
    std::vector<double> mom_v;
    std::vector<double> mom_w;
    std::vector<double> eng;
    std::vector<double> enuc_rate;
    std::vector<double> mass_fractions;

    DeviceStateFixture(int total_size, int n_species)
        : rho(total_size), mom_u(total_size), mom_v(total_size),
          mom_w(total_size), eng(total_size), enuc_rate(total_size),
          mass_fractions(n_species * total_size)
    {
        const std::size_t field_bytes = total_size * sizeof(double);
        valid = cudaMalloc(&view.rho, field_bytes) == cudaSuccess
            && cudaMalloc(&view.mom_u, field_bytes) == cudaSuccess
            && cudaMalloc(&view.mom_v, field_bytes) == cudaSuccess
            && cudaMalloc(&view.mom_w, field_bytes) == cudaSuccess
            && cudaMalloc(&view.eng, field_bytes) == cudaSuccess
            && cudaMalloc(&view.enuc_rate, field_bytes) == cudaSuccess;
        if (valid && n_species > 0) {
            valid = cudaMalloc(
                &view.mass_fractions,
                n_species * total_size * sizeof(double)) == cudaSuccess;
        }
        view.total_size = total_size;
        view.n_species = n_species;
    }

    DeviceStateFixture(const DeviceStateFixture&) = delete;
    DeviceStateFixture& operator=(const DeviceStateFixture&) = delete;

    ~DeviceStateFixture()
    {
        if (view.rho) cudaFree(view.rho);
        if (view.mom_u) cudaFree(view.mom_u);
        if (view.mom_v) cudaFree(view.mom_v);
        if (view.mom_w) cudaFree(view.mom_w);
        if (view.eng) cudaFree(view.eng);
        if (view.enuc_rate) cudaFree(view.enuc_rate);
        if (view.mass_fractions) cudaFree(view.mass_fractions);
    }

    void store(int cell, const FluidVector& value)
    {
        rho[cell] = value.rho;
        mom_u[cell] = value.mom_u;
        mom_v[cell] = value.mom_v;
        mom_w[cell] = value.mom_w;
        eng[cell] = value.eng;
    }

    FluidVector load(int cell) const
    {
        return {rho[cell], mom_u[cell], mom_v[cell], mom_w[cell], eng[cell]};
    }

    void set_species(int species_index, int cell, double value)
    {
        mass_fractions[species_index * view.total_size + cell] = value;
    }

    double species(int species_index, int cell) const
    {
        return mass_fractions[species_index * view.total_size + cell];
    }

    bool upload() const
    {
        const std::size_t bytes = view.total_size * sizeof(double);
        return cudaMemcpy(view.rho, rho.data(), bytes, cudaMemcpyHostToDevice) == cudaSuccess
            && cudaMemcpy(view.mom_u, mom_u.data(), bytes, cudaMemcpyHostToDevice) == cudaSuccess
            && cudaMemcpy(view.mom_v, mom_v.data(), bytes, cudaMemcpyHostToDevice) == cudaSuccess
            && cudaMemcpy(view.mom_w, mom_w.data(), bytes, cudaMemcpyHostToDevice) == cudaSuccess
            && cudaMemcpy(view.eng, eng.data(), bytes, cudaMemcpyHostToDevice) == cudaSuccess
            && cudaMemcpy(view.enuc_rate, enuc_rate.data(), bytes, cudaMemcpyHostToDevice) == cudaSuccess
            && (view.n_species == 0
                || cudaMemcpy(
                       view.mass_fractions, mass_fractions.data(),
                       view.n_species * bytes, cudaMemcpyHostToDevice) == cudaSuccess);
    }

    bool download()
    {
        const std::size_t bytes = view.total_size * sizeof(double);
        return cudaMemcpy(rho.data(), view.rho, bytes, cudaMemcpyDeviceToHost) == cudaSuccess
            && cudaMemcpy(mom_u.data(), view.mom_u, bytes, cudaMemcpyDeviceToHost) == cudaSuccess
            && cudaMemcpy(mom_v.data(), view.mom_v, bytes, cudaMemcpyDeviceToHost) == cudaSuccess
            && cudaMemcpy(mom_w.data(), view.mom_w, bytes, cudaMemcpyDeviceToHost) == cudaSuccess
            && cudaMemcpy(eng.data(), view.eng, bytes, cudaMemcpyDeviceToHost) == cudaSuccess
            && cudaMemcpy(enuc_rate.data(), view.enuc_rate, bytes, cudaMemcpyDeviceToHost) == cudaSuccess
            && (view.n_species == 0
                || cudaMemcpy(
                       mass_fractions.data(), view.mass_fractions,
                       view.n_species * bytes, cudaMemcpyDeviceToHost) == cudaSuccess);
    }
};

bool device_leaf_result_matches(const DeviceLeafResult& actual)
{
    const FaceResult hll = characterize_face<FluxHLL>(0.0);
    const FaceResult hllc = characterize_face<FluxHLLC>(0.0);
    const FaceResult roe = characterize_face<FluxRoe>(0.1);
    const FaceResult sw = characterize_face<FluxSW>(0.1);
    const FaceResult vl = characterize_face<FluxVL>(0.0);
    const bool matches = vector_near(actual.hll, hll.flux)
        && vector_near(actual.hllc, hllc.flux)
        && vector_near(actual.roe, roe.flux)
        && vector_near(actual.sw, sw.flux)
        && vector_near(actual.vl, vl.flux)
        && exact_bits(actual.limiter, 0x3fe0000000000000ULL)
        && exact_bits(actual.limiter_negzero, 0x0000000000000000ULL)
        && exact_bits(actual.slope, 0x3fe0000000000000ULL)
        && exact_bits(actual.slope_below, 0x0000000000000000ULL)
        && std::abs(actual.ppm_left - 5.0 / 3.0)
               <= 2e-15 * std::max(1.0, std::abs(5.0 / 3.0))
        && std::abs(actual.ppm_right - 25.0 / 3.0)
               <= 2e-15 * std::max(1.0, std::abs(25.0 / 3.0))
        && actual.cfl_nan_minimum == 3.0
        && actual.cfl_infinite_minimum == 3.0;
    if (!matches) {
        const auto show = [](const char* name, double value) {
            std::cerr << name << "=" << std::setprecision(17) << value
                      << " bits=0x" << std::hex
                      << std::bit_cast<std::uint64_t>(value) << std::dec << '\n';
        };
        show("limiter", actual.limiter);
        show("limiter_negzero", actual.limiter_negzero);
        show("slope", actual.slope);
        show("slope_below", actual.slope_below);
        show("ppm_left", actual.ppm_left);
        show("ppm_right", actual.ppm_right);
        show("cfl_nan_minimum", actual.cfl_nan_minimum);
        show("cfl_infinite_minimum", actual.cfl_infinite_minimum);
        show("hll.rho", actual.hll.rho);
        show("hllc.rho", actual.hllc.rho);
        show("roe.rho", actual.roe.rho);
        show("sw.rho", actual.sw.rho);
        show("vl.rho", actual.vl.rho);
    }
    return matches;
}

int run_device_primitives()
{
    using namespace arch::cuda;
    constexpr int total = 32;
    constexpr int active = 8;
    constexpr int face = active;
    DeviceStateFixture state(total, 2);
    DeviceStateFixture flux(total, 2);
    if (!state.valid || !flux.valid) {
        std::cerr << "device state allocation: "
                  << cudaGetErrorString(cudaGetLastError()) << '\n';
        return 100;
    }
    const FluidVector left{1.0, 0.75, -0.2, 0.1, 2.80625};
    const FluidVector right{0.8, -0.2, 0.24, -0.12, 1.82};
    for (int cell = 0; cell < total; ++cell) {
        const bool is_left = cell < face;
        state.store(cell, is_left ? left : right);
        state.set_species(0, cell, is_left ? 0.4 : 0.7);
        state.set_species(1, cell, is_left ? 0.6 : 0.3);
    }
    if (!state.upload() || !flux.upload())
        return 101;

    DeviceGridView grid{};
    grid.dim = 1;
    grid.stride_y = total;
    grid.stride_z = total;
    grid.total_size = total;
    grid.total_x = total;
    grid.total_y = 1;
    grid.total_z = 1;
    grid.is = active;
    grid.ie = active + 1;
    grid.js = 0;
    grid.je = 1;
    grid.ks = 0;
    grid.ke = 1;
    grid.dx1 = 1.0;
    grid.dx2 = 1.0;
    grid.dx3 = 1.0;

    const FaceResult expected_pcm = characterize_hll_reconstruction<PCMReconstruction>();
    const FaceResult expected_muscl = characterize_hll_reconstruction<MusclReconstruction<MinMod>>();
    const FaceResult expected_ppm = characterize_hll_reconstruction<PPMReconstruction>();
    if (launch_hydro_faces<CudaPcmReconstruction, CudaHllFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !flux.download()
        || !vector_near(flux.load(face), expected_pcm.flux))
        return 102;
    if (launch_hydro_faces<CudaMusclReconstruction<MinMod>, CudaHllFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !flux.download()
        || !vector_near(flux.load(face), expected_muscl.flux))
        return 103;
    if (launch_hydro_faces<CudaPpmReconstruction, CudaHllFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !flux.download()
        || !vector_near(flux.load(face), expected_ppm.flux))
        return 104;

    double* volume = nullptr;
    double* lower_area = nullptr;
    double* upper_area = nullptr;
    if (cudaMalloc(&volume, total * sizeof(double)) != cudaSuccess
        || cudaMalloc(&lower_area, total * sizeof(double)) != cudaSuccess
        || cudaMalloc(&upper_area, total * sizeof(double)) != cudaSuccess)
        return 105;
    const std::vector<double> metric(total, 1.0);
    const std::size_t metric_bytes = total * sizeof(double);
    if (cudaMemcpy(volume, metric.data(), metric_bytes, cudaMemcpyHostToDevice) != cudaSuccess
        || cudaMemcpy(lower_area, metric.data(), metric_bytes, cudaMemcpyHostToDevice) != cudaSuccess
        || cudaMemcpy(upper_area, metric.data(), metric_bytes, cudaMemcpyHostToDevice) != cudaSuccess)
        return 106;
    grid.cell_volume = volume;
    grid.face_area_lower[0] = lower_area;
    grid.face_area_upper[0] = upper_area;

    DeviceStateFixture delta(total, 2);
    if (!delta.valid)
        return 107;
    delta.store(active, {1, 2, 3, 4, 5});
    delta.set_species(0, active, 0.5);
    delta.set_species(1, active, -1.0);
    flux.store(active, {2, 4, 6, 8, 10});
    flux.store(active + 1, {1, 1, 1, 1, 1});
    flux.set_species(0, active, 2.0);
    flux.set_species(0, active + 1, 1.0);
    flux.set_species(1, active, 4.0);
    flux.set_species(1, active + 1, 3.0);
    if (!delta.upload() || !flux.upload())
        return 108;
    FluidVector expected_delta{1, 2, 3, 4, 5};
    double expected_delta_species[2] = {0.5, -1.0};
    const double lower_species[2] = {2.0, 4.0};
    const double upper_species[2] = {1.0, 3.0};
    TimeIntegration::accumulate_cell_divergence(
        {2, 4, 6, 8, 10}, {1, 1, 1, 1, 1}, lower_species, upper_species,
        2, 1, 1.0, 1.0, 1.0, 0.5,
        expected_delta, expected_delta_species);
    if (launch_hydro_divergence(
            flux.view, delta.view, grid, 0.5, 0, nullptr) != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !delta.download()
        || !vector_near(delta.load(active), expected_delta)
        || delta.species(0, active) != expected_delta_species[0]
        || delta.species(1, active) != expected_delta_species[1])
        return 109;

    DeviceStateFixture old_state(total, 2);
    DeviceStateFixture current_state(total, 2);
    DeviceStateFixture destination(total, 2);
    DeviceStateFixture stage_delta(total, 2);
    if (!old_state.valid || !current_state.valid || !destination.valid
        || !stage_delta.valid)
        return 110;
    for (int cell = 0; cell < total; ++cell) {
        old_state.store(cell, {1, 2, 3, 4, 50});
        current_state.store(cell, {2, 4, 6, 8, 100});
        destination.store(cell, {9, 8, 7, 6, 5});
        stage_delta.store(cell, {0.2, 0.2, 0.2, 0.2, 0.1});
        old_state.set_species(0, cell, 0.25);
        old_state.set_species(1, cell, 0.75);
        current_state.set_species(0, cell, 0.25);
        current_state.set_species(1, cell, 0.75);
        destination.set_species(0, cell, 0.2);
        destination.set_species(1, cell, 0.8);
        destination.enuc_rate[cell] = 17.0;
    }
    if (!old_state.upload() || !current_state.upload() || !destination.upload()
        || !stage_delta.upload())
        return 111;
    FluidVector expected_stage;
    double expected_stage_species[2];
    const double input_species[2] = {0.25, 0.75};
    const double zero_species[2] = {0.0, 0.0};
    TimeIntegration::update_stage_cell(
        {1, 2, 3, 4, 50}, {2, 4, 6, 8, 100}, {0.2, 0.2, 0.2, 0.2, 0.1},
        input_species, input_species, zero_species, 2, 1,
        0.5, 0.5, 1e-12, 1e20, expected_stage, expected_stage_species);
    if (launch_hydro_single_stage_update(
            old_state.view, current_state.view, destination.view,
            stage_delta.view, grid, 0.5, 0.5, 1e-12, 1e20, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !destination.download()
        || !vector_near(destination.load(active), expected_stage)
        || destination.species(0, active) != expected_stage_species[0]
        || destination.species(1, active) != expected_stage_species[1]
        || destination.enuc_rate[active] != 17.0
        || !vector_near(destination.load(active - 1), {9, 8, 7, 6, 5})
        || destination.species(0, active - 1) != 0.2)
        return 112;

    state.store(active, {2.0, 2.0, 2.5, 2.0, 10.0});
    if (!state.upload())
        return 113;
    double* candidates = nullptr;
    double* result = nullptr;
    if (cudaMalloc(&candidates, sizeof(double)) != cudaSuccess
        || cudaMalloc(&result, sizeof(double)) != cudaSuccess)
        return 114;
    CudaHydroWorkspaceView workspace{};
    workspace.flux = flux.view;
    workspace.delta = delta.view;
    workspace.cfl_candidates = candidates;
    workspace.cfl_result = result;
    const double composition[2] = {
        state.species(0, active), state.species(1, active)};
    const double expected_dt = finalize_cfl_dt(
        0.8, evaluate_cfl_cell_dt(
                 state.load(active), composition, TestIdealGas{},
                 1, 1.0, 1.0, 1.0));
    double result_host = 0.0;
    if (launch_compute_hydro_dt(
            state.view, grid, TestIdealGas{}, 0.8, workspace, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || cudaMemcpy(&result_host, result, sizeof(double), cudaMemcpyDeviceToHost)
               != cudaSuccess
        || std::abs(result_host - expected_dt) > 3e-14 * std::abs(expected_dt))
        return 115;

    cudaFree(candidates);
    cudaFree(result);
    cudaFree(volume);
    cudaFree(lower_area);
    cudaFree(upper_area);
    return 0;
}
#endif
} // namespace

int main()
{
    if (!validate_characterized_host_paths())
        return 1;
    print_double("limiter.minmod.negzero", MinMod::calc(-0.0));
    print_double("limiter.minmod.half", MinMod::calc(0.5));
    print_double("limiter.superbee.half", SuperBee::calc(0.5));
    print_double("limiter.vanleer.inf", VanLeer::calc(std::numeric_limits<double>::infinity()));
    print_double("limiter.mc.nan", McLimiter::calc(std::numeric_limits<double>::quiet_NaN()));
    print_double("slope.below", compute_limited_slope<MinMod>(0.0, 1.0, 1.0 + 0.5e-12));
    print_double("slope.at", compute_limited_slope<MinMod>(0.0, 1.0, 1.0 + 1.0e-12));
    print_double("slope.nan", compute_limited_slope<MinMod>(0.0, 1.0, std::numeric_limits<double>::quiet_NaN()));
    print_double("slope.inf", compute_limited_slope<MinMod>(0.0, 1.0, std::numeric_limits<double>::infinity()));

    const FluidVector state{2.0, 6.0, 8.0, 10.0, 100.0};
    print_vector("fluid.sum", state + FluidVector{1.0, 2.0, 3.0, 4.0, 5.0});
    print_vector("flux.finite", get_flux(state, 7.0, 0));
    print_vector("flux.negzero_rho", get_flux({-0.0, 1.0, 2.0, 3.0, 4.0}, 7.0, 0));
    print_vector("flux.inf_pressure", get_flux(state, std::numeric_limits<double>::infinity(), 0));
    print_double("entropy.zero", entropy_fix(-0.0, 1.0));
    print_double("entropy.smooth", entropy_fix(0.5, 1.0));

    const auto pcm = PCMReconstruction::apply(
        FluidVector{1, 2, 3, 4, 5}, FluidVector{6, 7, 8, 9, 10});
    print_vector("pcm.left", pcm.first);
    print_vector("pcm.right", pcm.second);
    const auto muscl = MusclReconstruction<MinMod>::apply(
        {0.5, -2.0, 2.0, -1.0, 4.0}, {1.0, -1.0, 3.0, 0.0, 5.0},
        {3.0, 2.0, 4.0, 1.0, 9.0}, {6.0, 4.0, 8.0, 3.0, 12.0});
    print_vector("muscl.left", muscl.first);
    print_vector("muscl.right", muscl.second);
    const auto ppm = PPMReconstruction::apply(
        {1, 0, 0, 0, 2}, {1, 0, 0, 0, 2}, {1, 0, 0, 0, 2},
        {10, 0, 0, 0, 20}, {10, 0, 0, 0, 20}, {10, 0, 0, 0, 20});
    print_vector("ppm.left", ppm.first);
    print_vector("ppm.right", ppm.second);

    const FaceResult hll = characterize_face<FluxHLL>(0.0);
    const FaceResult hllc = characterize_face<FluxHLLC>(0.0);
    const FaceResult roe = characterize_face<FluxRoe>(0.1);
    const FaceResult sw = characterize_face<FluxSW>(0.1);
    const FaceResult vl = characterize_face<FluxVL>(0.0);
    print_vector("face.hll", hll.flux);
    print_vector("face.hllc", hllc.flux);
    print_vector("face.roe", roe.flux);
    print_vector("face.sw", sw.flux);
    print_vector("face.vl", vl.flux);
    print_double("face.hll.spec0", hll.species[0]);
    print_double("face.hll.spec1", hll.species[1]);
    print_double("face.hllc.spec0", hllc.species[0]);
    print_double("face.hllc.spec1", hllc.species[1]);
    print_double("face.roe.spec0", roe.species[0]);
    print_double("face.roe.spec1", roe.species[1]);
    print_double("face.sw.spec0", sw.species[0]);
    print_double("face.sw.spec1", sw.species[1]);
    print_double("face.vl.spec0", vl.species[0]);
    print_double("face.vl.spec1", vl.species[1]);

    Grid grid(3, 0.0, 16.0);
    grid.dim = 1;
    grid.InitializeTopology();
    const int total = grid.GetTotalSize();
    FluidState old_state, current_state, destination;
    for (FluidState* value : {&old_state, &current_state, &destination}) {
        value->Preallocate(total);
        value->InitSpecies(2);
    }
    std::vector<FluidVector> delta(total, FluidVector{0.2, 0.2, 0.2, 0.2, 0.1});
    std::vector<double> species_delta(2 * total, 0.0);
    for (int cell = 0; cell < total; ++cell) {
        old_state.set(cell, {1, 2, 3, 4, 50});
        current_state.set(cell, {2, 4, 6, 8, 100});
        old_state.X(0, cell) = current_state.X(0, cell) = 0.25;
        old_state.X(1, cell) = current_state.X(1, cell) = 0.75;
        destination.enuc_rate[cell] = 17.0;
    }
    TimeIntegration::perform_stage_update(
        old_state, current_state, destination, delta, species_delta, grid,
        0.5, 0.5, 1e-12, 1e20);
    print_vector("stage.active", destination.get(grid.Is()));
    print_double("stage.spec0", destination.X(0, grid.Is()));
    print_double("stage.spec1", destination.X(1, grid.Is()));
    print_double("stage.diagnostic", destination.enuc_rate[grid.Is()]);
    print_double("stage.inactive_rho", destination.rho[0]);

    FluidState cfl_state;
    cfl_state.Preallocate(total);
    cfl_state.InitSpecies(0);
    for (int cell = 0; cell < total; ++cell)
        cfl_state.set(cell, {2.0, 2.0, 2.5, 2.0, 10.0});
    print_double("cfl.1d", adaptive_dt(cfl_state, TestIdealGas{}, grid, 0.8));
#if defined(__CUDACC__)
    DeviceLeafResult* device_result = nullptr;
    if (cudaMalloc(&device_result, sizeof(DeviceLeafResult)) != cudaSuccess)
        return 90;
    evaluate_device_leaves_kernel<<<1, 1>>>(device_result);
    DeviceLeafResult copied_result{};
    const cudaError_t launch_error = cudaGetLastError();
    const cudaError_t sync_error = cudaDeviceSynchronize();
    const cudaError_t copy_error = cudaMemcpy(
        &copied_result, device_result, sizeof(DeviceLeafResult),
        cudaMemcpyDeviceToHost);
    cudaFree(device_result);
    if (launch_error != cudaSuccess || sync_error != cudaSuccess
        || copy_error != cudaSuccess) {
        std::cerr << "leaf launch=" << cudaGetErrorString(launch_error)
                  << " sync=" << cudaGetErrorString(sync_error)
                  << " copy=" << cudaGetErrorString(copy_error) << '\n';
    }
    if (launch_error != cudaSuccess || sync_error != cudaSuccess
        || copy_error != cudaSuccess || !device_leaf_result_matches(copied_result))
        return 91;
    const int primitive_result = run_device_primitives();
    if (primitive_result != 0)
        return primitive_result;
#endif
    return 0;
}
