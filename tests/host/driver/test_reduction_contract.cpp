/**
 * @file test_reduction_contract.cpp
 * @brief Check deterministic reduction choices on the CPU.
 *
 * Synthetic edge cases and production hydro, diffusion and burn consumers
 * exercise tie-breaking, invalid candidates and block-level reductions.
 */
#include "amr/storage/Block.h"
#include "driver/stages/DriverBurn.h"
#include "driver/schedule/ReductionSpec.h"
#include "driver/DriverUtils.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/IdealGas.h"

#include <algorithm>
#include <array>
#include <atomic>
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
    // Reduction oracle: enumerate the physical candidates serially. Analytic
    // CFL values are verified independently in curvilinear/low-density tests.
    double serial_hydro=std::numeric_limits<double>::max();
    for (int i=grid.Is();i<grid.Ie();++i)
        serial_hydro=std::min(serial_hydro,evaluate_cfl_cell_dt(state.get(grid.GetIndex(i)),
            nullptr,HydroEos{},GridMetrics::make_geometry_view(grid),i,0));
    expect_bits("production.hydro.active",
                adaptive_dt(state, HydroEos{}, grid, 0.8),
                std::bit_cast<std::uint64_t>(.8*serial_hydro));
    // The generic minimum may ignore NaN sentinels, but this traversal only
    // visits active cells. One invalid physical cell must reject the patch.
    const int broken=grid.GetIndex(grid.Is()+3);
    const auto saved=state.get(broken);
    for(bool parallel : {false,true}) {
        for(double bad : {0.,-1.,std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()}) {
            state.set(broken,saved);
            state.rho[broken]=bad;
            bool rejected=false;
            try { (void)adaptive_dt(state,HydroEos{},grid,.8,parallel); }
            catch(const std::runtime_error&) { rejected=true; }
            expect(rejected,"production.hydro.single-invalid-active-density");
        }
        state.set(broken,saved);state.mom_u[broken]=std::numeric_limits<double>::infinity();
        bool rejected=false;
        try { (void)adaptive_dt(state,HydroEos{},grid,.8,parallel); }
        catch(const std::runtime_error&) { rejected=true; }
        expect(rejected,"production.hydro.single-invalid-active-candidate");
    }
    state.set(broken,saved);
    std::fill(state.rho.begin(), state.rho.end(), 0.0);
    bool rejected = false;
    try { adaptive_dt(state, HydroEos{}, grid, 0.8); }
    catch (const std::runtime_error&) { rejected = true; }
    expect(rejected, "production.hydro.invalid-density-rejected");
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
    enabled = finite_seed;
    expect_bits("production.diffusion.finite-coefficient",
        DiffFlux::adaptive_dt_diff(finite_seed_state, eos, finite_seed_grid, finite_seed, 1.0),
        std::bit_cast<std::uint64_t>(serial_limit(finite_seed_state, finite_seed_grid)));
    enabled.physics.diffusion.use_diffusion = false;
    expect_bits("production.diffusion.disabled",
        DiffFlux::adaptive_dt_diff(state, eos, grid, enabled, 0.13),
        std::bit_cast<std::uint64_t>(std::numeric_limits<double>::max()));
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
        state.eng[i] = 1.0; // Valid inactive cell; density gate does not legitimize zero thermal energy.
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
    FluidState batch_first = state, batch_second = state;
    double global_dt = 99.0;
    execute_burn_step(
        state, 2.0, DriverEos{}, burner, grid, config, global_dt);
    expect_bits("production.burn.global", global_dt, 0x4006ebdd7baf75efULL);
    expect_bits("production.burn.low.enuc",
                state.enuc_rate[grid.Is() + 2], 0x4004400000000000ULL);
    expect_bits("production.burn.high.enuc",
                state.enuc_rate[grid.Is() + 3], 0xc006400000000000ULL);
    // Two independent patches share one worker team and must retain the
    // literal single-cell energy/limiter reference above on both patches.
    const DriverBurn::HostBurnPatch patches[]{
        {&batch_first, &grid}, {&batch_second, &grid}};
    double limits[]{99.0, 99.0};
    DriverBurn::execute_host_burn_batch(patches, 2.0, DriverEos{}, burner, config, limits);
    for (int patch = 0; patch < 2; ++patch) {
        expect_bits("production.burn.batch-limit", limits[patch], 0x4006ebdd7baf75efULL);
        const auto& actual = *patches[patch].state;
        expect(actual.eng == state.eng && actual.mass_fractions == state.mass_fractions
            && actual.enuc_rate == state.enuc_rate, "production.burn.batch-state");
    }
}

