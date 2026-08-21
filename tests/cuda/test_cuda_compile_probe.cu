#include "core/ArchPortability.h"

ARCH_HOST_DEVICE ARCH_FORCE_INLINE int arch_cuda_probe_value(int value)
{
    return value + 1;
}

ARCH_INLINE int arch_cuda_inline_probe_value(int value)
{
    return value * 2;
}

ARCH_FORCEINLINE int arch_cuda_forceinline_probe_value(int value)
{
    return value - 42;
}

__global__ void arch_cuda_probe_kernel(int* result)
{
    *result = arch_cuda_probe_value(41);
}

int main()
{
    // The target is a compile/link probe only; it intentionally owns no
    // production physics, runner, boundary, or integrator implementation.
    return arch_cuda_inline_probe_value(0) + arch_cuda_forceinline_probe_value(42);
}
