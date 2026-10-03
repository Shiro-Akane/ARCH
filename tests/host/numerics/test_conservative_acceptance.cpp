/**
 * @file test_conservative_acceptance.cpp
 * @brief Host leaf regression for accept_conservative_state and its receipt rules.
 *
 * Contract acceptance-tests-1. The frozen shared leaf in
 * numerics/state/StateAdmissibility.h and the RepairView/RepairBudget ledger in
 * data/StateDiagnostics.h are used unchanged. Each cell is checked against the
 * documented correction Delta M_s = -rho * X_s * V for X_s < 0 inside the trace
 * band composition_roundoff_limit = 64 * double epsilon, and every rejected cell
 * is checked to leave fractions, fluid and receipt bits untouched.
 *
 * Scope limits: a single-cell unit test of a shared numerical leaf. It does not
 * evolve an RT/AMR state, does not prove global positivity, does not re-derive
 * the production threshold and does not replace the device parity leaf in
 * tests/cuda/numerics.
 */
#include "numerics/state/StateAdmissibility.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using arch::state::RepairBudget;
using arch::state::RepairView;
using arch::state::Status;

// One trace band, one reference state and one triggering cell for the host leaf.
constexpr double kLimit = arch::state::composition_roundoff_limit;
constexpr int kSpecies = 3;
constexpr int kCell = 7;
constexpr double kVolume = 3.0e-15;
constexpr double kEnergyCeiling = 1.0e30;

int checks = 0;
int failures = 0;

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

bool same_bits(double left, double right) { return bits(left) == bits(right); }

void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++failures;
    }
}

const char* status_name(Status status)
{
    switch (status) {
    case Status::valid: return "valid";
    case Status::repaired: return "repaired";
    case Status::nonfinite: return "nonfinite";
    case Status::nonpositive_density: return "nonpositive_density";
    case Status::unresolved_energy: return "unresolved_energy";
    case Status::energy_ceiling: return "energy_ceiling";
    case Status::invalid_composition: return "invalid_composition";
    case Status::invalid_thermodynamics: return "invalid_thermodynamics";
    }
    return "unknown";
}

void check_status(Status actual, Status expected, const char* what)
{
    ++checks;
    if (actual != expected) {
        std::cerr << "FAIL " << what << ": expected " << status_name(expected)
                  << ", got " << status_name(actual) << '\n';
        ++failures;
    }
}

// Frozen rule: Delta M_s = -rho * X_s * V, evaluated left to right as the leaf does.
double documented_mass(const FluidVector& fluid, double fraction, double volume)
{
    return (fluid.rho * -fraction) * volume;
}

// One accepted cell: a reference conservative state plus the borrowed bounds.
struct Input {
    FluidVector fluid{2.5, 0.1, -0.4, 0.0, 6.0};
    double volume = kVolume;
    double density_floor = 0.0;
    double energy_floor = 0.0;
    double energy_ceiling = kEnergyCeiling;
    int count = kSpecies;
    int stride = 1;
    int cell = kCell;
};

Status run_cell(const Input& input, double* fractions, RepairView receipt)
{
    return arch::state::accept_conservative_state(
        input.fluid, fractions, input.count, input.stride,
        input.density_floor, input.energy_floor, input.energy_ceiling,
        input.volume, receipt, input.cell);
}

// One RepairView row plus accessors for the slot names frozen by the contract.
struct Ledger {
    explicit Ledger(int species)
        : row(RepairView::fixed_size + 2 * species, 0.0), species_(species) {}

    RepairView view() { return {row.data(), species_}; }
    double event_count() const { return row[0]; }
    double volume() const { return row[1]; }
    double trigger_cell() const { return row[9]; }
    double signed_species(int index) const { return row[RepairView::fixed_size + 2 * index]; }
    double absolute_species(int index) const { return row[RepairView::fixed_size + 2 * index + 1]; }

    bool is_empty() const
    {
        for (double value : row)
            if (value != 0.0) return false;
        return true;
    }

    // Slots 2..8 carry total mass/momentum/energy. A composition correction keeps
    // only fractions and species receipts, so these slots must stay exactly zero.
    bool has_no_fluid_change() const
    {
        for (int index = 2; index <= 8; ++index)
            if (row[index] != 0.0) return false;
        return true;
    }

    std::vector<double> row;

private:
    int species_;
};

