/**
 * @file test_native_rz_reflection.cu
 * @brief Exercise the actual unpublished native CUDA reflecting consumer.
 *
 * Lower a real bound Grid, upload immutable U/X/ENUC including padding, launch
 * the production candidate adapter, and compare downloaded V/W means with
 * independent long-double polynomial antiderivatives. These finite cases grant
 * neither a completed phase overlay nor a boundary/EOS/Runtime capability.
 */
#include "cuda/hydro/boundary/Boundary.cuh"
#include "grid/Grid.h"
#include "physics/eos/IdealGas.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace native = arch::boundary::native_rz_math;
using arch::boundary::BoundaryAxis;
using arch::boundary::BoundarySide;
using Polynomial = std::array<long double,5>;
constexpr auto kRz = GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr long double kInternal = 1.L / 33554432.L;
constexpr long double kRadial = 1.L / 16.L, kAxial = -1.L / 32.L;

void require(bool value, std::string_view message)
{
    if (!value) throw std::runtime_error(std::string(message));
}

template<class T> class DeviceArray {
public:
    explicit DeviceArray(std::size_t count) : count_(count)
    {
        if (count) require(cudaMalloc(reinterpret_cast<void**>(&data_),count*sizeof(T))
            == cudaSuccess,"native reflector cudaMalloc failed");
    }
    ~DeviceArray() { cudaFree(data_); }
    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;
    T* get() const { return data_; }
    void upload(const std::vector<T>& values)
    {
        require(values.size()==count_,"native upload extent mismatch");
        if (count_) require(cudaMemcpy(data_,values.data(),count_*sizeof(T),cudaMemcpyHostToDevice)
            ==cudaSuccess,"native reflector upload failed");
    }
    std::vector<T> download() const
    {
        std::vector<T> values(count_);
        if (count_) require(cudaMemcpy(values.data(),data_,count_*sizeof(T),cudaMemcpyDeviceToHost)
            ==cudaSuccess,"native reflector download failed");
        return values;
    }
private:
    T* data_=nullptr;
    std::size_t count_;
};

void raw_equal(const std::vector<double>& actual,const std::vector<double>& expected,
               std::string_view label)
{
    require(actual.size()==expected.size(),"native raw extent mismatch");
    for (std::size_t i=0;i<actual.size();++i)
        require(std::bit_cast<std::uint64_t>(actual[i])==std::bit_cast<std::uint64_t>(expected[i]),label);
}

std::array<double,5> fields(const FluidVector& value)
{
    return {value.rho,value.mom_u,value.mom_v,value.mom_w,value.eng};
}

void fluid_raw_equal(const std::vector<FluidVector>& actual,const std::vector<FluidVector>& expected,
                     std::string_view label)
{
    require(actual.size()==expected.size(),"native fluid extent mismatch");
    for (std::size_t row=0;row<actual.size();++row) {
        const auto a=fields(actual[row]),b=fields(expected[row]);
        for (std::size_t field=0;field<a.size();++field)
            require(std::bit_cast<std::uint64_t>(a[field])==std::bit_cast<std::uint64_t>(b[field]),label);
    }
}

struct DeviceState {
    DeviceArray<double> rho,mr,mz,q,energy,enuc,xi;
    arch::cuda::DeviceStateView view{};
    DeviceState(int total,int species) : rho(total),mr(total),mz(total),q(total),energy(total),
        enuc(total),xi(static_cast<std::size_t>(total)*species)
    {
        view={rho.get(),mr.get(),mz.get(),q.get(),energy.get(),enuc.get(),xi.get(),total,species};
    }
    void upload(const FluidState& state)
    {
        rho.upload(state.rho);mr.upload(state.mom_u);mz.upload(state.mom_v);q.upload(state.mom_w);
        energy.upload(state.eng);enuc.upload(state.enuc_rate);xi.upload(state.mass_fractions);
    }
    void require_unchanged(const FluidState& original) const
    {
        raw_equal(rho.download(),original.rho,"reflector changed source rho/padding bits");
        raw_equal(mr.download(),original.mom_u,"reflector changed source mr/padding bits");
        raw_equal(mz.download(),original.mom_v,"reflector changed source mz/padding bits");
        raw_equal(q.download(),original.mom_w,"reflector changed source q/padding bits");
        raw_equal(energy.download(),original.eng,"reflector changed source energy/padding bits");
        raw_equal(enuc.download(),original.enuc_rate,"reflector changed source ENUC/padding bits");
        raw_equal(xi.download(),original.mass_fractions,"reflector changed source Xi/padding bits");
    }
};

