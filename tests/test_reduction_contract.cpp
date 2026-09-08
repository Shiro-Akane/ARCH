#include "driver/DriverBurn.h"
#include "driver/ReductionSpec.h"
#include "driver/DriverUtils.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
using arch::reduction::ReductionCandidate;
using arch::reduction::ReductionResult;
using arch::reduction::ReductionStatus;

int failures = 0;
int edge_cases = 0;

void fail(std::string_view name)
{
    std::cerr << "FAIL " << name << '\n';
    ++failures;
}

void expect(bool condition, std::string_view name)
{
    if (!condition) fail(name);
}

void expect_bits(std::string_view name, double value, std::uint64_t bits)
{
    if (std::bit_cast<std::uint64_t>(value) != bits) {
        std::cerr << "FAIL " << name << " actual=0x" << std::hex
                  << std::bit_cast<std::uint64_t>(value)
                  << " expected=0x" << bits << std::dec << '\n';
        ++failures;
    }
}

amr::CellLogicalKey key(
    std::int64_t root_i, std::int64_t root_j, std::int64_t root_k,
    int level, std::uint64_t morton, int i, int j, int k, int component)
{
    return {{root_i, root_j, root_k}, level, morton, i, j, k, component};
}

ReductionCandidate candidate(double value, amr::CellLogicalKey logical_key,
                             bool active = true)
{
    return {value, logical_key, active};
}

bool same_key(const amr::CellLogicalKey& lhs, const amr::CellLogicalKey& rhs)
{
    return lhs.root.root_i == rhs.root.root_i
        && lhs.root.root_j == rhs.root.root_j
        && lhs.root.root_k == rhs.root.root_k
        && lhs.level == rhs.level && lhs.morton == rhs.morton
        && lhs.logical_i == rhs.logical_i && lhs.logical_j == rhs.logical_j
        && lhs.logical_k == rhs.logical_k && lhs.component == rhs.component;
}

ReductionResult run(
    const arch::reduction::ReductionSpec& spec,
    std::initializer_list<ReductionCandidate> values)
{
    return arch::reduction::execute_host_reduction(
        spec, std::span<const ReductionCandidate>(values.begin(), values.size()));
}

void expect_result(std::string_view name, const ReductionResult& actual,
                   ReductionStatus status, std::uint64_t bits,
                   std::uint64_t count, const amr::CellLogicalKey* winner = nullptr)
{
    ++edge_cases;
    expect(actual.status == status, std::string(name) + ".status");
    expect_bits(std::string(name) + ".value", actual.value, bits);
    expect(actual.accepted_count == count, std::string(name) + ".count");
    if (winner) expect(same_key(actual.key, *winner), std::string(name) + ".key");
}

