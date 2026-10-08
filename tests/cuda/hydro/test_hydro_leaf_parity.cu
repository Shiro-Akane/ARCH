/**
 * @file test_hydro_leaf_parity.cu
 * @brief Compare shared hydro numerical leaves on CPU and CUDA.
 *
 * Exercise reconstructed faces, directional fluxes, divergence and primitive
 * recovery against host results and independent physical identities.
 */
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

#if defined(__CUDACC__)
#include <cuda_runtime.h>
#endif

#include "driver/DriverUtils.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "numerics/flux/FluxFunctions.h"
#include "numerics/linalg/DenseWrap.h"
#include "physics/eos/IdealGas.h"
#include "physics/gravity/GravitySource.h"
#include "numerics/flux/FluxHLL.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/FluxRoe.h"
#include "numerics/flux/FluxSW.h"
#include "numerics/flux/FluxVL.h"
#include "numerics/flux/StationarySlipWallFlux.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include "numerics/reconstruction/Reconstruction.h"
#include "fixtures/hydro/MeanThermoCases.h"
#include "fixtures/hydro/RoeFluxReference.h"
#include "math/RoeThermodynamicCases.h"

#if defined(__CUDACC__)
#include "amr/storage/Block.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "cuda/hydro/policies/HydroFluxPolicies.cuh"
#include "cuda/hydro/policies/HydroReconstructionPolicies.cuh"
#include "cuda/hydro/kernels/HydroFaceKernel.cuh"
#include "cuda/hydro/kernels/HydroStageKernels.cuh"
#include "cuda/hydro/kernels/HydroBatchKernels.cuh"
#endif

namespace
{
struct TestIdealGas
{
    ARCH_INLINE double get_gamma(const double*) const { return 1.4; }

    // This fixed-composition test EOS borrows the production single-gas
    // temperature relation and CGS Cv; native mean admissibility requires it.
    ARCH_INLINE double get_temperature(double rho,double energy,const double*) const
    { return IdealGasView{}.get_temperature(rho,energy,nullptr); }

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

bool vector_near(const FluidVector& actual, const FluidVector& expected);

struct DeviceLeafResult
{
    RoeThermodynamicCases::Result roe_identities;
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
    double low_density_error = 0.0;
    bool low_density_valid = true;
};

// Independent Euler uniform flux, constant acceleration and original linear
// system identities, plus density similarity across every production flux.
// The budget is the same 1e-11 used by the CPU low-density manifest.
ARCH_INLINE void range_error(DeviceLeafResult& result, double actual, double expected)
{
    if (!std::isfinite(actual)) result.low_density_valid = false;
    result.low_density_error = std::max(result.low_density_error,
        std::abs(actual-expected)/std::max(1.0,std::abs(expected)));
}
template<class Flux>
ARCH_INLINE void range_flux(DeviceLeafResult& result, double scale)
{
    const IdealGasView eos{};
    const FluidVector left{1.,.3,0.,0.,2.545}, right{.125,-.0125,0.,0.,.250625};
    const double x[]{1.}; double species[1]; FluidVector baseline, actual;
    Flux::compute_face_flux(left,right,x,x,1,eos,0,.1,baseline,species);
    Flux::compute_face_flux(scale*left,scale*right,x,x,1,eos,0,.1,actual,species);
    range_error(result,actual.rho/scale,baseline.rho);
    range_error(result,actual.mom_u/scale,baseline.mom_u);
    range_error(result,actual.eng/scale,baseline.eng);
    range_error(result,species[0]/scale,actual.rho/scale);
    Flux::compute_face_flux(scale*left,scale*left,x,x,1,eos,0,.1,actual,species);
    range_error(result,actual.rho/scale,.3);
    range_error(result,actual.mom_u/scale,1.09);
    range_error(result,actual.eng/scale,.3*3.545);
}
ARCH_INLINE void evaluate_low_density(DeviceLeafResult& result)
{
    const double scales[]{1.,1e-12,1e-20,1e-30,1e-60,1e-100};
    for (double scale : scales) {
        range_flux<FluxHLL<PCMReconstruction>>(result,scale);
        range_flux<FluxHLLC<PCMReconstruction>>(result,scale);
        range_flux<FluxRoe<PCMReconstruction>>(result,scale);
        range_flux<FluxSW<PCMReconstruction>>(result,scale);
        range_flux<FluxVL<PCMReconstruction>>(result,scale);
        const FluidVector value{scale,2.*scale,0.,0.,9.5*scale};
        range_error(result,arch::state::recover(value).internal,7.5);
        range_error(result,compute_cfl_cell_dt(value,3.,1,.25,1.,1.),.025);
        FluidVector source;
        Physical::Gravity::add_external_gravity_source_cell(value,{3.,0.,0.,true},.5,source);
        range_error(result,source.mom_u/scale,1.5);
        range_error(result,source.eng/scale,3.);
        range_error(result,compute_limited_slope<VanLeer>(scale,2*scale,3*scale)/scale,.5);
        DenseMatrixData<2> a; a(1,1)=2*scale; a(1,2)=scale; a(2,1)=scale; a(2,2)=3*scale;
        double rhs[]{4*scale,7*scale};
        result.low_density_valid &= DenseLUSolver::solve<2,2>(a,rhs);
        range_error(result,rhs[0],1.); range_error(result,rhs[1],2.);
    }
    double bounded[]{.999,.001};
    result.low_density_valid &= arch::state::normalize_composition(bounded,2,1,.2)
        && bounded[0]>=.2 && bounded[1]>=.2;
    range_error(result,bounded[0]+bounded[1],1.);
}

ARCH_INLINE void evaluate_device_leaves(DeviceLeafResult* output)
{
    output->low_density_error=0.;
    output->low_density_valid=MeanThermoCases::evaluate();
    evaluate_low_density(*output);
    output->roe_identities = RoeThermodynamicCases::evaluate();
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

bool scalar_near(double actual, double expected);

struct RouteFingerprint
{
    int limiter_id;
    int flux_id;
    int stage_id;
    std::uint64_t limiter_hash;
    std::uint64_t reconstruction_hash;
    std::uint64_t flux_hash;
    std::uint64_t stage_hash;
    FluidVector flux{};
    FluidVector stage{};
    FluidVector left{}, right{};
    double limiter_values[4]{};
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
        1.0e-12, 1.0e-10, 1.0e20, stage, nullptr);

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
        limiter_hash, reconstruction_hash, flux_hash, stage_hash, flux, stage, left, right,
        {Limiter::calc(.21),Limiter::calc(.57),Limiter::calc(1.43),Limiter::calc(-.37)}};
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

    static const RouteFingerprint expected[route_count] = {
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
        // Independent equations retain the existing numerical budget even
        // where scale-safe algebra intentionally changes last-bit hashes.
        const auto& states = RoeFluxReference::route_state[e.limiter_id];
        const double* f = e.flux_id < 2
            ? RoeFluxReference::split_route_flux[e.limiter_id][e.flux_id]
            : RoeFluxReference::route_flux[e.limiter_id][e.flux_id-2];
        const double old_weights[3]{0., .5, .75};
        const double w = old_weights[e.stage_id];
        double stage[5]{};
        for (int field=0; field<5; ++field)
            stage[field]=w*states[0][field]+(1-w)*(states[1][field]+f[field]);
        const bool numerical_match=vector_near(a.flux,{f[0],f[1],f[2],f[3],f[4]})
            && vector_near(a.stage,{stage[0],stage[1],stage[2],stage[3],stage[4]});
        bool limiter_match=a.limiter_hash==e.limiter_hash;
        bool reconstruction_match=a.reconstruction_hash==e.reconstruction_hash;
        if (e.limiter_id==3) {
            // The harmonic limiter now avoids overflow for arbitrarily large r.
            // Evaluate its defining rational function independently here.
            const double ratios[4]{.21,.57,1.43,-.37};
            limiter_match=true;
            for (int j=0;j<4;++j) {
                const double expected=ratios[j]>0 ? 2*ratios[j]/(1+ratios[j]) : 0.;
                limiter_match=limiter_match && scalar_near(a.limiter_values[j],expected);
            }
            const auto& l=states[0]; const auto& r=states[1];
            reconstruction_match=vector_near(a.left,{l[0],l[1],l[2],l[3],l[4]})
                && vector_near(a.right,{r[0],r[1],r[2],r[3],r[4]});
        }
        const bool route_matches=a.limiter_id==e.limiter_id && a.flux_id==e.flux_id
            && a.stage_id==e.stage_id && limiter_match && reconstruction_match && numerical_match;
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
    const auto bits=std::bit_cast<std::uint64_t>(actual);
    if(bits!=expected)
        std::cerr << "Host characterization bit mismatch actual=0x" << std::hex
                  << bits << " expected=0x" << expected << std::dec << '\n';
    return bits == expected;
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

FaceResult independent_face_result(const double (&values)[7])
{
    return {{values[0], values[1], values[2], values[3], values[4]},
            {values[5], values[6]}};
}

bool independent_face_matches(const FaceResult& actual, const double (&values)[7])
{
    const auto expected = independent_face_result(values);
    return vector_near(actual.flux, expected.flux)
        && scalar_near(actual.species[0], expected.species[0])
        && scalar_near(actual.species[1], expected.species[1]);
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
                       0x4000000000000000ULL)
        || !exact_bits(McLimiter::calc(std::numeric_limits<double>::quiet_NaN()),
                       0x4000000000000000ULL)
        || compute_limited_slope<MinMod>(0.0, 1.0, 1.0 + 0.5e-12) != 0.5*((1.0+0.5e-12)-1.0)
        || !exact_bits(compute_limited_slope<MinMod>(0.0, 1.0, 1.0 + 1.0e-12),
                       0x3d61980000000000ULL))
        return false;

