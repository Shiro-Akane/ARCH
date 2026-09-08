/**
 * @file test_native_tabular_owner.cu
 * @brief Native EOS host/device parity, immutable owner lifetime and failures.
 */
#include "fixtures/NativeTabularFixture.h"
#include "cuda/microphysics/tabular3_eos_device_owner.h"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/common/DeviceEosStatus.h"
#include <filesystem>
#include <memory>
#include <iostream>
#include <stdexcept>

namespace {
struct Probe { double value[10]{}; };

template <class View>
ARCH_INLINE Probe evaluate(View view, double rho, double temperature, double ye)
{
    const double X[]{1.0-ye,ye};
    Probe p;
    const auto state = view.native_state(rho,temperature,X);
    p.value[0]=state.pressure; p.value[1]=state.energy; p.value[2]=state.cv;
    p.value[3]=state.sound_speed; p.value[4]=state.dp_drho_e; p.value[5]=state.dp_de_rho;
    p.value[6]=view.get_temperature(rho,state.energy,X);
    double gradient[3]{}, action[3]{};
    const double flow[]{0.0,1.0,0.0};
    view.template get_energy_composition_gradient<3>(rho,temperature,X,gradient);
    view.template get_energy_composition_hessian_action<3>(rho,temperature,X,flow,action);
    p.value[7]=gradient[1]; p.value[8]=action[1]; p.value[9]=state.dp_dT;
    return p;
}

__global__ void query(Tabular3DEOSView view, double rho, double temperature, double ye, Probe* out)
{
    *out=evaluate(view,rho,temperature,ye);
}
__global__ void invalid_query(Tabular3DEOSView view, int* status, Probe* out)
{
    view=arch::cuda::bind_device_eos_status(view,status);
    const double X[]{0.7,0.3};
    out->value[0]=view.get_eint_from_T(0.0,1e8,X);
    double gradient[3]{}, capacity[3]{}, action[3]{};
    const double flow[]{0.0,1.0,0.0};
    view.get_energy_composition_gradient<3>(0.0,1e8,X,gradient);
    view.get_cv_gradient<3>(1e4,1e5,X,capacity);
    view.get_energy_composition_hessian_action<3>(0.0,1e8,X,flow,action);
    out->value[1]=gradient[1]; out->value[2]=capacity[1]; out->value[3]=action[1];
}
} // namespace

int main(int argc,char** argv)
{
    using arch::cuda::check_cuda;
    if(argc<2 || argc>3) throw std::runtime_error("expected output directory [native EOSDriver file]");
    std::filesystem::create_directories(argv[1]);
    const std::string fixture=(std::filesystem::path(argv[1])/"native-device.h5").string();
    native_tabular_test::write_table(fixture);
    const std::string source=argc==3 ? argv[2] : fixture;
    const double rho=argc==3 ? 1e10 : 2e4, temperature=argc==3 ? 1e10 : 5e7, ye=0.3;
    std::unique_ptr<arch::cuda::Tabular3DEOSDeviceOwner> owner;
    Probe expected;
    {
        SpeciesManager species;
        species.add_species("n",1,0,5.0/3.0,1.5e8);
        species.add_species("p",1,1,5.0/3.0,1.5e8);
        Tabular3DEOS host(source,&species);
        expected=evaluate(host.get_view(),rho,temperature,ye);
        owner=std::make_unique<arch::cuda::Tabular3DEOSDeviceOwner>(host.get_view(),nullptr);
    }
    // The queued upload owns staging after both host table and metadata die.
    arch::cuda::Tabular3DEOSDeviceOwner moved(std::move(*owner));
    if(!owner->empty()) throw std::runtime_error("moved native owner retained data");
    arch::cuda::DeviceAllocation<Probe> output;
    arch::cuda::DeviceAllocation<int> status;
    output.allocate(1); status.allocate(1);
    query<<<1,1>>>(moved.view(),rho,temperature,ye,output.get());
    check_cuda(cudaGetLastError(),"native query launch");
    Probe actual;
    check_cuda(cudaMemcpy(&actual,output.get(),sizeof(Probe),cudaMemcpyDeviceToHost),"native query result");
    for(int i=0;i<10;++i)
        if(!std::isfinite(actual.value[i]) || std::abs(actual.value[i]-expected.value[i])
            >2e-10*std::max(std::abs(expected.value[i]),1.0))
            throw std::runtime_error("native EOS host/device field mismatch");
    check_cuda(cudaMemset(status.get(),0,sizeof(int)),"reset native status");
    invalid_query<<<1,1>>>(moved.view(),status.get(),output.get());
    check_cuda(cudaGetLastError(),"invalid native query launch");
    int failed=0;
    check_cuda(cudaMemcpy(&failed,status.get(),sizeof(int),cudaMemcpyDeviceToHost),"native status result");
    check_cuda(cudaMemcpy(&actual,output.get(),sizeof(Probe),cudaMemcpyDeviceToHost),"native failure result");
    if(failed!=1 || !std::isnan(actual.value[0]) || !std::isnan(actual.value[1])
        || !std::isnan(actual.value[2]) || !std::isnan(actual.value[3]))
        throw std::runtime_error("native EOS failure did not remain latched");
    check_cuda(cudaDeviceSynchronize(),"native owner completion");
    std::cout<<"native EOSDriver CUDA parity, host-release/move lifetime and failure latch PASS\n";
}
