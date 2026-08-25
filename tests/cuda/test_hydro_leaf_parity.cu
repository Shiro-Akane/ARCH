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
#include "driver/dispatch/PolicyDescriptor.h"
#include "numerics/flux/FluxFunctions.h"
#include "numerics/flux/FluxHLL.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/FluxRoe.h"
#include "numerics/flux/FluxSW.h"
#include "numerics/flux/FluxVL.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "numerics/reconstruction/Reconstruction.h"

#if defined(__CUDACC__)
#include "cuda/hydro/HydroFluxPolicies.cuh"
#include "cuda/hydro/HydroReconstructionPolicies.cuh"
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

struct RouteFingerprint
{
    int limiter_id;
    int flux_id;
    int stage_id;
    std::uint64_t limiter_hash;
    std::uint64_t reconstruction_hash;
    std::uint64_t flux_hash;
    std::uint64_t stage_hash;
};

ARCH_INLINE std::uint64_t route_mix(std::uint64_t hash, double value)
{
    hash ^= std::bit_cast<std::uint64_t>(value);
    return hash * 1099511628211ULL;
}

ARCH_INLINE std::uint64_t route_mix_vector(
    std::uint64_t hash, const FluidVector& value)
{
    hash = route_mix(hash, value.rho);
    hash = route_mix(hash, value.mom_u);
    hash = route_mix(hash, value.mom_v);
    hash = route_mix(hash, value.mom_w);
    return route_mix(hash, value.eng);
}

template<class Binding> struct LimiterFor;
template<> struct LimiterFor<arch::dispatch::CudaMinModBinding>
{ using type = MinMod; };
template<> struct LimiterFor<arch::dispatch::CudaMcBinding>
{ using type = McLimiter; };
template<> struct LimiterFor<arch::dispatch::CudaSuperBeeBinding>
{ using type = SuperBee; };
template<> struct LimiterFor<arch::dispatch::CudaVanLeerLimiterBinding>
{ using type = VanLeer; };

template<class Binding> struct FluxFor;
template<> struct FluxFor<arch::dispatch::CudaVlBinding>
{ using type = arch::cuda::CudaVlFlux; static constexpr double coefficient = 0.0; };
template<> struct FluxFor<arch::dispatch::CudaSwBinding>
{ using type = arch::cuda::CudaSwFlux; static constexpr double coefficient = 0.1; };
template<> struct FluxFor<arch::dispatch::CudaRoeBinding>
{ using type = arch::cuda::CudaRoeFlux; static constexpr double coefficient = 0.1; };
template<> struct FluxFor<arch::dispatch::CudaHllBinding>
{ using type = arch::cuda::CudaHllFlux; static constexpr double coefficient = 0.0; };
template<> struct FluxFor<arch::dispatch::CudaHllcBinding>
{ using type = arch::cuda::CudaHllcFlux; static constexpr double coefficient = 0.0; };

template<class Binding> struct StageFor;
template<> struct StageFor<arch::dispatch::CudaEulerBinding>
{ static constexpr double old_weight = 0.0; static constexpr double flux_weight = 1.0; };
template<> struct StageFor<arch::dispatch::CudaRk2Binding>
{ static constexpr double old_weight = 0.5; static constexpr double flux_weight = 0.5; };
template<> struct StageFor<arch::dispatch::CudaRk3Binding>
{ static constexpr double old_weight = 0.75; static constexpr double flux_weight = 0.25; };

template<class LimiterRegistration, class FluxRegistration,
         class StageRegistration>
__global__ void route_matrix_kernel(RouteFingerprint* output, int index)
{
    using LimiterBinding = typename arch::dispatch::PolicyRegistration<
        LimiterRegistration>::CudaBinding;
    using FluxBinding = typename arch::dispatch::PolicyRegistration<
        FluxRegistration>::CudaBinding;
    using StageBinding = typename arch::dispatch::PolicyRegistration<
        StageRegistration>::CudaBinding;
    using Limiter = typename LimiterFor<LimiterBinding>::type;
    using Flux = typename FluxFor<FluxBinding>::type;

    double rho[8], mom_u[8], mom_v[8], mom_w[8], eng[8], enuc[8];
    for (int i = 0; i < 8; ++i) {
        const double x = static_cast<double>(i - 3);
        rho[i] = 1.2 + 0.06 * x + 0.013 * x * x;
        mom_u[i] = 0.31 + 0.047 * x - 0.009 * x * x;
        mom_v[i] = -0.08 + 0.019 * x + 0.004 * x * x;
        mom_w[i] = 0.04 - 0.011 * x + 0.002 * x * x;
        eng[i] = 3.7 + 0.21 * x + 0.027 * x * x;
        enuc[i] = 0.0;
    }
    arch::cuda::DeviceStateView state{
        rho, mom_u, mom_v, mom_w, eng, enuc, nullptr, 8, 0};
    FluidVector left{}, right{};
    double species_left[1]{}, species_right[1]{}, species_cell[1]{};
    arch::cuda::CudaMusclReconstruction<Limiter>::reconstruct(
        state, 3, 1, TestIdealGas{}, left, right,
        species_left, species_right, species_cell);
    FluidVector flux{};
    double species_flux[1]{};
    Flux::compute(
        left, right, nullptr, nullptr, 0, TestIdealGas{}, 0,
        FluxFor<FluxBinding>::coefficient, flux, species_flux);
    FluidVector stage{};
    TimeIntegration::update_stage_cell(
        left, right, flux, nullptr, nullptr, nullptr, 0, 1,
        StageFor<StageBinding>::old_weight,
        StageFor<StageBinding>::flux_weight,
        1.0e-12, 1.0e20, stage, nullptr);

    std::uint64_t limiter_hash = 1469598103934665603ULL;
    limiter_hash = route_mix(limiter_hash, Limiter::calc(0.21));
    limiter_hash = route_mix(limiter_hash, Limiter::calc(0.57));
    limiter_hash = route_mix(limiter_hash, Limiter::calc(1.43));
    limiter_hash = route_mix(limiter_hash, Limiter::calc(-0.37));
    std::uint64_t reconstruction_hash = 1469598103934665603ULL;
    reconstruction_hash = route_mix_vector(reconstruction_hash, left);
    reconstruction_hash = route_mix_vector(reconstruction_hash, right);
    std::uint64_t flux_hash = route_mix_vector(
        1469598103934665603ULL, flux);
    std::uint64_t stage_hash = route_mix_vector(
        1469598103934665603ULL, stage);
    output[index] = {
        static_cast<int>(arch::dispatch::PolicyRegistration<
            LimiterRegistration>::id),
        static_cast<int>(arch::dispatch::PolicyRegistration<
            FluxRegistration>::id),
        static_cast<int>(arch::dispatch::PolicyRegistration<
            StageRegistration>::id),
        limiter_hash, reconstruction_hash, flux_hash, stage_hash};
}