    const FluidVector state{2.0, 6.0, 8.0, 10.0, 100.0};
    if (!vector_bits(get_flux(state, 7.0, 0),
                     0x4018000000000000ULL, 0x4039000000000000ULL,
                     0x4038000000000000ULL, 0x403e000000000000ULL,
                     0x4074100000000000ULL)
        || !std::isnan(get_flux({-0.0, 1.0, 2.0, 3.0, 4.0}, 7.0, 0).rho)
        || !exact_bits(entropy_fix(-0.0, 1.0), 0x3fe0000000000000ULL))
        return false;

    const auto ppm = PPMReconstruction::apply(
        {1, 0, 0, 0, 2}, {1, 0, 0, 0, 2}, {1, 0, 0, 0, 2},
        {10, 0, 0, 0, 20}, {10, 0, 0, 0, 20}, {10, 0, 0, 0, 20});
    // On this unsupported sharp step, one neighboring second difference is
    // zero on each side. The current curvature support k is exactly zero:
    // theta=0 selects the CW bounds, which return the own-cell means 1/10
    // (energy 2/20). These exact analytical values retire the old method snapshot.
    if (!vector_bits(ppm.first,
                     0x3ff0000000000000ULL, 0, 0, 0, 0x4000000000000000ULL)
        || !vector_bits(ppm.second,
                        0x4024000000000000ULL, 0, 0, 0, 0x4034000000000000ULL))
        return false;

    const FaceResult hll = characterize_face<FluxHLL>(0.0);
    const FaceResult hllc = characterize_face<FluxHLLC>(0.0);
    const FaceResult roe = characterize_face<FluxRoe>(0.1);
    const FaceResult sw = characterize_face<FluxSW>(0.1);
    const FaceResult vl = characterize_face<FluxVL>(0.0);
    return independent_face_matches(hll, RoeFluxReference::hll)
        && independent_face_matches(hllc, RoeFluxReference::hllc)
        && independent_face_matches(roe, RoeFluxReference::roe)
        && vector_bits(sw.flux,
                       0x3fdd6f579bdceca1ULL, 0x4000492c964085f2ULL,
                       0xbfd41edd0ad3fc16ULL, 0x3fc41edd0ad3fc16ULL,
                       0x3fff91085fc8c1d9ULL)
        && vector_bits(vl.flux,
                       0x3fdd3f7f1d3b7d98ULL, 0x40006dfb578715e0ULL,
                       0xbfd07e985324a906ULL, 0x3fc07e985324a906ULL,
                       0x3ffff6d62a7013d3ULL)
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
    const auto hll = independent_face_result(RoeFluxReference::hll);
    const auto hllc = independent_face_result(RoeFluxReference::hllc);
    const auto roe = independent_face_result(RoeFluxReference::roe);
    const FluidVector sw = frozen_vector(
        0x3fdd6f579bdceca1ULL, 0x4000492c964085f2ULL,
        0xbfd41edd0ad3fc16ULL, 0x3fc41edd0ad3fc16ULL,
        0x3fff91085fc8c1d9ULL);
    const FluidVector vl = frozen_vector(
        0x3fdd3f7f1d3b7d98ULL, 0x40006dfb578715e0ULL,
        0xbfd07e985324a906ULL, 0x3fc07e985324a906ULL,
        0x3ffff6d62a7013d3ULL);
    const std::uint64_t species_bits[4] = {
        0x3fa9dee10cbc3d20ULL, 0x3fda337b7a4564feULL,
        0x3fb53fc3c72dfef2ULL, 0x3fd7ef8e2b6ffddcULL};
    bool species_match = true;
    const double independent_species[6] = {
        hll.species[0], hll.species[1], hllc.species[0], hllc.species[1],
        roe.species[0], roe.species[1]};
    for (int species = 0; species < 6; ++species)
        species_match = species_match
            && scalar_near(actual.species[species], independent_species[species]);
    for (int species = 6; species < 10; ++species)
        species_match = species_match
            && scalar_near(actual.species[species],
                           frozen_double(species_bits[species-6]));
    const bool matches = actual.low_density_valid && actual.low_density_error <= 1e-11
        && actual.roe_identities.passed()
        && vector_near(actual.hll, hll.flux)
        && vector_near(actual.hllc, hllc.flux)
        && vector_near(actual.roe, roe.flux)
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
        && actual.slope_below == 0.5*((1.0+0.5e-12)-1.0)
        // The same unsupported scalar step has exact CW endpoints 0/10;
        // no rounding window or positive floor is needed for these values.
        && actual.ppm_left == 0.0
        && actual.ppm_right == 10.0
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
        show("roe.reflection", actual.roe_identities.reflection);
        show("roe.rotation", actual.roe_identities.rotation);
        show("roe.ideal_sound_speed", actual.roe_identities.ideal_sound_speed);
        show("roe.ideal_pressure_jump", actual.roe_identities.ideal_pressure_jump);
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