// A rejected cell must not change any fraction bit and must not touch the ledger.
void check_rejected(const char* what, Status expected, const Input& input,
                    double* fractions, std::size_t count, Ledger& ledger,
                    RepairView receipt)
{
    std::vector<double> fractions_before;
    if (count != 0) fractions_before.assign(fractions, fractions + count);
    const std::vector<double> ledger_before = ledger.row;
    const Status status = run_cell(input, fractions, receipt);
    check_status(status, expected, what);
    for (std::size_t index = 0; index < count; ++index)
        check(same_bits(fractions[index], fractions_before[index]),
              "rejected cell keeps every fraction bit");
    check(ledger.row == ledger_before, "rejected cell keeps the receipt row");
}

// A legal zero species and a trace species survive bit-for-bit with no receipt.
void valid_compositions_are_preserved()
{
    const double cases[2][kSpecies] = {{1.0, 0.0, 1.0e-300}, {0.5, 0.5, 0.0}};
    for (int index = 0; index < 2; ++index) {
        double fractions[kSpecies] = {cases[index][0], cases[index][1], cases[index][2]};
        Ledger ledger(kSpecies);
        check_status(run_cell(Input{}, fractions, ledger.view()), Status::valid,
                     "legal composition is accepted");
        for (int species = 0; species < kSpecies; ++species)
            check(same_bits(fractions[species], cases[index][species]),
                  "legal composition keeps every fraction bit");
        check(ledger.is_empty(), "legal composition writes no receipt");
    }
}

// The documented trace negatives -2.05e-143 and -1.61e-218 are repairable and the
// receipt records exactly -rho*X*V per affected species.
void documented_traces_are_repaired()
{
    const Input input;
    double fractions[kSpecies] = {0.6, -1.61e-218, 0.4};
    Ledger ledger(kSpecies);
    check_status(run_cell(input, fractions, ledger.view()), Status::repaired,
                 "documented trace is repaired");
    check(bits(fractions[1]) == 0, "repaired trace species becomes exact zero");
    check(same_bits(fractions[0], 0.6) && same_bits(fractions[2], 0.4),
          "repair keeps positive species bit-for-bit");
    const double expected = documented_mass(input.fluid, -1.61e-218, input.volume);
    check(ledger.event_count() == 1.0, "one repaired cell is counted");
    check(same_bits(ledger.volume(), input.volume), "receipt volume is the cell volume");
    check(ledger.trigger_cell() == static_cast<double>(kCell),
          "receipt keeps the triggering cell");
    check(ledger.signed_species(1) == expected && ledger.absolute_species(1) == expected,
          "species receipt is the signed/absolute -rho*X*V");
    check(ledger.signed_species(0) == 0.0 && ledger.absolute_species(0) == 0.0
              && ledger.signed_species(2) == 0.0 && ledger.absolute_species(2) == 0.0,
          "untouched species keep zero receipt slots");
    check(ledger.has_no_fluid_change(), "repair records no total-fluid correction");
}

// x == -64*eps is inside the band; one ulp beyond it is not.
void trace_band_boundary()
{
    const Input input;
    {
        double fractions[kSpecies] = {1.0, -kLimit, 0.0};
        Ledger ledger(kSpecies);
        check_status(run_cell(input, fractions, ledger.view()), Status::repaired,
                     "negative at exactly -limit is repairable");
        check(bits(fractions[1]) == 0, "limit trace species becomes exact zero");
        check(same_bits(ledger.signed_species(1),
                        documented_mass(input.fluid, -kLimit, input.volume)),
              "limit receipt matches -rho*X*V");
    }
    {
        double fractions[kSpecies] = {1.0, -std::nextafter(kLimit, 1.0), 0.0};
        Ledger ledger(kSpecies);
        check_rejected("a negative below -limit is rejected", Status::invalid_composition,
                       input, fractions, kSpecies, ledger, ledger.view());
    }
}

