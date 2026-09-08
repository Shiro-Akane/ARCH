// Focused end-to-end burn-cell witness, including temperature recovery and the
// Strang-half-step controller. The full-program matrix remains authoritative
// for hydro, block reduction and persistence; this test localizes its first
// divergence without introducing another mathematical implementation.
#include "core/RuntimeParams.h"
#include "cuda/microphysics/helm_eos_device_owner.h"
#include "cuda/microphysics/microphysics_api.h"
#include "numerics/burnsolver/ode_bd.h"
#include "physics/network/aprox13/NetAprox13.h"
#include "../math/DenseLuCases.h"

#include <cuda_runtime.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Net = NetAprox13;
using Cell = arch::cuda::BurnPolicyCell;
using Workspace = arch::cuda::BurnOdeMatrixWorkspaceFor<Net::ODE_NEQ>;
struct Trace {
    Cell cell{};
    double prepared_temperature = 0.0;
};
__global__ void linear_kernel(bool* success) {
    *success = DenseLuCases::mixed_units() && DenseLuCases::failure_controls();
}

void check(cudaError_t result) {
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}

template<class Eos>
ARCH_HOST_DEVICE void burn(Trace& trace, Workspace& workspace,
                           const Eos& eos, const BurnConfigView& config) {
    double prepared[Net::ODE_NEQ];
    for (int i = 0; i < Net::ODE_NEQ; ++i) prepared[i] = trace.cell.state[i];
    DriverBurn::prepare_burn_cell(trace.cell.fluid, prepared,
                                 Net::NUM_SPECIES, eos, config);
    trace.prepared_temperature = prepared[Net::NUM_SPECIES];
    arch::cuda::execute_burn_policy_cell<Net, Solver_BD>(
        trace.cell, workspace, eos, config);
}

__global__ void burn_kernel(Trace* traces, Workspace* workspaces,
                            HelmEosView eos, BurnConfigView config) {
    const int lane = threadIdx.x;
    if (lane < 2) burn(traces[lane], workspaces[lane], eos, config);
}

void print(int step, int half, const char* lane, const Trace& trace,
           const HelmEos& host_eos) {
    const auto& c = trace.cell;
    std::cout << step << ',' << half << ',' << lane << ',' << c.burn_dt << ','
              << trace.prepared_temperature << ',' << c.state[Net::NUM_SPECIES]
              << ',' << c.eint_old << ',' << c.eint_new << ',' << c.enuc_rate
              << ',' << c.limiter_candidate << ','
              << host_eos.get_eint_from_T(c.fluid.rho,
                     c.state[Net::NUM_SPECIES], c.state) << ','
              << c.ode.success() << '\n';
}

}

int main() {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count == 0) return 77;
    try {
        const std::string root = ARCH_SOURCE_DIR;
        auto cfg = RuntimeParams::Load(root + "/validation/burn/inputs/bd.par");
        if (cfg.Get<double>("tstep_change_factor", 2.0) != 2.0)
            throw std::runtime_error("canonical BD timestep progression changed");
        const auto config = make_burn_config_view(cfg.physics.burn);
        SpeciesManager species;
        Net::RegisterSpecies(species);
        HelmEos eos(root + "/" + cfg.physics.eos_table_path, &species);
        arch::cuda::HelmEosDeviceOwner owner(eos, nullptr);
        Trace cpu{}, gpu{};
        cpu.cell.fluid.rho = cfg.Get<double>("rho0", 1.0e7);
        std::vector<double> fractions;
        Net::SetupInitialFractions(cfg, species, fractions);
        for (int i = 0; i < Net::NUM_SPECIES; ++i)
            cpu.cell.state[i] = fractions[i];
        cpu.cell.state[Net::NUM_SPECIES] = cfg.Get<double>("temperature0", 3.0e9);
        cpu.cell.fluid.eng = cpu.cell.fluid.rho * eos.get_eint_from_T(
            cpu.cell.fluid.rho, cpu.cell.state[Net::NUM_SPECIES], cpu.cell.state);
        gpu = cpu;
        Workspace host_workspace{};
        Trace* device = nullptr;
        Workspace* workspaces = nullptr;
        check(cudaMalloc(&device, 2 * sizeof(Trace)));
        check(cudaMalloc(&workspaces, 2 * sizeof(Workspace)));
        bool* device_linear = nullptr;
        check(cudaMalloc(&device_linear, sizeof(bool)));
        linear_kernel<<<1, 1>>>(device_linear);
        check(cudaGetLastError());
        bool success = false;
        check(cudaMemcpy(&success, device_linear, sizeof(bool), cudaMemcpyDeviceToHost));
        check(cudaFree(device_linear));
        success = success && DenseLuCases::mixed_units() && DenseLuCases::failure_controls();
        std::cout << std::setprecision(17)
                  << "step,half,lane,burn_dt,T_in,T_out,e_old,e_new,enuc,limiter,host_e_at_output,success\n";
        for (int step = 1; step <= 10; ++step) {
            double cpu_min = DriverBurn::INACTIVE_LIMITER_CANDIDATE;
            double gpu_min = cpu_min;
            for (int half = 0; half < 2; ++half) {
                // The canonical ten-step case doubles its initial timestep.
                cpu.cell.burn_dt = std::ldexp(cfg.Get<double>("dt_init", 1.0e-16), step - 2);
                gpu.cell.burn_dt = cpu.cell.burn_dt;
                Trace lanes[2]{gpu, cpu}; // lane 1: independent same-input replay
                check(cudaMemcpy(device, lanes, sizeof(lanes), cudaMemcpyHostToDevice));
                burn(cpu, host_workspace, eos, config);
                burn_kernel<<<1, 2>>>(device, workspaces, owner.view(), config);
                check(cudaGetLastError());
                check(cudaMemcpy(lanes, device, sizeof(lanes), cudaMemcpyDeviceToHost));
                gpu = lanes[0];
                print(step, half, "cpu", cpu, eos);
                print(step, half, "gpu", gpu, eos);
                print(step, half, "gpu_cpu_input", lanes[1], eos);
                success = success && cpu.cell.ode.success() && gpu.cell.ode.success()
                    && lanes[1].cell.ode.success();
                cpu_min = DriverBurn::combine_burn_minimum(cpu_min, cpu.cell.limiter_candidate);
                gpu_min = DriverBurn::combine_burn_minimum(gpu_min, gpu.cell.limiter_candidate);
            }
            const double relative = std::abs(cpu_min - gpu_min) / std::abs(cpu_min);
            std::cout << "minimum," << step << ',' << cpu_min << ',' << gpu_min
                      << ',' << relative << '\n';
            success = success && relative <= 2.0e-8;
        }
        check(cudaFree(workspaces));
        check(cudaFree(device));
        return success ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