void verify_contract_edges()
{
    static_assert(std::is_standard_layout_v<arch::reduction::ReductionSpec>);
    static_assert(std::is_trivially_copyable_v<arch::reduction::ReductionSpec>);
    static_assert(std::is_standard_layout_v<ReductionCandidate>);
    static_assert(std::is_trivially_copyable_v<ReductionCandidate>);
    static_assert(std::is_standard_layout_v<ReductionResult>);
    static_assert(std::is_trivially_copyable_v<ReductionResult>);

    const auto k0 = key(-1, 0, 0, 0, 0, 0, 0, 0, 0);
    const auto k1 = key(0, 0, 0, 0, 1, 0, 0, 0, 0);
    const auto k2 = key(0, 0, 0, 0, 2, 0, 0, 0, 0);
    const auto k3 = key(1, 0, 0, 0, 3, 0, 0, 0, 0);
    const double nan = std::bit_cast<double>(0x7ff8000000000042ULL);
    const double pinf = std::numeric_limits<double>::infinity();
    const double ninf = -std::numeric_limits<double>::infinity();
    const auto min_spec = arch::reduction::minimum_spec(99.0);
    const auto max_spec = arch::reduction::maximum_spec(-99.0);
    const auto sum_spec = arch::reduction::conservation_sum_spec();

    expect_result("01.min.finite", run(min_spec, {
        candidate(4.0, k2), candidate(2.0, k1), candidate(3.0, k3)}),
        ReductionStatus::Ok, 0x4000000000000000ULL, 3, &k1);
    expect_result("02.max.finite", run(max_spec, {
        candidate(4.0, k2), candidate(2.0, k1), candidate(3.0, k3)}),
        ReductionStatus::Ok, 0x4010000000000000ULL, 3, &k2);
    expect_result("03.sum.binary64", run(sum_spec, {
        candidate(1.0e16, k0), candidate(-1.0e16, k2), candidate(1.0, k1)}),
        ReductionStatus::Ok, 0x0000000000000000ULL, 3, &k0);

    expect_result("04.min.nan.first", run(min_spec, {
        candidate(nan, k0), candidate(2.0, k1), candidate(3.0, k2)}),
        ReductionStatus::Ok, 0x4000000000000000ULL, 2, &k1);
    expect_result("05.min.nan.middle", run(min_spec, {
        candidate(2.0, k1), candidate(nan, k0), candidate(3.0, k2)}),
        ReductionStatus::Ok, 0x4000000000000000ULL, 2, &k1);
    expect_result("06.min.nan.last", run(min_spec, {
        candidate(2.0, k1), candidate(3.0, k2), candidate(nan, k0)}),
        ReductionStatus::Ok, 0x4000000000000000ULL, 2, &k1);
    expect_result("07.max.nan", run(max_spec, {
        candidate(nan, k0), candidate(2.0, k1)}),
        ReductionStatus::Ok, 0x4000000000000000ULL, 1, &k1);
    expect_result("08.sum.nan.first", run(sum_spec, {
        candidate(nan, k0), candidate(2.0, k1)}),
        ReductionStatus::NanRejected, 0x0000000000000000ULL, 0);
    expect_result("09.sum.nan.middle", run(sum_spec, {
        candidate(1.0, k0), candidate(nan, k1), candidate(2.0, k2)}),
        ReductionStatus::NanRejected, 0x3ff0000000000000ULL, 1);
    expect_result("10.sum.nan.last", run(sum_spec, {
        candidate(1.0, k0), candidate(2.0, k1), candidate(nan, k2)}),
        ReductionStatus::NanRejected, 0x4008000000000000ULL, 2);

    expect_result("11.min.neginf", run(min_spec, {
        candidate(2.0, k1), candidate(ninf, k2)}),
        ReductionStatus::Ok, 0xfff0000000000000ULL, 2, &k2);
    expect_result("12.min.posinf", run(min_spec, {candidate(pinf, k1)}),
        ReductionStatus::Ok, 0x7ff0000000000000ULL, 1, &k1);
    expect_result("13.max.neginf", run(max_spec, {candidate(ninf, k1)}),
        ReductionStatus::Ok, 0xfff0000000000000ULL, 1, &k1);
    expect_result("14.max.posinf", run(max_spec, {
        candidate(2.0, k1), candidate(pinf, k2)}),
        ReductionStatus::Ok, 0x7ff0000000000000ULL, 2, &k2);
    expect_result("15.sum.neginf", run(sum_spec, {candidate(ninf, k1)}),
        ReductionStatus::InfiniteRejected, 0x0000000000000000ULL, 0);
    expect_result("16.sum.posinf", run(sum_spec, {candidate(pinf, k1)}),
        ReductionStatus::InfiniteRejected, 0x0000000000000000ULL, 0);

    expect_result("17.min.zero.low.plus", run(min_spec, {
        candidate(-0.0, k2), candidate(+0.0, k1)}),
        ReductionStatus::Ok, 0x0000000000000000ULL, 2, &k1);
    expect_result("18.min.zero.low.minus", run(min_spec, {
        candidate(+0.0, k2), candidate(-0.0, k1)}),
        ReductionStatus::Ok, 0x8000000000000000ULL, 2, &k1);
    expect_result("19.max.zero.low.plus", run(max_spec, {
        candidate(-0.0, k2), candidate(+0.0, k1)}),
        ReductionStatus::Ok, 0x0000000000000000ULL, 2, &k1);
    expect_result("20.max.zero.low.minus", run(max_spec, {
        candidate(+0.0, k2), candidate(-0.0, k1)}),
        ReductionStatus::Ok, 0x8000000000000000ULL, 2, &k1);

    expect_result("21.min.empty", run(min_spec, {}),
        ReductionStatus::Empty, std::bit_cast<std::uint64_t>(99.0), 0);
    expect_result("22.max.empty", run(max_spec, {}),
        ReductionStatus::Empty, std::bit_cast<std::uint64_t>(-99.0), 0);
    expect_result("23.sum.empty", run(sum_spec, {}),
        ReductionStatus::Empty, 0x0000000000000000ULL, 0);
    expect_result("24.inactive.ignored", run(min_spec, {
        candidate(1.0, k0, false), candidate(2.0, k1)}),
        ReductionStatus::Ok, 0x4000000000000000ULL, 1, &k1);
    expect_result("25.all.inactive", run(min_spec, {
        candidate(1.0, k0, false), candidate(2.0, k1, false)}),
        ReductionStatus::Empty, std::bit_cast<std::uint64_t>(99.0), 0);

    expect_result("26.duplicate.min", run(min_spec, {
        candidate(1.0, k1), candidate(3.0, k2), candidate(2.0, k1)}),
        ReductionStatus::InvalidKeyOrder,
        std::bit_cast<std::uint64_t>(99.0), 0);
    expect_result("27.duplicate.max", run(max_spec, {
        candidate(1.0, k1), candidate(3.0, k2), candidate(2.0, k1)}),
        ReductionStatus::InvalidKeyOrder,
        std::bit_cast<std::uint64_t>(-99.0), 0);
    expect_result("28.duplicate.sum", run(sum_spec, {
        candidate(1.0, k1), candidate(2.0, k1)}),
        ReductionStatus::InvalidKeyOrder, 0x0000000000000000ULL, 0);
    expect_result("29.equal.tie", run(min_spec, {
        candidate(2.0, k2), candidate(2.0, k0), candidate(2.0, k1)}),
        ReductionStatus::Ok, 0x4000000000000000ULL, 3, &k0);
    expect_result("30.permutation", run(min_spec, {
        candidate(3.0, k3), candidate(2.0, k2), candidate(2.0, k0)}),
        ReductionStatus::Ok, 0x4000000000000000ULL, 3, &k0);
    const auto low_root = key(-1, 0, 0, 4, 99, 9, 9, 9, 9);
    const auto high_root = key(1, 0, 0, 0, 0, 0, 0, 0, 0);
    expect_result("31.multiple.roots", run(min_spec, {
        candidate(-0.0, high_root), candidate(+0.0, low_root)}),
        ReductionStatus::Ok, 0x0000000000000000ULL, 2, &low_root);

    const auto root = amr::root_logical_key_from_leaf(3, 17, 9, 25);
    expect(root.has_value(), "32.checkpoint.root.available");
    const auto reconstructed = key(
        root->root_i, root->root_j, root->root_k, 3, 0x1234, 17, 9, 25, 7);
    const auto checkpoint = key(2, 1, 3, 3, 0x1234, 17, 9, 25, 7);
    expect_result("32.checkpoint.reconstructed", run(min_spec, {
        candidate(5.0, reconstructed), candidate(5.0, k3)}),
        ReductionStatus::Ok, 0x4014000000000000ULL, 2, &k3);
    expect(same_key(reconstructed, checkpoint), "32.checkpoint.key.stable");

    const std::array invalid_min_order = {
        candidate(3.0, k1), candidate(2.0, k2), candidate(1.0, k1)};
    expect_result("33.min.invalid-key-order",
        arch::reduction::execute_ordered_reduction(
            min_spec, invalid_min_order.data(), invalid_min_order.size()),
        ReductionStatus::InvalidKeyOrder,
        std::bit_cast<std::uint64_t>(99.0), 0);
    const std::array invalid_max_order = {
        candidate(1.0, k1), candidate(2.0, k2), candidate(3.0, k1)};
    expect_result("34.max.invalid-key-order",
        arch::reduction::execute_ordered_reduction(
            max_spec, invalid_max_order.data(), invalid_max_order.size()),
        ReductionStatus::InvalidKeyOrder,
        std::bit_cast<std::uint64_t>(-99.0), 0);

    const std::array partial_counterexample = {
        candidate(1.0e16, k0), candidate(1.0, k1),
        candidate(-1.0e16, k2), candidate(1.0, k3)};
    expect_result("35.sum.partial-counterexample.full",
        arch::reduction::execute_host_reduction(
            sum_spec, std::span(partial_counterexample)),
        ReductionStatus::Ok, 0x3ff0000000000000ULL, 4, &k0);
    auto left_partial = arch::reduction::begin_reduction(sum_spec);
    auto right_partial = arch::reduction::begin_reduction(sum_spec);
    for (std::size_t index = 0; index < 2; ++index)
        arch::reduction::combine_candidate(
            sum_spec, left_partial, partial_counterexample[index]);
    for (std::size_t index = 2; index < partial_counterexample.size(); ++index)
        arch::reduction::combine_candidate(
            sum_spec, right_partial, partial_counterexample[index]);
    auto merged_partial = arch::reduction::begin_reduction(sum_spec);
    arch::reduction::combine_state(
        sum_spec, merged_partial, left_partial);
    arch::reduction::combine_state(
        sum_spec, merged_partial, right_partial);
    expect_result("36.sum.partial-state-rejected",
        arch::reduction::finalize_reduction(sum_spec, merged_partial),
        ReductionStatus::PartialStateRejected,
        0x0000000000000000ULL, 0);

    const std::array<amr::CellLogicalKey, 10> ordered = {
        key(0,0,0,0,0,0,0,0,0), key(1,0,0,0,0,0,0,0,0),
        key(1,1,0,0,0,0,0,0,0), key(1,1,1,0,0,0,0,0,0),
        key(1,1,1,1,0,0,0,0,0), key(1,1,1,1,1,0,0,0,0),
        key(1,1,1,1,1,1,0,0,0), key(1,1,1,1,1,1,1,0,0),
        key(1,1,1,1,1,1,1,1,0), key(1,1,1,1,1,1,1,1,1)};
    for (std::size_t i = 1; i < ordered.size(); ++i) {
        expect(arch::reduction::cell_logical_key_less(ordered[i - 1], ordered[i]),
               "key.field.order");
    }
}

