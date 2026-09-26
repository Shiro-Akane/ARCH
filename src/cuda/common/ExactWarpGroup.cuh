/**
 * @file ExactWarpGroup.cuh
 * @brief Execute identical pure inputs once within the currently active warp.
 *
 * Workflow:
 * 1. Begin with active lanes at this call site (or one lane on older devices).
 * 2. Partition by every input bit; no hash, tolerance or approximate bin exists.
 * 3. Let each group's first lane call the original shared mathematical policy.
 * 4. Broadcast the result to that group before the caller validates/commits it.
 *
 * The kernel owns immutable policy/configuration and storage lifetime. Callers
 * must include every varying input and preserve any failure status separately.
 * This adapter has no physical formulas and stores no result between launches.
 */
#pragma once

#include <cuda_runtime.h>

#include "core/ArchPortability.h"

namespace arch::cuda {
class ExactWarpGroup {
    unsigned members_ = 1;
    int lane_ = 0;
public:
    /** Restrict reuse to devices with exact warp match and opted-in policies. */
    ARCH_INLINE explicit ExactWarpGroup(bool enabled = true) {
#if defined(__CUDA_ARCH__)
        lane_ = threadIdx.x & 31;
#if __CUDA_ARCH__ >= 700
        members_ = enabled ? __activemask() : (1u << lane_);
#else
        members_ = 1u << lane_;
#endif
#else
        static_cast<void>(enabled);
#endif
    }

    /** Refine a disjoint group by exact 64-bit equality. */
    ARCH_INLINE void match_word(unsigned long long value) {
#if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 700
        members_ = __match_any_sync(members_, value);
#else
        static_cast<void>(value);
#endif
    }

    /** Preserve all binary64 bits, including signed zero and NaN payloads. */
    ARCH_INLINE void match(double value) {
#if defined(__CUDA_ARCH__)
        match_word(static_cast<unsigned long long>(__double_as_longlong(value)));
#else
        static_cast<void>(value);
#endif
    }

    /** Identify the sole mathematical evaluator in this group. */
    ARCH_INLINE bool leader() const {
#if defined(__CUDA_ARCH__)
        return lane_ == __ffs(members_) - 1;
#else
        return true;
#endif
    }

    /** Share one returned scalar; all group members call this point. */
    template<class Scalar>
    ARCH_INLINE Scalar broadcast(Scalar value) const {
#if defined(__CUDA_ARCH__)
        return __shfl_sync(members_, value, __ffs(members_) - 1);
#else
        return value;
#endif
    }
};
} // namespace arch::cuda
