/**
 * @file test_tabular_completion_device.cu
 * @brief Strict completed 3D/4D EOS queries through the actual CUDA owners.
 *
 * Inputs are the partial tables written by TabularCompletionRegression. Host
 * owners assemble missing components once; device owners stage/upload the same
 * potential and mask. Host tables/species die before moved device owners are
 * queried. This is backend/lifetime acceptance, not a second EOS oracle.
 */
#include "cuda/microphysics/tabular3_eos_device_owner.h"
#include "cuda/microphysics/tabular4_eos_device_owner.h"
#include "cuda/common/DeviceAllocation.h"
#include "cuda/common/DeviceEosStatus.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using arch::cuda::check_cuda;
constexpr int sample_count=6, field_count=23;
constexpr double parity_budget=2e-10;
struct Sample { double rho,T,X[3]; };
struct Probe { double values[field_count]{}; };
struct FailureProbe { double values[7]{}; };
static_assert(std::is_trivially_copyable_v<Sample> && std::is_trivially_copyable_v<Probe>);

void require(bool condition,const char* message)
{ if(!condition) throw std::runtime_error(message); }

template<class Action>
void host_rejects(Action action,const char* expected)
{
    try { action(); }
    catch(const std::runtime_error& error) {
        if(std::string(error.what())==expected) return;
        throw;
    }
    throw std::runtime_error("Host strict completion accepted an invalid query");
}

template<class View>
ARCH_INLINE Probe evaluate(View view,const Sample& sample)
{
    Probe out;
    const double rho=sample.rho,T=sample.T;
    const double* X=sample.X;
    out.values[0]=view.get_pressure_from_rho_T(rho,T,X);
    out.values[1]=view.get_eint_from_T(rho,T,X);
    out.values[2]=view.get_cv(rho,T,X);
    out.values[3]=view.get_sound_speed_from_rho_T(rho,T,X);
    out.values[4]=view.get_dp_drho_e(rho,out.values[1],X);
    out.values[5]=view.get_dp_de_rho(rho,out.values[1],X);
    eos_state_t state{};
    state.rho=rho; state.T=T; state.Xi=X;
    view.evaluate_state(state);
    out.values[6]=state.dp_dT;
    // Both inverses execute on the GPU, not on a host-precomputed temperature.
    out.values[7]=view.get_temperature(rho,out.values[1],X);
    out.values[8]=view.strict_temperature(rho,out.values[0],X,true);
    out.values[9]=view.get_total_energy_primitive(rho,0,0,0,out.values[0],X);
    double energy_gradient[4]{},capacity_gradient[4]{},hessian_action[4]{};
    const double flow[4]{.17,-.11,-.06,0};
    view.template get_energy_composition_gradient<4>(rho,T,X,energy_gradient);
    view.template get_cv_gradient<4>(rho,T,X,capacity_gradient);
    view.template get_energy_composition_hessian_action<4>(rho,T,X,flow,hessian_action);
    for(int i=0;i<4;++i) {
        out.values[10+i]=energy_gradient[i]; out.values[14+i]=capacity_gradient[i];
        out.values[18+i]=hessian_action[i];
    }
    out.values[22]=view.composition_derivatives(rho,T,X).cv_temperature;
    return out;
}

template<class View>
__global__ void query_kernel(View view,const Sample* samples,Probe* output,int* status)
{
    const int index=threadIdx.x;
    if(index<sample_count)
        output[index]=evaluate(arch::cuda::bind_device_eos_status(view,status),samples[index]);
}

