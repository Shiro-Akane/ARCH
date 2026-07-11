/**
 * @file burnbench.cu
 * @brief CPU-vs-GPU benchmark for the ARCH aprox19 nuclear burn kernel.
 *
 * One thread (CPU: one loop iteration) integrates one cell's stiff burn ODE:
 * isothermal 16-species backward-Euler + Newton-Raphson with adaptive
 * sub-stepping and dense pivoted-LU — the exact numerics of
 * src/numerics/burnsolver/ode_be-nr.h, adapted to be __host__ __device__.
 *
 * Both paths compile from THIS translation unit with the SAME annotated
 * aprox19 headers, so any CPU/GPU result difference comes from floating-point
 * scheduling (fma, libm), not from different code.
 *
 * Build (on jaist-gpu):
 *   nvcc -O3 -std=c++17 -arch=sm_90 --expt-relaxed-constexpr \
 *        -I aprox19_gpu -Xcompiler "-O3 -fopenmp" burnbench.cu -o burnbench
 *
 * Run:  ./burnbench [n_cells] [dt_target_seconds]
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <omp.h>
#include <cuda_runtime.h>

// Annotated mirror copies of the ARCH aprox19 network headers (-I aprox19_gpu)
#include <amrex_bridge.H>
#include <network_properties.H>
#include <burn_type.H>
#include <actual_network.H>
#include <actual_rhs.H>

#define CUDA_CHECK(call)                                                      \
    do {                                                                      \
        cudaError_t e_ = (call);                                              \
        if (e_ != cudaSuccess) {                                              \
            printf("CUDA error %s:%d: %s\n", __FILE__, __LINE__,              \
                   cudaGetErrorString(e_));                                   \
            exit(1);                                                          \
        }                                                                     \
    } while (0)

// ============================================================================
// Solver core — line-by-line port of ARCH ode_be-nr.h / odeFunction.h /
// DenseWrap.h, restricted to the isothermal species-only system (NEQ = 16).
// ============================================================================

constexpr int NSP = NumSpec; // 16 species
constexpr int MAXN = NumSpec; // actual_jac (do_T_derivatives=0) writes the species block only

struct BurnParams
{
    double rtol = 1.0e-4;
    double atol = 1.0e-8;
    int max_newton_iter = 50;
    int max_substeps = 10000;
    double initial_dt_frac = 1.0e-3;
    double dt_safe_factor = 0.9;
    double dt_fac_min = 0.1;
    double dt_fac_max = 2.0;
    double small_x = 1.0e-20;
};

// Same interface as ARCH DenseMatrixData (1-based, zero/set/operator()) so the
// pynucastro-generated actual_jac writes into it unchanged. Backed by external
// storage with a runtime stride: stride=1 gives a compact per-thread matrix
// (CPU / local memory), stride=blockDim gives a lane-interleaved shared-memory
// layout where uniform (i,j) across a warp maps to distinct banks.
struct DenseMat
{
    double *d;
    int stride;

    __host__ __device__ double &at(int i0, int j0) { return d[(i0 * MAXN + j0) * stride]; }
    __host__ __device__ double &operator()(int i, int j) { return at(i - 1, j - 1); }
    __host__ __device__ const double &operator()(int i, int j) const
    {
        return d[((i - 1) * MAXN + (j - 1)) * stride];
    }
    __host__ __device__ void set(int i, int j, double v) { at(i - 1, j - 1) = v; }
    __host__ __device__ void zero()
    {
        for (int i = 0; i < MAXN * MAXN; ++i)
            d[i * stride] = 0.0;
    }
};

// Pivoted dense LU solve, port of ARCH DenseLUSolver::solve<ACTIVE_N, MAX_N>
template <int ACTIVE_N>
__host__ __device__ bool lu_solve(DenseMat &A, double *b)
{
    int p[ACTIVE_N];
    for (int i = 0; i < ACTIVE_N; ++i)
        p[i] = i;

    for (int i = 0; i < ACTIVE_N; ++i)
    {
        double max_val = 0.0;
        int pivot_row = i;
        for (int j = i; j < ACTIVE_N; ++j)
        {
            double val = fabs(A.at(p[j], i));
            if (val > max_val)
            {
                max_val = val;
                pivot_row = j;
            }
        }
        if (max_val < 1e-20)
            return false;

        int tmp = p[i];
        p[i] = p[pivot_row];
        p[pivot_row] = tmp;

        double pivot_inv = 1.0 / A.at(p[i], i);
        for (int j = i + 1; j < ACTIVE_N; ++j)
        {
            A.at(p[j], i) *= pivot_inv;
            for (int k = i + 1; k < ACTIVE_N; ++k)
                A.at(p[j], k) -= A.at(p[j], i) * A.at(p[i], k);
        }
    }

    double y[ACTIVE_N];
    for (int i = 0; i < ACTIVE_N; ++i)
    {
        y[i] = b[p[i]];
        for (int j = 0; j < i; ++j)
            y[i] -= A.at(p[i], j) * y[j];
    }
    for (int i = ACTIVE_N - 1; i >= 0; --i)
    {
        b[i] = y[i];
        for (int j = i + 1; j < ACTIVE_N; ++j)
            b[i] -= A.at(p[i], j) * b[j];
        b[i] /= A.at(p[i], i);
    }
    return true;
}

__host__ __device__ double pi_controller(double err_n, double err_n_1, double dt_n,
                                         double safe, double min_fac, double max_fac)
{
    const double k1 = 0.7; // order_q = 1
    const double k2 = 0.2;
    if (err_n < 1e-10)
        return dt_n * max_fac;
    double fac = safe * pow(err_n, -k1) * pow(err_n_1, k2);
    fac = fmax(min_fac, fmin(max_fac, fac));
    return dt_n * fac;
}

// RHS / Jacobian wrappers around the pynucastro-generated network (isothermal:
// temperature enters as a fixed parameter, only species evolve).
__host__ __device__ void net_rhs(const double *X, double rho, double T, double *RHS)
{
    burn_t st;
    st.rho = rho;
    st.T = T;
    for (int i = 0; i < NSP; ++i)
        st.xn[i] = X[i];
    compute_ye(st);

    Array1D<Real, 1, NumSpec> ydot;
    Real enu_weak = 0.0;
    actual_rhs(st, ydot, enu_weak);
    for (int i = 0; i < NSP; ++i)
        RHS[i] = ydot(i + 1);
}

__host__ __device__ void net_jac(const double *X, double rho, double T, DenseMat &J)
{
    burn_t st;
    st.rho = rho;
    st.T = T;
    for (int i = 0; i < NSP; ++i)
        st.xn[i] = X[i];
    compute_ye(st);
    actual_jac(st, J);
}

/**
 * Integrate one cell's species over dt_target at fixed (rho, T).
 * Port of Solver_BE_NR::integrate with NEQ = NSP (no temperature equation).
 * Returns number of substeps taken, or -1 on failure.
 */
