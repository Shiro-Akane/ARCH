/**
 * @file ArchPortability.h
 * @brief Annotation-only portability surface shared by ordinary C++ and CUDA.
 */
#pragma once

#if defined(__CUDACC__)
#define ARCH_HOST __host__
#define ARCH_DEVICE __device__
#define ARCH_HOST_DEVICE __host__ __device__
#define ARCH_FORCE_INLINE __forceinline__
#define ARCH_FORCEINLINE __forceinline__
#define ARCH_INLINE __host__ __device__ __forceinline__
// Heavy shared leaves retain one numerical body but must not be force-expanded
// into every device policy combination. Host C++ keeps ordinary inline/ODR.
#define ARCH_HEAVY_INLINE __host__ __device__ __noinline__ inline
#else
#define ARCH_HOST
#define ARCH_DEVICE
#define ARCH_HOST_DEVICE
#define ARCH_FORCE_INLINE inline
#define ARCH_FORCEINLINE inline
#define ARCH_INLINE inline
#define ARCH_HEAVY_INLINE inline
#endif