template<class View>
__global__ void failure_kernel(View valid,View masked,Sample sample,double energy,
                               int mode,int* status,FailureProbe* out)
{
    valid=arch::cuda::bind_device_eos_status(valid,status);
    auto invalid=arch::cuda::bind_device_eos_status(mode==2 ? masked : valid,status);
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double rho=mode==0 ? std::pow(10.0,valid.log_rho_min-1) : sample.rho;
    const double T=mode==1 ? nan : sample.T;
    if(mode==3) {
        out->values[0]=invalid.get_temperature(rho,nan,sample.X);
        out->values[1]=invalid.strict_temperature(rho,nan,sample.X,true);
        out->values[2]=invalid.get_pressure_from_rho_e(rho,nan,sample.X);
        out->values[3]=invalid.get_total_energy_primitive(rho,0,0,0,nan,sample.X);
        out->values[4]=invalid.get_dp_de_rho(rho,nan,sample.X);
        out->values[5]=invalid.get_dp_drho_e(rho,nan,sample.X);
    } else {
        double gradient[4]{},capacity[4]{},action[4]{};
        const double flow[4]{.17,-.11,-.06,0};
        out->values[0]=invalid.get_eint_from_T(rho,T,sample.X);
        out->values[1]=invalid.get_cv(rho,T,sample.X);
        invalid.template get_energy_composition_gradient<4>(rho,T,sample.X,gradient);
        invalid.template get_cv_gradient<4>(rho,T,sample.X,capacity);
        invalid.template get_energy_composition_hessian_action<4>(rho,T,sample.X,flow,action);
        out->values[2]=gradient[0]; out->values[3]=capacity[0]; out->values[4]=action[0];
        out->values[5]=mode==2 ? invalid.get_temperature(rho,energy,sample.X)
            : invalid.get_eta(rho,T,sample.X);
    }
    // A later valid call must remain usable but cannot clear a previous error.
    out->values[6]=valid.get_eint_from_T(sample.rho,sample.T,sample.X);
}