__host__ __device__ int burn_cell(double *X, double rho, double T,
                                  double dt_target, const BurnParams &cfg,
                                  DenseMat A)
{
    double Y_old[NSP], Y_k[NSP], RHS[NSP], b[MAXN], W[NSP];

    double t_current = 0.0;
    double dt = fmin(dt_target, dt_target * cfg.initial_dt_frac);
    double err_prev = 1.0;
    int substeps = 0;

    while (t_current < dt_target)
    {
        if (++substeps > cfg.max_substeps)
            return -1;
        if (t_current + dt > dt_target)
            dt = dt_target - t_current;

        for (int i = 0; i < NSP; ++i)
        {
            Y_old[i] = X[i];
            Y_k[i] = X[i];
        }

        bool step_converged = false;
        double current_err = 0.0;

        for (int iter = 0; iter < cfg.max_newton_iter; ++iter)
        {
            // no A.zero() here: actual_jac() zeroes the matrix itself
            net_rhs(Y_k, rho, T, RHS);
            net_jac(Y_k, rho, T, A);

            // A = I - dt*J  (species block), b = Y_old - Y_k + dt*RHS
            for (int i = 0; i < NSP; ++i)
            {
                b[i] = Y_old[i] - Y_k[i] + dt * RHS[i];
                for (int j = 0; j < NSP; ++j)
                {
                    double jac_val = A(i + 1, j + 1);
                    A.set(i + 1, j + 1, -dt * jac_val);
                }
                A.set(i + 1, i + 1, A(i + 1, i + 1) + 1.0);
            }

            if (!lu_solve<NSP>(A, b))
                break;

            for (int i = 0; i < NSP; ++i)
                W[i] = cfg.rtol * fabs(Y_k[i]) + cfg.atol;

            for (int i = 0; i < NSP; ++i)
                Y_k[i] = Y_k[i] + b[i];

            // enforce_mass_conservation
            double sum_X = 0.0;
            for (int i = 0; i < NSP; ++i)
            {
                if (Y_k[i] < cfg.small_x)
                    Y_k[i] = cfg.small_x;
                sum_X += Y_k[i];
            }
            double inv_sum = 1.0 / sum_X;
            for (int i = 0; i < NSP; ++i)
                Y_k[i] *= inv_sum;

            // WRMS norm of the Newton update
            double sum = 0.0;
            for (int i = 0; i < NSP; ++i)
            {
                double v = b[i] / W[i];
                sum += v * v;
            }
            current_err = sqrt(sum / NSP);

            if (current_err < 1.0)
            {
                step_converged = true;
                break;
            }
        }

        if (step_converged)
        {
            t_current += dt;
            for (int i = 0; i < NSP; ++i)
                X[i] = Y_k[i];

            double dt_new = pi_controller(current_err, err_prev, dt,
                                          cfg.dt_safe_factor, cfg.dt_fac_min, cfg.dt_fac_max);
            dt_new = fmax(dt * cfg.dt_fac_min, fmin(dt * cfg.dt_fac_max, dt_new));
            dt = dt_new * cfg.dt_safe_factor;
            err_prev = current_err;
        }
        else
        {
            dt *= 0.25;
            if (dt < 1e-22)
                return -1;
        }
    }
    return substeps;
}

