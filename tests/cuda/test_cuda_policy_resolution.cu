#include "driver/dispatch/PolicyDescriptor.h"

#include "cuda/diffusion/DiffusionKernels.cuh"
#include "cuda/hydro/HydroFluxPolicies.cuh"
#include "cuda/hydro/HydroReconstructionPolicies.cuh"
#include "cuda/hydro/HydroStageKernels.cuh"
#include "cuda/microphysics/microphysics_api.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/reconstruction/Limiters.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

#include <cuda_runtime.h>

#include <bit>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <type_traits>

namespace {

using namespace arch::dispatch;

struct OdeRouteFingerprint
{
    std::uint64_t state_hash{};
    std::uint64_t dt_bits{};
    int status{};
    int attempted_substeps{};
    int rejected_substeps{};
};

struct OdeProbeEos
{
    ARCH_INLINE double get_eint_from_T(double, double temperature,
                                       const double*) const
    { return temperature; }
    ARCH_INLINE double get_cv(double, double, const double*) const { return 1.0; }
    ARCH_INLINE double get_eta(double, double, const double*) const { return 0.0; }
};

struct OdeProbeNet
{
    static constexpr int NUM_SPECIES = 2;
    static constexpr int ODE_NEQ = 3;
    static constexpr double ENERGY_CONVERSION = 1.0;

    ARCH_INLINE static constexpr double aion(int) { return 1.0; }
    ARCH_INLINE static constexpr double zion(int) { return 0.5; }
    ARCH_INLINE static constexpr double binding_energy(int) { return 0.0; }
    ARCH_INLINE static constexpr double spin_weight(int) { return 0.0; }
    ARCH_INLINE static constexpr double energy_weight(int) { return 0.0; }

    ARCH_INLINE static void eval_rhs(
        const double* state, double, double, double* rhs, double& enuc)
    {
        rhs[0] = -0.02 * state[0];
        rhs[1] = 0.02 * state[0];
        enuc = 0.0;
    }

    template <class Matrix>
    ARCH_INLINE static void eval_jacobian(
        const double*, double, double, Matrix& matrix, double* denuc)
    {
        matrix.set(1, 1, -0.02);
        matrix.set(2, 1, 0.02);
        denuc[0] = 0.0;
        denuc[1] = 0.0;
    }

