#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cuda_runtime.h>

#define CUDA_CHECK(call)                                                          \
    do {                                                                          \
        cudaError_t _e = (call);                                                  \
        if (_e != cudaSuccess) {                                                  \
            printf("CUDA error at %s:%d: %s\n", __FILE__, __LINE__,               \
                   cudaGetErrorString(_e));                                       \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

__global__ void saxpy(int n, double a, const double *x, double *y)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) y[i] = a * x[i] + y[i];
}

int main()
{
    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    printf("Device: %s | SM count: %d | CC: %d.%d | Mem: %.1f GB | managedMemory=%d\n",
           prop.name, prop.multiProcessorCount, prop.major, prop.minor,
           prop.totalGlobalMem / 1e9, prop.managedMemory);

    const int n = 1 << 24; // 16M doubles = 128 MB per array
    size_t bytes = n * sizeof(double);

    double *hx = (double *)malloc(bytes), *hy = (double *)malloc(bytes);
    for (int i = 0; i < n; ++i) { hx[i] = 1.0; hy[i] = 2.0; }

    double *dx, *dy;
    CUDA_CHECK(cudaMalloc(&dx, bytes));
    CUDA_CHECK(cudaMalloc(&dy, bytes));
    CUDA_CHECK(cudaMemcpy(dx, hx, bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(dy, hy, bytes, cudaMemcpyHostToDevice));

    // warm-up + timing
    cudaEvent_t t0, t1;
    CUDA_CHECK(cudaEventCreate(&t0));
    CUDA_CHECK(cudaEventCreate(&t1));
    saxpy<<<(n + 255) / 256, 256>>>(n, 3.0, dx, dy);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaEventRecord(t0));
    for (int rep = 0; rep < 100; ++rep)
        saxpy<<<(n + 255) / 256, 256>>>(n, 0.0, dx, dy); // a=0 keeps y stable
    CUDA_CHECK(cudaEventRecord(t1));
    CUDA_CHECK(cudaEventSynchronize(t1));
    float ms = 0;
    CUDA_CHECK(cudaEventElapsedTime(&ms, t0, t1));
    double gbps = 100.0 * 3.0 * bytes / (ms / 1e3) / 1e9; // 2 reads + 1 write

    CUDA_CHECK(cudaMemcpy(hy, dy, bytes, cudaMemcpyDeviceToHost));
    double max_err = 0.0;
    for (int i = 0; i < n; ++i) max_err = fmax(max_err, fabs(hy[i] - 5.0));

    printf("saxpy 16M doubles: max_err=%.1e -> %s | mem bandwidth ~%.0f GB/s\n",
           max_err, max_err == 0.0 ? "PASS" : "FAIL", gbps);

    CUDA_CHECK(cudaFree(dx));
    CUDA_CHECK(cudaFree(dy));
    free(hx); free(hy);
    return max_err == 0.0 ? 0 : 1;
}