    const auto expected_hll = independent_face_result(RoeFluxReference::hll);
    const auto expected_hllc = independent_face_result(RoeFluxReference::hllc);
    const auto expected_roe = independent_face_result(RoeFluxReference::roe);
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
    const auto expected_pcm = independent_face_result(RoeFluxReference::linear_pcm);
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
    bad_state.n_species = kLocalSpeciesScratchCapacity + 1;
    if (!pcm_face_error(bad_state, flux.view, grid, 0))
        return 119;
    bad_flux = flux.view;
    bad_flux.n_species = -1;
    if (!pcm_face_error(state.view, bad_flux, grid, 0))
        return 120;
    bad_flux = flux.view;
    bad_flux.n_species = kLocalSpeciesScratchCapacity + 1;
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
    bad_delta.n_species = kLocalSpeciesScratchCapacity + 1;
    if (!divergence_error(flux.view, bad_delta, grid, 0))
        return 134;
    DeviceStateView bad_input_flux = flux.view;
    bad_input_flux.n_species = -1;
    if (!divergence_error(bad_input_flux, delta.view, grid, 0))
        return 135;
    bad_input_flux = flux.view;
    bad_input_flux.n_species = kLocalSpeciesScratchCapacity + 1;
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
        // Binary-exact species densities isolate stage algebra from normalization.
        stage_delta.store(cell, {0.5, 0.2, 0.2, 0.2, 0.1});
        stage_delta.set_species(0, cell, .5*.25);
        stage_delta.set_species(1, cell, .5*.75);
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
                   candidate_grid, 0.5, 0.5, 1e-12, 1e-10, 1e20,
                   nullptr)
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
    bad_old_state.n_species = kLocalSpeciesScratchCapacity + 1;
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
        0x3ffc000000000000ULL, 0x4008cccccccccccdULL,
        0x4012666666666666ULL, 0x4018666666666666ULL,
        0x4052c33333333333ULL);
    const double expected_stage_species[2] = {0.25, 0.75};
    if (launch_hydro_single_stage_update(
            old_state.view, current_state.view, destination.view,
            stage_delta.view, grid, 0.5, 0.5, 1e-12, 1e-10, 1e20,
            nullptr)
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

    // Workflow: reuse the actual stage buffers/launcher for SSPRK3's final
    // (1/3,2/3) pair with zero conserved and species RHS. Each seed component
    // is a power-of-two multiple of the independently specified nextafter(1)
    // bit pattern; ample thermal energy prevents a repair from masking drift.
    constexpr double stationary_q = 0x1.0000000000001p+0;
    const FluidVector stationary_seed{
        stationary_q, stationary_q / 16.0, stationary_q / 8.0,
        stationary_q / 32.0, 8.0 * stationary_q};
    old_state.store(active, stationary_seed);
    current_state.store(active, stationary_seed);
    stage_delta.store(active, {});
    for (int species_index = 0; species_index < 2; ++species_index) {
        const double fraction = species_index == 0 ? .25 : .75;
        old_state.set_species(species_index, active, fraction);
        current_state.set_species(species_index, active, fraction);
        stage_delta.set_species(species_index, active, 0.0);
    }
    if (!old_state.upload() || !current_state.upload() || !stage_delta.upload())
        return 203;
    if (launch_hydro_single_stage_update(
            old_state.view, current_state.view, destination.view,
            stage_delta.view, grid, 1.0 / 3.0, 2.0 / 3.0,
            1e-12, 1e-10, 1e20, nullptr) != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess || !destination.download()
        || !vector_bits(destination.load(active),
                        0x3ff0000000000001ULL, 0x3fb0000000000001ULL,
                        0x3fc0000000000001ULL, 0x3fa0000000000001ULL,
                        0x4020000000000001ULL)
        || !exact_bits(destination.species(0, active), 0x3fd0000000000000ULL)
        || !exact_bits(destination.species(1, active), 0x3fe8000000000000ULL))
        return 204;
    for (int cell = 0; cell < total; ++cell) {
        if (!exact_bits(destination.enuc_rate[cell], 0x4031000000000000ULL))
            return 205;
        if (cell != active
            && (!vector_bits(destination.load(cell),
                             0x4022000000000000ULL, 0x4020000000000000ULL,
                             0x401c000000000000ULL, 0x4018000000000000ULL,
                             0x4014000000000000ULL)
                || !exact_bits(destination.species(0, cell), 0x3fc999999999999aULL)
                || !exact_bits(destination.species(1, cell), 0x3fe999999999999aULL)))
            return 205;
    }
    std::cout << "CUDA_RK3_STATIONARY_PASS cells=1 species=2\n";

    state.store(active, {2.0, 2.0, 2.5, 2.0, 10.0});
    if (!state.upload())
        return 113;
    double* candidates = nullptr;
    double* result = nullptr;
    int* cfl_status = nullptr;
    if (cudaMalloc(&candidates, sizeof(double)) != cudaSuccess
        || cudaMalloc(&result, sizeof(double)) != cudaSuccess
        || cudaMalloc(&cfl_status, sizeof(int)) != cudaSuccess)
        return 114;
    CudaHydroWorkspaceView workspace{};
    workspace.flux = flux.view;
    workspace.delta = delta.view;
    workspace.cfl_candidates = candidates;
    workspace.cfl_result = result;
    workspace.cfl_status = cfl_status;
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
    bad_state.n_species = kLocalSpeciesScratchCapacity + 1;
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
    bad_workspace = workspace;
    bad_workspace.cfl_status = nullptr;
    if (!cfl_error(state.view, grid, bad_workspace))
        return 172;
    double invalid_result_host = 0.0;
    if (cudaDeviceSynchronize() != cudaSuccess
        || cudaMemcpy(&invalid_result_host, result, sizeof(double),
                      cudaMemcpyDeviceToHost) != cudaSuccess
        || !exact_bits(invalid_result_host, 0x405ec00000000000ULL))
        return 171;
    // rho=2, m=(2,2.5,2), E=10: P=(gamma-1)*(E-|m|^2/(2*rho)).
    const double pressure = .4 * (10.0 - (4.0 + 6.25 + 4.0) / 4.0);
    const double expected_dt = .5 * .8 * grid.dx1 / (1.0 + std::sqrt(1.4*pressure/2.0));
    double result_host = 0.0;
    int status_host = -1;
    if (launch_compute_hydro_dt(
            state.view, grid, TestIdealGas{}, 0.8, workspace, nullptr)
            != cudaSuccess
        || cudaDeviceSynchronize() != cudaSuccess
        || cudaMemcpy(&result_host, result, sizeof(double), cudaMemcpyDeviceToHost)
               != cudaSuccess
        || cudaMemcpy(&status_host, cfl_status, sizeof(int),
                      cudaMemcpyDeviceToHost) != cudaSuccess
        || status_host
               != static_cast<int>(arch::reduction::ReductionStatus::Ok)
        || std::abs(result_host - expected_dt) > 3e-14 * std::abs(expected_dt))
        return 115;

    cudaFree(candidates);
    cudaFree(result);
    cudaFree(cfl_status);
    cudaFree(volume);
    cudaFree(lower_area);
    cudaFree(upper_area);
    return 0;
}

// Exercise the production stage leaf with concurrent repairs and a partial
// final CUDA block. Expected integrals follow directly from rho, u, e and X.
__global__ void concurrent_repair_kernel(double scale, double* ledger, int* failure)
{
    constexpr int count=1025;
    const int cell=blockIdx.x*blockDim.x+threadIdx.x;
    if(cell>=count) return;
    const FluidVector old{scale,2.*scale,0.,0.,5.*scale}; // u=2, e=3
    const double x[]{.25,.75}, delta_x[]{0.,0.};
    double out_x[2]; FluidVector output;
    const auto status=TimeIntegration::update_stage_cell(old,old,FluidVector{},
        x,x,delta_x,2,1,0.,1.,2.*scale,4.,100.,output,out_x,{ledger,2},.25,cell);
    if(status!=arch::state::Status::repaired || output.rho!=2.*scale ||
       std::abs(output.mom_u/scale-4.)>1e-13 || std::abs(output.eng/scale-12.)>1e-13 ||
       out_x[0]!=.25 || out_x[1]!=.75) atomicExch(failure,1);
}

int run_concurrent_repairs()
{
    double* ledger=nullptr; int* failure=nullptr;
    if(cudaMalloc(&ledger,14*sizeof(double))!=cudaSuccess) return 120;
    if(cudaMalloc(&failure,sizeof(int))!=cudaSuccess) { cudaFree(ledger); return 120; }
    int result=0;
    for(double scale : {1.,1e-100}) {
        cudaMemset(ledger,0,14*sizeof(double)); cudaMemset(failure,0,sizeof(int));
        concurrent_repair_kernel<<<5,256>>>(scale,ledger,failure);
        double actual[14]; int failed=0;
        if(cudaGetLastError()!=cudaSuccess ||
           cudaMemcpy(actual,ledger,sizeof(actual),cudaMemcpyDeviceToHost)!=cudaSuccess ||
           cudaMemcpy(&failed,failure,sizeof(int),cudaMemcpyDeviceToHost)!=cudaSuccess || failed) {
            result=121; break;
        }
        constexpr double volume=1025.*.25;
        const double expected[]{1025.,volume,volume,volume,2.*volume,0.,0.,
                                7.*volume,7.*volume,0.,.25*volume,.25*volume,.75*volume,.75*volume};
        for(int i=0;i<14;++i) {
            if(i==9) { if(actual[i]<0. || actual[i]>=1025. || std::floor(actual[i])!=actual[i]) result=122; }
            else {
                const double scaled=i<2 ? actual[i] : actual[i]/scale;
                if(!std::isfinite(scaled) || std::abs(scaled-expected[i])>
                    1e-11*std::max(1.,std::abs(expected[i]))) result=122;
            }
        }
    }
    cudaFree(ledger); cudaFree(failure);
    return result;
}

/** One bounded device arena for native CFL candidates and actual metric planes.
 * Host uploads shared geometry leaves; independent integral checks below
 * verify W/torque, rather than inventing a second device metric formula.
 */
struct NativeHydroLeafArena {
    double* values=nullptr;
    int* status=nullptr;
    const int total,count;
    NativeHydroLeafArena(int size,int active):total(size),count(active) {
        if(cudaMalloc(&values,(5*total+count+1)*sizeof(double))!=cudaSuccess)
            throw std::runtime_error("native hydro leaf arena allocation failed");
        if(cudaMalloc(&status,sizeof(int))!=cudaSuccess) {
            cudaFree(values);values=nullptr;
            throw std::runtime_error("native hydro status allocation failed");
        }
    }
    ~NativeHydroLeafArena(){cudaFree(status);cudaFree(values);}
    NativeHydroLeafArena(const NativeHydroLeafArena&)=delete;
    NativeHydroLeafArena& operator=(const NativeHydroLeafArena&)=delete;
    arch::cuda::CudaHydroWorkspaceView workspace() const {
        arch::cuda::CudaHydroWorkspaceView w{};
        w.cfl_candidates=values+5*total;w.cfl_result=w.cfl_candidates+count;
        w.cfl_status=status;return w;
    }
};