// Several species and a guarded stride: only multiples of the stride are read, so
// the illegal guard values must never influence the outcome or change.
void repairs_use_guarded_strides()
{
    constexpr int count = 5;
    constexpr int stride = 3;
    Input input;
    input.count = count;
    input.stride = stride;
    double fractions[count * stride];
    for (int index = 0; index < count * stride; ++index) fractions[index] = -5.0;
    fractions[0] = 0.25;
    fractions[3] = -2.05e-143;
    fractions[6] = 0.5;
    fractions[9] = -1.61e-218;
    fractions[12] = 0.25;
    Ledger ledger(count);
    check_status(run_cell(input, fractions, ledger.view()), Status::repaired,
                 "strided trace cell is repaired");
    check(bits(fractions[3]) == 0 && bits(fractions[9]) == 0,
          "strided negative traces become exact zero");
    check(same_bits(fractions[0], 0.25) && same_bits(fractions[6], 0.5)
              && same_bits(fractions[12], 0.25),
          "strided positive species keep their bits");
    for (int index = 0; index < count * stride; ++index)
        if (index % stride != 0)
            check(same_bits(fractions[index], -5.0), "guard padding is never touched");
    check(ledger.event_count() == 1.0, "one strided cell is counted");
    check(ledger.signed_species(1) == documented_mass(input.fluid, -2.05e-143, input.volume)
              && ledger.signed_species(3)
                     == documented_mass(input.fluid, -1.61e-218, input.volume),
          "strided receipts match -rho*X*V");
    check(ledger.signed_species(0) == 0.0 && ledger.signed_species(2) == 0.0
              && ledger.signed_species(4) == 0.0,
          "untouched strided species keep zero receipt slots");
    check(ledger.has_no_fluid_change()
              && ledger.trigger_cell() == static_cast<double>(kCell),
          "strided receipt metadata is recorded");
}

void rejected_inputs_do_not_mutate_fractions_or_receipt()
{
    const Input input;
    Ledger ledger(kSpecies);
    {
        double fractions[kSpecies] = {0.4, -1.0e-200, 0.4};
        check_rejected("non-negative fractions that do not sum to one",
                       Status::invalid_composition, input, fractions, kSpecies, ledger,
                       ledger.view());
    }
    {
        double fractions[kSpecies] = {0.5, std::numeric_limits<double>::quiet_NaN(), 0.5};
        check_rejected("NaN fraction", Status::invalid_composition, input, fractions,
                       kSpecies, ledger, ledger.view());
    }
    {
        double fractions[kSpecies] = {0.5, std::numeric_limits<double>::infinity(), 0.5};
        check_rejected("infinite fraction", Status::invalid_composition, input, fractions,
                       kSpecies, ledger, ledger.view());
    }
    {
        double fractions[kSpecies] = {0.5, -std::numeric_limits<double>::infinity(), 0.5};
        check_rejected("negative infinite fraction", Status::invalid_composition, input,
                       fractions, kSpecies, ledger, ledger.view());
    }
    {
        // A repair is needed but there is no ledger that could record it.
        double fractions[kSpecies] = {0.6, -1.61e-218, 0.4};
        check_rejected("repair without a receipt", Status::invalid_composition, input,
                       fractions, kSpecies, ledger, RepairView{nullptr, kSpecies});
    }
    {
        double fractions[kSpecies] = {0.6, -1.61e-218, 0.4};
        check_rejected("receipt with too few species", Status::invalid_composition, input,
                       fractions, kSpecies, ledger,
                       RepairView{ledger.row.data(), kSpecies - 1});
    }
    {
        double fractions[kSpecies] = {0.6, -1.61e-218, 0.4};
        check_rejected("receipt with too many species", Status::invalid_composition, input,
                       fractions, kSpecies, ledger,
                       RepairView{ledger.row.data(), kSpecies + 1});
    }
    {
        Input zero_volume = input;
        zero_volume.volume = 0.0;
        double fractions[kSpecies] = {0.6, -1.61e-218, 0.4};
        check_rejected("zero receipt volume", Status::invalid_composition, zero_volume,
                       fractions, kSpecies, ledger, ledger.view());
    }
    {
        Input infinite_volume = input;
        infinite_volume.volume = std::numeric_limits<double>::infinity();
        double fractions[kSpecies] = {0.6, -1.61e-218, 0.4};
        check_rejected("infinite receipt volume", Status::invalid_composition,
                       infinite_volume, fractions, kSpecies, ledger, ledger.view());
    }
    {
        // rho * |x| underflows, so the ledger could not account for the change.
        Input underflow;
        underflow.fluid = FluidVector(1.0e-300, 0.0, 0.0, 0.0, 1.0e-290);
        underflow.volume = 1.0;
        double fractions[kSpecies] = {1.0, -1.0e-100, 0.0};
        check_rejected("underflowed receipt", Status::invalid_composition, underflow,
                       fractions, kSpecies, ledger, ledger.view());
    }
    {
        // (rho * |x|) * V overflows; the leaf must reject instead of clipping.
        Input overflow;
        overflow.fluid = FluidVector(1.0e300, 0.0, 0.0, 0.0, 1.0e305);
        overflow.volume = 1.0e300;
        double fractions[kSpecies] = {1.0, -1.4e-14, 0.0};
        check_rejected("overflowed receipt", Status::invalid_composition, overflow,
                       fractions, kSpecies, ledger, ledger.view());
    }
    {
        Input bad_density = input;
        bad_density.density_floor = 3.0;
        double fractions[kSpecies] = {1.0, -1.61e-218, 0.0};
        check_rejected("density below the configured floor",
                       Status::invalid_thermodynamics, bad_density, fractions, kSpecies,
                       ledger, ledger.view());
    }
    {
        Input bad_energy = input;
        bad_energy.energy_floor = 5.0;
        double fractions[kSpecies] = {1.0, -1.61e-218, 0.0};
        check_rejected("internal energy below the floor",
                       Status::invalid_thermodynamics, bad_energy, fractions, kSpecies,
                       ledger, ledger.view());
    }
    {
        Input ceiling = input;
        ceiling.energy_ceiling = 1.0;
        double fractions[kSpecies] = {1.0, -1.61e-218, 0.0};
        check_rejected("internal energy above the ceiling", Status::energy_ceiling,
                       ceiling, fractions, kSpecies, ledger, ledger.view());
    }
    {
        Input vacuum;
        vacuum.fluid = FluidVector(0.0, 0.0, 0.0, 0.0, 1.0);
        double fractions[kSpecies] = {1.0, -1.61e-218, 0.0};
        check_rejected("non-positive density", Status::nonpositive_density, vacuum,
                       fractions, kSpecies, ledger, ledger.view());
    }
    {
        Input nonfinite;
        nonfinite.fluid = FluidVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0,
                                      0.0, 1.0);
        double fractions[kSpecies] = {1.0, -1.61e-218, 0.0};
        check_rejected("non-finite fluid state", Status::nonfinite, nonfinite, fractions,
                       kSpecies, ledger, ledger.view());
    }
    {
        Input bad_count = input;
        bad_count.count = -1;
        double fractions[kSpecies] = {1.0, -1.61e-218, 0.0};
        check_rejected("negative species count", Status::invalid_composition, bad_count,
                       fractions, kSpecies, ledger, ledger.view());
    }
    {
        Input bad_stride = input;
        bad_stride.stride = 0;
        double fractions[kSpecies] = {1.0, -1.61e-218, 0.0};
        check_rejected("non-positive stride", Status::invalid_composition, bad_stride,
                       fractions, kSpecies, ledger, ledger.view());
    }
    check_rejected("missing fractions array", Status::invalid_composition, input, nullptr,
                   0, ledger, ledger.view());
    check(ledger.is_empty(), "rejections write no receipt at all");
    {
        Input empty = input;
        empty.count = 0;
        check_status(run_cell(empty, nullptr, RepairView{nullptr, 0}), Status::valid,
                     "an empty cell is the degenerate legal shape");
    }
}