struct HydroEos
{
    double get_pressure(const FluidVector& state, const double*) const
    {
        const double kinetic = 0.5
            * (state.mom_u * state.mom_u + state.mom_v * state.mom_v
               + state.mom_w * state.mom_w)
            / state.rho;
        return 0.4 * (state.eng - kinetic);
    }

    double get_sound_speed(
        const FluidVector& state, double pressure, const double*) const
    {
        return std::sqrt(1.4 * pressure / state.rho);
    }
};

struct DriverEos
{
    double get_temperature(double, double eint, const double* x) const
    {
        return eint / (10.0 + x[0]);
    }

    double get_eint_from_T(double, double temperature, const double* x) const
    {
        return temperature * (10.0 + x[0]);
    }
};

struct ScriptedBurner
{
    static constexpr int NEQ = 4; // Two species, temperature, passive energy.
    template<class Eos>
    bool integrate(double* state, double rho, double, const Eos& eos,
                   const BurnConfig&, double& dt_rec, double* energy_change = nullptr)
    {
        if (state[3] != 0.0) return false; // Each reused cell buffer starts clean.
        const double old_energy = eos.get_eint_from_T(rho, state[2], state);
        state[3] = -1.0;
        state[0] -= 0.125;
        state[1] += 0.125;
        state[2] *= rho < 3.0 ? 1.25 : 0.75;
        dt_rec = rho < 3.0 ? 0.125 : 0.25;
        if (energy_change) *energy_change = eos.get_eint_from_T(rho, state[2], state) - old_energy;
        return true;
    }
};