/** Actual Block-generated Native leaf observations, without Runtime/BC grant.
 * Two small fixtures cover cold axis rotation and a nonbinary deep annulus.
 * Full logical rho support is physical analytic extension; pitch is poison.
 * This qualifies only the existing resident CFL/divergence mathematical lane.
 */
int run_native_cfl_divergence()
{
    using namespace arch::cuda;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto checked=[](cudaError_t error) {
        if(error!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(error));
    };
    const auto require=[](bool ok,const char* message) {
        if(!ok)throw std::runtime_error(message);
    };
    try {
        for(bool deep:{false,true}) {
            Grid root(amr::MAX_NG,deep?.4:0.,deep?1.3:1.,-.3,.7,
                0.,1.,deep?3:1,deep?5:1,1);
            root.geometry="cylindrical";root.dim=2;
            amr::Block block;block.Reset();block.level=deep?10:0;
            block.logical_x1=deep?2U:0U;block.logical_x2=deep?7U:0U;
            block.InitGeometry(root,(root.x1_max-root.x1_min)/(root.nblockx1*amr::BLOCK_NX),
                (root.x2_max-root.x2_min)/(root.nblockx2*amr::BLOCK_NY),1.,native);
            block.RequireNativeGeometryIdentity();const auto& host_grid=block.grid;
            auto grid=make_device_grid_view(host_grid,native);
            const auto geometry=GridMetrics::make_geometry_view(host_grid,native);
            const int total=grid.total_size,count=grid.active_cell_count();
            DeviceStateFixture state(total,0),flux(total,0),delta(total,0);
            require(state.valid&&flux.valid&&delta.valid,"native state allocation failed");
            FluidState host;host.Preallocate(total);host.InitSpecies(0);
            const double poison=std::numeric_limits<double>::quiet_NaN();
            for(int c=0;c<total;++c){state.store(c,{poison,poison,poison,poison,poison});
                host.set(c,{poison,poison,poison,poison,poison});}
            const double e0=deep?.001:host_grid.dx1*host_grid.dx1/64.;
            for(int j=0;j<host_grid.GetTotalY();++j)for(int i=0;i<host_grid.GetTotalX();++i) {
                const int c=host_grid.GetIndex(i,j,0);
                const long double l=host_grid.GetFacePosL(i),h=host_grid.GetFacePosR(i);
                const long double lo=std::min(std::abs(l),std::abs(h)),hi=std::max(std::abs(l),std::abs(h));
                const long double v=(hi*hi-lo*lo)/2,w=(hi*hi*hi-lo*lo*lo)/3;
                const long double inertia=(hi*hi*hi*hi-lo*lo*lo*lo)/4;
                const long double sign=h<=0?-1:1;
                const FluidVector u{1.,0.,0.,double(sign*inertia/w),double(e0+.5L*inertia/v)};
                state.store(c,u);host.set(c,u);
            }
            if(!deep)require(arch::state::recover(host.get(host_grid.GetIndex(host_grid.Is(),host_grid.Js(),0))).status
                ==arch::state::Status::unresolved_energy,"cold native raw oracle no longer rejects");
            require(state.upload(),"native state upload failed");
            NativeHydroLeafArena arena(total,count);
            auto workspace=arena.workspace();
            const double host_dt=adaptive_dt(host,IdealGasView{},host_grid,.8,false,native);
            checked(launch_compute_hydro_dt(state.view,grid,IdealGasView{},.8,workspace,nullptr));
            double actual_dt=0.;int status=0;
            checked(cudaMemcpy(&actual_dt,workspace.cfl_result,sizeof(double),cudaMemcpyDeviceToHost));
            checked(cudaMemcpy(&status,workspace.cfl_status,sizeof(int),cudaMemcpyDeviceToHost));
            require(status==0&&std::isfinite(actual_dt)&&scalar_near(actual_dt,host_dt),
                "native resident CFL differs from actual shared Host closure");
            std::vector<double> metrics(5*total,0.);
            for(int j=grid.js;j<grid.je;++j)for(int i=grid.is;i<grid.ie;++i) {
                const int c=host_grid.GetIndex(i,j,0);metrics[c]=GridMetrics::CellVolume(geometry,i,j,0);
                for(int axis=0;axis<2;++axis){metrics[(1+axis)*total+c]=GridMetrics::FaceArea(geometry,axis,i,j,0,false);
                    metrics[(3+axis)*total+c]=GridMetrics::FaceArea(geometry,axis,i,j,0,true);}
            }
            checked(cudaMemcpy(arena.values,metrics.data(),metrics.size()*sizeof(double),cudaMemcpyHostToDevice));
            grid.cell_volume=arena.values;
            for(int axis=0;axis<2;++axis){grid.face_area_lower[axis]=arena.values+(1+axis)*total;
                grid.face_area_upper[axis]=arena.values+(3+axis)*total;}
            for(int direction=0;direction<2;++direction) {
                for(int c=0;c<total;++c){const double q=1.+.01*(c%grid.stride_y)+.03*(c/grid.stride_y);
                    flux.store(c,{q,2.*q,3.*q,4.*q,5.*q});delta.store(c,{0.,0.,0.,0.,0.});}
                require(flux.upload()&&delta.upload(),"native divergence upload failed");
                checked(launch_hydro_divergence(flux.view,delta.view,grid,.001,direction,nullptr));
                require(delta.download(),"native divergence download failed");
                for(int j=grid.js;j<grid.je;++j)for(int i=grid.is;i<grid.ie;++i) {
                    const int c=host_grid.GetIndex(i,j,0),stride=grid.stride(direction);
                    FluidVector expected{};
                    const TimeIntegration::NativeAngularDivergence angular{&geometry,direction,i,j};
                    require(TimeIntegration::accumulate_cell_divergence(flux.load(c),flux.load(c+stride),nullptr,nullptr,
                        0,total,metrics[(1+direction)*total+c],metrics[(3+direction)*total+c],metrics[c],.001,
                        expected,nullptr,&angular),"native Host reference divergence rejected valid geometry");
                    require(vector_near(delta.load(c),expected),"native device divergence lost shared Host parity");
                    const long double l=host_grid.GetFacePosL(i),h=host_grid.GetFacePosR(i);
                    const long double dz=static_cast<long double>(host_grid.GetAxialFacePosR(j))-host_grid.GetAxialFacePosL(j);
                    const long double radial_w=(h*h*h-l*l*l)/3;
                    const long double wl=direction==0?l*l*dz:radial_w;
                    const long double wh=direction==0?h*h*dz:radial_w;
                    const double independent=double(.001L*(flux.load(c).mom_w*wl-flux.load(c+stride).mom_w*wh)/(radial_w*dz));
                    require(scalar_near(delta.load(c).mom_w,independent),"native device angular divergence used V or one missing lever");
                }
            }
            auto bad=grid;bad.dyadic_identity.level=amr::kMaxRefinementLevel+1;
            const double sentinel=123.;checked(cudaMemcpy(workspace.cfl_result,&sentinel,sizeof(double),cudaMemcpyHostToDevice));
            require(launch_compute_hydro_dt(state.view,bad,IdealGasView{},.8,workspace,nullptr)==cudaErrorInvalidValue
                &&launch_hydro_divergence(flux.view,delta.view,bad,.001,0,nullptr)==cudaErrorInvalidValue,
                "native malformed identity reached a hydro kernel");
            checked(cudaMemcpy(&actual_dt,workspace.cfl_result,sizeof(double),cudaMemcpyDeviceToHost));
            require(actual_dt==sentinel,"rejected native identity modified CFL output");
            // Construct a complete authentic no-halo Grid from the actual
            // Block bounds/root identity. All logical extents/strides are
            // rebuilt; this is not an inconsistent mutation of a POD's ng.
            Grid no_halo(0,host_grid.x1_min,host_grid.x1_max,
                host_grid.x2_min,host_grid.x2_max,host_grid.x3_min,host_grid.x3_max,
                host_grid.nblockx1,host_grid.nblockx2,host_grid.nblockx3);
            no_halo.geometry=host_grid.geometry;no_halo.dim=host_grid.dim;
            no_halo.dyadic_identity=host_grid.dyadic_identity;
            no_halo.InitializeTopology(native);
            const auto no_halo_view=make_device_grid_view(no_halo,native);
            require(valid_hydro_grid(no_halo_view)&&no_halo_view.ng==0
                &&no_halo_view.is==0&&no_halo_view.ie==no_halo_view.total_x,
                "true no-halo Native mathematical Grid became malformed");
            DeviceStateFixture no_halo_state(no_halo_view.total_size,0);
            require(no_halo_state.valid,"no-halo state allocation failed");
            for(int c=0;c<no_halo_view.total_size;++c)
                no_halo_state.store(c,{1.,0.,0.,0.,1.});
            require(no_halo_state.upload(),"no-halo state upload failed");
            NativeHydroLeafArena no_halo_arena(no_halo_view.total_size,no_halo_view.active_cell_count());
            const auto no_halo_workspace=no_halo_arena.workspace();
            const int status_sentinel=7;
            std::vector<double> candidate_sentinels(no_halo_view.active_cell_count(),sentinel);
            checked(cudaMemcpy(no_halo_workspace.cfl_result,&sentinel,sizeof(double),cudaMemcpyHostToDevice));
            checked(cudaMemcpy(no_halo_workspace.cfl_status,&status_sentinel,sizeof(int),cudaMemcpyHostToDevice));
            checked(cudaMemcpy(no_halo_workspace.cfl_candidates,candidate_sentinels.data(),
                candidate_sentinels.size()*sizeof(double),cudaMemcpyHostToDevice));
            require(launch_compute_hydro_dt(no_halo_state.view,no_halo_view,IdealGasView{},.8,
                no_halo_workspace,nullptr)==cudaErrorInvalidValue,
                "Native no-halo CFL launched an out-of-domain rho stencil");
            checked(cudaMemcpy(&actual_dt,no_halo_workspace.cfl_result,sizeof(double),cudaMemcpyDeviceToHost));
            checked(cudaMemcpy(&status,no_halo_workspace.cfl_status,sizeof(int),cudaMemcpyDeviceToHost));
            std::vector<double> no_halo_candidates(candidate_sentinels.size());
            checked(cudaMemcpy(no_halo_candidates.data(),no_halo_workspace.cfl_candidates,
                no_halo_candidates.size()*sizeof(double),cudaMemcpyDeviceToHost));
            require(actual_dt==sentinel&&status==status_sentinel&&no_halo_candidates==candidate_sentinels,
                "Native no-halo CFL rejection modified outputs/status before returning");
            const int ghost=host_grid.GetIndex(grid.is-1,grid.js,0);state.rho[ghost]=poison;
            require(state.upload(),"invalid closure upload failed");
            checked(launch_compute_hydro_dt(state.view,grid,IdealGasView{},.8,workspace,nullptr));
            checked(cudaMemcpy(&actual_dt,workspace.cfl_result,sizeof(double),cudaMemcpyDeviceToHost));
            checked(cudaMemcpy(&status,workspace.cfl_status,sizeof(int),cudaMemcpyDeviceToHost));
            require(status!=0&&std::isnan(actual_dt),"native bad actual rho support silently passed CFL reduction");
            require(state.download(),"native source verification download failed");
            for(int c=0;c<total;++c) {
                const auto a=state.load(c),b=host.get(c);
                for(auto member:{&FluidVector::rho,&FluidVector::mom_u,&FluidVector::mom_v,&FluidVector::mom_w,&FluidVector::eng})
                    require(c==ghost&&member==&FluidVector::rho?std::isnan(a.*member):
                        std::bit_cast<std::uint64_t>(a.*member)==std::bit_cast<std::uint64_t>(b.*member),
                        "native CFL/divergence changed resident source or pitch bits");
            }
        }
        std::cout<<"NATIVE_CUDA_CFL_DIVERGENCE_LEAF_PASS layouts=2 runtime_authority=0 physical_qualification=0\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 198;}
}
/** Invoke the actual mean worker, independently of still-held Native full-stage
 * launch. Metadata, source and scratch are real DeviceState/Block allocations;
 * this helper grants neither a Runtime ghost lease nor scientific capability.
 */
__global__ void native_required_mean_test_kernel(arch::cuda::DeviceHydroBatchBlock b,
    arch::state::Bounds bounds)
{
    arch::cuda::detail::hydro_mean_thermo_work(b,IdealGasView{}, {},bounds);
}

/** Independent antiderivatives generate rho_V, J/W and E_V, including signed
 * axis ghosts. Constant and quadratic densities both have constant e0 and
 * Omega=1: P=(gamma-1)rho_V e0, c^2=gamma(gamma-1)e0. Check the actual required
 * interior/one-normal-ghost strip, ignored pitch, sticky failure and source bits.
 */
int run_native_required_mean_cache()
{
    using namespace arch::cuda;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto checked=[](cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));};
    const auto require=[](bool v,const char* m){if(!v)throw std::runtime_error(m);};
    try {
        for(bool variable:{false,true})for(int ns:{0,2,13}) {
            Grid root(amr::MAX_NG,variable?.4:0.,variable?1.3:1.,-.3,.7,0.,1.,1,1,1);
            root.geometry="cylindrical";root.dim=2;
            amr::Block block;block.Reset();block.level=0;block.logical_x1=block.logical_x2=0;
            block.InitGeometry(root,(root.x1_max-root.x1_min)/amr::BLOCK_NX,
                (root.x2_max-root.x2_min)/amr::BLOCK_NY,1.,native);
            block.RequireNativeGeometryIdentity();const auto& hg=block.grid;
            const auto g=make_device_grid_view(hg,native);const int total=g.total_size;
            DeviceStateFixture state(total,ns);require(state.valid,"mean state allocation");
            const double poison=std::numeric_limits<double>::quiet_NaN(),sentinel=123.;
            const double e0=variable?1./64.:hg.dx1*hg.dx1/64.;
            std::vector<double> rho_reference(total,poison);
            for(int c=0;c<total;++c)state.store(c,{poison,poison,poison,poison,poison});
            for(int j=0;j<hg.GetTotalY();++j)for(int i=0;i<hg.GetTotalX();++i) {
                const int c=hg.GetIndex(i,j,0);const long double a=hg.GetFacePosL(i),b=hg.GetFacePosR(i);
                const long double l=std::min(std::abs(a),std::abs(b)),u=std::max(std::abs(a),std::abs(b));
                const long double V=(u*u-l*l)/2,W=(u*u*u-l*l*l)/3;
                const long double c0=variable?7.L/8:1.L,c2=variable?1.L/4:0.L;
                const long double M=c0*(u*u-l*l)/2+c2*(u*u*u*u-l*l*l*l)/4;
                const long double I=c0*(u*u*u*u-l*l*l*l)/4
                    +c2*(u*u*u*u*u*u-l*l*l*l*l*l)/6;
                rho_reference[c]=double(M/V);
                state.store(c,{double(M/V),0.,0.,double((a<0?-1:1)*I/W),double((e0*M+.5L*I)/V)});
                for(int n=0;n<ns;++n)state.set_species(n,c,ns==2?(n==0?.25:.75):
                    (n==0?.5:(n==1?.5:(n==12?1e-20:0.))));
            }
            const auto before_rho=state.rho,before_u=state.mom_u,before_v=state.mom_v,
                before_w=state.mom_w,before_e=state.eng,before_enuc=state.enuc_rate,before_x=state.mass_fractions;
            require(state.upload(),"mean source upload");
            NativeHydroLeafArena arena(total,g.active_cell_count());
            DeviceHydroBatchBlock batch{};batch.input=state.view;batch.grid=g;
            batch.mean_pressure=arena.values;batch.mean_sound_speed=arena.values+total;batch.eos_status=arena.status;
            const arch::state::Bounds bounds{1e-12,1e-12,1e20};
            const auto run=[&](const DeviceHydroBatchBlock& b,const arch::state::Bounds& lim) {
                std::vector<double> initial(2*total,sentinel);
                checked(cudaMemcpy(arena.values,initial.data(),initial.size()*sizeof(double),cudaMemcpyHostToDevice));
                checked(cudaMemset(arena.status,0,sizeof(int)));
                native_required_mean_test_kernel<<<1,128>>>(b,lim);checked(cudaGetLastError());
                checked(cudaDeviceSynchronize());
            };
            run(batch,bounds);int failed=-1;std::vector<double> pc(2*total);
            checked(cudaMemcpy(pc.data(),arena.values,pc.size()*sizeof(double),cudaMemcpyDeviceToHost));
            checked(cudaMemcpy(&failed,arena.status,sizeof(int),cudaMemcpyDeviceToHost));
            require(failed==0,"native mean cache failed legitimate cold closure");
            for(int c=0;c<total;++c) {
                const int j=c/g.stride_y,i=c-j*g.stride_y;
                const bool ai=i>=g.is&&i<g.ie,aj=j>=g.js&&j<g.je;
                const bool needed=(ai&&aj)||(aj&&(i==g.is-1||i==g.ie))||(ai&&(j==g.js-1||j==g.je));
                if(needed)require(scalar_near(pc[c],.4*rho_reference[c]*e0)
                    &&scalar_near(pc[total+c],std::sqrt(1.4*.4*e0)),"native mean EOS differs independent physical P/c");
                else require(pc[c]==sentinel&&pc[total+c]==sentinel,"mean cache queried corner/pitch outside required strip");
            }
            if(!variable)require(arch::state::recover(state.load(hg.GetIndex(g.is,g.js,0))).status!=arch::state::Status::valid,
                "cold raw-mean contrast was lost");
            require(!detail::valid_hydro_batch_block<CudaPcmReconstruction>(batch,{}),
                "mean-only adapter accidentally promoted Native full batch");
            auto malformed=batch;malformed.grid.semantics=static_cast<GridMetrics::GeometrySemantics>(255);
            malformed.input.rho=nullptr;run(malformed,bounds);
            checked(cudaMemcpy(&failed,arena.status,sizeof(int),cudaMemcpyDeviceToHost));
            checked(cudaMemcpy(pc.data(),arena.values,pc.size()*sizeof(double),cudaMemcpyDeviceToHost));
            require(failed!=0&&std::all_of(pc.begin(),pc.end(),[](double v){return std::isnan(v);}),
                "unknown chart read source/published a finite required cache");
            auto invalid=bounds;invalid.internal_min=std::numeric_limits<double>::quiet_NaN();
            run(batch,invalid);checked(cudaMemcpy(&failed,arena.status,sizeof(int),cudaMemcpyDeviceToHost));
            require(failed!=0,"malformed actual mean Bounds accepted");
            require(state.download(),"mean immutable source download");
            const auto bits=[](const std::vector<double>& a,const std::vector<double>& b) {
                if(a.size()!=b.size())return false;
                for(std::size_t n=0;n<a.size();++n)if(std::bit_cast<std::uint64_t>(a[n])!=std::bit_cast<std::uint64_t>(b[n]))return false;
                return true;
            };
            require(bits(state.rho,before_rho)&&bits(state.mom_u,before_u)&&bits(state.mom_v,before_v)
                &&bits(state.mom_w,before_w)&&bits(state.eng,before_e)&&bits(state.enuc_rate,before_enuc)
                &&bits(state.mass_fractions,before_x),"mean adapter modified actual source/pitch/species bits");
        }
        std::cout<<"NATIVE_CUDA_REQUIRED_MEAN_CACHE_PASS layouts=2 species=0,2,13 runtime_authority=0 physical_qualification=0\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 199;}
}