struct DeviceMaterial {
    DeviceArray<double> a,z,gamma,cv;
    IdealGasView eos{};
    explicit DeviceMaterial(int species,double heat_capacity=2.) : a(species),z(species),
        gamma(species),cv(species)
    {
        a.upload(std::vector<double>(species,1.));z.upload(std::vector<double>(species,1.));
        gamma.upload(std::vector<double>(species,1.4));cv.upload(std::vector<double>(species,heat_capacity));
        eos.species={a.get(),z.get(),gamma.get(),cv.get(),species};eos.global_gamma=1.4;
    }
};

// Error-transport witness only: all numerical queries still delegate to the
// real IdealGas. A finite positive returned value cannot clear its error latch.
struct LatchedFiniteTemperatureEos : IdealGasView {
    int* device_error_status=nullptr;
    ARCH_INLINE double get_temperature(double rho,double internal,const double* fractions) const
    {
        const double temperature=IdealGasView::get_temperature(rho,internal,fractions);
        if (device_error_status) {
#if defined(__CUDA_ARCH__)
            atomicExch(device_error_status,1);
#else
            *device_error_status=1;
#endif
        }
        return temperature;
    }
};

Grid real_grid(double lower=1.)
{
    Grid grid(amr::MAX_NG,lower,lower+2.,-.125,.125,0.,1.);
    grid.dim=2;grid.geometry="cylindrical";grid.InitializeTopology(kRz);
    grid.dyadic_identity.bound=true;
    grid.dyadic_identity.root_lower={lower,-.125};grid.dyadic_identity.root_upper={lower+2.,.125};
    grid.dyadic_identity.root_blocks={1,1};grid.dyadic_identity.level=0;
    grid.dyadic_identity.logical={0,0};grid.dyadic_identity.periodic_axial=false;
    grid.InitializeTopology(kRz);
    return grid;
}

long double power(long double value,int exponent)
{
    long double product=1.L;
    for (int n=0;n<exponent;++n) product*=value;
    return product;
}

Polynomial multiply(const Polynomial& first,const Polynomial& second)
{
    Polynomial result{};
    for (int i=0;i<5;++i) for (int j=0;i+j<5;++j) result[i+j]+=first[i]*second[j];
    return result;
}

long double mean(const Polynomial& value,long double lower,long double upper,int weight)
{
    long double integral=0.L;
    for (int n=0;n<5;++n)
        integral+=value[n]*(power(upper,n+weight+1)-power(lower,n+weight+1))/(n+weight+1);
    return integral/((power(upper,weight+1)-power(lower,weight+1))/(weight+1));
}

// Independently integrate rho=rho0*(1+beta*r_s^2), v_phi=r_s, r_s=c+d*r.
// V weights rho/mr/mz/E by r; W weights native q by r^2. No production closure,
// point sampling or native integration routine enters the expected result.
FluidVector reference(double lower,double upper,long double beta,long double rho0,
                      long double c=0.L,long double d=1.L,int normal=-1)
{
    const Polynomial rho{rho0*(1.L+beta*c*c),rho0*2.L*beta*c*d,rho0*beta*d*d,0.L,0.L};
    const Polynomial swirl{c,d,0.L,0.L,0.L};
    const auto angular=multiply(rho,swirl);
    auto energy=multiply(rho,multiply(swirl,swirl));
    const long double constant=kInternal+.5L*(kRadial*kRadial+kAxial*kAxial);
    for (int n=0;n<5;++n) energy[n]=.5L*energy[n]+constant*rho[n];
    const long double density=mean(rho,lower,upper,1);
    return {double(density),double((normal==0?-kRadial:kRadial)*density),
        double((normal==1?-kAxial:kAxial)*density),double(mean(angular,lower,upper,2)),
        double(mean(energy,lower,upper,1))};
}

FluidState source_state(const Grid& grid,int species,long double beta,long double rho0)
{
    FluidState state;state.Preallocate(grid.GetTotalSize());state.InitSpecies(species);
    for (int index=0;index<grid.GetTotalSize();++index) {
        state.set(index,{987.25,123.5,-456.75,321.125,654.625});
        state.enuc_rate[index]=42.+index/8.;
        for (int s=0;s<species;++s) state.X(s,index)=.125+s/256.;
    }
    for (int j=0;j<grid.GetTotalY();++j) for (int i=0;i<grid.GetTotalX();++i) {
        const int index=grid.GetIndex(i,j,0);
        state.set(index,reference(grid.GetFacePosL(i),grid.GetFacePosR(i),beta,rho0));
        const double first=species==1?1.:.25+((i+2*j)%8)/64.;
        for (int s=0;s<species;++s) state.X(s,index)=s==0?first:(1.-first)/(species-1);
    }
    return state;
}