Grid make_grid(int dimension, double cell_width = 0.01)
{
    Grid grid(2,
              0.0, amr::BLOCK_NX * cell_width,
              0.0, amr::BLOCK_NY * cell_width,
              0.0, amr::BLOCK_NZ * cell_width);
    grid.dim = dimension;
    grid.geometry = "cartesian";
    grid.InitializeTopology();
    return grid;
}

FluidState make_diffusion_state(const Grid& grid)
{
    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(2);
    for (int k = 0; k < grid.GetTotalZ(); ++k) {
        for (int j = 0; j < grid.GetTotalY(); ++j) {
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                const double q = 0.013 * i + 0.021 * j + 0.034 * k;
                const double rho = 1.1 + 0.07 * q;
                const double u = 0.2 + 0.03 * q;
                const double v = -0.1 + 0.02 * q;
                const double w = 0.05 - 0.01 * q;
                const double x0 = 0.35 + 0.01 * q;
                const double x1 = 1.0 - x0;
                const double cv = x0 * 3.5 + x1 * 7.25;
                const double temperature = 2.0 + 0.4 * q;
                state.rho[cell] = rho;
                state.mom_u[cell] = rho * u;
                state.mom_v[cell] = rho * v;
                state.mom_w[cell] = rho * w;
                state.eng[cell] = rho * cv * temperature
                    + 0.5 * rho * (u * u + v * v + w * w);
                state.X(0, cell) = x0;
                state.X(1, cell) = x1;
            }
        }
    }
    return state;
}

