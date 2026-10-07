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
} // namespace Physical::Gravity