/** Two explicit lanes exercise the private 41*S resident arena, not launch ABI.
 * Every result is published by the actual adapter/shared numerical compute.
 */
__global__ void native_selected_face_test_kernel(arch::cuda::DeviceStateView input,
    arch::cuda::DeviceStateView output,arch::cuda::DeviceGridView grid,int method,
    int direction,int i,int j,arch::state::Bounds bounds,
    arch::cuda::detail::NativeFaceScratchView scratch,int* failure,
    arch::boundary::HydroBoundaryView walls={},bool foreign_means=false,int lanes=1,bool null_source=false)
{
    const int lane=threadIdx.x;
    if(lane>=lanes)return;
    if(null_source)input.rho=nullptr;
    FluxAdmissibility::MeanThermoView means{};
    means.geometry_semantics=foreign_means?GridMetrics::GeometrySemantics::Existing:
        GridMetrics::GeometrySemantics::AxisymmetricRz;
    const int tangent=lane;
    using namespace arch::cuda;
    if(method==0)detail::hydro_native_face_math<CudaPcmReconstruction,CudaHllcFlux>(
        input,output,grid,IdealGasView{},direction,i+(direction==1?tangent:0),
        j+(direction==0?tangent:0),0.,bounds,scratch,lane,failure,&means,walls);
    else if(method==1)detail::hydro_native_face_math<CudaMusclReconstruction<McLimiter>,CudaHllcFlux>(
        input,output,grid,IdealGasView{},direction,i+(direction==1?tangent:0),
        j+(direction==0?tangent:0),0.,bounds,scratch,lane,failure,&means,walls);
    else detail::hydro_native_face_math<CudaPpmReconstruction,CudaHllcFlux>(
        input,output,grid,IdealGasView{},direction,i+(direction==1?tangent:0),
        j+(direction==0?tangent:0),0.,bounds,scratch,lane,failure,&means,walls);
}