/** Independent physical antiderivative for the frozen native RZ burn source.
 * rho(r)=7/8+r^2/4, Omega=1, u_r=u_z=0; even density and odd m_phi
 * supply the reflected negative-radius ghosts as well as physical cells.
 */
long double rz_burn_density_moment(long double lower,long double upper,int power)
{
    const auto integral=[&](int exponent) {
        return (std::pow(upper,exponent+1)-std::pow(lower,exponent+1))/(exponent+1);
    };
    return 7.L/8.L*integral(power)+integral(power+2)/4.L;
}

/** Compare new independent physical references with the unchanged 2e-12 gate. */
void expect_rz_burn_close(double actual,long double reference,std::string_view name)
{
    expect(std::isfinite(actual)&&std::abs(static_cast<long double>(actual)-reference)
        <=2.e-12L*std::max(1.L,std::abs(reference)),name);
}

/** Thread-safe observer for a one-zone constant integral(delta e) operation.
 * It reads the actual packed X/T and interval, transfers a known fraction,
 * and returns a fixed heat release; no production EOS/closure serves as oracle.
 */
struct NativeRzBurner {
    static constexpr int NEQ=4;
    static constexpr bool exact_input_reusable=false;
    static constexpr double delta_energy=1./32.;
    static constexpr double half_interval=1./8.;
    double expected_temperature=1./64.,expected_x0=.25;
    int failure_mode=0;
    std::atomic<int> calls{0},input_failures{0};

    template<class Eos>
    bool integrate(double* packed,double rho,double interval,const Eos&,
        const BurnConfig&,double& recommended,double* energy)
    {
        calls.fetch_add(1,std::memory_order_relaxed);
        bool known_density=false;
        for(int n=0;n<amr::BLOCK_NX;++n)
            known_density=known_density||rho==1.+n*(n+1)/4.;
        if(!known_density||interval!=half_interval||packed[0]!=expected_x0
           ||packed[1]!=1.-expected_x0||packed[3]!=0.
           ||!std::isfinite(packed[2])||std::abs(packed[2]-expected_temperature)>2.e-12)
            input_failures.fetch_add(1,std::memory_order_relaxed);
        packed[0]+=1./16.;packed[1]-=1./16.;packed[2]+=delta_energy;
        packed[3]=1.;recommended=interval;*energy=delta_energy;
        if(failure_mode==1)return false; // Mutated packed workspace must not publish.
        if(failure_mode==2)*energy=-expected_temperature-1.;
        return true;
    }
};

/** Verify real native RZ preparation, ODE input and checked energy publication.
 * The two actual wrapper calls preserve the split half-step interface, but
 * this local source test does not qualify the complete production Strang cycle
 * or scheduler/ghost-version identity. Physical ghosts are supplied explicitly.
 */
