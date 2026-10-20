/**
 * @file test_conservative_acceptance.cu
 * @brief Device/host leaf parity for accept_conservative_state and its receipt row.
 *
 * Contract acceptance-tests-1. The frozen shared leaf is compiled for host and
 * device without changes. Statuses and repaired fraction bits agree exactly; the
 * shared receipt row is accumulated with device atomics, so its volume and
 * species slots use the stated roundoff allowance below and the trigger cell is
 * checked by membership instead of by a fixed value. CUDA errors - including a
 * missing or unusable device - are reported as visible failures; this leaf never
 * passes by skipping the comparison.
 *
 * Scope limits: a unit leaf parity check, not a full RT/AMR evolution, not a
 * global positivity theorem, and it does not alter production tolerances.
 */
#include "numerics/state/StateAdmissibility.h"

#include <cuda_runtime.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using arch::state::RepairBudget;
using arch::state::RepairView;
using arch::state::Status;

constexpr int kSpecies = 3;
constexpr int kCells = 6;
constexpr int kRowSize = RepairView::fixed_size + 2 * kSpecies;
constexpr double kDensityFloor = 0.0;
constexpr double kEnergyFloor = 0.0;
constexpr double kEnergyCeiling = 1.0e30;

// Receipt slots that device atomics accumulate. The per-cell increments are
// completed in an arbitrary order, so a slot built from several terms may differ
// from the host sum by a few ulps of its own magnitude. The allowance is
// therefore scale-relative: 64 eps * |expected|. The earlier max(1, |expected|)
// form allowed 1.42e-14 absolute error, which an entirely zero receipt satisfied
// for the ~1e-232 species slots below, so it could not test nonzero receipts.
// A denormal floor is still required because those trace receipts underflow far
// below DBL_MIN (2.2e-308) and relative error is ill-defined there; at
// 8 * DBL_TRUE_MIN (~3.95e-323) it is orders of magnitude smaller than the
// smallest representable nonzero receipt asserted here (~1.7e-232), so a zeroed
// nonzero receipt is still rejected. Statuses and fraction bits are compared
// exactly: this allowance never weakens state acceptance.
constexpr double kReceiptRelTolerance = 64.0 * std::numeric_limits<double>::epsilon();
constexpr double kReceiptDenormalFloor = 8.0 * std::numeric_limits<double>::denorm_min();

struct CellInput {
    FluidVector fluid;
    double fractions[kSpecies];
};

// Cells 1 and 2 carry the documented trace negatives and are repaired. Cell 0 is
// legal; cells 3..5 are rejected (below -limit, non-negative sum != 1, non-finite)
// and must leave their fractions and the shared receipt untouched.
const CellInput kInputs[kCells] = {
    {FluidVector(2.0, 0.3, -0.2, 0.05, 5.0), {1.0, 0.0, 0.0}},
    {FluidVector(2.5, 0.1, -0.4, 0.0, 6.0), {0.6, -1.61e-218, 0.4}},
    {FluidVector(3.0, 0.2, 0.1, -0.3, 7.0), {1.0, -2.05e-143, -1.61e-218}},
    {FluidVector(1.0, 0.0, 0.0, 0.0, 2.0), {0.5, -1.0e-13, 0.5}},
    {FluidVector(1.5, 0.0, 0.0, 0.0, 3.0), {0.4, -1.0e-200, 0.4}},
    {FluidVector(0.75, 0.0, 0.0, 0.0, 1.5),
     {0.5, std::numeric_limits<double>::quiet_NaN(), 0.5}},
};

const double kVolumes[kCells] = {1.0e-15, 2.0e-15, 3.5e-15, 1.0e-15, 2.0e-15, 1.0e-15};

int checks = 0;
int failures = 0;

void check(bool ok, const char* what, int cell = -1)
{
    ++checks;
    if (!ok) {
        std::cerr << "FAIL " << what;
        if (cell >= 0) std::cerr << " cell=" << cell;
        std::cerr << '\n';
        ++failures;
    }
}