native::Request request(const Grid& grid,int normal,BoundarySide side,int depth,int tangent)
{
    const bool upper=side==BoundarySide::Upper;
    const int begin=normal==0?grid.Is():grid.Js(),end=normal==0?grid.Ie():grid.Je();
    const int donor=upper?end-depth:begin+depth-1;
    const int target=upper?end+depth-1:begin-depth;
    const auto source=normal==0?std::array<int,2>{donor,std::clamp(tangent,grid.Js(),grid.Je()-1)}
        :std::array<int,2>{tangent,donor};
    const auto destination=normal==0?std::array<int,2>{target,tangent}:std::array<int,2>{tangent,target};
    return {source,destination,normal==0?BoundaryAxis::X1:BoundaryAxis::X2,side,depth,
        arch::boundary::BoundaryPurpose::Hydro};
}

std::vector<native::Request> requests(const Grid& grid)
{
    std::vector<native::Request> result;
    for (int normal:{0,1}) for (auto side:{BoundarySide::Lower,BoundarySide::Upper})
        for (int depth=1;depth<=grid.ng;++depth) {
            const int begin=normal==0?grid.Js():grid.Is(),end=normal==0?grid.Je():grid.Ie();
            // Last two tangents exercise real completed corner donors outside
            // the active radial prefix; storage stride never substitutes for nx.
            for (int tangent:{begin,(begin+end)/2,end-1,begin-1,end})
                result.push_back(request(grid,normal,side,depth,tangent));
        }
    return result;
}

FluidVector sentinel() { return {901.25,902.5,903.75,904.125,905.625}; }

struct CandidateOwner {
    std::vector<FluidVector> seed_u;
    std::vector<double> seed_x;
    DeviceArray<FluidVector> u;
    DeviceArray<double> x,scratch;
    DeviceArray<int> failed;
    arch::cuda::SpeciesWorkspaceView workspace{};
    CandidateOwner(int count,int species) : seed_u(count,sentinel()),
        seed_x(static_cast<std::size_t>(count)*species,-77.25),u(count),x(seed_x.size()),
        scratch(static_cast<std::size_t>(species)*11*7),failed(1)
    {
        if (species) workspace={scratch.get(),static_cast<std::size_t>(species)*11*7,7,species,11};
        reset();
    }
    void reset() { u.upload(seed_u);x.upload(seed_x);failed.upload({0}); }
    void unchanged() const
    {
        fluid_raw_equal(u.download(),seed_u,"failed candidate wrote output U sentinel");
        raw_equal(x.download(),seed_x,"failed candidate wrote output Xi sentinel");
    }
};

void relative(double actual,double expected,std::string_view message)
{
    const double scale=std::max(std::abs(actual),std::abs(expected));
    if (!(std::isfinite(actual)&&std::isfinite(expected)
        &&std::abs(actual-expected)<=64.*std::numeric_limits<double>::epsilon()*scale)) {
        std::cerr<<message<<" actual="<<actual<<" expected="<<expected<<'\n';
        throw std::runtime_error(std::string(message));
    }
}

template<class Eos>
void launch(DeviceState& input,const arch::cuda::DeviceGridView& grid,
            const DeviceArray<native::Request>& device_requests,int count,
            const arch::state::Bounds& bounds,const Eos& eos,CandidateOwner& candidate)
{
    require(arch::cuda::launch_native_reflecting_candidates(input.view,grid,device_requests.get(),count,
        bounds,eos,candidate.workspace,candidate.u.get(),candidate.x.get(),candidate.failed.get(),nullptr)
        ==cudaSuccess,"native candidate kernel launch failed");
    require(cudaDeviceSynchronize()==cudaSuccess,"native candidate kernel execution failed");
}