// ============================================================================
// GPU kernel: one thread = one cell
// ============================================================================
// Variant A: per-thread Jacobian in local memory (cached in L1).
__global__ void __launch_bounds__(256) burn_kernel_local(double *X_all, const double *rho_all,
                                                         const double *T_all, int n, double dt_target,
                                                         BurnParams cfg, int *substeps_out)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    double A_local[MAXN * MAXN];
    double X[NSP];
    for (int k = 0; k < NSP; ++k)
        X[k] = X_all[(size_t)k * n + i]; // SoA: species-major, same as ARCH FluidState

    int s = burn_cell(X, rho_all[i], T_all[i], dt_target, cfg, DenseMat{A_local, 1});
    substeps_out[i] = s;

    for (int k = 0; k < NSP; ++k)
        X_all[(size_t)k * n + i] = X[k];
}

// Variant B: per-thread Jacobian in shared memory, lane-interleaved so that
// uniform (i,j) access across a warp is bank-conflict-free.
__global__ void __launch_bounds__(128) burn_kernel_shared(double *X_all, const double *rho_all,
                                                         const double *T_all, int n, double dt_target,
                                                         BurnParams cfg, int *substeps_out)
{
    extern __shared__ double A_smem[];

    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    double X[NSP];
    for (int k = 0; k < NSP; ++k)
        X[k] = X_all[(size_t)k * n + i];

    int s = burn_cell(X, rho_all[i], T_all[i], dt_target, cfg,
                      DenseMat{&A_smem[threadIdx.x], (int)blockDim.x});
    substeps_out[i] = s;

    for (int k = 0; k < NSP; ++k)
        X_all[(size_t)k * n + i] = X[k];
}

// ============================================================================
// Host driver
// ============================================================================
static void init_states(double *X, double *rho, double *T, int n)
{
    // Deterministic spread over cellular-detonation conditions.
    for (int i = 0; i < n; ++i)
    {
        // Cheap deterministic hash -> two uniforms in [0,1)
        unsigned int h = (unsigned int)i * 2654435761u;
        double r1 = ((h >> 8) & 0xFFFF) / 65536.0;
        double r2 = ((h >> 16) & 0xFFFF) / 65536.0;

        rho[i] = pow(10.0, 5.0 + 2.5 * r1);   // 1e5 .. ~3e7 g/cm^3
        T[i] = (1.5 + 2.0 * r2) * 1.0e9;      // 1.5e9 .. 3.5e9 K

        for (int k = 0; k < NSP; ++k)
            X[(size_t)k * n + i] = 1.0e-20;
        X[(size_t)4 * n + i] = 0.5; // C12
        X[(size_t)5 * n + i] = 0.5; // O16
    }
}

