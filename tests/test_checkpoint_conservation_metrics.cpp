#include "checkpoint_conservation_metrics.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void rejected(Function&& function)
{
    bool failed = false;
    try { function(); } catch (const std::runtime_error&) { failed = true; }
    expect(failed, "invalid metric input did not fail closed");
}

io::CheckpointData checkpoint(const GridConfig& config,
                              std::vector<int> levels = {0},
                              std::vector<std::uint32_t> logical = {0})
{
    io::CheckpointData result;
    result.dim = config.dim;
    result.geometry = config.geometry;
    result.num_species = 2;
    result.cells_per_block = amr::BLOCK_NX
        * (config.dim >= 2 ? amr::BLOCK_NY : 1)
        * (config.dim == 3 ? amr::BLOCK_NZ : 1);
    result.levels = std::move(levels);
    result.logical_x1 = std::move(logical);
    result.logical_x2.resize(result.levels.size());
    result.logical_x3.resize(result.levels.size());
    const auto cells = result.levels.size() * result.cells_per_block;
    result.rho.assign(cells, 2.0);
    result.mom_u.assign(cells, 0.25);
    result.mom_v.assign(cells, -0.5);
    result.mom_w.assign(cells, 0.75);
    result.eng.assign(cells, 5.0);
    result.rhoX.resize(2 * cells);
    for (std::size_t cell = 0; cell < cells; ++cell) {
        result.rhoX[cell] = 0.5;
        result.rhoX[cells + cell] = 1.5;
    }
    return result;
}

GridConfig config(int dim, const std::string& geometry)
{
    GridConfig result;
    result.dim = dim;
    result.geometry = geometry;
    result.nblockx1 = 1;
    result.nblockx2 = dim >= 2 ? 1 : 0;
    result.nblockx3 = dim == 3 ? 1 : 0;
    result.x1_min = 1.0;
    result.x1_max = 2.0;
    result.x2_min = 0.25;
    result.x2_max = 0.75;
    result.x3_min = 0.1;
    result.x3_max = 0.6;
    return result;
}

void check_nine_geometries_and_cell_order()
{
    for (const std::string name : {"cartesian", "cylindrical", "spherical"}) {
        for (int dim = 1; dim <= 3; ++dim) {
            const auto parameters = config(dim, name);
            auto data = checkpoint(parameters);
            const Grid grid = checkpoint_metrics::checkpoint_root_grid(parameters);
            long double expected = 0.0L;
            std::size_t cell = 0;
            for (int k = 0; k < (dim == 3 ? amr::BLOCK_NZ : 1); ++k)
                for (int j = 0; j < (dim >= 2 ? amr::BLOCK_NY : 1); ++j)
                    for (int i = 0; i < amr::BLOCK_NX; ++i, ++cell) {
                        data.rho[cell] = 1.0 + i + 2.0 * j + 3.0 * k;
                        expected += static_cast<long double>(GridMetrics::CellVolume(
                            grid, i + amr::MAX_NG, dim >= 2 ? j + amr::MAX_NG : 0,
                            dim == 3 ? k + amr::MAX_NG : 0)) * data.rho[cell];
                    }
            const auto observed = checkpoint_metrics::compute(data, &parameters);
            expect(observed.mass == expected, "physical cell volume/index order differs from GridMetrics");
        }
    }
}

void check_mixed_annulus_and_shell()
{
    for (const std::string name : {"cartesian", "cylindrical", "spherical"}) {
        auto parameters = config(1, name);
        parameters.nblockx1 = 2;
        const auto data = checkpoint(parameters, {1, 1, 0}, {0, 1, 1});
        const auto observed = checkpoint_metrics::compute(data, &parameters);
        // Independent analytic [1,2] Cartesian interval, cylindrical annulus,
        // and spherical shell, using the code's suppressed angular factors.
        const long double volume = name == "cartesian" ? 1.0L
            : name == "cylindrical" ? 1.5L : 7.0L / 3.0L;
        expect(std::abs(observed.mass - 2.0L * volume) < 1e-14L, "mixed-level physical mass failed");
        expect(std::abs(observed.energy - 5.0L * volume) < 1e-14L, "mixed-level physical energy failed");
        expect(std::abs(observed.species[0] - 0.5L * volume) < 1e-14L, "first species physical mass failed");
        expect(std::abs(observed.species[1] - 1.5L * volume) < 1e-14L, "second species physical mass failed");
        if (name == "cartesian") {
            const auto legacy = checkpoint_metrics::compute(data);
            expect(legacy.mass == 64.0L && legacy.energy == 160.0L,
                   "legacy Cartesian measure/budget units changed");
        } else {
            rejected([&] { checkpoint_metrics::compute(data); });
        }
    }
}

void check_invalid_metadata()
{
    auto parameters = config(1, "cylindrical");
    auto data = checkpoint(parameters);
    auto invalid = parameters;
    invalid.geometry = "spherical";
    rejected([&] { checkpoint_metrics::compute(data, &invalid); });
    invalid = parameters;
    invalid.dim = 2;
    rejected([&] { checkpoint_metrics::compute(data, &invalid); });
    data.logical_x1[0] = 1;
    rejected([&] { checkpoint_metrics::compute(data, &parameters); });
    data.logical_x1[0] = 0;
    data.levels[0] = -1;
    rejected([&] { checkpoint_metrics::compute(data, &parameters); });
    data.levels[0] = 0;
    data.rhoX.pop_back();
    rejected([&] { checkpoint_metrics::compute(data, &parameters); });
}

} // namespace

int main()
{
    check_nine_geometries_and_cell_order();
    check_mixed_annulus_and_shell();
    check_invalid_metadata();
    std::cout << "checkpoint conservation metrics: shared geometry and legacy/negative controls PASS\n";
}