void positive(int species,long double beta,long double rho0)
{
    const auto grid=real_grid();const auto device_grid=arch::cuda::make_device_grid_view(grid);
    require(arch::cuda::valid_hydro_grid(device_grid),"actual Native grid failed preflight");
    const auto original=source_state(grid,species,beta,rho0);
    const auto mirrors=requests(grid);
    DeviceState input(grid.GetTotalSize(),species);input.upload(original);
    DeviceMaterial material(species);
    DeviceArray<native::Request> device_requests(mirrors.size());device_requests.upload(mirrors);
    CandidateOwner candidate(static_cast<int>(mirrors.size()),species);
    const arch::state::Bounds bounds{rho0<1.L?1.e-30:1.e-14,0.,1.e8};
    launch(input,device_grid,device_requests,static_cast<int>(mirrors.size()),bounds,material.eos,candidate);
    require(candidate.failed.download()[0]==0,"valid actual Native reflector returned failure");
    const auto actual_u=candidate.u.download();
    const auto actual_x=candidate.x.download();
    for (std::size_t row=0;row<mirrors.size();++row) {
        const auto& mirror=mirrors[row];const int normal=static_cast<int>(mirror.axis);
        const long double wall=mirror.side==BoundarySide::Lower?grid.x1_min:grid.x1_max;
        const auto expected=reference(grid.GetFacePosL(mirror.destination[0]),
            grid.GetFacePosR(mirror.destination[0]),beta,rho0,normal==0?2.L*wall:0.L,
            normal==0?-1.L:1.L,normal);
        const auto a=fields(actual_u[row]),b=fields(expected);
        for (int field=0;field<5;++field)
            relative(a[field],b[field],"Native CUDA reflecting V/W mean differs from independent integral");
        const int source=grid.GetIndex(mirror.source[0],mirror.source[1],0);
        for (int s=0;s<species;++s) {
            relative(actual_x[row*species+s],original.X(s,source),"native reflector omitted or changed full Xi");
            relative(actual_u[row].rho*actual_x[row*species+s],expected.rho*original.X(s,source),
                "native reflector rho*Xi differs from independent mass moment");
        }
    }
    input.require_unchanged(original);
    if (beta==0.L&&rho0==1.L) {
        const auto mirror=request(grid,0,BoundarySide::Upper,1,grid.Js());
        const long double a=grid.GetFacePosL(mirror.source[0]),b=grid.GetFacePosR(mirror.source[0]);
        const long double c=grid.GetFacePosL(mirror.destination[0]),d=grid.GetFacePosR(mirror.destination[0]);
        const long double source_q=(power(b,4)-power(a,4))/4.L/((power(b,3)-power(a,3))/3.L);
        const long double source_r2=(power(b,4)-power(a,4))/4.L/((b*b-a*a)/2.L);
        const long double omega=source_q*((power(d,3)-power(c,3))/3.L)/((power(d,4)-power(c,4))/4.L);
        const long double target_r2=(power(d,4)-power(c,4))/4.L/((d*d-c*c)/2.L);
        require(kInternal+.5L*source_r2-.5L*omega*omega*target_r2<0.L,
            "raw-copy cold reflected state lost its independent negative-thermal counterexample");
    }
    std::cout<<"Native CUDA reflecting candidate passed species="<<species<<" beta="<<double(beta)
        <<" rho0="<<double(rho0)<<" requests="<<mirrors.size()<<" (64 eps; provisional only)\n";
}