void characterize_native_rz_burn()
{
    const int failures_before=failures;
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr long double initial_internal=1.L/64.L;
    Grid grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,.5,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(2);
    for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
        const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
        const long double volume=(hi*hi-lo*lo)/2.L;
        const long double angular_measure=(hi*hi*hi-lo*lo*lo)/3.L;
        const long double rho=rz_burn_density_moment(lo,hi,1)/volume;
        const long double inertia=rz_burn_density_moment(lo,hi,3);
        const int cell=grid.GetIndex(i,j,0);
        state.set(cell,{static_cast<double>(rho),0.,0.,
            static_cast<double>(inertia/angular_measure),
            static_cast<double>(rho*initial_internal+inertia/(2.L*volume))});
        state.X(0,cell)=.25;state.X(1,cell)=.75;
    }
    const int first=grid.GetIndex(grid.Is(),grid.Js(),0);
    const auto first_native=state.get(first);
    expect_rz_burn_close(first_native.rho,1.L,"production.burn.rz.source-rho-V");
    expect_rz_burn_close(first_native.mom_w,25.L/32.L,"production.burn.rz.source-J-over-W");
    expect_rz_burn_close(first_native.eng,53.L/192.L,"production.burn.rz.source-E-V");
    expect_rz_burn_close(first_native.eng-.5*first_native.mom_w*first_native.mom_w/first_native.rho,
        -179.L/6144.L,"production.burn.rz.raw-negative-energy-reference");
    expect(arch::state::recover(first_native).status==arch::state::Status::unresolved_energy,
        "production.burn.rz.raw-mixed-energy-invalid");
    // The physical inertia 25/96 differs from the constant-rho guess 1/4.
    expect_rz_burn_close(static_cast<double>(rz_burn_density_moment(0.L,1.L,3)),25.L/96.L,
        "production.burn.rz.variable-density-inertia-reference");

    SpeciesManager species;
    species.add_species("a",1.,1.,1.4,1.);
    species.add_species("b",4.,2.,1.4,1.);
    IdealGas eos(1.4,species); // Both Cv=1, so the real EOS requires T=e.
    SimConfig config{};
    config.physics.burn.use_burn=true;
    config.physics.burn.nuclearDensMin=0.;config.physics.burn.nuclearTempMin=0.;
    config.physics.burn.smallt=1.e-12;config.physics.burn.smallx=1.e-20;
    config.physics.burn.enucDtFactor=.5;
    config.numerics.sml_rho=1.e-12;config.numerics.min_eint=1.e-10;
    config.numerics.max_eint=1.e21;
    const auto original_rho=state.rho,original_mr=state.mom_u;
    const auto original_mz=state.mom_v,original_mphi=state.mom_w;
    NativeRzBurner burner;
    for(int half=0;half<2;++half) {
        const auto previous_energy=state.eng;
        burner.expected_temperature=1./64.+half*NativeRzBurner::delta_energy;
        burner.expected_x0=.25+half/16.;
        double limit=99.;
        execute_burn_step(state,NativeRzBurner::half_interval,eos,burner,grid,config,limit,rz);
        expect(burner.calls.load(std::memory_order_relaxed)
                ==(half+1)*amr::BLOCK_NX*amr::BLOCK_NY,
            "production.burn.rz.exact-two-half-call-count");
        expect(burner.input_failures.load(std::memory_order_relaxed)==0,
            "production.burn.rz.actual-rho-X-T-half-dt-inputs");
        expect_rz_burn_close(limit,(3.L+2.L*half)/32.L,
            "production.burn.rz.physical-thermal-energy-limiter");
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const int cell=grid.GetIndex(i,j,0);
            const bool active=i>=grid.Is()&&i<grid.Ie()&&j>=grid.Js()&&j<grid.Je();
            if(active) {
                const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
                const long double rho=rz_burn_density_moment(lo,hi,1)/((hi*hi-lo*lo)/2.L);
                expect_rz_burn_close(state.eng[cell]-previous_energy[cell],
                    rho*NativeRzBurner::delta_energy,"production.burn.rz.delta-E-rho-V-delta-e");
                expect(state.X(0,cell)==.25+(half+1)/16.
                    &&state.X(1,cell)==.75-(half+1)/16.,
                    "production.burn.rz.accepted-X-publication");
                expect_bits("production.burn.rz.enuc-rate",state.enuc_rate[cell],
                    0x3fd0000000000000ULL); // (1/32)/(1/8)=1/4.
            } else {
                expect_bits("production.burn.rz.ghost-energy-unchanged",state.eng[cell],
                    std::bit_cast<std::uint64_t>(previous_energy[cell]));
                expect(state.X(0,cell)==.25&&state.X(1,cell)==.75,
                    "production.burn.rz.ghost-X-unchanged");
                expect_bits("production.burn.rz.ghost-enuc-zero",state.enuc_rate[cell],0);
            }
        }
        for(int cell=0;cell<grid.GetTotalSize();++cell) {
            expect_bits("production.burn.rz.rho-bits-unchanged",state.rho[cell],
                std::bit_cast<std::uint64_t>(original_rho[cell]));
            expect_bits("production.burn.rz.mr-bits-unchanged",state.mom_u[cell],
                std::bit_cast<std::uint64_t>(original_mr[cell]));
            expect_bits("production.burn.rz.mz-bits-unchanged",state.mom_v[cell],
                std::bit_cast<std::uint64_t>(original_mz[cell]));
            expect_bits("production.burn.rz.mphi-bits-unchanged",state.mom_w[cell],
                std::bit_cast<std::uint64_t>(original_mphi[cell]));
        }
    }

    // Publication is a cell-level contract. The batch intentionally clears
    // enuc before traversal, so use the actual cell owner to test rollback of
    // a nonzero diagnostic and changes to its temporary packed ODE workspace.
    const auto geometry=GridMetrics::make_geometry_view(grid,rz);
    burner.expected_temperature=5./64.;burner.expected_x0=.375;
    for(int failure_case=0;failure_case<4;++failure_case) {
        FluidState trial=state;
        SimConfig trial_config=config;
        if(failure_case==0)trial.eng[first]=0.;
        if(failure_case==3)trial_config.numerics.max_eint=6./64.;
        const auto before_energy=trial.eng,before_x=trial.mass_fractions;
        const auto before_enuc=trial.enuc_rate;
        burner.failure_mode=failure_case==1?1:failure_case==2?2:0;
        std::vector<double> packed(NativeRzBurner::NEQ,0.);
        DriverBurn::HostBurnMemo memo(NativeRzBurner::NEQ);
        const auto controls=make_burn_config_view(trial_config.physics.burn);
        bool rejected=false;
        try {
            (void)DriverBurn::advance_host_burn_cell(trial,first,
                NativeRzBurner::half_interval,eos,burner,trial_config,controls,packed,memo,
                &geometry,grid.Is());
        } catch(const std::runtime_error&) {rejected=true;}
        expect(rejected,"production.burn.rz.invalid-closure-ODE-heat-bounds-rejected");
        expect(trial.eng==before_energy&&trial.mass_fractions==before_x
            &&trial.enuc_rate==before_enuc,
            "production.burn.rz.failed-cell-does-not-publish-E-X-enuc");
        expect(trial.rho==original_rho&&trial.mom_u==original_mr
            &&trial.mom_v==original_mz&&trial.mom_w==original_mphi,
            "production.burn.rz.failed-cell-keeps-native-moments");
    }
    std::cout<<"RZ_NATIVE_BURN half_steps=2 active_cells="
        <<amr::BLOCK_NX*amr::BLOCK_NY<<" failure_cases=4 "
        <<(failures==failures_before?"PASS":"FAIL")<<'\n';
}