/** Antiderivative-built Native cold solid rotation tests the real face adapter.
 * V=(h²-l²)/2, W=(h³-l³)/3, I=(h⁴-l⁴)/4; rho=1, J/W=sign*I/W,
 * E_V=e0+I/(2V)+v_z²/2. Independent axial flux is v_z*(E_V+P),
 * and phi flux uses W: v_z*J/W. Host/device parity uses the SAME compute;
 * these independent physical identities keep that parity from being its oracle.
 * Given analytic halos are not an authenticated Runtime BC/stage qualification.
 */
int run_native_selected_faces()
{
    using namespace arch::cuda;
    constexpr auto native=GridMetrics::GeometrySemantics::AxisymmetricRz;
    const auto checked=[](cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));};
    const auto require=[](bool v,const char* m){if(!v)throw std::runtime_error(m);};
    const auto bits=[](const std::vector<double>& a,const std::vector<double>& b) {
        if(a.size()!=b.size())return false;
        for(std::size_t n=0;n<a.size();++n)
            if(std::bit_cast<std::uint64_t>(a[n])!=std::bit_cast<std::uint64_t>(b[n]))return false;
        return true;
    };
    try {
        for(bool annulus:{false,true})for(int ns:{0,2,13}) {
            Grid root(amr::MAX_NG,annulus?1.:0.,annulus?3.:1.,-.5,.5,0.,1.,1,1,1);
            root.geometry="cylindrical";root.dim=2;
            amr::Block block;block.Reset();block.level=0;block.logical_x1=block.logical_x2=0;
            block.InitGeometry(root,(root.x1_max-root.x1_min)/amr::BLOCK_NX,
                (root.x2_max-root.x2_min)/amr::BLOCK_NY,1.,native);
            block.RequireNativeGeometryIdentity();const auto& hg=block.grid;
            auto grid=make_device_grid_view(hg,native);
            const auto geometry=GridMetrics::make_geometry_view(hg,native);
            const int total=grid.total_size;
            DeviceStateFixture input(total,ns),output(total,ns);
            require(input.valid&&output.valid,"native face state allocation");
            const double poison=std::numeric_limits<double>::quiet_NaN(),sentinel=123.;
            const double e0=annulus?1./64.:hg.dx1*hg.dx1/64.,vz=.125;
            for(int c=0;c<total;++c)input.store(c,{poison,poison,poison,poison,poison});
            for(int j=0;j<hg.GetTotalY();++j)for(int i=0;i<hg.GetTotalX();++i) {
                const int c=hg.GetIndex(i,j,0);
                const long double a=hg.GetFacePosL(i),b=hg.GetFacePosR(i);
                const long double l=std::min(std::abs(a),std::abs(b)),u=std::max(std::abs(a),std::abs(b));
                const long double V=(u*u-l*l)/2,W=(u*u*u-l*l*l)/3,I=(u*u*u*u-l*l*l*l)/4;
                input.store(c,{1.,0.,vz,double((b<=0?-1:1)*I/W),double(e0+.5L*I/V+.5L*vz*vz)});
                for(int s=0;s<ns;++s)input.set_species(s,c,ns==2?(s==0?.25:.75):
                    (s==0?.5:s==1?.5:s==12?1e-20:0.));
            }
            const auto rho=input.rho,mr=input.mom_u,mz=input.mom_v,phi=input.mom_w,
                energy=input.eng,enuc=input.enuc_rate,x=input.mass_fractions;
            require(input.upload(),"native face input upload");
            double* device_scratch=nullptr;int* failure=nullptr;
            const std::size_t capacity=std::max<std::size_t>(1,82*std::size_t(ns));
            checked(cudaMalloc(&device_scratch,capacity*sizeof(double)));
            checked(cudaMalloc(&failure,sizeof(int)));
            const detail::NativeFaceScratchView arena{device_scratch,capacity,2};
            const arch::state::Bounds bounds{1e-12,1e-12,1e20};
            auto reset=[&] {
                for(int c=0;c<total;++c){output.store(c,{sentinel,sentinel,sentinel,sentinel,sentinel});
                    for(int s=0;s<ns;++s)output.set_species(s,c,sentinel);}
                require(output.upload(),"native face output upload");checked(cudaMemset(failure,0,sizeof(int)));
            };
            auto execute=[&](const DeviceGridView& g,int method,int direction,int i,int j,
                arch::state::Bounds lim,detail::NativeFaceScratchView memory,
                arch::boundary::HydroBoundaryView walls={},bool foreign=false,int lanes=1,bool null_source=false) {
                reset();native_selected_face_test_kernel<<<1,2>>>(input.view,output.view,g,method,direction,
                    i,j,lim,memory,failure,walls,foreign,lanes,null_source);checked(cudaGetLastError());
                checked(cudaDeviceSynchronize());require(output.download(),"native face output download");
                int failed=0;checked(cudaMemcpy(&failed,failure,sizeof(int),cudaMemcpyDeviceToHost));return failed;
            };
            auto host_face=[&]<class Policy>(int direction,int i,int j,arch::boundary::HydroBoundaryView walls) {
                std::vector<double> work(std::max(1,41*ns));double* q=ns?work.data():nullptr;
                RzNativeFaceFlux::Scratch scratch{q,ns?q+16*ns:nullptr,19*std::size_t(ns),
                    ns?q+35*ns:nullptr,ns?q+36*ns:nullptr,ns?q+37*ns:nullptr,
                    ns?q+38*ns:nullptr,ns?q+39*ns:nullptr};
                RzSelectedReconstruction::Context context{geometry,hg.GetTotalX(),hg.GetTotalY(),i,j,direction,ns,bounds};
                const auto read=[&](int c){return input.load(c);};
                const auto fraction=[&](int s,int c){return input.species(s,c);};
                FluidVector flux{};std::vector<double> species(ns);
                const int left=hg.GetIndex(i,j,0),right=left+(direction==0?1:hg.stride_y);
                FluxAdmissibility::MeanThermoView means{};means.geometry_semantics=native;
                const auto status=RzNativeFaceFlux::compute<FluxHLLC<PCMReconstruction>,Policy>(
                    read,fraction,context,IdealGasView{},0.,&means,left,right,scratch,flux,
                    ns?q+40*ns:nullptr,walls);
                require(status==arch::state::Status::valid,"native host selected face failed");
                for(int s=0;s<ns;++s)species[s]=q[40*ns+s];
                return std::pair{flux,species};
            };
            for(int method=0;method<3;++method)for(int direction=0;direction<2;++direction) {
                const int i=grid.is,j=grid.js+4;
                require(execute(grid,method,direction,i,j,bounds,arena,{},false,2)==0,
                    "native configured face rejected valid cold source");
                for(int lane=0;lane<2;++lane) {
                    const int fi=i+(direction==1?lane:0),fj=j+(direction==0?lane:0);
                    const auto expected=method==0?host_face.template operator()<PCMReconstruction>(direction,fi,fj,{}):
                        method==1?host_face.template operator()<MusclReconstruction<McLimiter>>(direction,fi,fj,{}):
                        host_face.template operator()<PPMReconstruction>(direction,fi,fj,{});
                    const int c=grid.index(fi,fj)+grid.stride(direction);
                    require(vector_near(output.load(c),expected.first),"native selected CPU/device face parity");
                    for(int s=0;s<ns;++s)require(scalar_near(output.species(s,c),expected.second[s]),
                        "native selected rhoX CPU/device parity");
                    if(method==0&&direction==1) {
                        const long double l=hg.GetFacePosL(fi),u=hg.GetFacePosR(fi);
                        const long double V=(u*u-l*l)/2,W=(u*u*u-l*l*l)/3,I=(u*u*u*u-l*l*l*l)/4;
                        const FluidVector independent{vz,0.,vz*vz+.4*e0,double(vz*I/W),
                            double(vz*(1.4L*e0+.5L*I/V+.5L*vz*vz))};
                        require(vector_near(output.load(c),independent),"native PCM true axial V/W antiderivative flux");
                        for(int s=0;s<ns;++s)require(scalar_near(output.species(s,c),vz*input.species(s,c)),
                            "native PCM species flux changed retained fractions");
                        if(ns==13) {
                            const double trace=vz*input.species(12,c);
                            require(output.species(12,c)>0.&&std::abs(output.species(12,c)-trace)
                                <=64.*std::numeric_limits<double>::epsilon()*std::abs(trace),
                                "native face lost or changed independently known positive trace flux");
                        }
                    }
                }
            }
            // A real 2:1 face flag selects the shared MinMod policy, not NG.
            auto coarse=grid;coarse.amr_coarse_fine_face[0]=1;
            require(execute(coarse,2,0,grid.is,grid.js+4,bounds,arena)==0,"native coarse-fine selected fallback");
            const auto fallback=host_face.template operator()<MusclReconstruction<MinMod>>(0,grid.is,grid.js+4,{});
            require(vector_near(output.load(grid.index(grid.is+1,grid.js+4)),fallback.first),"native actual MinMod fallback differs shared host");
            if(annulus&&ns==2)for(int face=0;face<4;++face) {
                const int dir=face/2,side=face%2;
                const int i=dir==0?(side?grid.ie-1:grid.is-1):grid.is+4;
                const int j=dir==1?(side?grid.je-1:grid.js-1):grid.js+4;
                arch::boundary::HydroBoundaryView walls{};walls.reflecting[face]=true;
                for(int method=0;method<3;++method) {
                    require(execute(grid,method,dir,i,j,bounds,arena,walls)==0,"native physical reflecting wall face failed");
                    const auto f=output.load(grid.index(i,j)+grid.stride(dir));
                    require(scalar_near(f.rho,0.)&&scalar_near(f.eng,0.)&&scalar_near(f.mom_w,0.)
                        &&scalar_near(dir==0?f.mom_v:f.mom_u,0.)
                        &&std::isfinite(dir==0?f.mom_u:f.mom_v)&&(dir==0?f.mom_u:f.mom_v)>0.,
                        "native mirror wall lost zero advection/positive pressure traction");
                    for(int s=0;s<ns;++s)require(scalar_near(output.species(s,grid.index(i,j)+grid.stride(dir)),0.),
                        "native reflecting wall transported species");
                }
            }
            auto unchanged=[&] {
                for(int c=0;c<total;++c) {
                    const auto f=output.load(c);
                    require(f.rho==sentinel&&f.mom_u==sentinel&&f.mom_v==sentinel
                        &&f.mom_w==sentinel&&f.eng==sentinel,"rejected native face wrote real flux");
                    for(int s=0;s<ns;++s)require(output.species(s,c)==sentinel,"rejected native face wrote rhoX flux");
                }
            };
            auto bad=grid;bad.semantics=static_cast<GridMetrics::GeometrySemantics>(255);
            require(execute(bad,0,0,grid.is,grid.js+4,bounds,arena,{},false,1,true)!=0,"unknown native face chart accepted");unchanged();
            auto bad_bounds=bounds;bad_bounds.internal_max=std::numeric_limits<double>::quiet_NaN();
            require(execute(grid,0,0,grid.is,grid.js+4,bad_bounds,arena)!=0,"native malformed physical Bounds accepted");unchanged();
            require(execute(grid,0,0,grid.is,grid.js+4,bounds,arena,{},true)!=0,"native reused foreign ordinary mean cache");unchanged();
            arch::boundary::HydroBoundaryView malformed_wall{};malformed_wall.reflecting[4]=true;
            require(execute(grid,0,0,grid.is,grid.js+4,bounds,arena,malformed_wall)!=0,
                "native RZ accepted an unsupported third-axis wall");unchanged();
            if(ns) {
                auto insufficient=arena;insufficient.capacity=82*std::size_t(ns)-1;
                require(execute(grid,0,0,grid.is,grid.js+4,bounds,insufficient)!=0,"native insufficient 41*S/lane arena accepted");unchanged();
            }
            require(input.download(),"native face immutable input download");
            require(bits(input.rho,rho)&&bits(input.mom_u,mr)&&bits(input.mom_v,mz)&&bits(input.mom_w,phi)
                &&bits(input.eng,energy)&&bits(input.enuc_rate,enuc)&&bits(input.mass_fractions,x),
                "native face changed source/halo/pitch/species bits");
            cudaFree(failure);cudaFree(device_scratch);
        }
        std::cout<<"NATIVE_CUDA_SELECTED_FACE_LEAF_PASS layouts=2 policies=PCM,MC,PPM species=0,2,13 runtime_authority=0 physical_qualification=0\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 200;}
}