int main(int argc, char **argv)
{
    int n = (argc > 1) ? atoi(argv[1]) : (1 << 20);
    double dt_target = (argc > 2) ? atof(argv[2]) : 1.0e-8; // seconds

    BurnParams cfg;
    size_t xbytes = (size_t)NSP * n * sizeof(double);

    printf("=== ARCH aprox19 burn kernel benchmark ===\n");
    printf("cells: %d | dt_target: %.2e s | NEQ: %d (isothermal)\n", n, dt_target, NSP);

    double *X_cpu = (double *)malloc(xbytes);
    double *X_gpu_res = (double *)malloc(xbytes);
    double *X_init = (double *)malloc(xbytes);
    double *rho = (double *)malloc(n * sizeof(double));
    double *T = (double *)malloc(n * sizeof(double));
    int *sub_cpu = (int *)malloc(n * sizeof(int));
    int *sub_gpu = (int *)malloc(n * sizeof(int));

    init_states(X_init, rho, T, n);

    // Sort cells by temperature (stiffness proxy) so warp lanes get similar
    // substep counts — reduces divergence. Same ordering feeds CPU and GPU.
    {
        std::vector<int> idx(n);
        for (int i = 0; i < n; ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return T[a] > T[b]; });

        double *rho2 = (double *)malloc(n * sizeof(double));
        double *T2 = (double *)malloc(n * sizeof(double));
        double *X2 = (double *)malloc(xbytes);
        for (int i = 0; i < n; ++i)
        {
            rho2[i] = rho[idx[i]];
            T2[i] = T[idx[i]];
            for (int k = 0; k < NSP; ++k)
                X2[(size_t)k * n + i] = X_init[(size_t)k * n + idx[i]];
        }
        memcpy(rho, rho2, n * sizeof(double));
        memcpy(T, T2, n * sizeof(double));
        memcpy(X_init, X2, xbytes);
        free(rho2); free(T2); free(X2);
    }

    // ---------------- CPU (OpenMP, same code path) ----------------
    memcpy(X_cpu, X_init, xbytes);
    int nthreads = omp_get_max_threads();

    double t0 = omp_get_wtime();