// A rejected cell inside a mixed batch must leave the shared receipt where it was
// and a later repair must still record the first triggering cell.
void mixed_cells_keep_receipt_on_rejection()
{
    const Input input;
    Ledger ledger(kSpecies);
    double repaired_first[kSpecies] = {0.6, -1.61e-218, 0.4};
    Input first = input;
    first.cell = 0;
    check_status(run_cell(first, repaired_first, ledger.view()), Status::repaired,
                 "first cell is repaired");
    const std::vector<double> ledger_after_repair = ledger.row;

    double rejected[kSpecies] = {0.5, -1.0e-13, 0.5};
    const double rejected_before[kSpecies] = {rejected[0], rejected[1], rejected[2]};
    Input middle = input;
    middle.cell = 1;
    check_status(run_cell(middle, rejected, ledger.view()), Status::invalid_composition,
                 "invalid cell is rejected");
    for (int species = 0; species < kSpecies; ++species)
        check(same_bits(rejected[species], rejected_before[species]),
              "rejected cell keeps its fraction bits");
    check(ledger.row == ledger_after_repair,
          "rejected cell adds nothing to the shared receipt");

    double last[kSpecies] = {1.0, -2.05e-143, -1.61e-218};
    Input final = input;
    final.cell = 2;
    check_status(run_cell(final, last, ledger.view()), Status::repaired,
                 "last cell is repaired");
    check(ledger.event_count() == 2.0, "only repaired cells are counted");
    check(ledger.trigger_cell() == 0.0, "receipt keeps the first triggering cell");
    check(ledger.volume() == input.volume + input.volume, "receipt volume sums repairs");
    check(ledger.signed_species(0) == 0.0 && ledger.absolute_species(0) == 0.0,
          "species without a negative trace stays zero");
    check(ledger.signed_species(1)
              == documented_mass(input.fluid, -1.61e-218, input.volume)
                     + documented_mass(input.fluid, -2.05e-143, input.volume),
          "signed receipt accumulates both repairs");
    check(ledger.absolute_species(2)
              == documented_mass(input.fluid, -1.61e-218, input.volume),
          "absolute receipt accumulates both repairs");
    check(ledger.has_no_fluid_change(), "mixed batch records no total-fluid correction");
}