SimConfig make_diffusion_config()
{
    SimConfig config{};
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_thermal_diffusion = true;
    config.physics.diffusion.use_viscous_diffusion = true;
    config.physics.diffusion.use_species_diffusion = true;
    config.physics.diffusion.alpha_therm = 0.37;
    config.physics.diffusion.nu_visc = 0.19;
    config.physics.diffusion.D_spec = 0.11;
    return config;
}

SpeciesManager make_species()
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 3.5);
    species.add_species("b", 4.0, 2.0, 1.5, 7.25);
    return species;
}

void set_burn_cell(FluidState& state, int cell, double rho,
                   double temperature, double mx, double my, double mz)
{
    state.rho[cell] = rho;
    state.mom_u[cell] = mx;
    state.mom_v[cell] = my;
    state.mom_w[cell] = mz;
    state.X(0, cell) = 0.75;
    state.X(1, cell) = 0.25;
    const double kinetic = 0.5 * (mx * mx + my * my + mz * mz) / rho;
    state.eng[cell] = rho * temperature * 10.75 + kinetic;
}

void characterize_hydro()
{
    Grid grid(3, 0.0, 16.0);
    grid.dim = 1;
    grid.InitializeTopology();
    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(0);
    for (int cell = 0; cell < grid.GetTotalSize(); ++cell)
        state.set(cell, {2.0, 2.0, 2.5, 2.0, 10.0});
    state.set(grid.GetIndex(grid.Is() + 3),
              {1.5, 3.0, -0.5, 0.2, 8.0});
    expect_bits("production.hydro.active",
                adaptive_dt(state, HydroEos{}, grid, 0.8),
                0x3fce8a38358aef78ULL);
    std::fill(state.rho.begin(), state.rho.end(), 0.0);
    expect_bits("production.hydro.inactive",
                adaptive_dt(state, HydroEos{}, grid, 0.8),
                0x41fdcd6500000000ULL);
}