/** Check actual DriverUtils native CFL traversal against physical acoustics.
 * Native rho_V/J/W/E_V are independent antiderivatives of the same quadratic
 * density/rigid-rotation source as the burn case. Kappa=rho_V*W^2/(V*I)
 * gives the known e0, while only u_r/u_z and the actual length spacings enter
 * the two-face CFL rate. No closure/CFL leaf is called to form the reference.
 * This is a fluid CFL adapter test, not a source/RKL stability certificate.
 */
void characterize_native_rz_cfl()
{
    const int failures_before=failures;
    constexpr auto rz=GridMetrics::GeometrySemantics::AxisymmetricRz;
    constexpr long double internal=1.L/64.L,radial=1.L/8.L,axial=-1.L/4.L;
    constexpr double cfl=.4;
    SpeciesManager species;
    species.add_species("a",1.,1.,1.5,2.);
    species.add_species("b",4.,2.,1.75,4.);
    IdealGas eos(1.4,species);
    amr::Block block{};
    block.active=true;
    block.grid=Grid(amr::MAX_NG,0.,amr::BLOCK_NX,-.5,-.5+amr::BLOCK_NY/2.,0.,1.);
    auto& grid=block.grid;grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(rz);
    auto& state=block.fluid_state;
    state.Preallocate(grid.GetTotalSize());state.InitSpecies(2);
    double zero_rotation_dt=0.;
    for(int omega=0;omega<=1;++omega) {
        long double reference=std::numeric_limits<long double>::max();
        for(int j=0;j<grid.GetTotalY();++j)for(int i=0;i<grid.GetTotalX();++i) {
            const long double lo=grid.GetFacePosL(i),hi=grid.GetFacePosR(i);
            const long double volume=(hi*hi-lo*lo)/2.L;
            const long double angular_measure=(hi*hi*hi-lo*lo*lo)/3.L;
            const long double rho=rz_burn_density_moment(lo,hi,1)/volume;
            const long double inertia=rz_burn_density_moment(lo,hi,3);
            const long double ur=hi<=0.L?-radial:radial;
            const long double phi=omega*inertia/angular_measure;
            const long double energy=rho*(internal+(ur*ur+axial*axial)/2.L)
                +omega*omega*inertia/(2.L*volume);
            const int cell=grid.GetIndex(i,j,0);
            state.set(cell,{static_cast<double>(rho),static_cast<double>(rho*ur),
                static_cast<double>(rho*axial),static_cast<double>(phi),static_cast<double>(energy)});
            state.X(0,cell)=.25+(i-grid.Is())/128.+(j-grid.Js())/256.;
            state.X(1,cell)=1.-state.X(0,cell);
            if(i<grid.Is()||i>=grid.Ie()||j<grid.Js()||j>=grid.Je())continue;
            const long double kappa=rho*angular_measure*angular_measure/(volume*inertia);
            const long double decoded=(energy-rho*(ur*ur+axial*axial)/2.L
                -kappa*phi*phi/(2.L*rho))/rho;
            expect_rz_burn_close(static_cast<double>(decoded),internal,
                "production.cfl.rz.independent-native-kappa-e0-reference");
            if(i==grid.Is())expect_rz_burn_close(static_cast<double>(kappa),64.L/75.L,
                "production.cfl.rz.variable-density-first-cell-kappa");
            const long double x0=state.X(0,cell),x1=state.X(1,cell);
            const long double gamma_minus_one=(x0+3.L*x1)/(2.L*x0+4.L*x1);
            const long double sound=std::sqrt((1.L+gamma_minus_one)*gamma_minus_one*internal);
            // Independent r/z length transport: dr=1, dz=1/2. Phi is inactive.
            const long double rate=(std::abs(radial)+sound)+(std::abs(axial)+sound)/.5L;
            reference=std::min(reference,static_cast<long double>(cfl)/(2.L*rate));
        }
        const FluidState original=state;
        if(omega)expect(arch::state::recover(state.get(grid.GetIndex(grid.Is(),grid.Js(),0))).status
                ==arch::state::Status::unresolved_energy,
            "production.cfl.rz.cold-raw-mean-is-not-a-point");
        const double serial=adaptive_dt(block.fluid_state,eos,block.grid,cfl,false,rz);
        const double parallel=adaptive_dt(block.fluid_state,eos,block.grid,cfl,true,rz);
        expect_rz_burn_close(serial,reference,"production.cfl.rz.actual-serial-physical-acoustic-dt");
        expect_rz_burn_close(parallel,reference,"production.cfl.rz.actual-parallel-physical-acoustic-dt");
        expect_bits("production.cfl.rz.adapter-reduction-schedule-stable",parallel,
            std::bit_cast<std::uint64_t>(serial));
        if(!omega)zero_rotation_dt=serial;
        else expect_rz_burn_close(serial,zero_rotation_dt,
            "production.cfl.rz.inactive-phi-does-not-change-fluid-transport-bound");
        expect(state.rho==original.rho&&state.mom_u==original.mom_u
            &&state.mom_v==original.mom_v&&state.mom_w==original.mom_w
            &&state.eng==original.eng&&state.mass_fractions==original.mass_fractions
            &&state.enuc_rate==original.enuc_rate,"production.cfl.rz.immutable-block-stage");
    }

    const int first=grid.GetIndex(grid.Is(),grid.Js(),0);
    for(bool parallel:{false,true}) {
        for(double bad:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::infinity()}) {
            FluidState trial=state;trial.rho[first]=bad;
            bool rejected=false;
            try {(void)adaptive_dt(trial,eos,grid,cfl,parallel,rz);}
            catch(const std::runtime_error&) {rejected=true;}
            expect(rejected,"production.cfl.rz.invalid-active-density-rejected");
        }
        FluidState missing_ghost=state;
        missing_ghost.rho[grid.GetIndex(grid.Is()-1,grid.Js(),0)]=0.;
        bool rejected=false;
        try {(void)adaptive_dt(missing_ghost,eos,grid,cfl,parallel,rz);}
        catch(const std::runtime_error&) {rejected=true;}
        expect(rejected,"production.cfl.rz.missing-required-density-ghost-rejected");

        // Actual zero-halo Grid/state storage, not a forged closure context.
        amr::Block no_halo{};
        no_halo.grid=Grid(0,0.,amr::BLOCK_NX,-.5,-.5+amr::BLOCK_NY/2.,0.,1.);
        no_halo.grid.dim=2;no_halo.grid.geometry="cylindrical";no_halo.grid.InitializeTopology(rz);
        no_halo.fluid_state.Preallocate(no_halo.grid.GetTotalSize());no_halo.fluid_state.InitSpecies(2);
        for(int j=0;j<amr::BLOCK_NY;++j)for(int i=0;i<amr::BLOCK_NX;++i) {
            const int cell=no_halo.grid.GetIndex(i,j,0),source=grid.GetIndex(grid.Is()+i,grid.Js()+j,0);
            no_halo.fluid_state.set(cell,state.get(source));
            no_halo.fluid_state.X(0,cell)=state.X(0,source);
            no_halo.fluid_state.X(1,cell)=state.X(1,source);
        }
        rejected=false;
        try {(void)adaptive_dt(no_halo.fluid_state,eos,no_halo.grid,cfl,parallel,rz);}
        catch(const std::runtime_error&) {rejected=true;}
        expect(rejected,"production.cfl.rz.actual-grid-without-halo-rejected");
    }
    std::cout<<"RZ_NATIVE_CFL serial_parallel=1 rotation_cases=2 invalid_inputs=12 "
        <<(failures==failures_before?"PASS":"FAIL")<<'\n';
}