/** Actual shared point-wall assembly on Host and in a CUDA kernel.
 * The at-rest reference is F=(0,P*n,0), with zero species advection. The
 * optional negative high trial is deliberate NaN input, not a failed EOS.
 * This leaf grants no Runtime wall, native geometry or rollback capability.
 */
struct SharedWallLeafResult { int cases=0, failures=0; };

/** Read true immutable base composition after the selected high callback. */
struct SharedWallBaseSpecies {
    int* high_calls;
    int* base_reads;
    int* order_errors;
    ARCH_INLINE double operator()(int species) const {
        if(*high_calls!=1) ++*order_errors;
        ++*base_reads;
        return species==0?.75:.25;
    }
};

/** Preserve the actual HLLC/PCM high policy or supply an optional NaN trial. */
struct SharedWallHighPolicy {
    int direction, species, trial;
    int* high_calls;
    int* order_errors;
    template<class Eos>
    ARCH_INLINE void operator()(const FluidVector& left,const FluidVector& right,
        const double* xl,const double* xr,const Eos& eos,
        FluidVector& high,double* species_flux) const {
        ++*high_calls;
        if(species && (xl[0]!=.25||xr[0]!=.25)) ++*order_errors;
        if(trial) {
            high={arch::state::invalid(),0.,0.,0.,arch::state::invalid()};
            for(int s=0;s<species;++s)species_flux[s]=arch::state::invalid();
        } else {
            FluxHLLC<PCMReconstruction>::compute_face_flux(left,right,xl,xr,
                species,eos,direction,1.,high,species_flux);
        }
    }
};