template<class HostOwner,class DeviceOwner>
void test_rank(const char* source,const char* helm_path,const char* rank)
{
    std::array<Sample,sample_count> samples{};
    std::array<Probe,sample_count> expected{},actual{};
    for(int i=0;i<sample_count;++i) {
        const double h=.18+.01*i,he=.60+.008*i;
        samples[i]={std::pow(10.,5.12+.12*i),std::pow(10.,8.11+.07*i),{h,he,1-h-he}};
    }
    std::unique_ptr<DeviceOwner> device,masked_device;
    {
        SpeciesManager species;
        species.add_species("h1",1,1,5.0/3.0,1.5e8);
        species.add_species("he4",4,2,5.0/3.0,3.75e7);
        species.add_species("n",1,0,5.0/3.0,1.5e8);
        HostOwner host(source,&species,helm_path);
        auto view=host.get_view();
        require(view.strict_domain && view.uses_free_energy && view.table_valid,
                "completed host fixture did not retain strict potential/mask");
        // Exercise nonzero gauge transfer even though this manufactured source
        // already has positive energy. The fixed test-owned offset adds no heat.
        view.energy_reference_shift+=3e16;
        for(int i=0;i<sample_count;++i) {
            expected[i]=evaluate(view,samples[i]);
            require(std::abs(expected[i].values[7]-samples[i].T)<=2e-8*samples[i].T
                && std::abs(expected[i].values[8]-samples[i].T)<=2e-8*samples[i].T,
                "host manufactured inverse is inaccurate");
        }
        device=std::make_unique<DeviceOwner>(view,nullptr);
        // A private upload fixture changes only validity, not numerical fields.
        // Staging must own this vector after this scope ends.
        std::vector<double> rejected_mask(view.valid_extent,0.0);
        auto masked=view; masked.table_valid=rejected_mask.data();
        masked_device=std::make_unique<DeviceOwner>(masked,nullptr);
        constexpr const char* domain="Native tabular EOS query is outside its finite declared domain";
        constexpr const char* cell="Native tabular EOS query touches an invalid thermodynamic cell";
        constexpr const char* inverse="Native tabular EOS has no valid temperature inverse";
        host_rejects([&]{view.get_eint_from_T(std::pow(10.,view.log_rho_min-1),samples[0].T,samples[0].X);},domain);
        host_rejects([&]{view.get_eint_from_T(samples[0].rho,NAN,samples[0].X);},domain);
        host_rejects([&]{masked.get_eint_from_T(samples[0].rho,samples[0].T,samples[0].X);},cell);
        host_rejects([&]{view.get_temperature(samples[0].rho,NAN,samples[0].X);},inverse);
    }
    // The source owner, metadata and temporary mask have all been destroyed.
    const auto potential_pointer=device->view().free_energy_fields[0];
    const auto mask_pointer=masked_device->view().table_valid;
    DeviceOwner moved(std::move(*device)),masked_moved(std::move(*masked_device));
    require(device->empty() && masked_device->empty(),"moved completion owner retained storage");
    require(moved.view().free_energy_fields[0]==potential_pointer
        && masked_moved.view().table_valid==mask_pointer,"owner move changed allocation identity");
    require(moved.view().strict_domain && moved.view().energy_reference_shift>=3e16,
            "device owner lost strict-domain or energy-reference metadata");
    arch::cuda::DeviceAllocation<Sample> inputs;
    arch::cuda::DeviceAllocation<Probe> output;
    arch::cuda::DeviceAllocation<FailureProbe> failure;
    arch::cuda::DeviceAllocation<int> status;
    inputs.allocate(sample_count); output.allocate(sample_count); failure.allocate(1); status.allocate(1);
    check_cuda(cudaMemcpy(inputs.get(),samples.data(),sizeof(samples),cudaMemcpyHostToDevice),"upload completion samples");
    check_cuda(cudaMemset(status.get(),0,sizeof(int)),"reset completion success status");
    query_kernel<<<1,sample_count>>>(moved.view(),inputs.get(),output.get(),status.get());
    check_cuda(cudaGetLastError(),"launch strict completion queries");
    check_cuda(cudaMemcpy(actual.data(),output.get(),sizeof(actual),cudaMemcpyDeviceToHost),"download strict completion queries");
    int failed=0;
    check_cuda(cudaMemcpy(&failed,status.get(),sizeof(int),cudaMemcpyDeviceToHost),"download strict success status");
    require(failed==0,"valid strict completion query latched a device failure");
    double worst=0;
    for(int i=0;i<sample_count;++i) for(int field=0;field<field_count;++field) {
        const double error=std::abs(actual[i].values[field]-expected[i].values[field])/
            std::max(std::abs(expected[i].values[field]),1.0);
        if(!std::isfinite(actual[i].values[field]) || error>parity_budget) {
            std::cerr << rank << " sample=" << i << " field=" << field
                << " host=" << expected[i].values[field] << " device=" << actual[i].values[field]
                << " relative=" << error << '\n';
            throw std::runtime_error("strict completion host/device mismatch");
        }
        worst=std::max(worst,error);
    }
    for(int mode=0;mode<4;++mode) {
        check_cuda(cudaMemset(status.get(),0,sizeof(int)),"reset independent completion failure status");
        failure_kernel<<<1,1>>>(moved.view(),masked_moved.view(),samples[0],expected[0].values[1],mode,status.get(),failure.get());
        check_cuda(cudaGetLastError(),"launch strict completion failure query");
        FailureProbe result;
        check_cuda(cudaMemcpy(&result,failure.get(),sizeof(result),cudaMemcpyDeviceToHost),"download completion failure");
        check_cuda(cudaMemcpy(&failed,status.get(),sizeof(int),cudaMemcpyDeviceToHost),"download completion failure status");
        require(failed==1,"strict completion failure was not sticky");
        for(int i=0;i<6;++i) require(std::isnan(result.values[i]),"strict completion failure returned a non-NaN field");
        require(std::isfinite(result.values[6]),"a valid query after a failure became unusable");
    }
    check_cuda(cudaDeviceSynchronize(),"strict completion owner completion witness");
    std::cout << rank << " completed EOS CUDA: samples=" << sample_count << " fields=" << field_count
        << " max_scaled_difference=" << worst
        << " T(E)/T(P), host-release/move, independent mask/domain/NaN latches PASS\n";
}
} // namespace

int main(int argc,char** argv)
{
    try {
        if(argc!=4) throw std::runtime_error("expected Helm table, partial 3D table, partial 4D table");
        test_rank<Tabular3DEOS,arch::cuda::Tabular3DEOSDeviceOwner>(argv[2],argv[1],"3D");
        test_rank<Tabular4DEOS,arch::cuda::Tabular4DEOSDeviceOwner>(argv[3],argv[1],"4D");
    } catch(const std::exception& error) {
        std::cerr << "strict completion CUDA acceptance FAILED: " << error.what() << '\n';
        return 1;
    }
}