#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < n; ++i)
    {
        double X[NSP];
        double A_local[MAXN * MAXN];
        for (int k = 0; k < NSP; ++k)
            X[k] = X_cpu[(size_t)k * n + i];
        sub_cpu[i] = burn_cell(X, rho[i], T[i], dt_target, cfg, DenseMat{A_local, 1});
        for (int k = 0; k < NSP; ++k)
            X_cpu[(size_t)k * n + i] = X[k];
    }
    double t_cpu = omp_get_wtime() - t0;

    // ---------------- GPU ----------------
    double *d_X, *d_rho, *d_T;
    int *d_sub;
    CUDA_CHECK(cudaMalloc(&d_X, xbytes));
    CUDA_CHECK(cudaMalloc(&d_rho, n * sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_T, n * sizeof(double)));
    CUDA_CHECK(cudaMalloc(&d_sub, n * sizeof(int)));

    CUDA_CHECK(cudaMemcpy(d_rho, rho, n * sizeof(double), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_T, T, n * sizeof(double), cudaMemcpyHostToDevice));

    cudaEvent_t ev0, ev1;
    CUDA_CHECK(cudaEventCreate(&ev0));
    CUDA_CHECK(cudaEventCreate(&ev1));

    float kernel_ms = 1e30f;
    double t_gpu_full = 1e30;

    // Variant A: local-memory Jacobian (L1-cached), block = 128
    {
        double t0v = omp_get_wtime();
        CUDA_CHECK(cudaMemcpy(d_X, X_init, xbytes, cudaMemcpyHostToDevice));
        int block = 256;
        int grid = (n + block - 1) / block;
        CUDA_CHECK(cudaEventRecord(ev0));
        burn_kernel_local<<<grid, block>>>(d_X, d_rho, d_T, n, dt_target, cfg, d_sub);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaEventRecord(ev1));
        CUDA_CHECK(cudaEventSynchronize(ev1));
        float ms = 0.f;
        CUDA_CHECK(cudaEventElapsedTime(&ms, ev0, ev1));
        CUDA_CHECK(cudaMemcpy(X_gpu_res, d_X, xbytes, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(sub_gpu, d_sub, n * sizeof(int), cudaMemcpyDeviceToHost));
        double tv = omp_get_wtime() - t0v;
        printf("variant local  (block 128): kernel %8.3f s\n", ms / 1e3);
        if (ms < kernel_ms) { kernel_ms = ms; t_gpu_full = tv; }
    }

    // Variant B: shared-memory Jacobian, lane-interleaved, block = 96
    {
        double t0v = omp_get_wtime();
        CUDA_CHECK(cudaMemcpy(d_X, X_init, xbytes, cudaMemcpyHostToDevice));
        int block = 112;
        int grid = (n + block - 1) / block;
        size_t smem_bytes = (size_t)block * MAXN * MAXN * sizeof(double); // 112 x 2 KB = 224 KB
        CUDA_CHECK(cudaFuncSetAttribute(burn_kernel_shared, cudaFuncAttributeMaxDynamicSharedMemorySize,
                                        (int)smem_bytes));
        CUDA_CHECK(cudaEventRecord(ev0));
        burn_kernel_shared<<<grid, block, smem_bytes>>>(d_X, d_rho, d_T, n, dt_target, cfg, d_sub);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaEventRecord(ev1));
        CUDA_CHECK(cudaEventSynchronize(ev1));
        float ms = 0.f;
        CUDA_CHECK(cudaEventElapsedTime(&ms, ev0, ev1));
        printf("variant shared (block  96): kernel %8.3f s\n", ms / 1e3);
        if (ms < kernel_ms)
        {
            kernel_ms = ms;
            CUDA_CHECK(cudaMemcpy(X_gpu_res, d_X, xbytes, cudaMemcpyDeviceToHost));
            CUDA_CHECK(cudaMemcpy(sub_gpu, d_sub, n * sizeof(int), cudaMemcpyDeviceToHost));
            t_gpu_full = omp_get_wtime() - t0v;
        }
    }

    // ---------------- Compare ----------------
    int fail_cpu = 0, fail_gpu = 0;
    long long steps_cpu = 0, steps_gpu = 0;
    double max_rel = 0.0, max_abs = 0.0;
    int argmax = -1;
    for (int i = 0; i < n; ++i)
    {
        if (sub_cpu[i] < 0) fail_cpu++;
        else steps_cpu += sub_cpu[i];
        if (sub_gpu[i] < 0) fail_gpu++;
        else steps_gpu += sub_gpu[i];

        for (int k = 0; k < NSP; ++k)
        {
            double a = X_cpu[(size_t)k * n + i];
            double g = X_gpu_res[(size_t)k * n + i];
            double ad = fabs(a - g);
            double rd = ad / fmax(fabs(a), 1e-10); // ignore noise below atol-ish floor
            if (ad > max_abs) max_abs = ad;
            if (fabs(a) > 1e-10 && rd > max_rel) { max_rel = rd; argmax = i; }
        }
    }

    printf("\n--- results ---\n");
    printf("CPU  (%2d threads): %8.3f s  (%.2e cells/s)  avg substeps %.1f  fails %d\n",
           nthreads, t_cpu, n / t_cpu, (double)steps_cpu / n, fail_cpu);
    printf("GPU  (kernel)    : %8.3f s  (%.2e cells/s)  avg substeps %.1f  fails %d\n",
           kernel_ms / 1e3, n / (kernel_ms / 1e3), (double)steps_gpu / n, fail_gpu);
    printf("GPU  (with PCIe) : %8.3f s  (%.2e cells/s)\n", t_gpu_full, n / t_gpu_full);
    printf("speedup (kernel) : %.1fx   | speedup (with transfer): %.1fx\n",
           t_cpu / (kernel_ms / 1e3), t_cpu / t_gpu_full);
    printf("max |dX|: %.3e   max relX (X>1e-10): %.3e (cell %d)\n", max_abs, max_rel, argmax);

    bool pass = (fail_cpu == 0 && fail_gpu == 0 && max_rel < 1e-5);
    printf("\n%s\n", pass ? "[PASS] CPU and GPU agree within tolerance."
                          : "[CHECK] see numbers above.");

    cudaFree(d_X); cudaFree(d_rho); cudaFree(d_T); cudaFree(d_sub);
    free(X_cpu); free(X_gpu_res); free(X_init); free(rho); free(T); free(sub_cpu); free(sub_gpu);
    return pass ? 0 : 2;
}
