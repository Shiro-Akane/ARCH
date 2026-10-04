/**
 * @file test_jeans_resolution.cu
 * @brief Execute the shared Jeans leaf and production accepted-cell reduction on GPU.
 * Independent Decimal references and the original 16-epsilon static budget are
 * reused. This witness does not qualify full AMR lifecycle or public CUDA JENS.
 */
#include "cuda/amr/RefinementIndicators.h"
#include "physics/diagnostics/JeansDiagnostics.h"
#include "../../math/physics/JeansNumericCases.h"
#include <iostream>
#include <iomanip>
#include <vector>

namespace {
void check(cudaError_t value) {
    if (value != cudaSuccess) throw std::runtime_error(cudaGetErrorString(value));
}
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class T> struct Buffer {
    T* data = nullptr;
    explicit Buffer(std::size_t size) {
        check(cudaMalloc(reinterpret_cast<void**>(&data), size*sizeof(T)));
    }
    ~Buffer() { if (data) cudaFree(data); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};
__global__ void numeric_kernel(const JeansNumericReference::Case* input,
                               JeansDiagnostics::Resolution* output, int count) {
    const int i = blockIdx.x*blockDim.x+threadIdx.x;
    if (i < count) output[i] = JeansDiagnostics::evaluate(input[i].rho,input[i].cs2,input[i].h);
}
void numeric() {
    std::vector<JeansNumericReference::Case> inputs(
        JeansNumericReference::cases.begin(), JeansNumericReference::cases.end());
    const std::size_t reference_count = inputs.size();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (double bad : {0.,-1.,std::numeric_limits<double>::infinity(),nan})
        for (int axis=0; axis<3; ++axis) {
            JeansNumericReference::Case value{1.,1.,1.,nan};
            if (axis==0) value.rho=bad;
            if (axis==1) value.cs2=bad;
            if (axis==2) value.h=bad;
            inputs.push_back(value);
        }
    Buffer<JeansNumericReference::Case> input(inputs.size());
    Buffer<JeansDiagnostics::Resolution> output(inputs.size());
    check(cudaMemcpy(input.data,inputs.data(),inputs.size()*sizeof(inputs[0]),cudaMemcpyHostToDevice));
    numeric_kernel<<<(inputs.size()+127)/128,128>>>(input.data,output.data,inputs.size());
    check(cudaGetLastError());
    std::vector<JeansDiagnostics::Resolution> result(inputs.size());
    check(cudaMemcpy(result.data(),output.data,result.size()*sizeof(result[0]),cudaMemcpyDeviceToHost));
    int valid=0, unrepresentable=0; double max_error=0.;
    for (std::size_t i=0;i<reference_count;++i) {
        const double expected=inputs[i].expected;
        if (!std::isfinite(expected)||expected==0.) {
            require(result[i].status==JeansDiagnostics::Status::unrepresentable
                && result[i].cells==expected,"device Jeans numeric range failure hidden");
            ++unrepresentable; continue;
        }
        const double error=static_cast<double>(std::abs(
            static_cast<long double>(result[i].cells)-expected)/expected);
        require(result[i].status==JeansDiagnostics::Status::valid
            && error<=16*std::numeric_limits<double>::epsilon(),"device Decimal reference mismatch");
        max_error=std::max(max_error,error); ++valid;
    }
    for (std::size_t i=reference_count;i<inputs.size();++i)
        require(result[i].status==JeansDiagnostics::Status::invalid_input,"device invalid numeric input accepted");
    std::cout<<std::setprecision(17)<<"CUDA_JEANS_NUMERIC_PASS valid="<<valid
        <<" unrepresentable="<<unrepresentable<<" invalid="<<inputs.size()-reference_count
        <<" max_relative_error="<<max_error<<"\n";
}
void caloric_states() {
    Grid grid(2,0.,4.,0.,8.);grid.dim=2;grid.InitializeTopology();
    const auto layout=arch::cuda::make_device_grid_view(grid);
    const int cells=grid.GetTotalSize();
    Buffer<double> properties(8),state(8*cells),composition(2*cells),
        resolutions(layout.active_cell_count()),minimum(1);
    Buffer<int> status(1);
    // A, Z, gamma, Cv: same independently approved two-species caloric fixture.
    const double data[]{1.,2.,1.,1.,1.5,2.,2.,4.};
    check(cudaMemcpy(properties.data,data,sizeof(data),cudaMemcpyHostToDevice));
    IdealGasView mixture;
    mixture.species={properties.data,properties.data+2,properties.data+4,properties.data+6,2};
    double max_error=0.;int cases=0;
    for(bool use_mixture:{false,true})
    for(double density:{.25,1.,4.})
    for(double energy:{4.,16.})
    for(bool moving:{false,true}) {
        IdealGasView eos=mixture;
        if (!use_mixture) {eos.species={};eos.global_gamma=1.5;}
        std::vector<double> fields(8*cells);
        const double u=moving?2.:0.,v=moving?3.:0.,w=moving?4.:0.;
        for(int i=0;i<cells;++i) {
            fields[i]=density;fields[cells+i]=density*u;
            fields[2*cells+i]=density*v;fields[3*cells+i]=density*w;
            fields[4*cells+i]=density*(energy+.5*(u*u+v*v+w*w));
            fields[6*cells+i]=.25;fields[7*cells+i]=.75;
        }
        check(cudaMemcpy(state.data,fields.data(),fields.size()*sizeof(double),cudaMemcpyHostToDevice));
        arch::cuda::DeviceStateView view{state.data,state.data+cells,state.data+2*cells,
            state.data+3*cells,state.data+4*cells,state.data+5*cells,
            use_mixture?state.data+6*cells:nullptr,cells,use_mixture?2:0};
        arch::cuda::DeviceJeansWorkspace work{resolutions.data,minimum.data,composition.data,status.data};
        check(arch::cuda::launch_cuda_jeans_resolution(view,layout,eos,work,nullptr));
        double value;check(cudaMemcpy(&value,minimum.data,sizeof(double),cudaMemcpyDeviceToHost));
        const long double gamma=use_mixture?27.L/14.L:1.5L;
        const long double expected=std::sqrt(3.141592653589793238462643383279502884L
            *gamma*(gamma-1)*energy/(6.67430e-8L*density))/.5L;
        const double error=static_cast<double>(std::abs(static_cast<long double>(value)-expected)/expected);
        require(std::isfinite(value)&&error<=16*std::numeric_limits<double>::epsilon(),
            "device moving/mixture caloric independent reference mismatch");
        max_error=std::max(max_error,error);++cases;
        if (use_mixture) {
            --view.n_species;
            check(cudaMemset(status.data,0x7f,sizeof(int)));
            require(arch::cuda::launch_cuda_jeans_resolution(view,layout,eos,work,nullptr)
                ==cudaErrorInvalidValue,"species/EOS extent mismatch accepted");
            int untouched;check(cudaMemcpy(&untouched,status.data,sizeof(int),cudaMemcpyDeviceToHost));
            require(untouched==0x7f7f7f7f,"species preflight partially enqueued work");
        }
    }
    std::cout<<std::setprecision(17)<<"CUDA_JEANS_CALORIC_PASS cases="<<cases
        <<" max_relative_error="<<max_error<<"\n";
}
void accepted_block(int dimension, int level) {
    // One of the frozen package's roots: L0 side 1/4, L1 side 1/8.
    const double side=level==0?.25:.125;
    Grid grid(2,0.,side,0.,side,0.,side); grid.dim=dimension; grid.InitializeTopology();
    const auto layout=arch::cuda::make_device_grid_view(grid);
    const int count=grid.GetTotalSize();
    std::vector<double> values(6*count,0.);
    for(int i=0;i<count;++i) { values[i]=1e7; values[4*count+i]=1e7; }
    // Invalid ghosts are intentionally ignored: only accepted active cells enter JENS.
    values[0]=0.; values[4*count]=0.;
    Buffer<double> state(values.size()), resolutions(layout.active_cell_count()), minimum(1);
    Buffer<int> status(1);
    arch::cuda::DeviceStateView view{state.data,state.data+count,state.data+2*count,
        state.data+3*count,state.data+4*count,state.data+5*count,nullptr,count,0};
    arch::cuda::DeviceJeansWorkspace work{resolutions.data,minimum.data,nullptr,status.data};
    IdealGasView eos; eos.global_gamma=1.6666666666666667;
    auto run=[&] {
        check(cudaMemcpy(state.data,values.data(),values.size()*sizeof(double),cudaMemcpyHostToDevice));
        check(cudaMemset(status.data,0x7f,sizeof(int)));
        check(arch::cuda::launch_cuda_jeans_resolution(view,layout,eos,work,nullptr));
        double value; check(cudaMemcpy(&value,minimum.data,sizeof(double),cudaMemcpyDeviceToHost));
        int failure;check(cudaMemcpy(&failure,status.data,sizeof(int),cudaMemcpyDeviceToHost));
        require(failure==0,"reused EOS latch not reset for IdealGas");
        return value;
    };
    const long double gamma=static_cast<long double>(eos.global_gamma);
    const long double expected=std::sqrt(3.141592653589793238462643383279502884L
        *gamma*(gamma-1)/(6.67430e-8L*1e7L))/(static_cast<long double>(side)/16);
    auto close=[&](double value,long double reference) {
        require(std::isfinite(value)&&std::abs(static_cast<long double>(value)-reference)/reference
            <=16*std::numeric_limits<double>::epsilon(),"device EOS/grid independent reference mismatch");
    };
    close(run(),expected);
    const int active=layout.active_cell(0);
    values[active]=4e7;values[4*count+active]=4e7;
    close(run(),expected/2); // One changed active cell, not the first allocation cell.
    for(double bad:{0.,-1.,std::numeric_limits<double>::quiet_NaN()}) {
        values[active]=bad;
        require(std::isnan(run()),"invalid active density silently omitted from minimum");
    }
    values[active]=1e7;values[4*count+active]=-1e7;
    require(std::isnan(run()),"invalid active thermodynamics accepted");
    values[4*count+active]=1e7;close(run(),expected);
    work.cell_resolution=nullptr;
    check(cudaMemset(status.data,0x7f,sizeof(int)));
    require(arch::cuda::launch_cuda_jeans_resolution(view,layout,eos,work,nullptr)
        ==cudaErrorInvalidValue,"missing scratch accepted");
    int untouched;check(cudaMemcpy(&untouched,status.data,sizeof(int),cudaMemcpyDeviceToHost));
    require(untouched==0x7f7f7f7f,"invalid binding reset latch before preflight");
    std::cout<<"CUDA_JEANS_ACCEPTED_BLOCK_PASS dimension="<<dimension<<" level="<<level<<"\n";
}
}
void verify_cuda_jeans_resolution() {
    numeric();
    caloric_states();
    for(int dimension:{1,2,3})for(int level:{0,1})accepted_block(dimension,level);
    std::cout<<"CUDA_JEANS_RESOLUTION_PASS\n";
}