template<class LimiterRegistration, class FluxRegistration, class List>
struct StageRouteLauncher;

template<class LimiterRegistration, class FluxRegistration, class Id,
         arch::dispatch::UnknownPolicyBehavior Behavior, class... Stages>
struct StageRouteLauncher<
    LimiterRegistration, FluxRegistration,
    arch::dispatch::TypeList<Id, Behavior, Stages...>>
{
    static void launch(RouteFingerprint* output, int& index)
    {
        (launch_one<Stages>(output, index), ...);
    }

    template<class StageRegistration>
    static void launch_one(RouteFingerprint* output, int& index)
    {
        route_matrix_kernel<LimiterRegistration, FluxRegistration,
                            StageRegistration><<<1, 1>>>(output, index++);
    }
};

template<class LimiterRegistration, class FluxList, class StageList>
struct FluxRouteLauncher;

template<class LimiterRegistration, class Id,
         arch::dispatch::UnknownPolicyBehavior Behavior, class... Fluxes,
         class StageList>
struct FluxRouteLauncher<
    LimiterRegistration,
    arch::dispatch::TypeList<Id, Behavior, Fluxes...>, StageList>
{
    static void launch(RouteFingerprint* output, int& index)
    {
        (StageRouteLauncher<LimiterRegistration, Fluxes, StageList>::launch(
             output, index), ...);
    }
};

template<class LimiterList, class FluxList, class StageList>
struct LimiterRouteLauncher;

template<class Id, arch::dispatch::UnknownPolicyBehavior Behavior,
         class... Limiters, class FluxList, class StageList>
struct LimiterRouteLauncher<
    arch::dispatch::TypeList<Id, Behavior, Limiters...>, FluxList, StageList>
{
    static void launch(RouteFingerprint* output, int& index)
    {
        (FluxRouteLauncher<Limiters, FluxList, StageList>::launch(
             output, index), ...);
    }
};