// A second acceptance of an already repaired cell is a no-op.
void second_acceptance_is_idempotent()
{
    const Input input;
    double fractions[kSpecies] = {0.6, -1.61e-218, 0.4};
    Ledger ledger(kSpecies);
    check_status(run_cell(input, fractions, ledger.view()), Status::repaired,
                 "first acceptance repairs");
    const double after[kSpecies] = {fractions[0], fractions[1], fractions[2]};
    const std::vector<double> ledger_after = ledger.row;
    check_status(run_cell(input, fractions, ledger.view()), Status::valid,
                 "second acceptance is valid");
    for (int species = 0; species < kSpecies; ++species)
        check(same_bits(fractions[species], after[species]),
              "second acceptance keeps fraction bits");
    check(ledger.row == ledger_after, "second acceptance leaves the receipt unchanged");
}

// A negative recurrence gain may reverse signed corrections, but the volume and
// absolute-error budgets must stay non-negative.
void negative_weight_combine_keeps_absolute_budgets_positive()
{
    RepairBudget block(kSpecies);
    block.values[0] = 1.0;   // event count
    block.values[1] = 4.0;   // affected volume (absolute slot)
    block.values[2] = -3.0;  // signed mass
    block.values[3] = 3.0;   // absolute mass
    block.values[4] = -1.0;  // signed momentum x
    block.values[5] = -2.0;  // signed momentum y
    block.values[6] = -3.0;  // signed momentum z
    block.values[7] = -4.0;  // signed energy
    block.values[8] = 4.0;   // absolute energy
    block.values[9] = 5.0;   // first triggering cell
    block.values[10] = -6.0; // species 0 signed
    block.values[11] = 6.0;  // species 0 absolute
    block.values[12] = -7.0;
    block.values[13] = 7.0;
    block.values[14] = -8.0;
    block.values[15] = 8.0;
    block.block_uid = 42;
    block.stage = 2;
    block.time = 0.5;
    block.position[0] = 1.0;
    block.position[1] = -2.0;
    block.position[2] = 3.0;

    RepairBudget merged(kSpecies);
    merged.combine(block, -1.5);
    check(merged.values.size() == 16 && merged.species() == kSpecies,
          "budget keeps the compact row layout");
    check(merged.values[0] == 1.0, "the event count is not weighted");
    check(merged.values[1] == 6.0, "volume uses |weight|");
    check(merged.values[2] == 4.5 && merged.values[3] == 4.5,
          "signed and absolute mass follow their own weights");
    check(merged.values[4] == 1.5 && merged.values[5] == 3.0 && merged.values[6] == 4.5,
          "signed momentum follows the signed weight");
    check(merged.values[7] == 6.0 && merged.values[8] == 6.0,
          "signed and absolute energy use their own weights");
    check(merged.values[9] == 5.0, "the triggering cell is copied, never weighted");
    check(merged.values[10] == 9.0 && merged.values[11] == 9.0 && merged.values[12] == 10.5
              && merged.values[13] == 10.5 && merged.values[14] == 12.0
              && merged.values[15] == 12.0,
          "species receipts keep signed/absolute weights");
    const int absolute_slots[] = {1, 3, 8, 11, 13, 15};
    for (int slot : absolute_slots)
        check(merged.values[slot] >= 0.0,
              "absolute budget slot stays non-negative under a negative weight");
    check(merged.block_uid == 42 && merged.stage == 2 && merged.time == 0.5
              && merged.position[0] == 1.0 && merged.position[1] == -2.0
              && merged.position[2] == 3.0,
          "the first triggering location is retained");
}

} // namespace

int main()
{
    valid_compositions_are_preserved();
    documented_traces_are_repaired();
    trace_band_boundary();
    repairs_use_guarded_strides();
    rejected_inputs_do_not_mutate_fractions_or_receipt();
    mixed_cells_keep_receipt_on_rejection();
    second_acceptance_is_idempotent();
    negative_weight_combine_keeps_absolute_budgets_positive();
    std::cout << "CONSERVATIVE_ACCEPTANCE_HOST_PASS checks=" << checks << '\n';
    return failures == 0 ? 0 : 1;
}