/** Exercise all point directions/sides and required-output rejection. */
ARCH_INLINE SharedWallLeafResult evaluate_shared_wall_assembly() {
    using Status=StationarySlipWallFlux::PointWallStatus;
    constexpr double pressure=.00625;
    const double sound=std::sqrt(1.4*pressure);
    const FluidVector point{1.,0.,0.,0.,pressure/.4};
    const FluidVector sentinel{123.,456.,789.,123.,456.};
    const IdealGasView eos{};
    SharedWallLeafResult result{};
    for(int dir=0;dir<3;++dir)for(int side=0;side<2;++side)
        for(int species_case=0;species_case<2;++species_case)for(int trial=0;trial<2;++trial) {
            const int species=species_case?2:0;
            double left[2]{.25,.75},right[2]{.25,.75},low[2]{123.,456.},high_species[2]{123.,456.};
            int high_calls=0,base_reads=0,order_errors=0;
            const SharedWallBaseSpecies read_base{&high_calls,&base_reads,&order_errors};
            const SharedWallHighPolicy selected{dir,species,trial,&high_calls,&order_errors};
            auto output=sentinel;
            const auto status=StationarySlipWallFlux::assemble_point(point,point,point,point,
                dir,side,species,pressure,sound,eos,species?left:nullptr,
                species?right:nullptr,species?low:nullptr,species?high_species:nullptr,
                read_base,selected,output);
            const double normal=dir==0?output.mom_u:dir==1?output.mom_v:output.mom_w;
            const bool valid=status==Status::valid&&high_calls==1&&base_reads==species
                &&order_errors==0&&output.rho==0.&&output.eng==0.
                &&std::isfinite(normal)
                &&std::abs(normal-pressure)<=arch::state::composition_roundoff_limit*pressure
                &&(dir==0||output.mom_u==0.)&&(dir==1||output.mom_v==0.)
                &&(dir==2||output.mom_w==0.)
                &&(!species||(high_species[0]==0.&&high_species[1]==0.
                    &&left[0]==.75&&right[0]==.75));
            ++result.cases;if(!valid)++result.failures;
        }
    for(int negative=0;negative<2;++negative) {
        double left[2]{.25,.75},right[2]{.25,.75},low[2]{123.,456.},high_species[2]{123.,456.};
        int high_calls=0,base_reads=0,order_errors=0;
        const SharedWallBaseSpecies read_base{&high_calls,&base_reads,&order_errors};
        const SharedWallHighPolicy selected{0,2,0,&high_calls,&order_errors};
        auto output=sentinel;
        const auto status=StationarySlipWallFlux::assemble_point(point,point,point,point,
            negative?0:3,0,2,negative?arch::state::invalid():pressure,sound,eos,
            left,right,low,high_species,read_base,selected,output);
        const bool valid=status!=Status::valid&&output.rho==sentinel.rho
            &&output.mom_u==sentinel.mom_u&&output.mom_v==sentinel.mom_v
            &&output.mom_w==sentinel.mom_w&&output.eng==sentinel.eng
            &&(negative||high_calls==0);
        ++result.cases;if(!valid)++result.failures;
    }
    return result;
}

/** Launch the same mathematical leaf using genuine device-local scratch. */
__global__ void shared_wall_assembly_kernel(SharedWallLeafResult* output) {
    if(blockIdx.x==0&&threadIdx.x==0)*output=evaluate_shared_wall_assembly();
}

/** Check Host and copied device evidence against the same physical identities. */
int run_shared_point_wall_assembler() {
    const auto host=evaluate_shared_wall_assembly();
    SharedWallLeafResult* device=nullptr;
    const auto allocation=cudaMalloc(&device,sizeof(SharedWallLeafResult));
    if(allocation!=cudaSuccess)return 201;
    shared_wall_assembly_kernel<<<1,1>>>(device);
    const auto launch=cudaGetLastError();
    const auto sync=cudaDeviceSynchronize();
    SharedWallLeafResult copied{};
    const auto transfer=cudaMemcpy(&copied,device,sizeof(copied),cudaMemcpyDeviceToHost);
    const auto release=cudaFree(device);
    if(launch!=cudaSuccess||sync!=cudaSuccess||transfer!=cudaSuccess||release!=cudaSuccess
        ||host.cases!=26||copied.cases!=host.cases||host.failures||copied.failures) {
        std::cerr<<"shared point-wall leaf: host="<<host.failures<<'/'<<host.cases
            <<" device="<<copied.failures<<'/'<<copied.cases<<'\n';return 202;
    }
    std::cout<<"SHARED_POINT_WALL_ASSEMBLER_LEAF_PASS host_cases=26 device_cases=26 "
        "optional_trial_negative=1 runtime_authority=0 native_authority=0\n";
    return 0;
}

#endif
} // namespace

int main()
{
    DeviceLeafResult range_result{};
    evaluate_low_density(range_result);
    if (!range_result.low_density_valid || range_result.low_density_error > 1e-11) return 3;
    const auto identities = RoeThermodynamicCases::evaluate();
    std::cout << "Roe identities reflection=" << identities.reflection
              << " rotation=" << identities.rotation
              << " ideal_sound_speed=" << identities.ideal_sound_speed
              << " ideal_pressure_jump=" << identities.ideal_pressure_jump
              << " stationary_transport=" << identities.stationary_transport << '\n';
    if (!identities.passed())
        return 2;
    if (!validate_characterized_host_paths()) {
        std::cerr << "Independent Host characterization failed before any device launch\n";
        return 1;
    }
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
        species_delta[cell] = .2*.25;
        species_delta[total + cell] = .2*.75;
        old_state.X(0, cell) = current_state.X(0, cell) = 0.25;
        old_state.X(1, cell) = current_state.X(1, cell) = 0.75;
        destination.enuc_rate[cell] = 17.0;
    }
    TimeIntegration::perform_stage_update(
        old_state, current_state, destination, delta, species_delta, grid,
        0.5, 0.5, 1e-12, 1e-10, 1e20);
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
    const int repair_result = run_concurrent_repairs();
    if (repair_result != 0) return repair_result;
    const int primitive_result = run_device_primitives();
    if (primitive_result != 0)
        return primitive_result;
    const int route_result = run_route_matrix();
    if (route_result != 0)
        return route_result;
    const int native_result=run_native_cfl_divergence();
    if(native_result!=0)return native_result;
    const int mean_result=run_native_required_mean_cache();
    if(mean_result!=0)return mean_result;
    const int selected_face_result=run_native_selected_faces();
    if(selected_face_result!=0)return selected_face_result;
    const int wall_assembly_result=run_shared_point_wall_assembler();
    if(wall_assembly_result!=0)return wall_assembly_result;
#endif
    return 0;
}