void negatives()
{
    const auto grid=real_grid();const auto device_grid=arch::cuda::make_device_grid_view(grid);
    const auto original=source_state(grid,1,0.L,1.L);
    DeviceState input(grid.GetTotalSize(),1);input.upload(original);
    DeviceMaterial material(1),nonfinite_eos(1,1.e-320);
    CandidateOwner candidate(1,1);DeviceArray<native::Request> device_requests(1);
    const auto good=request(grid,0,BoundarySide::Upper,1,grid.Js());
    const arch::state::Bounds bounds{1.e-14,0.,1.e8};
    int rejected=0;
    const auto reject=[&](const native::Request& bad,const arch::cuda::DeviceGridView& geometry,
                          const IdealGasView& eos,const arch::state::Bounds& limits) {
        candidate.reset();device_requests.upload({bad});
        launch(input,geometry,device_requests,1,limits,eos,candidate);
        const int sticky=candidate.failed.download()[0];
        require(sticky!=0,"invalid Native candidate did not set failure");candidate.unchanged();
        input.require_unchanged(original);
        // A later valid launch may produce its unpublished candidate, but
        // cannot erase a failure from the same transaction's earlier launch.
        device_requests.upload({good});
        launch(input,device_grid,device_requests,1,bounds,material.eos,candidate);
        require(candidate.failed.download()[0]==sticky,"later native success cleared sticky failure");
        const auto completed=candidate.u.download();
        const auto expected=reference(grid.GetFacePosL(good.destination[0]),
            grid.GetFacePosR(good.destination[0]),0.L,1.L,2.L*grid.x1_max,-1.L,0);
        relative(completed[0].rho,expected.rho,"later valid native launch did not write its candidate");
        relative(candidate.x.download()[0],original.X(0,grid.GetIndex(good.source[0],good.source[1],0)),
            "later valid native launch did not write complete candidate Xi");
        input.require_unchanged(original);++rejected;
    };
    auto wrong=good;++wrong.source[0];reject(wrong,device_grid,material.eos,bounds);
    wrong=good;wrong.axis=arch::boundary::BoundaryAxis::X3;reject(wrong,device_grid,material.eos,bounds);
    wrong=good;wrong.ghost_depth=grid.ng+1;reject(wrong,device_grid,material.eos,bounds);
    reject(good,device_grid,material.eos,{1.e-14,0.,double(kInternal/2.L)});
    // The selected real IdealGas has positive finite Cv but nonfinite T=e/Cv.
    require(std::isfinite(1.e-320)&&1.e-320>0.&&!std::isfinite(double(kInternal)/1.e-320),
        "selected real IdealGas negative fixture does not overflow temperature");
    reject(good,device_grid,nonfinite_eos.eos,bounds);

    LatchedFiniteTemperatureEos latched;
    static_cast<IdealGasView&>(latched)=material.eos;
    candidate.reset();device_requests.upload({good});
    launch(input,device_grid,device_requests,1,bounds,latched,candidate);
    require(candidate.failed.download()[0]!=0,"native consumer published finite EOS output with a required-query failure");
    candidate.unchanged();input.require_unchanged(original);++rejected;

    // Prelaunch invalid workspace/bounds reject before any candidate/source write.
    candidate.reset();device_requests.upload({good});
    auto short_workspace=candidate.workspace;--short_workspace.capacity;
    require(arch::cuda::launch_native_reflecting_candidates(input.view,device_grid,device_requests.get(),1,
        bounds,material.eos,short_workspace,candidate.u.get(),candidate.x.get(),candidate.failed.get(),nullptr)
        ==cudaErrorInvalidValue,"native launcher accepted insufficient per-lane scratch");
    auto invalid_bounds=bounds;invalid_bounds.internal_max=-1.;
    require(arch::cuda::launch_native_reflecting_candidates(input.view,device_grid,device_requests.get(),1,
        invalid_bounds,material.eos,candidate.workspace,candidate.u.get(),candidate.x.get(),candidate.failed.get(),nullptr)
        ==cudaErrorInvalidValue,"native launcher accepted invalid full Bounds");
    candidate.unchanged();require(candidate.failed.download()[0]==0,"prelaunch rejection wrote failure word");
    input.require_unchanged(original);rejected+=2;

    // A real distinct positive root can have a target ghost crossing r=0.
    const auto crossing=real_grid(.03125);
    const auto crossing_original=source_state(crossing,1,0.L,1.L);
    DeviceState crossing_input(crossing.GetTotalSize(),1);crossing_input.upload(crossing_original);
    const auto crossing_request=request(crossing,0,BoundarySide::Lower,1,crossing.Js());
    require(crossing.GetFacePosL(crossing_request.destination[0])<0.
        &&crossing.GetFacePosR(crossing_request.destination[0])>0.,"cross-zero fixture does not cross zero");
    candidate.reset();device_requests.upload({crossing_request});
    launch(crossing_input,arch::cuda::make_device_grid_view(crossing),device_requests,1,bounds,material.eos,candidate);
    require(candidate.failed.download()[0]!=0,"native reflector folded a crossing-zero target");
    candidate.unchanged();crossing_input.require_unchanged(crossing_original);++rejected;

    // Corrupt one actual support observation, not a detached synthetic callback.
    for (bool nonfinite:{false,true}) {
        auto corrupted=original;const int donor=grid.GetIndex(good.source[0],good.source[1],0);
        if (nonfinite) corrupted.eng[donor]=std::numeric_limits<double>::quiet_NaN();
        else corrupted.rho[donor]=-1.;
        input.upload(corrupted);candidate.reset();device_requests.upload({good});
        launch(input,device_grid,device_requests,1,bounds,material.eos,candidate);
        require(candidate.failed.download()[0]!=0,"native reflector accepted corrupt actual source U");
        candidate.unchanged();input.require_unchanged(corrupted);++rejected;
    }
    std::cout<<"Native CUDA reflecting rejections passed cases="<<rejected
        <<" (unchanged failure rows/source/padding; sticky latch)\n";
}
} // namespace

void test_native_rz_reflecting_candidates()
{
    positive(4,0.L,1.L);
    positive(4,1.L/64.L,1.L);
    positive(4,0.L,1.e-20L);
    positive(0,0.L,1.L);
    positive(41,1.L/64.L,1.L);
    negatives();
}