void checked(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

bool receipt_slot_matches(double actual, double expected)
{
    if (expected == 0.0) return actual == 0.0;
    const double allowance =
        std::max(kReceiptRelTolerance * std::abs(expected), kReceiptDenormalFloor);
    return std::abs(actual - expected) <= allowance;
}

// Host reference for one cell: the same frozen leaf on the host with its own
// receipt row, so host/device statuses and fraction bits can be compared directly.
struct HostCell {
    Status status = Status::valid;
    double fractions[kSpecies]{};
    double row[kRowSize]{};
};

HostCell run_host_cell(int cell)
{
    HostCell result;
    for (int species = 0; species < kSpecies; ++species)
        result.fractions[species] = kInputs[cell].fractions[species];
    result.status = arch::state::accept_conservative_state(
        kInputs[cell].fluid, result.fractions, kSpecies, 1, kDensityFloor, kEnergyFloor,
        kEnergyCeiling, kVolumes[cell], RepairView{result.row, kSpecies}, cell);
    return result;
}

__global__ void evaluate_cells(const CellInput* inputs, const double* volumes, int count,
                               double* shared_receipt, double* fractions_out,
                               unsigned char* statuses_out)
{
    const int cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell >= count) return;
    double fractions[kSpecies];
    for (int species = 0; species < kSpecies; ++species)
        fractions[species] = inputs[cell].fractions[species];
    const Status status = arch::state::accept_conservative_state(
        inputs[cell].fluid, fractions, kSpecies, 1, kDensityFloor, kEnergyFloor,
        kEnergyCeiling, volumes[cell], RepairView{shared_receipt, kSpecies}, cell);
    statuses_out[cell] = static_cast<unsigned char>(status);
    for (int species = 0; species < kSpecies; ++species)
        fractions_out[cell * kSpecies + species] = fractions[species];
}

// A negative recurrence gain may reverse signed corrections, but volume and
// absolute-error budgets must stay non-negative on this leaf too.
void check_negative_weight_combine()
{
    RepairBudget block(2);
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

    RepairBudget merged(2);
    merged.combine(block, -1.5);
    check(merged.values[0] == 1.0, "combine: event count is not weighted");
    check(merged.values[1] == 6.0 && merged.values[3] == 4.5 && merged.values[8] == 6.0
              && merged.values[11] == 9.0 && merged.values[13] == 10.5,
          "combine: |weight| scales volume and absolute budgets");
    check(merged.values[2] == 4.5 && merged.values[4] == 1.5 && merged.values[5] == 3.0
              && merged.values[6] == 4.5 && merged.values[7] == 6.0
              && merged.values[10] == 9.0 && merged.values[12] == 10.5,
          "combine: the signed weight scales signed budgets");
    check(merged.values[9] == 5.0, "combine: the triggering cell is copied");
    const int absolute_slots[] = {1, 3, 8, 11, 13};
    for (int slot : absolute_slots)
        check(merged.values[slot] >= 0.0,
              "combine: absolute budget stays non-negative");
}

} // namespace

