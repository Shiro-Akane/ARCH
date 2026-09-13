// Isolated native API capability probe, NOT a production burn parity test.
// Keeps ARCH's BTF_COLAMD ordering, GPU-only numeric execution and two IR passes.
// Reference: https://docs.nvidia.com/cuda/cudss/functions.html#cudssmatrixcreatebatchcsr
#include <cuda_runtime_api.h>
#include <cudss.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

void check(cudaError_t status) {
    if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status));
}
void check(cudssStatus_t status, const char* operation) {
    std::cout << "native," << operation << ',' << static_cast<int>(status) << std::endl;
    if (status != CUDSS_STATUS_SUCCESS) throw std::runtime_error(operation);
}
template<class T> struct Device {
    T* p{};
    explicit Device(std::size_t n) { check(cudaMalloc(reinterpret_cast<void**>(&p), n * sizeof(T))); }
    ~Device() { cudaFree(p); }
    Device(const Device&) = delete;
    void upload(const std::vector<T>& v) { check(cudaMemcpy(p, v.data(), v.size()*sizeof(T), cudaMemcpyHostToDevice)); }
};
struct System {
    std::vector<int> rows{0}, cols;
    std::vector<double> values, exact, rhs, solved;
    std::unique_ptr<Device<int>> drows, dcols;
    std::unique_ptr<Device<double>> dvalues, drhs, dsolved;
    System(int n, int lane) : exact(n), rhs(n), solved(n) {
        for (int r = 0; r < n; ++r) {
            exact[r] = 0.5 + (r + lane) / 16.0;
            if (r) { cols.push_back(r-1); values.push_back(-0.75); }
            cols.push_back(r); values.push_back(4.0 + lane / 8.0);
            if (r+1 < n) { cols.push_back(r+1); values.push_back(-1.25); }
            rows.push_back(static_cast<int>(cols.size()));
        }
        for (int r = 0; r < n; ++r)
            for (int k = rows[r]; k < rows[r+1]; ++k) rhs[r] += values[k] * exact[cols[k]];
        drows = std::make_unique<Device<int>>(rows.size()); drows->upload(rows);
        dcols = std::make_unique<Device<int>>(cols.size()); dcols->upload(cols);
        dvalues = std::make_unique<Device<double>>(values.size()); dvalues->upload(values);
        drhs = std::make_unique<Device<double>>(rhs.size()); drhs->upload(rhs);
        dsolved = std::make_unique<Device<double>>(solved.size());
    }
};
struct Native {
    cudaStream_t stream{};
    cudssHandle_t handle{};
    cudssConfig_t config{};
    cudssData_t data{};
    cudssMatrix_t a{}, b{}, x{};
    ~Native() {
        if (stream) cudaStreamSynchronize(stream);
        if (a) cudssMatrixDestroy(a);
        if (b) cudssMatrixDestroy(b);
        if (x) cudssMatrixDestroy(x);
        if (data) cudssDataDestroy(handle, data);
        if (config) cudssConfigDestroy(config);
        if (handle) cudssDestroy(handle);
        if (stream) cudaStreamDestroy(stream);
    }
};
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("provide batch count (1..32)");
        const int count = std::stoi(argv[1]);
        if (count < 1 || count > 32) throw std::runtime_error("batch must be 1..32");
        std::vector<std::unique_ptr<System>> systems;
        std::vector<int> sizes, nonzeros, ones(count,1);
        std::vector<const void*> rows, cols, values, rhs, solution;
        for (int i = 0; i < count; ++i) {
            sizes.push_back(i % 2 ? 201 : 151);
            auto& s = *systems.emplace_back(std::make_unique<System>(sizes.back(),i));
            nonzeros.push_back(static_cast<int>(s.cols.size()));
            rows.push_back(s.drows->p); cols.push_back(s.dcols->p); values.push_back(s.dvalues->p);
            rhs.push_back(s.drhs->p); solution.push_back(s.dsolved->p);
        }
        // cuDSS requires DEVICE arrays of DEVICE pointers; dimensions are Host arrays.
        Device<const void*> drows(count), dcols(count), dvalues(count), drhs(count), dsolution(count);
        drows.upload(rows); dcols.upload(cols); dvalues.upload(values); drhs.upload(rhs); dsolution.upload(solution);
        Native n;
        check(cudaStreamCreate(&n.stream));
        check(cudssCreate(&n.handle), "create");
        check(cudssSetStream(n.handle, n.stream), "stream");
        check(cudssConfigCreate(&n.config), "config");
        const auto ordering = CUDSS_REORDERING_ALG_BTF_COLAMD;
        const int disabled = 0, ir = 2;
        const double tolerance = 0;
        check(cudssConfigSet(n.config,CUDSS_CONFIG_REORDERING_ALG,&ordering,sizeof(ordering)),"btf_colamd");
        check(cudssConfigSet(n.config,CUDSS_CONFIG_HYBRID_EXECUTE_MODE,&disabled,sizeof(disabled)),"gpu_numeric");
        check(cudssConfigSet(n.config,CUDSS_CONFIG_HYBRID_MEMORY_MODE,&disabled,sizeof(disabled)),"no_host_spill");
        check(cudssConfigSet(n.config,CUDSS_CONFIG_IR_N_STEPS,&ir,sizeof(ir)),"ir_passes");
        check(cudssConfigSet(n.config,CUDSS_CONFIG_IR_TOL,&tolerance,sizeof(tolerance)),"ir_tolerance");
        check(cudssDataCreate(n.handle,&n.data),"data");
        check(cudssMatrixCreateBatchCsr(&n.a,count,sizes.data(),sizes.data(),nonzeros.data(),
            drows.p,nullptr,dcols.p,dvalues.p,CUDSS_R_32I,CUDSS_R_32I,CUDSS_R_64F,
            CUDSS_MTYPE_GENERAL,CUDSS_MVIEW_FULL,CUDSS_BASE_ZERO),"batch_csr");
        check(cudssMatrixCreateBatchDn(&n.b,count,sizes.data(),ones.data(),sizes.data(),
            drhs.p,CUDSS_R_32I,CUDSS_R_64F,CUDSS_LAYOUT_COL_MAJOR),"batch_rhs");
        check(cudssMatrixCreateBatchDn(&n.x,count,sizes.data(),ones.data(),sizes.data(),
            dsolution.p,CUDSS_R_32I,CUDSS_R_64F,CUDSS_LAYOUT_COL_MAJOR),"batch_solution");
        for (auto phase : {CUDSS_PHASE_ANALYSIS,CUDSS_PHASE_FACTORIZATION,CUDSS_PHASE_SOLVE}) {
            std::cout << "phase," << static_cast<int>(phase) << std::endl;
            check(cudssExecute(n.handle,phase,n.config,n.data,n.a,n.x,n.b),"execute");
            check(cudaStreamSynchronize(n.stream));
            // INFO is a Host int in the documented API, even for a batch.
            int info = -999;
            std::size_t written = 0;
            check(cudssDataGet(n.handle,n.data,CUDSS_DATA_INFO,&info,sizeof(info),&written),"info");
            std::cout << "info_bytes," << written << std::endl;
            if (written != sizeof(info) || info != 0) throw std::runtime_error("invalid batch numerical info");
        }
        double error = 0;
        for (auto& s : systems) {
            check(cudaMemcpy(s->solved.data(),s->dsolved->p,s->solved.size()*sizeof(double),cudaMemcpyDeviceToHost));
            for (std::size_t i = 0; i < s->solved.size(); ++i) {
                if (!std::isfinite(s->solved[i])) throw std::runtime_error("nonfinite solution");
                error = std::max(error,std::abs(s->solved[i]-s->exact[i])/std::max(1.0,std::abs(s->exact[i])));
            }
        }
        std::cout << "max_relative_error," << error << std::endl;
        if (error > 1e-12) throw std::runtime_error("manufactured solution mismatch");
        std::cout << "NONUNIFORM_BTF_CAPABILITY_PASS," << count << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "NONUNIFORM_BTF_CAPABILITY_FAIL: " << e.what() << std::endl;
        return 1;
    }
}
