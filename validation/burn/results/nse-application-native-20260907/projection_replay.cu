// Diagnostic only: replay the shared preparation/NSE/handoff leaves without
// compiling unrelated reaction RHS or ODE routes. The real application remains
// the acceptance authority for hydro, reductions, splitting and checkpoints.
#include "core/RuntimeParams.h"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/microphysics/helm_eos_device_owner.h"
#include "driver/DriverBurnPolicy.h"
#include "numerics/burnsolver/odeFunction.h"
#include "physics/network/aprox21/NetAprox21.h"

#include <cuda_runtime.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Network = NetAprox21;
struct Replay {
    FluidVector fluid{};
    double state[Network::ODE_NEQ]{};
    double temperature_in = 0.;
    double old_e = 0.;
    double q = 0.;
    double rate = 0.;
    double ye = 0.;
    bool success = false;
};

void checked(cudaError_t code) {
    if (code != cudaSuccess) throw std::runtime_error(cudaGetErrorString(code));
}

template<class Eos>
ARCH_HOST_DEVICE void advance(Replay& r, const Eos& eos,
                              BurnConfigView config, double dt) {
    const auto prepared = DriverBurn::prepare_burn_cell(
        r.fluid, r.state, Network::NUM_SPECIES, eos, config);
    r.temperature_in = r.state[Network::NUM_SPECIES];
    r.old_e = prepared.internal_energy;
    arch::math::CompensatedSum charge;
    for (int i = 0; i < Network::NUM_SPECIES; ++i)
        charge.add(r.state[i] * (Network::zion(i) / Network::aion(i)));
    r.ye = charge.value();
    double recommended = dt;
    r.success = prepared.disposition == DriverBurn::BurnCellDisposition::Ready
        && OdeMath::integrate_nse_state<Network>(
            r.state, r.fluid.rho, dt, eos, config, recommended, &r.q);
    if (!r.success) return;
    const auto handoff = DriverBurn::compute_burn_energy_handoff(
        r.fluid, r.state, Network::NUM_SPECIES, prepared.internal_energy,
        prepared.kinetic_energy, dt, eos, config, r.q);
    r.success = handoff.valid;
    r.rate = handoff.enuc_rate;
    if (r.success) DriverBurn::commit_burn_energy(r.fluid, handoff);
}

__global__ void replay_kernel(Replay* state, HelmEosView eos,
                              BurnConfigView config, double dt) {
    if (threadIdx.x < 2) advance(state[threadIdx.x], eos, config, dt);
}

void emit(int half, const char* lane, const Replay& r) {
    std::cout << half << ',' << lane << ',' << r.success << ','
              << r.temperature_in << ',' << r.state[Network::NUM_SPECIES]
              << ',' << r.old_e << ',' << r.q << ',' << r.rate << ','
              << r.fluid.eng << ',' << r.ye;
    for (int i = 0; i < Network::NUM_SPECIES; ++i) std::cout << ',' << r.state[i];
    std::cout << '\n';
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("expected the retained application .par path");
        auto config = RuntimeParams::Load(argv[1]);
        if (config.Get<std::string>("network_name", "") != Network::NETWORK_NAME)
            throw std::invalid_argument("probe network differs from input");
        const auto burn = make_burn_config_view(config.physics.burn);
        SpeciesManager species;
        Network::RegisterSpecies(species);
        HelmEos eos(config.physics.eos_table_path, &species);
        arch::cuda::HelmEosDeviceOwner owner(eos, nullptr);
        Replay cpu{}, gpu{};
        std::vector<double> fractions;
        Network::SetupInitialFractions(config, species, fractions);
        for (int i = 0; i < Network::NUM_SPECIES; ++i) cpu.state[i] = fractions[i];
        const double missing = std::numeric_limits<double>::quiet_NaN();
        cpu.fluid.rho = config.Get<double>("rho0", missing);
        cpu.state[Network::NUM_SPECIES] = config.Get<double>("temperature0", missing);
        cpu.fluid.eng = cpu.fluid.rho * eos.get_eint_from_T(
            cpu.fluid.rho, cpu.state[Network::NUM_SPECIES], cpu.state);
        const double dt = 0.5 * config.Get<double>("tmax", missing);
        if (!(dt > 0.)) throw std::invalid_argument("positive end time required");
        gpu = cpu;
        arch::cuda::DeviceAllocation<Replay> storage;
        storage.allocate(2);
        std::cout << std::setprecision(17)
                  << "half,lane,success,T_in,T_out,e_old,q,enuc,total_energy,Ye,X...\n";
        emit(-1, "initial", cpu);
        for (int half = 0; half != 4; ++half) {
            Replay lanes[2]{gpu, cpu};
            checked(cudaMemcpy(storage.get(), lanes, sizeof(lanes), cudaMemcpyHostToDevice));
            advance(cpu, eos.get_view(), burn, dt);
            replay_kernel<<<1, 2>>>(storage.get(), owner.view(), burn, dt);
            checked(cudaGetLastError());
            checked(cudaMemcpy(lanes, storage.get(), sizeof(lanes), cudaMemcpyDeviceToHost));
            gpu = lanes[0];
            emit(half, "cpu", cpu);
            emit(half, "cuda", gpu);
            emit(half, "cuda_cpu_input", lanes[1]);
            if (!cpu.success || !gpu.success || !lanes[1].success) return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