int main()
{
    int devices = 0;
    const cudaError_t probe = cudaGetDeviceCount(&devices);
    if (probe != cudaSuccess || devices == 0) {
        // Visible failure, not a silent skip: the contract for this leaf requires
        // CUDA problems to be reported instead of reported as a pass.
        std::cerr << "CUDA device unavailable: " << cudaGetErrorString(probe)
                  << " (devices=" << devices << ")\n";
        return 1;
    }

    CellInput* device_inputs = nullptr;
    double* device_volumes = nullptr;
    double* device_receipt = nullptr;
    double* device_fractions = nullptr;
    unsigned char* device_statuses = nullptr;

    try {
        checked(cudaMalloc(&device_inputs, sizeof(kInputs)));
        checked(cudaMalloc(&device_volumes, sizeof(kVolumes)));
        checked(cudaMalloc(&device_receipt, sizeof(double) * kRowSize));
        checked(cudaMalloc(&device_fractions, sizeof(double) * kCells * kSpecies));
        checked(cudaMalloc(&device_statuses, sizeof(unsigned char) * kCells));
        checked(cudaMemcpy(device_inputs, kInputs, sizeof(kInputs), cudaMemcpyHostToDevice));
        checked(cudaMemcpy(device_volumes, kVolumes, sizeof(kVolumes), cudaMemcpyHostToDevice));
        checked(cudaMemset(device_receipt, 0, sizeof(double) * kRowSize));
        evaluate_cells<<<1, kCells>>>(device_inputs, device_volumes, kCells, device_receipt,
                                      device_fractions, device_statuses);
        checked(cudaGetLastError());
        checked(cudaDeviceSynchronize());

        std::array<double, kRowSize> device_row{};
        std::array<double, kCells * kSpecies> device_cells{};
        std::array<unsigned char, kCells> device_state{};
        std::array<CellInput, kCells> echoed{};
        checked(cudaMemcpy(device_row.data(), device_receipt, sizeof(double) * kRowSize,
                           cudaMemcpyDeviceToHost));
        checked(cudaMemcpy(device_cells.data(), device_fractions,
                           sizeof(double) * device_cells.size(), cudaMemcpyDeviceToHost));
        checked(cudaMemcpy(device_state.data(), device_statuses, device_state.size(),
                           cudaMemcpyDeviceToHost));
        // The kernel receives const inputs; this pins that device execution leaves
        // the caller's fluid and fraction storage byte-identical.
        checked(cudaMemcpy(echoed.data(), device_inputs, sizeof(echoed),
                           cudaMemcpyDeviceToHost));
        checked(cudaFree(device_statuses));
        checked(cudaFree(device_fractions));
        checked(cudaFree(device_receipt));
        checked(cudaFree(device_volumes));
        checked(cudaFree(device_inputs));

        std::array<HostCell, kCells> host{};
        std::array<double, kRowSize> expected_row{};
        for (int cell = 0; cell < kCells; ++cell) {
            host[cell] = run_host_cell(cell);
            for (int slot = 0; slot < kRowSize; ++slot)
                if (slot != 9) expected_row[slot] += host[cell].row[slot];
        }

        // Fixture self-checks: a mislabelled cell must fail here, not look like a
        // host/device divergence.
        check(host[0].status == Status::valid, "fixture: cell 0 is legal", 0);
        check(host[1].status == Status::repaired && host[2].status == Status::repaired,
              "fixture: documented traces are repaired");
        check(host[3].status == Status::invalid_composition
                  && host[4].status == Status::invalid_composition
                  && host[5].status == Status::invalid_composition,
              "fixture: invalid cells are rejected");
        check(expected_row[0] == 2.0, "fixture: exactly two repaired cells");
        check(expected_row[1] == kVolumes[1] + kVolumes[2],
              "fixture: repaired volume is the sum of the repaired cells");
        // The tightened comparison must separate a zeroed receipt from a
        // representable nonzero expected correction; otherwise a dropped trace
        // receipt would slip through the roundoff allowance and pass this leaf.
        check(expected_row[12] != 0.0 && expected_row[14] != 0.0,
              "fixture: trace species receipts are representable and nonzero");
        check(std::abs(expected_row[12]) < 1.0e-100 && std::abs(expected_row[14]) < 1.0e-100,
              "fixture: trace receipts sit far below the former absolute allowance");
        check(!receipt_slot_matches(0.0, expected_row[12]),
              "receipt comparison rejects a zeroed nonzero trace receipt");
        check(!receipt_slot_matches(0.0, expected_row[14]),
              "receipt comparison rejects a zeroed nonzero trace receipt (species 2)");
        check(!receipt_slot_matches(1.0e-14, expected_row[14]),
              "receipt comparison rejects the former 1.42e-14 absolute error");
        check(receipt_slot_matches(expected_row[14], expected_row[14]),
              "receipt comparison accepts an exact trace receipt");
        // 32 ulps of drift is inside kReceiptRelTolerance, which is 64 ulps.
        check(receipt_slot_matches(
                  expected_row[14] * (1.0 + 32.0 * std::numeric_limits<double>::epsilon()),
                  expected_row[14]),
              "receipt comparison accepts a few-ulp atomic-reduction drift");
        check(expected_row[10] == 0.0 && receipt_slot_matches(expected_row[10], expected_row[10]),
              "receipt comparison accepts an exact zero expectation");
        check(!receipt_slot_matches(4.0 * std::numeric_limits<double>::denorm_min(), 0.0),
              "receipt comparison requires exact zero where zero is expected");
        check(std::memcmp(echoed.data(), kInputs, sizeof(kInputs)) == 0,
              "device leaves the caller's fluid and fraction storage unchanged");

        for (int cell = 0; cell < kCells; ++cell) {
            check(device_state[cell] == static_cast<unsigned char>(host[cell].status),
                  "device status equals the host leaf status", cell);
            for (int species = 0; species < kSpecies; ++species)
                check(bits(device_cells[cell * kSpecies + species])
                          == bits(host[cell].fractions[species]),
                      "device fraction bits equal the host leaf bits", cell);
        }

        check(device_row[0] == expected_row[0],
              "device counts exactly the repaired cells");
        for (int slot = 2; slot <= 8; ++slot)
            check(device_row[slot] == 0.0,
                  "device records no total-fluid correction");
        check(receipt_slot_matches(device_row[1], expected_row[1]),
              "device receipt volume matches the host leaf");
        for (int slot = RepairView::fixed_size; slot < kRowSize; ++slot)
            check(receipt_slot_matches(device_row[slot], expected_row[slot]),
                  "device species receipt matches the host leaf");
        check(device_row[9] == 1.0 || device_row[9] == 2.0,
              "device trigger cell is one of the repaired cells");
    } catch (const std::exception& error) {
        std::cerr << "CUDA failure: " << error.what() << '\n';
        return 1;
    }

    check_negative_weight_combine();
    std::cout << "CONSERVATIVE_ACCEPTANCE_CUDA_PASS checks=" << checks << '\n';
    return failures == 0 ? 0 : 1;
}
