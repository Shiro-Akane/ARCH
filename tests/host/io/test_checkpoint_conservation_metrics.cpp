/**
 * @file test_checkpoint_conservation_metrics.cpp
 * @brief Check volume-weighted totals reconstructed from checkpoints.
 *
 * Cartesian, annular and shell cases test physical measures and cell ordering;
 * invalid metadata must be rejected before conservation is evaluated.
 */
#include "fixtures/io/checkpoint_conservation_metrics.h"
#include "fixtures/io/checkpoint_analysis_inputs.h"

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
    // Cylindrical2D fixtures now carry the canonical RZ identity explicitly;
    // this is fixture data and does not pretend to be a runtime producer.
    if (result.dim == 2 && result.geometry == "cylindrical") {
        result.geometry_identity = io::current_rz_checkpoint_geometry();
        result.native_domain = {{config.x1_min,config.x1_max,config.x2_min,config.x2_max},
            {config.nblockx1,config.nblockx2},{amr::BLOCK_NX,amr::BLOCK_NY}};
    }
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

void check_explicit_metric_inputs()
{
    const std::string text = "geometry=cartesian\nnblockx1=1\nnblockx2=0\nnblockx3=0\n"
                             "x1_min=0\nx1_max=exp(1)\n";
    const auto read = [](const std::string& source) {
        std::istringstream stream(source);
        return checkpoint_analysis::Inputs(stream);
    };
    const auto inputs = read(text);
    const auto grid = inputs.grid();
    auto upper = text;
    upper.replace(upper.find("cartesian"), 9, "CaRtEsIaN");
    expect(read(upper).grid().geometry == "cartesian", "metric enum canonicalization changed");
    expect(grid.dim == 1 && grid.x1_min == 0 && grid.x1_max == std::exp(1.0),
           "metric input altered explicit geometry or zero/expression values");
    expect(checkpoint_metrics::compute(checkpoint(grid), &grid).mass > 0,
           "explicit metric grid does not produce a physical measure");
    // Missing scientific-run controls are irrelevant to a grid metric; missing
    // metric controls, malformed tokens and duplicates must never be filled.
    for (const std::string key : {"geometry", "nblockx1", "nblockx2", "nblockx3", "x1_min", "x1_max"}) {
        auto missing = text;
        const auto begin = missing.find(key + "="), end = missing.find('\n', begin);
        missing.erase(begin, end - begin + 1);
        bool failed = false;
        try { (void)read(missing).grid(); }
        catch (const ConfigValueError&) { failed = true; }
        expect(failed, "metric input silently filled a missing grid control");
    }
    for (const auto suffix : {"x1_max=2\n", "bad line\n", "cfl=nan\n"}) {
        bool failed = false;
        try { (void)read(text + suffix).grid(); }
        catch (const ConfigInputError&) { failed = true; }
        expect(failed, "invalid explicit metric input was ignored");
    }
    const auto force = read(text + "gravity_type=external\ngravity_g_x=0\n"
                           "gravity_g_y=0\ngravity_g_z=-2\neos_type=ideal\ngamma=1.4\n"
                           "rho0=1\npressure0=2\nvelocity_x0=0\n");
    expect(force.standard<double>("gravity_g_x") == 0 && force.case_number("velocity_x0") == 0,
           "explicit zero acceleration or velocity was lost");
    bool missing_case = false;
    try { (void)inputs.case_number("rho0"); }
    catch (const ConfigValueError&) { missing_case = true; }
    expect(missing_case, "analytic reference silently filled missing initial density");
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
            std::ostringstream source;
            source << "geometry=" << name << "\n"
                   << "nblockx1=1\nnblockx2=" << parameters.nblockx2
                   << "\nnblockx3=" << parameters.nblockx3
                   << "\nx1_min=1\nx1_max=2\nx2_min=0.25\nx2_max=0.75\nx3_min=0.1\nx3_max=0.6\n";
            std::istringstream stream(source.str());
            const auto parsed = checkpoint_analysis::Inputs(stream).grid();
            const auto observed = checkpoint_metrics::compute(data, &parsed);
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

void check_native_rz_full_ring_measures()
{
    const auto parameters = config(2, "cylindrical");
    auto data = checkpoint(parameters);
    expect(data.geometry_identity.revision == io::rz_checkpoint_revision
               && data.geometry_identity.chart == "axisymmetric-rz",
           "cylindrical2D metric fixture lost its explicit native RZ identity");
    // Independent full-ring references over r in [1,2], z in [.25,.75]:
    // V = 2*pi*int r dr dz = 3*pi/2 and W = 2*pi*int r^2 dr dz = 7*pi/3.
    // The stored mom_w is m_phi = J/W, so its total is the angular momentum.
    const long double pi = 3.14159265358979323846L;
    const long double volume = 3.0L * pi / 2.0L;
    const long double angular_weight = 7.0L * pi / 3.0L;
    const auto close = [](long double observed, long double reference) {
        return std::abs(observed - reference) <= 1e-14L * std::abs(reference);
    };
    const auto observed = checkpoint_metrics::compute(data, &parameters);
    expect(close(observed.mass, 2.0L * volume), "RZ full-ring physical mass reference drifted");
    expect(close(observed.energy, 5.0L * volume), "RZ full-ring physical energy reference drifted");
    expect(close(observed.mom_w, 0.75L * angular_weight),
           "RZ stored mom_w=m_phi=J/W angular-momentum total drifted");
    expect(close(observed.species[0], 0.5L * volume), "RZ first species mass reference drifted");
    expect(close(observed.species[1], 1.5L * volume), "RZ second species mass reference drifted");
    // A retired cylindrical2D polar payload must be refused before the metric:
    // neither an Existing revision-1 clone nor an absent identity can certify
    // the canonical RZ state, and the checkpoint is never marked here.
    auto retired = data;
    retired.geometry_identity = {1, "existing"};
    rejected([&] { checkpoint_metrics::compute(retired, &parameters); });
    auto unmarked = data;
    unmarked.geometry_identity = {};
    rejected([&] { checkpoint_metrics::compute(unmarked, &parameters); });
    auto missing_domain = data;
    missing_domain.native_domain = {};
    rejected([&] { checkpoint_metrics::compute(missing_domain, &parameters); });
    auto mismatched_domain = data;
    mismatched_domain.native_domain.bounds[3] = 1.0;
    rejected([&] { checkpoint_metrics::compute(mismatched_domain, &parameters); });
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
    check_explicit_metric_inputs();
    check_nine_geometries_and_cell_order();
    check_mixed_annulus_and_shell();
    check_native_rz_full_ring_measures();
    check_invalid_metadata();
    std::cout << "checkpoint conservation metrics: shared geometry and legacy/negative controls PASS\n";
}