    ARCH_INLINE static void eval_temperature_derivative(
        const double*, double, double, double* rhs, double& denuc)
    {
        rhs[0] = 0.0;
        rhs[1] = 0.0;
        denuc = 0.0;
    }
};

ARCH_INLINE BurnConfigView ode_probe_config()
{
    return {true, 0.1, 0.1, 0.1, 1.0e-30, 1.0e30,
            false, 1.0e20, 1.0e20, true,
            {1.0e-8, 1.0e-12, 50, 20, 0.9, 2.0, 0.1, 1.0,
             false, false}};
}

ARCH_INLINE std::uint64_t ode_state_hash(const double* values, int count)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (int i = 0; i < count; ++i) {
        hash ^= std::bit_cast<std::uint64_t>(values[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <class Binding>
ARCH_INLINE double policy_witness(
    double* storage, arch::cuda::BurnOdeMatrixWorkspace& ode_workspace,
    OdeRouteFingerprint& ode_fingerprint)
{
    IdealGasView eos{};
    eos.global_gamma = 1.4;

    if constexpr (std::is_same_v<Binding, CudaVlBinding>
                  || std::is_same_v<Binding, CudaSwBinding>
                  || std::is_same_v<Binding, CudaRoeBinding>
                  || std::is_same_v<Binding, CudaHllBinding>
                  || std::is_same_v<Binding, CudaHllcBinding>) {
        using Flux = std::conditional_t<std::is_same_v<Binding, CudaVlBinding>, arch::cuda::CudaVlFlux,
            std::conditional_t<std::is_same_v<Binding, CudaSwBinding>, arch::cuda::CudaSwFlux,
            std::conditional_t<std::is_same_v<Binding, CudaRoeBinding>, arch::cuda::CudaRoeFlux,
            std::conditional_t<std::is_same_v<Binding, CudaHllBinding>, arch::cuda::CudaHllFlux,
                               arch::cuda::CudaHllcFlux>>>>;
        FluidVector flux{};
        const FluidVector left{1.0, 0.7, 0.1, -0.05, 3.4};
        const FluidVector right{0.42, -0.08, 0.02, 0.03, 1.1};
        Flux::compute(left, right, nullptr, nullptr, 0, eos, 0, 0.9, flux, nullptr);
        return flux.rho + 0.13 * flux.mom_u + 0.017 * flux.eng;
    } else if constexpr (std::is_same_v<Binding, CudaPcmBinding>
                         || std::is_same_v<Binding, CudaMusclBinding>
                         || std::is_same_v<Binding, CudaPpmBinding>) {
        for (int i = 0; i < 8; ++i) {
            storage[i] = 1.0 + 0.07 * i + 0.011 * i * i;
            storage[8 + i] = 0.2 + 0.03 * i;
            storage[16 + i] = -0.1 + 0.02 * i;
            storage[24 + i] = 0.05 - 0.004 * i;
            storage[32 + i] = 2.5 + 0.2 * i + 0.03 * i * i;
            storage[40 + i] = 0.0;
        }
        arch::cuda::DeviceStateView state{
            storage, storage + 8, storage + 16, storage + 24,
            storage + 32, storage + 40, nullptr, 8, 0};
        FluidVector left{}, right{};
        double species_left[1]{}, species_right[1]{}, species_cell[1]{};
        if constexpr (std::is_same_v<Binding, CudaPcmBinding>)
            arch::cuda::CudaPcmReconstruction::reconstruct(
                state, 2, 1, eos, left, right,
                species_left, species_right, species_cell);
        else if constexpr (std::is_same_v<Binding, CudaMusclBinding>)
            arch::cuda::CudaMusclReconstruction<McLimiter>::reconstruct(
                state, 2, 1, eos, left, right,
                species_left, species_right, species_cell);
        else
            arch::cuda::CudaPpmReconstruction::reconstruct(
                state, 2, 1, eos, left, right,
                species_left, species_right, species_cell);
        return left.rho + 0.3 * right.rho + 0.01 * left.eng;
    } else if constexpr (std::is_same_v<Binding, CudaMinModBinding>) {
        return MinMod::calc(0.37);
    } else if constexpr (std::is_same_v<Binding, CudaMcBinding>) {
        return McLimiter::calc(0.37);
    } else if constexpr (std::is_same_v<Binding, CudaSuperBeeBinding>) {
        return SuperBee::calc(0.37);
    } else if constexpr (std::is_same_v<Binding, CudaVanLeerLimiterBinding>) {
        return VanLeer::calc(0.37);
    } else if constexpr (std::is_same_v<Binding, CudaEulerBinding>
                         || std::is_same_v<Binding, CudaRk2Binding>
                         || std::is_same_v<Binding, CudaRk3Binding>) {
        const FluidVector old_state{1.0, 0.2, 0.1, -0.1, 3.0};
        const FluidVector current{1.2, 0.3, 0.05, -0.02, 3.4};
        const FluidVector delta{0.1, 0.02, -0.01, 0.03, 0.4};
        FluidVector result{};
        const double old_weight = std::is_same_v<Binding, CudaEulerBinding> ? 0.0
            : (std::is_same_v<Binding, CudaRk2Binding> ? 0.5 : 0.75);
        const double flux_weight = std::is_same_v<Binding, CudaEulerBinding> ? 1.0
            : (std::is_same_v<Binding, CudaRk2Binding> ? 0.5 : 0.25);
        TimeIntegration::update_stage_cell(
            old_state, current, delta, nullptr, nullptr, nullptr, 0, 1,
            old_weight, flux_weight, 1e-12, 1e21, result, nullptr);
        return result.rho + 0.1 * result.eng;
    } else if constexpr (std::is_same_v<Binding, CudaIdealBinding>) {
        return eos.get_pressure_from_rho_e(1.7, 2.3, nullptr);
    } else if constexpr (std::is_same_v<Binding, CudaHelmholtzBinding>) {
        HelmEosView view{};
        return view.get_gamma(nullptr) * 2.1;
    } else if constexpr (std::is_same_v<Binding, CudaTabular3DBinding>) {
        Tabular3DEOSView view{};
        return view.fallback_pressure(1.7, 2.3);
    } else if constexpr (std::is_same_v<Binding, CudaTabular4DBinding>) {
        Tabular4DEOSView view{};
        return view.fallback_pressure(1.9, 2.3);
    } else if constexpr (std::is_same_v<Binding, CudaAprox13Binding>) {
        return NetAprox13::binding_energy(1)
            + 0.001 * NetAprox13::NUM_SPECIES + 0.00001 * NetAprox13::ODE_NEQ;
    } else if constexpr (std::is_same_v<Binding, CudaAprox19Binding>) {
        return NetAprox19::binding_energy(1)
            + 0.001 * NetAprox19::NUM_SPECIES + 0.00001 * NetAprox19::ODE_NEQ;
    } else if constexpr (std::is_same_v<Binding, CudaAprox21Binding>) {
        return NetAprox21::binding_energy(1)
            + 0.001 * NetAprox21::NUM_SPECIES + 0.00001 * NetAprox21::ODE_NEQ;
    } else if constexpr (std::is_same_v<Binding, CudaIso7Binding>) {
        return NetIso7::binding_energy(1)
            + 0.001 * NetIso7::NUM_SPECIES + 0.00001 * NetIso7::ODE_NEQ;
    } else if constexpr (std::is_same_v<Binding, CudaBeNrBinding>
                         || std::is_same_v<Binding, CudaBdBinding>
                         || std::is_same_v<Binding, CudaRos4Binding>) {
        arch::cuda::BurnPolicyCell cell{};
        cell.fluid.rho = 1.0;
        cell.state[0] = 0.75;
        cell.state[1] = 0.25;
        cell.state[2] = 2.0;
        cell.burn_dt = 0.5;
        if constexpr (std::is_same_v<Binding, CudaBeNrBinding>)
            arch::cuda::execute_ode_policy<OdeProbeNet, Solver_BE_NR>(
                cell, ode_workspace, OdeProbeEos{}, ode_probe_config());
        else if constexpr (std::is_same_v<Binding, CudaBdBinding>)
            arch::cuda::execute_ode_policy<OdeProbeNet, Solver_BD>(
                cell, ode_workspace, OdeProbeEos{}, ode_probe_config());
        else
            arch::cuda::execute_ode_policy<OdeProbeNet, Solver_ROS4>(
                cell, ode_workspace, OdeProbeEos{}, ode_probe_config());
        ode_fingerprint.state_hash = ode_state_hash(cell.state, OdeProbeNet::ODE_NEQ);
        ode_fingerprint.dt_bits =
            std::bit_cast<std::uint64_t>(cell.dt_recommended);
        ode_fingerprint.status = static_cast<int>(cell.ode.status);
        ode_fingerprint.attempted_substeps = cell.ode.attempted_substeps;
        ode_fingerprint.rejected_substeps = cell.ode.rejected_substeps;
        return cell.state[0] + 0.1 * cell.state[1]
            + 0.001 * cell.dt_recommended
            + 0.0001 * cell.ode.attempted_substeps
            + 0.00001 * cell.ode.rejected_substeps;
    } else if constexpr (std::is_same_v<Binding, CudaDenseLuBinding>) {
        DenseMatrixData matrix{};
        matrix.set(1, 1, 3.0); matrix.set(1, 2, 1.0);
        matrix.set(2, 1, 1.0); matrix.set(2, 2, 2.0);
        double rhs[BurnLimits::MAX_ODE_NEQ]{};
        rhs[0] = 9.0; rhs[1] = 8.0;
        return DenseLUSolver::solve<2, BurnLimits::MAX_ODE_NEQ>(matrix, rhs)
            ? rhs[0] + 0.1 * rhs[1] : -1.0;
    } else if constexpr (std::is_same_v<Binding, CudaRkl1Binding>
                         || std::is_same_v<Binding, CudaRkl2Binding>) {
        const FluidVector base{1.0, 0.2, -0.1, 0.05, 3.0};
        const FluidVector increment{0.1, -0.03, 0.02, 0.01, 0.4};
        FluidVector result{};
        const double coefficient = std::is_same_v<Binding, CudaRkl1Binding> ? 0.2 : 0.35;
        Numerics::Diffusion::detail::apply_first_rkl_stage_cell(
            base, nullptr, increment, nullptr, 0, 1, coefficient, result, nullptr);
        return result.rho + 0.1 * result.eng;
    } else if constexpr (std::is_same_v<Binding, CudaNoNetworkBinding>
                         || std::is_same_v<Binding, CudaNoOdeBinding>
                         || std::is_same_v<Binding, CudaNoLinearBinding>
                         || std::is_same_v<Binding, CudaNoDiffusionBinding>) {
        return -0.5;
    } else {
        return -999.0;
    }
}

template <class Registration>
__global__ void route_kernel(
    int* ids, double* values, double* storage,
    arch::cuda::BurnOdeMatrixWorkspace* ode_workspaces,
    OdeRouteFingerprint* ode_fingerprints, int index)
{
    if (blockIdx.x != 0 || threadIdx.x != 0) return;
    using Binding = typename PolicyRegistration<Registration>::CudaBinding;
    ids[index] = static_cast<int>(PolicyRegistration<Registration>::id);
    values[index] = policy_witness<Binding>(
        storage + index * 48, ode_workspaces[index], ode_fingerprints[index]);
}

template <class List>
struct ListLauncher;

template <class Id, UnknownPolicyBehavior Behavior, class... Registrations>
struct ListLauncher<TypeList<Id, Behavior, Registrations...>>
{
    static void launch(
        int* ids, double* values, double* storage,
        arch::cuda::BurnOdeMatrixWorkspace* ode_workspaces,
        OdeRouteFingerprint* ode_fingerprints, int& index)
    {
        ([&] {
            using Binding = typename PolicyRegistration<Registrations>::CudaBinding;
            if constexpr (!std::is_same_v<Binding, AbsentBinding>) {
                route_kernel<Registrations><<<1, 1>>>(
                    ids, values, storage, ode_workspaces,
                    ode_fingerprints, index++);
            }
        }(), ...);
    }
};

void check(cudaError_t error, const char* operation)
{
    if (error != cudaSuccess) {
        std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(error));
        std::exit(2);
    }
}

} // namespace

int main()
{
    constexpr int maximum_routes = 40;
    int* device_ids = nullptr;
    double* device_values = nullptr;
    double* device_storage = nullptr;
    arch::cuda::BurnOdeMatrixWorkspace* device_ode_workspaces = nullptr;
    OdeRouteFingerprint* device_ode_fingerprints = nullptr;
    check(cudaMalloc(&device_ids, maximum_routes * sizeof(int)), "cudaMalloc(ids)");
    check(cudaMalloc(&device_values, maximum_routes * sizeof(double)), "cudaMalloc(values)");
    check(cudaMalloc(&device_storage, maximum_routes * 48 * sizeof(double)), "cudaMalloc(storage)");
    check(cudaMalloc(&device_ode_workspaces,
                     maximum_routes * sizeof(arch::cuda::BurnOdeMatrixWorkspace)),
          "cudaMalloc(ode workspaces)");
    check(cudaMalloc(&device_ode_fingerprints,
                     maximum_routes * sizeof(OdeRouteFingerprint)),
          "cudaMalloc(ode fingerprints)");
    check(cudaMemset(device_ode_fingerprints, 0,
                     maximum_routes * sizeof(OdeRouteFingerprint)),
          "clear ode fingerprints");

    int count = 0;
    ListLauncher<FluxPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    ListLauncher<ReconstructionPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    ListLauncher<LimiterPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    ListLauncher<TimeIntegratorPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    ListLauncher<EosPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    ListLauncher<NetworkPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    ListLauncher<OdeSolverPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    ListLauncher<LinearSolverPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    ListLauncher<DiffusionIntegratorPolicies>::launch(device_ids, device_values, device_storage, device_ode_workspaces, device_ode_fingerprints, count);
    check(cudaGetLastError(), "route launches");
    check(cudaDeviceSynchronize(), "route synchronize");

    int ids[maximum_routes]{};
    double values[maximum_routes]{};
    OdeRouteFingerprint ode_fingerprints[maximum_routes]{};
    check(cudaMemcpy(ids, device_ids, count * sizeof(int), cudaMemcpyDeviceToHost), "copy ids");
    check(cudaMemcpy(values, device_values, count * sizeof(double), cudaMemcpyDeviceToHost), "copy values");
    check(cudaMemcpy(ode_fingerprints, device_ode_fingerprints,
                     count * sizeof(OdeRouteFingerprint), cudaMemcpyDeviceToHost),
          "copy ode fingerprints");
    check(cudaFree(device_ode_fingerprints), "cudaFree(ode fingerprints)");
    check(cudaFree(device_ode_workspaces), "cudaFree(ode workspaces)");
    check(cudaFree(device_storage), "cudaFree(storage)");
    check(cudaFree(device_values), "cudaFree(values)");
    check(cudaFree(device_ids), "cudaFree(ids)");

    if (count != 33) {
        std::fprintf(stderr, "route count mismatch: %d\n", count);
        return 1;
    }
    constexpr double expected_values[] = {
        0.90457253867564613, 1.013305618008822, 1.0233133021289982,
        1.0385394119668141, 1.0148855113826238,
        1.6069000000000002, 1.6451000000000002, 1.6475335300217602,
        0.37, 0.68500000000000005, 0.73999999999999999,
        0.54014598540145986,
        1.6800000000000002, 1.49, 1.395,
        1.5639999999999994, 2.9399999999999999, 2.6066666666666669,
        2.9133333333333331,
        -0.5, 92.176080000000013, 7.7373900000000004,
        7.7394099999999995, 92.170020000000008,
        -0.5, 0.76931683168316833, 0.76928363778068864,
        0.76898601719931348,
        -0.5, 2.2999999999999998,
        -0.5, 1.3280000000000001, 1.349};
    static_assert(std::size(expected_values) == 33);
    for (int i = 25; i <= 27; ++i)
        std::printf("ode[%d] state=%016llx dt=%016llx status=%d attempts=%d rejected=%d\n",
                    i,
                    static_cast<unsigned long long>(ode_fingerprints[i].state_hash),
                    static_cast<unsigned long long>(ode_fingerprints[i].dt_bits),
                    ode_fingerprints[i].status,
                    ode_fingerprints[i].attempted_substeps,
                    ode_fingerprints[i].rejected_substeps);
    for (int i = 25; i <= 27; ++i)
        std::printf("ode_route_value[%d]=%.17g\n", i, values[i]);
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(values[i]) || values[i] == -999.0) {
            std::fprintf(stderr, "route %d id=%d did not execute a real binding: %.17g\n",
                         i, ids[i], values[i]);
            return 1;
        }
        if (std::bit_cast<std::uint64_t>(values[i])
            != std::bit_cast<std::uint64_t>(expected_values[i])) {
            std::fprintf(stderr,
                         "route numeric fingerprint mismatch at %d id=%d expected=%.17g actual=%.17g\n",
                         i, ids[i], expected_values[i], values[i]);
            return 1;
        }
        std::printf("route[%d] id=%d value=%.17g\n", i, ids[i], values[i]);
    }
    constexpr OdeRouteFingerprint expected_ode_fingerprints[] = {
        {0xa79735bc40a33a77ULL, 0x3feccccccccccccdULL, 2, 1, 0},
        {0x857e1d0cc493fe93ULL, 0x3feccccccccccccdULL, 2, 1, 0},
        {0xd4884a9984a087d0ULL, 0x3fe346b134cf6d63ULL, 2, 1, 0},
    };
    for (int i = 25; i <= 27; ++i) {
        const OdeRouteFingerprint& actual = ode_fingerprints[i];
        const OdeRouteFingerprint& expected = expected_ode_fingerprints[i - 25];
        if (actual.state_hash != expected.state_hash
            || actual.dt_bits != expected.dt_bits
            || actual.status != expected.status
            || actual.attempted_substeps != expected.attempted_substeps
            || actual.rejected_substeps != expected.rejected_substeps) {
            std::fprintf(stderr,
                         "ODE route %d committed callable status/output/dt fingerprint drifted\n",
                         i);
            return 1;
        }
    }
    constexpr int group_sizes[] = {5, 3, 4, 3, 4, 5, 4, 2, 3};
    int group_offset = 0;
    for (int group_size : group_sizes) {
        for (int i = 0; i < group_size; ++i) {
            if (ids[group_offset + i] != i) {
                std::fprintf(stderr, "route identity mismatch at %d: %d\n",
                             group_offset + i, ids[group_offset + i]);
                return 1;
            }
            for (int j = i + 1; j < group_size; ++j) {
                if (values[group_offset + i] == values[group_offset + j]) {
                    std::fprintf(stderr,
                                 "route values are not discriminating at %d and %d: %.17g\n",
                                 group_offset + i, group_offset + j,
                                 values[group_offset + i]);
                    return 1;
                }
            }
        }
        group_offset += group_size;
    }
    constexpr auto linear = make_policy_descriptors<LinearSolverPolicies>();
    if (linear[2].cuda_supported) {
        std::fprintf(stderr, "SparseKLU route unexpectedly generated\n");
        return 1;
    }
    std::printf("cuda_policy_resolution routes=%d first=%.17g last=%.17g\n",
                count, values[0], values[count - 1]);
}