int run_route_matrix()
{
    constexpr int route_count = 60;
    RouteFingerprint* device_routes = nullptr;
    if (cudaMalloc(&device_routes, route_count * sizeof(RouteFingerprint))
        != cudaSuccess)
        return 180;
    int launched = 0;
    LimiterRouteLauncher<
        arch::dispatch::LimiterPolicies,
        arch::dispatch::FluxPolicies,
        arch::dispatch::TimeIntegratorPolicies>::launch(
            device_routes, launched);
    RouteFingerprint actual[route_count]{};
    const cudaError_t launch_error = cudaGetLastError();
    const cudaError_t sync_error = cudaDeviceSynchronize();
    const cudaError_t copy_error = cudaMemcpy(
        actual, device_routes, sizeof(actual), cudaMemcpyDeviceToHost);
    cudaFree(device_routes);
    if (launched != route_count || launch_error != cudaSuccess
        || sync_error != cudaSuccess || copy_error != cudaSuccess)
        return 181;

    static constexpr RouteFingerprint expected[route_count] = {
        {0,0,0,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xfd3f95c86181ce2aULL,0xd523883fde34fa8fULL},
        {0,0,1,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xfd3f95c86181ce2aULL,0x358b4526db819e36ULL},
        {0,0,2,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xfd3f95c86181ce2aULL,0x82900bc8432a2dbcULL},
        {0,1,0,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0x0740227b01c83d02ULL,0x3fcfa75ddf0cf1e1ULL},
        {0,1,1,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0x0740227b01c83d02ULL,0x5f14fdf013729b2cULL},
        {0,1,2,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0x0740227b01c83d02ULL,0xc7fa390a742e6ffbULL},
        {0,2,0,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xc6d550b25d02dd3fULL,0x5e5fdaef836ecb1bULL},
        {0,2,1,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xc6d550b25d02dd3fULL,0x135546d57009e587ULL},
        {0,2,2,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xc6d550b25d02dd3fULL,0xdad3c1dd7391944aULL},
        {0,3,0,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xe898926667814272ULL,0x56cd04c53be89160ULL},
        {0,3,1,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xe898926667814272ULL,0x7325855582b9a21eULL},
        {0,3,2,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xe898926667814272ULL,0x187e44beb5fb707cULL},
        {0,4,0,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xbe49c723491c31b7ULL,0x56c1215a51759a51ULL},
        {0,4,1,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xbe49c723491c31b7ULL,0x13da535acfb3229fULL},
        {0,4,2,0x8deed829162d1fe9ULL,0x3fc0ee771b5ae663ULL,0xbe49c723491c31b7ULL,0xa4bf7a7453c7024eULL},
        {1,0,0,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x99fb766e16105410ULL,0x8bc3289e2f0bc42cULL},
        {1,0,1,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x99fb766e16105410ULL,0xc3b3e11920fea9f1ULL},
        {1,0,2,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x99fb766e16105410ULL,0x246ec7bced010b67ULL},
        {1,1,0,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0xf2125c86da445969ULL,0x3d8a2a7c8bad66adULL},
        {1,1,1,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0xf2125c86da445969ULL,0xf0de9eb7d3b61460ULL},
        {1,1,2,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0xf2125c86da445969ULL,0xbe3128b9e5824224ULL},
        {1,2,0,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x6c672b50dc859f26ULL,0x8bc3289e2f0bc42cULL},
        {1,2,1,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x6c672b50dc859f26ULL,0xc3b3e11920fea9f1ULL},
        {1,2,2,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x6c672b50dc859f26ULL,0x246ec7bced010b67ULL},
        {1,3,0,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x40a62050d57c4545ULL,0x8bc3289e2f0bc42cULL},
        {1,3,1,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x40a62050d57c4545ULL,0xc3b3e11920fea9f1ULL},
        {1,3,2,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x40a62050d57c4545ULL,0x246ec7bced010b67ULL},
        {1,4,0,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x430c8b2c041555bcULL,0xb2585198c4675447ULL},
        {1,4,1,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x430c8b2c041555bcULL,0xc3bae91920ead9c6ULL},
        {1,4,2,0x9ac8d0702920dbf8ULL,0x17202ff85ac37066ULL,0x430c8b2c041555bcULL,0x1deec1601712f616ULL},
        {2,0,0,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xbd6b1a9f6dea19daULL,0xba9d63039da904acULL},
        {2,0,1,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xbd6b1a9f6dea19daULL,0xc2589ad43244e9e5ULL},
        {2,0,2,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xbd6b1a9f6dea19daULL,0x6b70bac2defd9053ULL},
        {2,1,0,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xdf83a88e1daebfb3ULL,0xd5c06b224dd3811cULL},
        {2,1,1,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xdf83a88e1daebfb3ULL,0x43bb7bcf3c6b322bULL},
        {2,1,2,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xdf83a88e1daebfb3ULL,0x9524d5d911db8348ULL},
        {2,2,0,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0x2870f9491b685703ULL,0x85839b2c65e824faULL},
        {2,2,1,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0x2870f9491b685703ULL,0x6db619eca5989eaaULL},
        {2,2,2,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0x2870f9491b685703ULL,0x08d012d18a358d2fULL},
        {2,3,0,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xb5909a8167862696ULL,0x4acbb059ccc871baULL},
        {2,3,1,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xb5909a8167862696ULL,0x32d2f5465a635108ULL},
        {2,3,2,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0xb5909a8167862696ULL,0x853ce326b6473a6bULL},
        {2,4,0,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0x5edb455bdf234a11ULL,0x698621eadf5452b2ULL},
        {2,4,1,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0x5edb455bdf234a11ULL,0x3a42485057b9f28fULL},
        {2,4,2,0x9f23f95b5828b24bULL,0xb35396756ee2e8e5ULL,0x5edb455bdf234a11ULL,0x18c1b3d7c61b7f03ULL},
        {3,0,0,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xec7eb00354d4d02aULL,0x26c4ff1e44ea80c0ULL},
        {3,0,1,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xec7eb00354d4d02aULL,0x8e701157a4a87f1cULL},
        {3,0,2,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xec7eb00354d4d02aULL,0x8082f99d0673f6faULL},
        {3,1,0,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xfe73cef99dd2a6b5ULL,0x75c849d7e399391dULL},
        {3,1,1,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xfe73cef99dd2a6b5ULL,0x5a5eae2ec894c085ULL},
        {3,1,2,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xfe73cef99dd2a6b5ULL,0x33fa1324370c351fULL},
        {3,2,0,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0x217cdc1c0c4814bbULL,0x85d7c92b983faa30ULL},
        {3,2,1,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0x217cdc1c0c4814bbULL,0x9b15d3cae5331d5aULL},
        {3,2,2,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0x217cdc1c0c4814bbULL,0xcb535dbfeb3ed5acULL},
        {3,3,0,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xcbec74d625af9f93ULL,0x585c05c73464367eULL},
        {3,3,1,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xcbec74d625af9f93ULL,0x74201af9e9c33472ULL},
        {3,3,2,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0xcbec74d625af9f93ULL,0xcf840140f8de5067ULL},
        {3,4,0,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0x60ff4a25b463c33eULL,0xb1b61706d1a38d47ULL},
        {3,4,1,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0x60ff4a25b463c33eULL,0x2c3cf514e69b5f79ULL},
        {3,4,2,0x37c37dc36e6aee60ULL,0xd1c57ff9caf479fcULL,0x60ff4a25b463c33eULL,0x3b92f3335189a734ULL}
    };
    bool matches = true;
    for (int i = 0; i < route_count; ++i) {
        const RouteFingerprint& a = actual[i];
        const RouteFingerprint& e = expected[i];
        const bool route_matches = a.limiter_id == e.limiter_id
            && a.flux_id == e.flux_id && a.stage_id == e.stage_id
            && a.limiter_hash == e.limiter_hash
            && a.reconstruction_hash == e.reconstruction_hash
            && a.flux_hash == e.flux_hash && a.stage_hash == e.stage_hash;
        if (!route_matches) {
            matches = false;
        }
        if (!route_matches) {
            std::cerr << "route fingerprint mismatch index=" << i
                      << " ids=" << a.limiter_id << ',' << a.flux_id
                      << ',' << a.stage_id << '\n';
        }
    }
    if (matches)
        std::cout << "D2_ROUTE_MATRIX_PASS routes=" << route_count << '\n';
    return matches ? 0 : 182;
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

double frozen_double(std::uint64_t bits)
{
    return std::bit_cast<double>(bits);
}

FluidVector frozen_vector(
    std::uint64_t rho, std::uint64_t mom_u, std::uint64_t mom_v,
    std::uint64_t mom_w, std::uint64_t eng)
{
    return {frozen_double(rho), frozen_double(mom_u), frozen_double(mom_v),
            frozen_double(mom_w), frozen_double(eng)};
}

bool scalar_near(double actual, double expected)
{
    return std::abs(actual - expected)
        <= 3e-14 * std::max(1.0, std::abs(expected));
}

FaceResult frozen_face_result(
    std::uint64_t rho, std::uint64_t mom_u, std::uint64_t mom_v,
    std::uint64_t mom_w, std::uint64_t eng,
    std::uint64_t species0, std::uint64_t species1)
{
    return {frozen_vector(rho, mom_u, mom_v, mom_w, eng),
            {frozen_double(species0), frozen_double(species1)}};
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

arch::cuda::DeviceGridView make_three_dimensional_validation_grid(
    const arch::cuda::DeviceGridView& source)
{
    arch::cuda::DeviceGridView grid = source;
    grid.dim = 3;
    grid.ng = 3;
    grid.stride_y = 8;
    grid.stride_z = 64;
    grid.total_size = 512;
    grid.total_x = 8;
    grid.total_y = 8;
    grid.total_z = 8;
    grid.is = 3;
    grid.ie = 4;
    grid.js = 3;
    grid.je = 4;
    grid.ks = 3;
    grid.ke = 4;
    return grid;
}

arch::cuda::DeviceGridView with_directional_edge(
    arch::cuda::DeviceGridView grid, int direction, bool upper_edge)
{
    int* begin[3] = {&grid.is, &grid.js, &grid.ks};
    int* end[3] = {&grid.ie, &grid.je, &grid.ke};
    const int extent[3] = {grid.total_x, grid.total_y, grid.total_z};
    if (upper_edge) {
        *begin[direction] = extent[direction] - 1;
        *end[direction] = extent[direction];
    }
    else {
        *begin[direction] = 0;
        *end[direction] = 1;
    }
    return grid;
}

arch::cuda::DeviceGridView with_directional_margin(
    arch::cuda::DeviceGridView grid, int direction,
    bool upper_margin, int margin)
{
    int* begin[3] = {&grid.is, &grid.js, &grid.ks};
    int* end[3] = {&grid.ie, &grid.je, &grid.ke};
    const int extent[3] = {grid.total_x, grid.total_y, grid.total_z};
    if (upper_margin)
        *end[direction] = extent[direction] - margin;
    else
        *begin[direction] = margin;
    return grid;
}

template <typename Reconstruction>
bool rejects_all_directional_face_edges(
    arch::cuda::DeviceStateView state, arch::cuda::DeviceStateView flux,
    arch::cuda::DeviceGridView grid)
{
    for (int direction = 0; direction < 3; ++direction) {
        if (arch::cuda::launch_hydro_faces<
                Reconstruction, arch::cuda::CudaHllFlux>(
                state, flux,
                with_directional_edge(grid, direction, true),
                TestIdealGas{}, direction, 0.0, nullptr)
            != cudaErrorInvalidValue)
            return false;
        if constexpr (Reconstruction::ghost_depth > 1) {
            constexpr int insufficient_margin =
                Reconstruction::ghost_depth - 1;
            if (arch::cuda::launch_hydro_faces<
                    Reconstruction, arch::cuda::CudaHllFlux>(
                    state, flux,
                    with_directional_margin(
                        grid, direction, true, insufficient_margin),
                    TestIdealGas{}, direction, 0.0, nullptr)
                != cudaErrorInvalidValue)
                return false;
            if (arch::cuda::launch_hydro_faces<
                    Reconstruction, arch::cuda::CudaHllFlux>(
                    state, flux,
                    with_directional_margin(
                        grid, direction, false, insufficient_margin),
                    TestIdealGas{}, direction, 0.0, nullptr)
                != cudaErrorInvalidValue)
                return false;
        }
        if (arch::cuda::launch_hydro_faces<
                Reconstruction, arch::cuda::CudaHllFlux>(
                state, flux,
                with_directional_edge(grid, direction, false),
                TestIdealGas{}, direction, 0.0, nullptr)
            != cudaErrorInvalidValue)
            return false;
    }
    return true;
}

bool rejects_all_directional_divergence_upper_edges(
    arch::cuda::DeviceStateView flux, arch::cuda::DeviceStateView delta,
    arch::cuda::DeviceGridView grid)
{
    for (int direction = 0; direction < 3; ++direction) {
        if (arch::cuda::launch_hydro_divergence(
                flux, delta, with_directional_edge(grid, direction, true),
                0.5, direction, nullptr)
            != cudaErrorInvalidValue)
            return false;
    }
    return true;
}

bool device_leaf_result_matches(const DeviceLeafResult& actual)
{
    const FluidVector hll = frozen_vector(
        0x3fdfe0fa91402e76ULL, 0x3ffc59d9bcfbe631ULL,
        0xbfd6c55f025c1539ULL, 0x3fc6c55f025c1539ULL,
        0x40008c51d5581401ULL);
    const FluidVector hllc = frozen_vector(
        0x3fe03e732de72443ULL, 0x3ffc695d1bdcbc59ULL,
        0xbfb9fd85163ea06cULL, 0x3fa9fd85163ea06cULL,
        0x4000b6f9c9e9bf2cULL);
    const FluidVector roe = frozen_vector(
        0x3fe0468a0283dcb1ULL, 0x3ffc65ce3bd35266ULL,
        0xbfc4507df7b36b44ULL, 0x3fb4507df7b36b44ULL,
        0x4000bdaf3bd85012ULL);
    const FluidVector sw = frozen_vector(
        0x3fdd6f579bdceca1ULL, 0x4000492c964085f2ULL,
        0xbfd41edd0ad3fc16ULL, 0x3fc41edd0ad3fc16ULL,
        0x3fff91085fc8c1d9ULL);
    const FluidVector vl = frozen_vector(
        0x3fdd3f7f1d3b7d98ULL, 0x40006dfb578715e0ULL,
        0xbfd07e985324a906ULL, 0x3fc07e985324a906ULL,
        0x3ffff6d62a7013d3ULL);
    const std::uint64_t species_bits[10] = {
        0x3fc980c87433585fULL, 0x3fd3209657268247ULL,
        0x3fc9fd85163ea06cULL, 0x3fd37e23d0aef850ULL,
        0x3fca0a766a6c944fULL, 0x3fd387d8cfd16f3bULL,
        0x3fa9dee10cbc3d20ULL, 0x3fda337b7a4564feULL,
        0x3fb53fc3c72dfef2ULL, 0x3fd7ef8e2b6ffddcULL};
    bool species_match = true;
    for (int species = 0; species < 10; ++species)
        species_match = species_match
            && scalar_near(actual.species[species],
                           frozen_double(species_bits[species]));
    const bool matches = vector_near(actual.hll, hll)
        && vector_near(actual.hllc, hllc)
        && vector_near(actual.roe, roe)
        && vector_near(actual.sw, sw)
        && vector_near(actual.vl, vl)
        && vector_bits(actual.pcm_left,
                       0x3ff0000000000000ULL, 0x3fe8000000000000ULL,
                       0xbfc999999999999aULL, 0x3fb999999999999aULL,
                       0x4006733333333333ULL)
        && vector_bits(actual.pcm_right,
                       0x3fe999999999999aULL, 0xbfc999999999999aULL,
                       0x3fceb851eb851eb8ULL, 0xbfbeb851eb851eb8ULL,
                       0x3ffd1eb851eb851fULL)
        && vector_bits(actual.muscl_left,
                       0x3ff4000000000000ULL, 0xbfe0000000000000ULL,
                       0x400c000000000000ULL, 0x3fe0000000000000ULL,
                       0x4016000000000000ULL)
        && vector_bits(actual.muscl_right,
                       0x4000000000000000ULL, 0x3ff0000000000000ULL,
                       0x400c000000000000ULL, 0x3fe0000000000000ULL,
                       0x401e000000000000ULL)
        && exact_bits(actual.limiter, 0x3fe0000000000000ULL)
        && exact_bits(actual.limiter_negzero, 0x0000000000000000ULL)
        && exact_bits(actual.slope, 0x3fe0000000000000ULL)
        && exact_bits(actual.slope_below, 0x0000000000000000ULL)
        && std::abs(actual.ppm_left - 5.0 / 3.0)
               <= 2e-15 * std::max(1.0, std::abs(5.0 / 3.0))
        && std::abs(actual.ppm_right - 25.0 / 3.0)
               <= 2e-15 * std::max(1.0, std::abs(25.0 / 3.0))
        && actual.cfl_nan_minimum == 3.0
        && actual.cfl_infinite_minimum == 3.0
        && species_match;
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
        for (int species = 0; species < 10; ++species)
            show("species", actual.species[species]);
    }
    return matches;
}

int run_device_primitives()
{
    using namespace arch::cuda;
    if (detail::hydro_launch_blocks(
            std::numeric_limits<int>::max(), 128) != 16777216)
        return 178;
    constexpr int total = 512;
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
    grid.ng = 3;
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

    const FaceResult expected_hll = frozen_face_result(
        0x3fdfe0fa91402e76ULL, 0x3ffc59d9bcfbe631ULL,
        0xbfd6c55f025c1539ULL, 0x3fc6c55f025c1539ULL,
        0x40008c51d5581401ULL, 0x3fc980c87433585fULL,
        0x3fd3209657268247ULL);
    const FaceResult expected_hllc = frozen_face_result(
        0x3fe03e732de72443ULL, 0x3ffc695d1bdcbc59ULL,
        0xbfb9fd85163ea06cULL, 0x3fa9fd85163ea06cULL,
        0x4000b6f9c9e9bf2cULL, 0x3fc9fd85163ea06cULL,
        0x3fd37e23d0aef850ULL);
    const FaceResult expected_roe = frozen_face_result(
        0x3fe0468a0283dcb1ULL, 0x3ffc65ce3bd35266ULL,
        0xbfc4507df7b36b44ULL, 0x3fb4507df7b36b44ULL,
        0x4000bdaf3bd85012ULL, 0x3fca0a766a6c944fULL,
        0x3fd387d8cfd16f3bULL);
    const FaceResult expected_sw = frozen_face_result(
        0x3fdd6f579bdceca1ULL, 0x4000492c964085f2ULL,
        0xbfd41edd0ad3fc16ULL, 0x3fc41edd0ad3fc16ULL,
        0x3fff91085fc8c1d9ULL, 0x3fa9dee10cbc3d20ULL,
        0x3fda337b7a4564feULL);
    const FaceResult expected_vl = frozen_face_result(
        0x3fdd3f7f1d3b7d98ULL, 0x40006dfb578715e0ULL,
        0xbfd07e985324a906ULL, 0x3fc07e985324a906ULL,
        0x3ffff6d62a7013d3ULL, 0x3fb53fc3c72dfef2ULL,
        0x3fd7ef8e2b6ffddcULL);
    const auto face_matches = [&](const FaceResult& expected) {
        return flux.download()
            && vector_near(flux.load(face), expected.flux)
            && scalar_near(flux.species(0, face), expected.species[0])
            && scalar_near(flux.species(1, face), expected.species[1]);
    };
    if (launch_hydro_faces<CudaPcmReconstruction, CudaHllFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !face_matches(expected_hll))
        return 102;
    if (launch_hydro_faces<CudaPcmReconstruction, CudaHllcFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !face_matches(expected_hllc))
        return 103;
    if (launch_hydro_faces<CudaPcmReconstruction, CudaRoeFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.1, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !face_matches(expected_roe))
        return 104;
    if (launch_hydro_faces<CudaPcmReconstruction, CudaSwFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.1, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !face_matches(expected_sw))
        return 105;
    if (launch_hydro_faces<CudaPcmReconstruction, CudaVlFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !face_matches(expected_vl))
        return 106;

    for (int cell = 0; cell < total; ++cell) {
        const double x = static_cast<double>(cell - (face - 1));
        state.store(cell, {1.0 + 0.05 * x, 0.2 + 0.03 * x,
                           0.1 - 0.01 * x, 0.05 + 0.02 * x,
                           3.0 + 0.1 * x});
        state.set_species(0, cell, 0.35 + 0.02 * x);
        state.set_species(1, cell, 0.65 - 0.02 * x);
    }
    if (!state.upload())
        return 107;
    const FaceResult expected_pcm = frozen_face_result(
        0x3fc72b82e6be8729ULL, 0x3ff3b19b7775ba24ULL,
        0x3f9ad40e51097a6cULL, 0xbf3a0335634151afULL,
        0x3fea454fc5825daaULL, 0x3fb0380ed4b891d0ULL,
        0x3fbe1ef6f8c47c82ULL);
    const FaceResult expected_muscl = frozen_face_result(
        0x3fcb851eb851eb86ULL, 0x3ff40ece37f720f0ULL,
        0x3f9467b2e014c79cULL, 0x3f89c65b35ff4cf9ULL,
        0x3fec9580dca0089aULL, 0x3fb3d07c84b5dcc7ULL,
        0x3fc19ce075f6fd23ULL);
    const FaceResult expected_ppm = frozen_face_result(
        0x3fcb87c6d1301341ULL, 0x3ff40f01561799ecULL,
        0x3f946652e47fa70cULL, 0x3f89d0a618abad90ULL,
        0x3fec9860c6f946b5ULL, 0x3fb3d2663037181aULL,
        0x3fc19e93b9148734ULL);
    if (launch_hydro_faces<CudaPcmReconstruction, CudaHllFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !face_matches(expected_pcm))
        return 108;
    if (launch_hydro_faces<CudaMusclReconstruction<MinMod>, CudaHllFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !face_matches(expected_muscl))
        return 109;
    if (launch_hydro_faces<CudaPpmReconstruction, CudaHllFlux>(
            state.view, flux.view, grid, TestIdealGas{}, 0, 0.0, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || !face_matches(expected_ppm))
        return 110;

    const FluidVector face_sentinel{19, 18, 17, 16, 15};
    flux.store(face, face_sentinel);
    flux.set_species(0, face, 0.125);
    flux.set_species(1, face, 0.875);
    if (!flux.upload())
        return 116;
    const std::vector<double> face_rho_sentinel = flux.rho;
    const std::vector<double> face_mom_u_sentinel = flux.mom_u;
    const std::vector<double> face_mom_v_sentinel = flux.mom_v;
    const std::vector<double> face_mom_w_sentinel = flux.mom_w;
    const std::vector<double> face_eng_sentinel = flux.eng;
    const std::vector<double> face_enuc_sentinel = flux.enuc_rate;
    const std::vector<double> face_species_sentinel = flux.mass_fractions;
    const auto pcm_face_error = [&](DeviceStateView input,
                                    DeviceStateView output,
                                    DeviceGridView candidate_grid,
                                    int direction) {
        return launch_hydro_faces<CudaPcmReconstruction, CudaHllFlux>(
                   input, output, candidate_grid, TestIdealGas{},
                   direction, 0.0, nullptr)
            == cudaErrorInvalidValue;
    };
    DeviceStateView bad_flux = flux.view;
    bad_flux.n_species = 1;
    if (!pcm_face_error(state.view, bad_flux, grid, 0)) {
        cudaDeviceSynchronize();
        return 117;
    }
    DeviceStateView bad_state = state.view;
    bad_state.n_species = -1;
    if (!pcm_face_error(bad_state, flux.view, grid, 0))
        return 118;
    bad_state = state.view;
    bad_state.n_species = kMaxDeviceSpecies + 1;
    if (!pcm_face_error(bad_state, flux.view, grid, 0))
        return 119;
    bad_flux = flux.view;
    bad_flux.n_species = -1;
    if (!pcm_face_error(state.view, bad_flux, grid, 0))
        return 120;
    bad_flux = flux.view;
    bad_flux.n_species = kMaxDeviceSpecies + 1;
    if (!pcm_face_error(state.view, bad_flux, grid, 0))
        return 121;
    bad_flux = flux.view;
    bad_flux.total_size = total - 1;
    if (!pcm_face_error(state.view, bad_flux, grid, 0))
        return 122;
    DeviceGridView bad_grid = grid;
    bad_grid.total_size = total - 1;
    if (!pcm_face_error(state.view, flux.view, bad_grid, 0))
        return 123;
    bad_state = state.view;
    bad_state.rho = nullptr;
    if (!pcm_face_error(bad_state, flux.view, grid, 0))
        return 124;
    bad_state = state.view;
    bad_state.enuc_rate = nullptr;
    if (!pcm_face_error(bad_state, flux.view, grid, 0)) {
        cudaDeviceSynchronize();
        return 124;
    }
    bad_state = state.view;
    bad_state.mass_fractions = nullptr;
    if (!pcm_face_error(bad_state, flux.view, grid, 0))
        return 125;
    bad_flux = flux.view;
    bad_flux.eng = nullptr;
    if (!pcm_face_error(state.view, bad_flux, grid, 0))
        return 126;
    bad_flux = flux.view;
    bad_flux.enuc_rate = nullptr;
    if (!pcm_face_error(state.view, bad_flux, grid, 0))
        return 126;
    bad_flux = flux.view;
    bad_flux.mass_fractions = nullptr;
    if (!pcm_face_error(state.view, bad_flux, grid, 0))
        return 127;
    if (!pcm_face_error(state.view, flux.view, grid, 1))
        return 128;
    bad_grid = grid;
    bad_grid.ng = 0;
    if (!pcm_face_error(state.view, flux.view, bad_grid, 0))
        return 129;
    bad_grid = grid;
    bad_grid.ng = 2;
    if (launch_hydro_faces<CudaPpmReconstruction, CudaHllFlux>(
            state.view, flux.view, bad_grid, TestIdealGas{},
            0, 0.0, nullptr) != cudaErrorInvalidValue)
        return 130;
    bad_grid = grid;
    bad_grid.ng = 1;
    if (launch_hydro_faces<CudaMusclReconstruction<MinMod>, CudaHllFlux>(
            state.view, flux.view, bad_grid, TestIdealGas{},
            0, 0.0, nullptr) != cudaErrorInvalidValue)
        return 130;
    const DeviceGridView three_dimensional_grid =
        make_three_dimensional_validation_grid(grid);
    if (!rejects_all_directional_face_edges<CudaPcmReconstruction>(
            state.view, flux.view, three_dimensional_grid)) {
        cudaDeviceSynchronize();
        return 172;
    }
    if (!rejects_all_directional_face_edges<
            CudaMusclReconstruction<MinMod>>(
            state.view, flux.view, three_dimensional_grid)) {
        cudaDeviceSynchronize();
        return 173;
    }
    if (!rejects_all_directional_face_edges<CudaPpmReconstruction>(
            state.view, flux.view, three_dimensional_grid)) {
        cudaDeviceSynchronize();
        return 174;
    }
    DeviceStateView overflow_state = state.view;
    DeviceStateView overflow_flux = flux.view;
    DeviceGridView overflow_grid = grid;
    const int overflowing_total = std::numeric_limits<int>::max() / 2 + 1;
    overflow_state.total_size = overflowing_total;
    overflow_flux.total_size = overflowing_total;
    overflow_grid.total_size = overflowing_total;
    overflow_grid.total_x = overflowing_total;
    overflow_grid.stride_y = overflowing_total;
    overflow_grid.stride_z = overflowing_total;
    if (launch_hydro_faces<CudaPcmReconstruction, CudaHllFlux>(
            overflow_state, overflow_flux, overflow_grid, TestIdealGas{},
            0, 0.0, nullptr) != cudaErrorInvalidValue)
        return 175;
    DeviceGridView overflowing_stride_grid = three_dimensional_grid;
    overflowing_stride_grid.total_y = std::numeric_limits<int>::max() / 4;
    if (launch_hydro_faces<CudaPcmReconstruction, CudaHllFlux>(
            state.view, flux.view, overflowing_stride_grid, TestIdealGas{},
            0, 0.0, nullptr) != cudaErrorInvalidValue)
        return 176;
    if (cudaDeviceSynchronize() != cudaSuccess || !flux.download()
        || !vector_bits(flux.load(face),
                        0x4033000000000000ULL, 0x4032000000000000ULL,
                        0x4031000000000000ULL, 0x4030000000000000ULL,
                        0x402e000000000000ULL)
        || !exact_bits(flux.species(0, face), 0x3fc0000000000000ULL)
        || !exact_bits(flux.species(1, face), 0x3fec000000000000ULL)
        || flux.rho != face_rho_sentinel
        || flux.mom_u != face_mom_u_sentinel
        || flux.mom_v != face_mom_v_sentinel
        || flux.mom_w != face_mom_w_sentinel
        || flux.eng != face_eng_sentinel
        || flux.enuc_rate != face_enuc_sentinel
        || flux.mass_fractions != face_species_sentinel)
        return 131;

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
    for (int cell = 0; cell < total; ++cell) {
        delta.store(cell, {1, 2, 3, 4, 5});
        delta.set_species(0, cell, 0.5);
        delta.set_species(1, cell, -1.0);
    }
    flux.store(active, {2, 4, 6, 8, 10});
    flux.store(active + 1, {1, 1, 1, 1, 1});
    flux.set_species(0, active, 2.0);
    flux.set_species(0, active + 1, 1.0);
    flux.set_species(1, active, 4.0);
    flux.set_species(1, active + 1, 3.0);
    if (!delta.upload() || !flux.upload())
        return 108;
    const std::vector<double> delta_rho_sentinel = delta.rho;
    const std::vector<double> delta_mom_u_sentinel = delta.mom_u;
    const std::vector<double> delta_mom_v_sentinel = delta.mom_v;
    const std::vector<double> delta_mom_w_sentinel = delta.mom_w;
    const std::vector<double> delta_eng_sentinel = delta.eng;
    const std::vector<double> delta_enuc_sentinel = delta.enuc_rate;
    const std::vector<double> delta_species_sentinel = delta.mass_fractions;
    const auto divergence_error = [&](DeviceStateView input_flux,
                                      DeviceStateView output_delta,
                                      DeviceGridView candidate_grid,
                                      int direction) {
        return launch_hydro_divergence(
                   input_flux, output_delta, candidate_grid,
                   0.5, direction, nullptr)
            == cudaErrorInvalidValue;
    };
    DeviceStateView bad_delta = delta.view;
    bad_delta.n_species = 1;
    if (!divergence_error(flux.view, bad_delta, grid, 0)) {
        cudaDeviceSynchronize();
        return 132;
    }
    bad_delta = delta.view;
    bad_delta.n_species = -1;
    if (!divergence_error(flux.view, bad_delta, grid, 0))
        return 133;
    bad_delta = delta.view;
    bad_delta.n_species = kMaxDeviceSpecies + 1;
    if (!divergence_error(flux.view, bad_delta, grid, 0))
        return 134;
    DeviceStateView bad_input_flux = flux.view;
    bad_input_flux.n_species = -1;
    if (!divergence_error(bad_input_flux, delta.view, grid, 0))
        return 135;
    bad_input_flux = flux.view;
    bad_input_flux.n_species = kMaxDeviceSpecies + 1;
    if (!divergence_error(bad_input_flux, delta.view, grid, 0))
        return 136;
    bad_delta = delta.view;
    bad_delta.total_size = total - 1;
    if (!divergence_error(flux.view, bad_delta, grid, 0))
        return 137;
    bad_grid = grid;
    bad_grid.total_size = total - 1;
    if (!divergence_error(flux.view, delta.view, bad_grid, 0))
        return 138;
    bad_input_flux = flux.view;
    bad_input_flux.rho = nullptr;
    if (!divergence_error(bad_input_flux, delta.view, grid, 0))
        return 139;
    bad_input_flux = flux.view;
    bad_input_flux.mass_fractions = nullptr;
    if (!divergence_error(bad_input_flux, delta.view, grid, 0))
        return 140;
    bad_delta = delta.view;
    bad_delta.eng = nullptr;
    if (!divergence_error(flux.view, bad_delta, grid, 0))
        return 141;
    bad_delta = delta.view;
    bad_delta.mass_fractions = nullptr;
    if (!divergence_error(flux.view, bad_delta, grid, 0))
        return 142;
    bad_grid = grid;
    bad_grid.cell_volume = nullptr;
    if (!divergence_error(flux.view, delta.view, bad_grid, 0))
        return 143;
    bad_grid = grid;
    bad_grid.face_area_lower[0] = nullptr;
    if (!divergence_error(flux.view, delta.view, bad_grid, 0))
        return 144;
    bad_grid = grid;
    bad_grid.face_area_upper[0] = nullptr;
    if (!divergence_error(flux.view, delta.view, bad_grid, 0))
        return 145;
    if (!divergence_error(flux.view, delta.view, grid, 1))
        return 146;
    DeviceGridView divergence_edge_grid =
        make_three_dimensional_validation_grid(grid);
    for (int direction = 0; direction < 3; ++direction) {
        divergence_edge_grid.face_area_lower[direction] = lower_area;
        divergence_edge_grid.face_area_upper[direction] = upper_area;
    }
    divergence_edge_grid.cell_volume = volume;
    if (!rejects_all_directional_divergence_upper_edges(
            flux.view, delta.view, divergence_edge_grid)) {
        cudaDeviceSynchronize();
        return 177;
    }
    if (cudaDeviceSynchronize() != cudaSuccess || !delta.download()
        || !vector_bits(delta.load(active),
                        0x3ff0000000000000ULL, 0x4000000000000000ULL,
                        0x4008000000000000ULL, 0x4010000000000000ULL,
                        0x4014000000000000ULL)
        || !exact_bits(delta.species(0, active), 0x3fe0000000000000ULL)
        || !exact_bits(delta.species(1, active), 0xbff0000000000000ULL)
        || delta.rho != delta_rho_sentinel
        || delta.mom_u != delta_mom_u_sentinel
        || delta.mom_v != delta_mom_v_sentinel
        || delta.mom_w != delta_mom_w_sentinel
        || delta.eng != delta_eng_sentinel
        || delta.enuc_rate != delta_enuc_sentinel
        || delta.mass_fractions != delta_species_sentinel)
        return 147;
    const FluidVector expected_delta = frozen_vector(
        0x3ff8000000000000ULL, 0x400c000000000000ULL,
        0x4016000000000000ULL, 0x401e000000000000ULL,
        0x4023000000000000ULL);
    const double expected_delta_species[2] = {1.0, -0.5};
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
    const auto stage_error = [&](DeviceStateView old_view,
                                 DeviceStateView current_view,
                                 DeviceStateView destination_view,
                                 DeviceStateView delta_view,
                                 DeviceGridView candidate_grid) {
        return launch_hydro_single_stage_update(
                   old_view, current_view, destination_view, delta_view,
                   candidate_grid, 0.5, 0.5, 1e-12, 1e20, nullptr)
            == cudaErrorInvalidValue;
    };
    DeviceStateView bad_old_state = old_state.view;
    bad_old_state.total_size = total - 1;
    if (!stage_error(bad_old_state, current_state.view, destination.view,
                     stage_delta.view, grid)) {
        cudaDeviceSynchronize();
        return 148;
    }
    DeviceStateView bad_current_state = current_state.view;
    bad_current_state.total_size = total - 1;
    if (!stage_error(old_state.view, bad_current_state, destination.view,
                     stage_delta.view, grid))
        return 149;
    DeviceStateView bad_destination = destination.view;
    bad_destination.total_size = total - 1;
    if (!stage_error(old_state.view, current_state.view, bad_destination,
                     stage_delta.view, grid))
        return 150;
    DeviceStateView bad_stage_delta = stage_delta.view;
    bad_stage_delta.total_size = total - 1;
    if (!stage_error(old_state.view, current_state.view, destination.view,
                     bad_stage_delta, grid))
        return 151;
    bad_grid = grid;
    bad_grid.total_size = total - 1;
    if (!stage_error(old_state.view, current_state.view, destination.view,
                     stage_delta.view, bad_grid))
        return 152;
    bad_old_state = old_state.view;
    bad_old_state.n_species = -1;
    if (!stage_error(bad_old_state, current_state.view, destination.view,
                     stage_delta.view, grid))
        return 153;
    bad_old_state = old_state.view;
    bad_old_state.n_species = kMaxDeviceSpecies + 1;
    if (!stage_error(bad_old_state, current_state.view, destination.view,
                     stage_delta.view, grid))
        return 154;
    bad_destination = destination.view;
    bad_destination.n_species = 1;
    if (!stage_error(old_state.view, current_state.view, bad_destination,
                     stage_delta.view, grid))
        return 155;
    bad_old_state = old_state.view;
    bad_old_state.rho = nullptr;
    if (!stage_error(bad_old_state, current_state.view, destination.view,
                     stage_delta.view, grid))
        return 156;
    bad_current_state = current_state.view;
    bad_current_state.mass_fractions = nullptr;
    if (!stage_error(old_state.view, bad_current_state, destination.view,
                     stage_delta.view, grid))
        return 157;
    bad_destination = destination.view;
    bad_destination.eng = nullptr;
    if (!stage_error(old_state.view, current_state.view, bad_destination,
                     stage_delta.view, grid))
        return 158;
    bad_stage_delta = stage_delta.view;
    bad_stage_delta.mass_fractions = nullptr;
    if (!stage_error(old_state.view, current_state.view, destination.view,
                     bad_stage_delta, grid))
        return 159;
    if (cudaDeviceSynchronize() != cudaSuccess || !destination.download()
        || !vector_bits(destination.load(active),
                        0x4022000000000000ULL, 0x4020000000000000ULL,
                        0x401c000000000000ULL, 0x4018000000000000ULL,
                        0x4014000000000000ULL)
        || !exact_bits(destination.species(0, active), 0x3fc999999999999aULL)
        || !exact_bits(destination.species(1, active), 0x3fe999999999999aULL)
        || !exact_bits(destination.enuc_rate[active], 0x4031000000000000ULL))
        return 160;
    const FluidVector expected_stage = frozen_vector(
        0x3ff999999999999aULL, 0x4008cccccccccccdULL,
        0x4012666666666666ULL, 0x4018666666666666ULL,
        0x4052c33333333333ULL);
    const double expected_stage_species[2] = {0.25, 0.75};
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
    const double cfl_sentinel = 123.0;
    if (cudaMemcpy(result, &cfl_sentinel, sizeof(double),
                   cudaMemcpyHostToDevice) != cudaSuccess)
        return 161;
    const auto cfl_error = [&](DeviceStateView input,
                               DeviceGridView candidate_grid,
                               CudaHydroWorkspaceView candidate_workspace) {
        return launch_compute_hydro_dt(
                   input, candidate_grid, TestIdealGas{}, 0.8,
                   candidate_workspace, nullptr)
            == cudaErrorInvalidValue;
    };
    bad_state = state.view;
    bad_state.total_size = total - 1;
    if (!cfl_error(bad_state, grid, workspace)) {
        cudaDeviceSynchronize();
        return 162;
    }
    bad_state = state.view;
    bad_state.n_species = -1;
    if (!cfl_error(bad_state, grid, workspace))
        return 163;
    bad_state = state.view;
    bad_state.n_species = kMaxDeviceSpecies + 1;
    if (!cfl_error(bad_state, grid, workspace))
        return 164;
    bad_state = state.view;
    bad_state.rho = nullptr;
    if (!cfl_error(bad_state, grid, workspace))
        return 165;
    bad_state = state.view;
    bad_state.mass_fractions = nullptr;
    if (!cfl_error(bad_state, grid, workspace))
        return 166;
    bad_grid = grid;
    bad_grid.total_size = total - 1;
    if (!cfl_error(state.view, bad_grid, workspace))
        return 167;
    bad_grid = grid;
    bad_grid.dim = 0;
    if (!cfl_error(state.view, bad_grid, workspace))
        return 168;
    CudaHydroWorkspaceView bad_workspace = workspace;
    bad_workspace.cfl_candidates = nullptr;
    if (!cfl_error(state.view, grid, bad_workspace))
        return 169;
    bad_workspace = workspace;
    bad_workspace.cfl_result = nullptr;
    if (!cfl_error(state.view, grid, bad_workspace))
        return 170;
    double invalid_result_host = 0.0;
    if (cudaDeviceSynchronize() != cudaSuccess
        || cudaMemcpy(&invalid_result_host, result, sizeof(double),
                      cudaMemcpyDeviceToHost) != cudaSuccess
        || !exact_bits(invalid_result_host, 0x405ec00000000000ULL))
        return 171;
    const double expected_dt = frozen_double(0x3fd5db37998729f9ULL);
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
    const int route_result = run_route_matrix();
    if (route_result != 0)
        return route_result;
#endif
    return 0;
}
