#include "math/RoeThermodynamicCases.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

// Read-only replay of saved uniform planar states using the production math.
// The analytic ideal-gas Roe relation is the independent check, not a new flux.
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
    const auto reconstruct = [&](const FluidVector (&stencil)[6], FluidVector& left, FluidVector& right) {
        double rho[6], u[6], v[6], w[6], pressure[6];
        for (int j = 0; j < 6; ++j)
            PPMReconstruction::gather_eos_stencil_point(stencil[j], nullptr, eos,
                rho[j], u[j], v[j], w[j], pressure[j]);
        PPMReconstruction::reconstruct_eos(rho, u, v, w, pressure, nullptr, nullptr, eos, left, right);
    };
    const auto error = [](const FluidVector& a, const FluidVector& b) {
        using RoeThermodynamicCases::scaled_error;
        return std::max({scaled_error(a.rho,b.rho), scaled_error(a.mom_u,b.mom_u),
                         scaled_error(a.eng,b.eng)});
    };
    std::cout << std::setprecision(17)
              << "face,reconstruction_reflection,flux_reflection,acoustic_error,delta_rho,delta_e,chi,kappa,c2,ideal_c2\n";
    for (int i = 2; i+3 < count; ++i) {
        FluidVector stencil[6], reversed[6], left{}, right{}, reversed_left{}, reversed_right{};
        for (int j = 0; j < 6; ++j) {
            stencil[j] = state[i-2+j];
            reversed[j] = RoeThermodynamicCases::reflect(state[i+3-j], 0);
        }
        reconstruct(stencil, left, right);
        reconstruct(reversed, reversed_left, reversed_right);
        const double reconstruction_error = std::max(
            error(reversed_left, RoeThermodynamicCases::reflect(right,0)),
            error(reversed_right, RoeThermodynamicCases::reflect(left,0)));
        const double flux_error = RoeThermodynamicCases::reflection_error<FluxHLLC<PCMReconstruction>>(
            left, right, nullptr, nullptr, 0, eos, 0);
        const double pl = eos.get_pressure(left, nullptr), pr = eos.get_pressure(right, nullptr);
        const double el = left.eng/left.rho - .5*(left.mom_u/left.rho)*(left.mom_u/left.rho);
        const double er = right.eng/right.rho - .5*(right.mom_u/right.rho)*(right.mom_u/right.rho);
        const auto rs = calc_glaister_state(left, right, pl, pr, el, er,
            (left.eng+pl)/left.rho, (right.eng+pr)/right.rho, nullptr, nullptr, 0, nullptr, eos);
        const double c2 = rs.c_hat*rs.c_hat;
        const double expected = (eos.global_gamma-1)*(rs.H_hat - .5*rs.u_hat*rs.u_hat);
        std::cout << i+1 << ',' << reconstruction_error << ',' << flux_error << ','
                  << RoeThermodynamicCases::scaled_error(c2,expected) << ','
                  << right.rho-left.rho << ',' << er-el << ',' << rs.chi << ',' << rs.kappa
                  << ',' << c2 << ',' << expected << '\n';
    }
}