// The counter is observational only; accepted outputs depend on the full key.
struct CountingBurner {
    static constexpr bool exact_input_reusable = true;
    int calls = 0;
    bool fail_next = false;
    template<class Eos>
    bool integrate(double* values, double rho, double dt, const Eos&,
        const BurnConfig&, double& recommended, double* energy)
    {
        ++calls;
        if (fail_next) { fail_next = false; return false; }
        *energy = values[0] + 2*values[1] + values[2] + values[3] + rho + dt;
        recommended = dt / (1+rho);
        for (int i=0; i<4; ++i) values[i] += (i+1)*dt;
        return true;
    }
};

void verify_burn_memo()
{
    DriverBurn::HostBurnMemo memo(4);
    CountingBurner burner;
    const BurnConfig controls{};
    const std::vector<double> input{.75,.25,100.,0.};
    auto solve = [&](std::vector<double> values, double rho, double dt) {
        const auto initial = values;
        double recommended=dt, energy=0;
        expect(memo.integrate(values,rho,dt,DriverEos{},burner,controls,recommended,energy),
               "memo.accepted");
        expect(energy == initial[0]+2*initial[1]+initial[2]+initial[3]+rho+dt
            && recommended == dt/(1+rho), "memo.complete-output");
        for(int i=0;i<4;++i) expect(values[i]==initial[i]+(i+1)*dt,"memo.packed-output");
    };
    solve(input,2.,.125); solve(input,2.,.125);
    expect(burner.calls==1,"memo.exact-hit");
    solve(input,3.,.125); solve(input,3.,.25);
    expect(burner.calls==3,"memo.rho-interval-key");
    for(int i=0;i<4;++i) { auto changed=input; changed[i]=std::nextafter(changed[i],1.);
        solve(changed,2.,.125); }
    expect(burner.calls==7,"memo.every-packed-bit-key");
    // More distinct keys than slots force collisions without assuming a hash.
    for(int round=0;round<2;++round) for(int i=0;i<600;++i) {
        auto changed=input; changed[3]=i*.125; solve(changed,2.,.125);
    }
    auto failed=input; failed[2]=777.; double dt=.125, energy=0;
    burner.fail_next=true;
    expect(!memo.integrate(failed,2.,.125,DriverEos{},burner,controls,dt,energy),"memo.failure");
    const int after_failure=burner.calls;
    solve(failed,2.,.125);
    expect(burner.calls==after_failure+1,"memo.failure-not-cached");
    DriverBurn::HostBurnMemo next_half(4);
    auto again=input;
    const int previous=burner.calls;
    next_half.integrate(again,2.,.125,DriverEos{},burner,controls,dt,energy);
    expect(burner.calls==previous+1,"memo.no-cross-half-reuse");
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
    characterize_native_rz_burn();
    characterize_native_rz_cfl();
    verify_burn_memo();
    verify_block_minimum_helper();
    expect(edge_cases == 36, "edge.case.count");
    if (failures == 0) {
        std::cout << "D2_REDUCTION_CONTRACT_PASS edge_cases="
                  << edge_cases << '\n';
    }
    return failures == 0 ? 0 : 1;
}