void characterize_diffusion()
{
    const Grid grid = make_grid(1);
    const FluidState state = make_diffusion_state(grid);
    SpeciesManager species = make_species();
    IdealGas eos(1.37, species);
    SimConfig enabled = make_diffusion_config();
    // This contract tests reduction of the current cell candidates. The old
    // enabled/mixed bit snapshots described the retired cell-only diffusion
    // limit, not the corrected face-capacity / covariant stability operator.
    // Its physics has independent manufactured/actual-matrix tests; do not
    // bless new physics by replacing those snapshots with observed outputs.
    const auto serial_limit = [&](const FluidState& values, const Grid& geometry) {
        const int count = values.GetNumSpecies();
        std::vector<double> composition(count), neighbour(count), face(count),
                            charge(count), inverse_mass(count);
        const auto view = species.get_host_view();
        const auto diffusion = DiffFlux::make_diffusion_config_view(enabled);
        double minimum = DiffFlux::diffusion_dt_sentinel();
        for (int k = geometry.Ks(); k < geometry.Ke(); ++k)
            for (int j = geometry.Js(); j < geometry.Je(); ++j)
                for (int i = geometry.Is(); i < geometry.Ie(); ++i) {
                    const int cell = geometry.GetIndex(i, j, k);
                    const auto candidate = DiffFlux::evaluate_diffusion_dt_candidate(
                        values.get(cell), values.mass_fractions.data() + cell,
                        count, geometry.GetTotalSize(), eos, view, diffusion,
                        GridMetrics::make_geometry_view(geometry), i, j, k,
                        composition.data(), neighbour.data(), face.data(),
                        charge.data(), inverse_mass.data(),
                        DiffFlux::HostDiffusionStateReader{values});
                    expect(candidate.valid, "production.diffusion.valid-candidate");
                    minimum = std::min(minimum, candidate.value);
                }
        return minimum;
    };
    expect_bits("production.diffusion.enabled",
                DiffFlux::adaptive_dt_diff(state, eos, grid, enabled, 1.0),
                std::bit_cast<std::uint64_t>(serial_limit(state, grid)));

    Grid mixed_grid = make_grid(2);
    mixed_grid.geometry = "cylindrical";
    const FluidState mixed_state = make_diffusion_state(mixed_grid);
    expect_bits("production.diffusion.mixed",
                DiffFlux::adaptive_dt_diff(
                    mixed_state, eos, mixed_grid, enabled, 1.0),
                std::bit_cast<std::uint64_t>(serial_limit(mixed_state, mixed_grid)));

    const Grid finite_seed_grid = make_grid(1, 1.0);
    const FluidState finite_seed_state = make_diffusion_state(finite_seed_grid);
    SimConfig finite_seed = enabled;
    finite_seed.physics.diffusion.use_thermal_diffusion = false;
    finite_seed.physics.diffusion.use_species_diffusion = false;
    finite_seed.physics.diffusion.nu_visc = 2.0e-12;
    finite_seed.physics.diffusion.alpha_therm = 0.0;
    finite_seed.physics.diffusion.D_spec = 0.0;
    expect_bits("production.diffusion.finite-seed",
                DiffFlux::adaptive_dt_diff(
                    finite_seed_state, eos, finite_seed_grid,
                    finite_seed, 1.0),
                0x4202a05f20000000ULL);

    enabled.physics.diffusion.use_diffusion = false;
    expect_bits("production.diffusion.disabled",
                DiffFlux::adaptive_dt_diff(state, eos, grid, enabled, 0.13),
                0x4202a05f20000000ULL);
}

