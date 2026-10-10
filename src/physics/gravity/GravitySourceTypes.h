/**
 * @file GravitySourceTypes.h
 * @brief Borrowed gravitational source views shared by launch declarations.
 *
 * Workflow:
 * 1. The actual source owner publishes resident density/face/work pointers or
 *    configured external components into these plain nonowning values.
 * 2. Backend declarations transport the unchanged views without importing Grid,
 *    quadrature, EOS, source arithmetic or execution/qualification authority.
 * 3. Actual mathematical consumers include GravitySource.h and retain all
 *    original source, lifetime, field-generation and stage acceptance gates.
 *
 * These types allocate no memory, own no field and certify no ready publication.
 */
#pragma once

#include <cstddef>

#include "core/ArchPortability.h"

namespace Physical::Gravity {
struct GravityPatchView {
    const double* density=nullptr;
    const double* faces[3]{};      // Physical acceleration at each native face.
    // Both are indexed by the SAME padded cell offset. Unlike acceleration,
    // curved work depends on this cell's volume and potential; adjacent cells
    // must retain different coefficients even at the same physical face.
    const double* work_low[3]{};
    const double* work_high[3]{};
    /** Report whether a resident density and face field have been published. */
    ARCH_INLINE bool enabled() const {return density!=nullptr;}
};
struct ExternalGravityView {
    double g_x = 0.0, g_y = 0.0, g_z = 0.0;
    bool enabled = false;
};

/**
 * @brief Optional resident Self-gravity rows for energy registration.
 *
 * Workflow:
 * 1. The actual Source owner publishes psi as resident per-stage global rows
 *    and maps every original route.plan.operations index to the SAME stage
 *    global psi row through row_of_energy_operation. That index is the original
 *    operations position; it is never a compiled-term ordinal.
 * 2. A paired CUDA registration reads at most one borrowed psi row per energy
 *    operation and reports arithmetic refusal through status.
 * 3. The runtime owner keeps the borrowed metadata, psi rows and status alive
 *    through the joined status of every queued use; returned status is the
 *    owner's provisional signal, not accepted-state publication.
 *
 * The view is plain and nonowning: it allocates no memory, dereferences no
 * pointer here, owns no field and grants no source, EOS, field-generation or
 * execution authority. A null psi means the ordinary SourceNone route.
 */
struct NativeSelfRefluxView {
    const double* psi=nullptr;
    // row_of_energy_operation[original operations index] = global psi row.
    const std::size_t* row_of_energy_operation=nullptr;
    std::size_t operation_count=0,row_count=0;
    int* status=nullptr;
};
} // namespace Physical::Gravity
