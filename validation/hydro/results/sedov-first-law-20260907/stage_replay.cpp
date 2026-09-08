#include "math/RoeThermodynamicCases.h"
#include "driver/DriverUtils.h"
#include "driver/StageScheduler.h"
#include "numerics/integrator/TimeIntegratorHelper.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

// Diagnostic traversal of the existing production leaves and stage plan on a
// saved uniform planar state. No independent physics or acceptance oracle.
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1]);
    int count = 0;
    input >> count;
    if (count < 6) return 3;
    std::vector<FluidVector> state(count);
    for (auto& q : state) input >> q.rho >> q.mom_u >> q.eng;
    if (!input) return 4;
    const IdealGasView eos{};
    const auto plan = arch::scheduler::make_hydro_plan(arch::scheduler::HydroMethod::RK3);
    const auto reconstruct = [&](const FluidVector (&stencil)[6], FluidVector& left, FluidVector& right) {
        double rho[6], u[6], v[6], w[6], pressure[6];
        for (int j = 0; j < 6; ++j)
            PPMReconstruction::gather_eos_stencil_point(stencil[j], nullptr, eos,
                rho[j], u[j], v[j], w[j], pressure[j]);
        PPMReconstruction::reconstruct_eos(rho, u, v, w, pressure, nullptr, nullptr, eos, left, right);
    };
    const auto error = [](const FluidVector& a, const FluidVector& b) {
        return std::max({std::abs(a.rho-b.rho), std::abs(a.mom_u-b.mom_u), std::abs(a.eng-b.eng)});
    };
    double time = 0.;
    std::cout << std::setprecision(17)
              << "step,stage,time,dt,reconstruction_reflection,flux_reflection,symmetry,center_mass_flux,center_energy_flux\n";
    for (int step = 1; time < .1 && step <= 2000; ++step) {
        double minimum = cfl_inactive_cell_dt();
        for (const auto& q : state)
            minimum = std::min(minimum, evaluate_cfl_cell_dt(q, nullptr, eos, 1, 1./count, 1., 1.));
        const double dt = std::min(finalize_cfl_dt(.4, minimum), .1-time);
        const auto old = state;
        for (const auto& stage : plan.stages) {
            std::vector<FluidVector> flux(count+1), next(count);
            double reconstruction_error = 0., flux_error = 0., symmetry = 0.;
            for (int face = 0; face <= count; ++face) {
                FluidVector stencil[6], reversed[6], left{}, right{}, reverse_left{}, reverse_right{};
                for (int j = 0; j < 6; ++j) {
                    stencil[j] = state[std::clamp(face-3+j, 0, count-1)];
                    reversed[j] = RoeThermodynamicCases::reflect(state[std::clamp(face+2-j, 0, count-1)], 0);
                }
                reconstruct(stencil, left, right);
                reconstruct(reversed, reverse_left, reverse_right);
                reconstruction_error = std::max({reconstruction_error,
                    error(reverse_left, RoeThermodynamicCases::reflect(right,0)),
                    error(reverse_right, RoeThermodynamicCases::reflect(left,0))});
                flux_error = std::max(flux_error,
                    RoeThermodynamicCases::reflection_error<FluxHLLC<PCMReconstruction>>(
                        left, right, nullptr, nullptr, 0, eos, 0));
                FluxHLLC<PCMReconstruction>::compute_face_flux(left, right, nullptr, nullptr,
                    0, eos, 0, 0., flux[face], nullptr);
            }
            for (int i = 0; i < count; ++i) {
                FluidVector delta{};
                TimeIntegration::accumulate_cell_divergence(flux[i], flux[i+1], nullptr, nullptr,
                    0, 1, 1., 1., 1./count, dt, delta, nullptr);
                TimeIntegration::update_stage_cell(old[i], state[i], delta, nullptr, nullptr, nullptr,
                    0, 1, stage.old_weight, stage.update_weight, 1e-12, 1e-10, 1e6, next[i], nullptr);
            }
            for (int i = 0; i < count; ++i)
                symmetry = std::max(symmetry, error(next[i], RoeThermodynamicCases::reflect(next[count-1-i],0)));
            std::cout << step << ',' << stage.stage << ',' << time+dt << ',' << dt << ','
                      << reconstruction_error << ',' << flux_error << ',' << symmetry << ','
                      << flux[count/2].rho << ',' << flux[count/2].eng << '\n';
            state = std::move(next);
        }
        time += dt;
    }
}