void characterize_burn()
{
    Grid grid(0, 0.0, 1.0);
    grid.dim = 1;
    grid.InitializeTopology();
    FluidState state;
    state.Preallocate(grid.GetTotalSize());
    state.InitSpecies(2);
    for (int i = 0; i < grid.GetTotalSize(); ++i) {
        state.rho[i] = 0.5;
        state.X(0, i) = 0.75;
        state.X(1, i) = 0.25;
    }
    set_burn_cell(state, grid.Is() + 1, 1.5, 0.5, 0.3, -0.2, 0.1);
    set_burn_cell(state, grid.Is() + 2, 2.0, 2.0, 1.5, -0.5, 0.25);
    set_burn_cell(state, grid.Is() + 3, 4.0, 2.0, -2.0, 0.75, -0.5);

    SimConfig config{};
    config.physics.burn.use_burn = true;
    config.physics.burn.nuclearDensMin = 1.0;
    config.physics.burn.nuclearTempMin = 1.0;
    config.physics.burn.smallx = 1.0e-20;
    config.physics.burn.enucDtFactor = 0.5;
    ScriptedBurner burner;
    double global_dt = 99.0;
    execute_burn_step(
        state, 2.0, DriverEos{}, burner, grid, config, global_dt);
    expect_bits("production.burn.global", global_dt, 0x4006ebdd7baf75efULL);
    expect_bits("production.burn.low.enuc",
                state.enuc_rate[grid.Is() + 2], 0x4004400000000000ULL);
    expect_bits("production.burn.high.enuc",
                state.enuc_rate[grid.Is() + 3], 0xc006400000000000ULL);
}

void verify_block_minimum_helper()
{
    const auto a = key(-1, 0, 0, 2, 17, 4, 0, 0, 101);
    const auto b = key(2, 0, 0, 1, 9, 8, 0, 0, 101);
    const std::array values = {
        candidate(4.0, b), candidate(2.0, a)};
    expect_bits("production.block.minimum",
                DriverReduction::reduce_block_minimum(1.0e99, std::span(values)),
                0x4000000000000000ULL);
    const std::array reversed = {
        candidate(-0.0, b), candidate(+0.0, a)};
    expect_bits("production.block.tie",
                DriverReduction::reduce_block_minimum(1.0e99, std::span(reversed)),
                0x0000000000000000ULL);
    bool empty_threw = false;
    try {
        DriverReduction::reduce_block_minimum(
            1.0e99, std::span<const ReductionCandidate>{});
    } catch (const std::runtime_error&) {
        empty_threw = true;
    }
    expect(empty_threw, "production.block.empty.hard-error");

    expect_bits("production.cfl.combine",
                combine_cfl_minimum(2.0, 3.0),
                0x4000000000000000ULL);
    expect_bits("production.diffusion.combine",
                DiffFlux::combine_diffusion_minimum(2.0, 3.0),
                0x4000000000000000ULL);
    expect_bits("production.burn.combine",
                DriverBurn::combine_burn_minimum(2.0, 3.0),
                0x4000000000000000ULL);
    const double nan = std::bit_cast<double>(0x7ff8000000000042ULL);
    expect_bits("production.cfl.combine.nan-seed",
                combine_cfl_minimum(nan, 2.0),
                0x4000000000000000ULL);
    expect_bits("production.diffusion.combine.nan-seed",
                DiffFlux::combine_diffusion_minimum(nan, 2.0),
                0x4000000000000000ULL);
    expect_bits("production.burn.combine.nan-seed",
                DriverBurn::combine_burn_minimum(nan, 2.0),
                0x4000000000000000ULL);
}
} // namespace

int main()
{
    verify_contract_edges();
    characterize_hydro();
    characterize_diffusion();
    characterize_burn();
    verify_block_minimum_helper();
    expect(edge_cases == 36, "edge.case.count");
    if (failures == 0) {
        std::cout << "D2_REDUCTION_CONTRACT_PASS edge_cases="
                  << edge_cases << '\n';
    }
    return failures == 0 ? 0 : 1;
}
