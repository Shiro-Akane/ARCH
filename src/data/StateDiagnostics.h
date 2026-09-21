#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <mutex>
#include <vector>
#include "core/ArchPortability.h"

namespace arch::state {
// Exceptions cannot cross an OpenMP structured region. Workers preserve the
// first failure; the caller rethrows only after all workers have joined and
// before publishing the stage. No process-wide exit is used by the library.
class HostFailure {
    std::mutex mutex_;
    std::exception_ptr error_;
public:
    void capture_current() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!error_) error_ = std::current_exception();
    }
    void rethrow() const { if (error_) std::rethrow_exception(error_); }
};

// Compact per-block accounting, independent of fluid field layout. Indices
// 0/1: event count/affected volume; 2/3: signed/absolute mass; 4..6: momentum;
// 7/8: signed/absolute energy; 9: one triggering local cell. Species follow as
// signed/absolute pairs. The device borrows one row, never a host vector.
struct RepairView {
    double* values = nullptr;
    int species = 0;
    static constexpr int fixed_size = 10;
    ARCH_INLINE void add(int index, double value) const {
        if (!values) return;
#if defined(__CUDA_ARCH__)
        atomicAdd(values + index, value);
#else
        values[index] += value;
#endif
    }
    ARCH_INLINE void event(double volume, int cell) const {
        if (!values) return;
#if defined(__CUDA_ARCH__)
        const double previous = atomicAdd(values, 1.0);
#else
        const double previous = values[0]++;
#endif
        if (previous == 0.0) values[9] = cell;
        add(1, volume);
    }
    ARCH_INLINE void conserved(double mass, double px, double py, double pz, double energy) const {
        add(2, mass); add(3, std::abs(mass));
        add(4, px); add(5, py); add(6, pz);
        add(7, energy); add(8, std::abs(energy));
    }
    ARCH_INLINE void species_mass(int index, double delta) const {
        add(fixed_size + 2 * index, delta);
        add(fixed_size + 2 * index + 1, std::abs(delta));
    }
};

struct RepairBudget {
    std::vector<double> values;
    std::uint64_t block_uid = 0; // 0 identifies initialization before topology publication.
    int stage = 0; // 0 initial state, 1..3 Hydro Runge-Kutta stage.
    double time = 0.0;
    double position[3]{}; // Physical coordinates of one triggering cell.

    RepairBudget() : RepairBudget(0) {}
    explicit RepairBudget(int species) : values(RepairView::fixed_size + 2 * species, 0.0) {}
    int species() const { return static_cast<int>((values.size() - RepairView::fixed_size) / 2); }
    void reset(int species) { *this = RepairBudget(species); }
    RepairView view() { return {values.data(), species()}; }
    void combine(const RepairBudget& other, double weight = 1.0) {
        if (values.size() < other.values.size()) values.resize(other.values.size(), 0.0);
        if (values[0] == 0.0 && other.values[0] > 0.0) {
            values[9] = other.values[9]; block_uid = other.block_uid;
            stage = other.stage; time = other.time;
            std::copy_n(other.position, 3, position);
        }
        values[0] += other.values[0];
        for (std::size_t i = 1; i < other.values.size(); ++i)
            if (i != 9) values[i] += weight * other.values[i];
    }
};
} // namespace arch::state
