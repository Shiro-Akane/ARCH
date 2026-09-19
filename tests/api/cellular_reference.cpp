// Direct call into the real case, independently of Preview's sampling/JSON
// assembly. No Driver, network evolution, output writer, or frontend formula.
#include "../../simulation/Cellular/Cellular.cpp"
#include "api/Json.h"
#include "core/InitialStateConversion.h"
#include "core/RuntimeParams.h"
#include "physics/eos/eosdispatch.h"

#include <iomanip>
#include <iostream>
#include <iterator>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const int nx = std::stoi(argv[1]), ny = std::stoi(argv[2]);
    const std::string text{std::istreambuf_iterator<char>(std::cin), {}};
    auto *old = std::cout.rdbuf(std::cerr.rdbuf());
    try {
        auto config = RuntimeParams::LoadText(text);
        SpeciesManager species;
        CellularDetonation problem;
        problem.Setup(config, species);
        Grid grid(0, config.grid.x1_min, config.grid.x1_max, config.grid.x2_min,
                  config.grid.x2_max, config.grid.x3_min, config.grid.x3_max);
        grid.dim = 2;
        grid.InitializeTopology();
        grid.dx1 = (grid.x1_max - grid.x1_min) / nx;
        grid.dx2 = (grid.x2_max - grid.x2_min) / ny;
        using arch::api::detail::Json;
        auto rows = Json::array();
        EOSDispatcher::dispatch_eos(config, species, [&](auto &&eos) {
            for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
                const auto point = grid.GetPhysicalCoords(i + grid.ng, j + grid.ng);
                PrimitiveData primitive{};
                problem.Init(point, primitive);
                const auto state = ProblemHelper::detail::InitialConservedState(primitive, eos);
                const double eint = eos_utils::extract_specific_internal_energy(state);
                rows.push(Json::array({point.x, point.y, point.z, primitive.rho,
                    eos.get_pressure(state, primitive.mass_fractions.data()),
                    eos.get_temperature(primitive.rho, eint, primitive.mass_fractions.data()),
                    primitive.u, state.eng, eint, primitive.v}));
            }
        });
        std::cout.rdbuf(old);
        std::cout << rows.dump() << '\n';
    } catch (const std::exception &error) {
        std::cout.rdbuf(old);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
