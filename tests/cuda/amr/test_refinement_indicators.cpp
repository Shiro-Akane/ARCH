/**
 * @file test_refinement_indicators.cpp
 * @brief Compare production GPU refinement indicators with shared host math.
 *
 * Cases cover different fields and species layouts, including recovered
 * tabular temperatures and propagation of invalid EOS states.
 */
#include "cuda/amr/RefinementIndicators.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"
#include "numerics/state/RzNativeClosure.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void verify_cuda_jeans_resolution();

namespace {
void check(cudaError_t status) {
    if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status));
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> struct Buffer {
    T* data = nullptr;
    explicit Buffer(std::size_t count) {
        check(cudaMalloc(reinterpret_cast<void**>(&data), count * sizeof(T)));
    }
    ~Buffer() { if (data) cudaFree(data); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

void test_batch_wave(std::size_t count) {
    Grid grid(2, 1.0, 2.0);
    grid.dim = 1;
    grid.InitializeTopology();
    const auto device_grid = arch::cuda::make_device_grid_view(grid);
    const std::size_t cells = grid.GetTotalSize();
    const auto capacity = count == 3 ? 2 : arch::cuda::indicator_wave_capacity(cells, 0, false, count);
    std::vector<double> fields(6 * cells * count, 0.0);
    for (std::size_t b = 0; b < count; ++b)
        for (std::size_t i = 0; i < cells; ++i) {
            const double x = static_cast<double>(i) / cells;
            fields[b*6*cells+i] = 1.0 + 0.001*(b+1)*x*x;
            fields[b*6*cells+4*cells+i] = 10.0;
        }
    Buffer<double> state(fields.size()), errors(capacity*cells), summaries(count);
    Buffer<int> status(count);
    Buffer<amr::indicator::Selection> selections(1);
    Buffer<arch::cuda::DeviceIndicatorBatchBlock> device(count);
    const amr::indicator::Selection density{amr::indicator::Field::Density};
    check(cudaMemcpy(state.data,fields.data(),fields.size()*sizeof(double),cudaMemcpyHostToDevice));
    check(cudaMemcpy(selections.data,&density,sizeof(density),cudaMemcpyHostToDevice));
    std::vector<arch::cuda::DeviceIndicatorBatchBlock> blocks(count);
    for (std::size_t b = 0; b < count; ++b) {
        auto* values = state.data + b*6*cells;
        blocks[b].state = {values,values+cells,values+2*cells,values+3*cells,
            values+4*cells,values+5*cells,nullptr,static_cast<int>(cells),0};
        blocks[b].grid = device_grid;
        if (count == 3 && b == 1) --blocks[b].grid.ie;
        blocks[b].workspace = {selections.data,1,1e-12,false,false,false,nullptr,nullptr,
            errors.data+(b%capacity)*cells,summaries.data+b,status.data+b};
        check(arch::cuda::launch_cuda_refinement_indicators(blocks[b].state,blocks[b].grid,
            IdealGasView{},blocks[b].workspace,nullptr));
    }
    std::vector<double> reference(count), actual(count);
    check(cudaMemcpy(reference.data(),summaries.data,count*sizeof(double),cudaMemcpyDeviceToHost));
    check(cudaMemcpy(device.data,blocks.data(),count*sizeof(blocks[0]),cudaMemcpyHostToDevice));
    check(cudaMemset(status.data,0x7f,count*sizeof(int)));
    const auto launched=arch::cuda::launch_cuda_refinement_indicators_batch(
        blocks,device.data,capacity,IdealGasView{},nullptr);
    check(launched.error);
    require(launched.kernels_launched==3*static_cast<int>((count+capacity-1)/capacity),
        "indicator batch did not fuse its launches");
    check(cudaMemcpy(actual.data(),summaries.data,count*sizeof(double),cudaMemcpyDeviceToHost));
    std::vector<int> flags(count);
    check(cudaMemcpy(flags.data(),status.data,count*sizeof(int),cudaMemcpyDeviceToHost));
    for (std::size_t b = 0; b < count; ++b) {
        require(std::isfinite(reference[b]) && std::bit_cast<std::uint64_t>(actual[b])
            == std::bit_cast<std::uint64_t>(reference[b]),"indicator batch changed ordered block maximum");
        require(flags[b]==0,"indicator batch failed to reset a reused status");
    }
    blocks.back().workspace.cell_errors=nullptr;
    check(cudaMemset(status.data,0x7f,count*sizeof(int)));
    const auto rejected=arch::cuda::launch_cuda_refinement_indicators_batch(
        blocks,device.data,capacity,IdealGasView{},nullptr);
    check(cudaMemcpy(flags.data(),status.data,count*sizeof(int),cudaMemcpyDeviceToHost));
    require(rejected.error==cudaErrorInvalidValue && rejected.kernels_launched==0 && flags[0]==0x7f7f7f7f,
        "late invalid indicator binding partially wrote an earlier block");
}

void run(int dimension, const std::string& geometry) {
    Grid grid(2, 1.0, 2.0, 0.4, 1.1, 0.2, 0.8);
    grid.dim = dimension;
    grid.geometry = geometry;
    grid.InitializeTopology();
    const int cells = grid.GetTotalSize();
    constexpr int species_count = 2;
    constexpr int field_count = 6 + species_count;
    std::vector<double> fields(static_cast<std::size_t>(field_count) * cells);
    for (int k = 0; k < grid.GetTotalZ(); ++k)
        for (int j = 0; j < grid.GetTotalY(); ++j)
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                const double x = grid.GetCellCenterX(i);
                const double y = grid.GetCellCenterY(j);
                const double z = grid.GetCellCenterZ(k);
                const double density = 2.0 + x * x + y * y + z * z;
                fields[cell] = density;
                fields[cells + cell] = density * (0.2 + x * y);
                fields[2 * cells + cell] = density * (0.3 + x * x);
                fields[3 * cells + cell] = density * (0.4 + z * x);
                // Keep every ghost and interior state thermally admissible, including
                // the largest polynomial velocities at the outer stencil.
                const double u=.2+x*y,v=.3+x*x,w=.4+z*x;
                fields[4*cells+cell]=density*(10.+x*x+y*y+.5*(u*u+v*v+w*w));
                fields[5 * cells + cell] = std::sin(x * x);
                fields[6 * cells + cell] = 0.2 + 0.05 * std::sin(x + y);
                fields[7 * cells + cell] = 1.0 - fields[6 * cells + cell];
            }
    IdealGasView eos;
    std::vector<double> thermo(3 * cells);
    for (int cell = 0; cell < cells; ++cell) {
        const FluidVector fluid{fields[cell], fields[cells + cell],
            fields[2 * cells + cell], fields[3 * cells + cell], fields[4 * cells + cell]};
        const auto value = amr::indicator::thermodynamics(fluid, nullptr, eos, true, true, true);
        thermo[cell] = value.pressure;
        thermo[cells + cell] = value.temperature;
        thermo[2 * cells + cell] = value.gamma1;
    }
    AmrConfig config;
    config.refine_on_p = config.refine_on_temp = config.refine_on_eng = true;
    config.refine_on_velx = config.refine_on_vorticity = config.refine_on_div_v = true;
    config.refine_on_vely = dimension >= 2;
    config.refine_on_velz = dimension == 3;
    config.refine_on_entropy = config.refine_on_enuc = config.refine_on_species = true;
    const int species[] = {0, 1};
    const auto selections = amr::indicator::make_selection(config, dimension, species);
    constexpr double density_floor = 1.e-12;
    const amr::indicator::StateView host{
        fields.data(), {fields.data() + cells, fields.data() + 2 * cells,
            fields.data() + 3 * cells}, fields.data() + 4 * cells,
        fields.data() + 5 * cells, fields.data() + 6 * cells,
        thermo.data(), thermo.data() + cells, thermo.data() + 2 * cells, cells,
        grid.Is(), grid.Ie(), grid.Js(), grid.Je(), grid.Ks(), grid.Ke(), density_floor,
        species_count};
    Buffer<double> device_fields(fields.size()), device_thermo(thermo.size()),
        composition(static_cast<std::size_t>(cells) * species_count), errors(cells), summary(1);
    Buffer<amr::indicator::Selection> selection(1);
    Buffer<int> eos_status(1);
    Buffer<double> peer_errors(cells), peer_summary(1);
    Buffer<int> peer_status(1);
    Buffer<amr::indicator::Selection> peer_selection(1);
    Buffer<arch::cuda::DeviceIndicatorBatchBlock> device_batch(2);
    const amr::indicator::Selection peer_density{amr::indicator::Field::Density};
    check(cudaMemcpy(peer_selection.data,&peer_density,sizeof(peer_density),cudaMemcpyHostToDevice));
    check(cudaMemcpy(device_fields.data, fields.data(), fields.size() * sizeof(double), cudaMemcpyHostToDevice));
    arch::cuda::DeviceStateView device{device_fields.data, device_fields.data + cells,
        device_fields.data + 2 * cells, device_fields.data + 3 * cells,
        device_fields.data + 4 * cells, device_fields.data + 5 * cells,
        device_fields.data + 6 * cells, cells, species_count};
    arch::cuda::DeviceIndicatorWorkspace workspace{selection.data, 1, density_floor,
        true, true, true, device_thermo.data, composition.data, errors.data, summary.data,
        eos_status.data};
    double expected_peer=0.0;
    for (int k=grid.Ks(); k<grid.Ke(); ++k)
        for (int j=grid.Js(); j<grid.Je(); ++j)
            for (int i=grid.Is(); i<grid.Ie(); ++i)
                expected_peer=std::max(expected_peer,amr::indicator::cell_error(host,grid,&peer_density,1,i,j,k));
    for (const auto selected : selections) {
        double expected = 0.0;
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
            for (int j = grid.Js(); j < grid.Je(); ++j)
                for (int i = grid.Is(); i < grid.Ie(); ++i) {
                    const auto value = amr::indicator::cell_error(host, grid, &selected, 1, i, j, k);
                    require(std::isfinite(value), "nonfinite Host fixture");
                    expected = std::max(expected, value);
                }
        check(cudaMemcpy(selection.data, &selected, sizeof(selected), cudaMemcpyHostToDevice));
        check(arch::cuda::launch_cuda_refinement_indicators(device,
            arch::cuda::make_device_grid_view(grid), eos, workspace, nullptr));
        check(cudaDeviceSynchronize());
        double actual = 0.0;
        check(cudaMemcpy(&actual, summary.data, sizeof(actual), cudaMemcpyDeviceToHost));
        require(std::isfinite(actual) && std::abs(actual - expected) <= 2.e-13,
            "CUDA AMR indicator differs from Host math");
        auto peer = workspace;
        peer.selection=peer_selection.data;
        peer.pressure=peer.temperature=peer.gamma1=false;
        peer.thermodynamics=peer.composition=nullptr;
        peer.cell_errors=peer_errors.data; peer.block_error=peer_summary.data; peer.eos_status=peer_status.data;
        const std::array<arch::cuda::DeviceIndicatorBatchBlock,2> batch{{
            {device,arch::cuda::make_device_grid_view(grid),workspace},
            {device,arch::cuda::make_device_grid_view(grid),peer}}};
        check(cudaMemcpy(device_batch.data,batch.data(),sizeof(batch),cudaMemcpyHostToDevice));
        const auto fused=arch::cuda::launch_cuda_refinement_indicators_batch(batch,device_batch.data,2,eos,nullptr);
        check(fused.error);
        require(fused.kernels_launched==4,"thermodynamic indicator batch was not fused");
        double fused_value=0.0;
        check(cudaMemcpy(&fused_value,summary.data,sizeof(double),cudaMemcpyDeviceToHost));
        require(std::bit_cast<std::uint64_t>(fused_value)==std::bit_cast<std::uint64_t>(actual),
            "batched thermodynamic/curved indicator changed scalar result");
        double peer_value=0.0;
        int peer_flag=-1;
        check(cudaMemcpy(&peer_value,peer_summary.data,sizeof(double),cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&peer_flag,peer_status.data,sizeof(int),cudaMemcpyDeviceToHost));
        require(std::isfinite(peer_value) && std::abs(peer_value-expected_peer)<=2.e-13 && peer_flag==0,
            "thermodynamic block contaminated a nonthermodynamic peer");
    }
    // A nonfinite selected field must propagate to the backend, not disappear
    // behind a max reduction (the Host executor rejects the same sentinel).
    fields[grid.GetIndex(grid.Is(), grid.Js(), grid.Ks())] = std::numeric_limits<double>::quiet_NaN();
    check(cudaMemcpy(device_fields.data, fields.data(), fields.size() * sizeof(double), cudaMemcpyHostToDevice));
    const amr::indicator::Selection density{amr::indicator::Field::Density};
    check(cudaMemcpy(selection.data, &density, sizeof(density), cudaMemcpyHostToDevice));
    workspace.pressure = workspace.temperature = workspace.gamma1 = false;
    check(arch::cuda::launch_cuda_refinement_indicators(device,
        arch::cuda::make_device_grid_view(grid), eos, workspace, nullptr));
    check(cudaDeviceSynchronize());
    double invalid = 0.0;
    check(cudaMemcpy(&invalid, summary.data, sizeof(invalid), cudaMemcpyDeviceToHost));
    require(!std::isfinite(invalid), "nonfinite indicator was silently accepted");
    // Density is invalid, energy remains finite. With thermodynamics disabled,
    // a different block's selected field must retain its own reduction result.
    const amr::indicator::Selection energy{amr::indicator::Field::Energy};
    check(cudaMemcpy(peer_selection.data,&energy,sizeof(energy),cudaMemcpyHostToDevice));
    auto peer=workspace;
    peer.selection=peer_selection.data;
    peer.cell_errors=peer_errors.data; peer.block_error=peer_summary.data; peer.eos_status=peer_status.data;
    std::array<arch::cuda::DeviceIndicatorBatchBlock,2> mixed{{
        {device,arch::cuda::make_device_grid_view(grid),workspace},
        {device,arch::cuda::make_device_grid_view(grid),peer}}};
    for (int ordering=0; ordering<2; ++ordering) {
        if (ordering) std::swap(mixed[0],mixed[1]);
        check(cudaMemcpy(device_batch.data,mixed.data(),sizeof(mixed),cudaMemcpyHostToDevice));
        check(arch::cuda::launch_cuda_refinement_indicators_batch(mixed,device_batch.data,2,eos,nullptr).error);
        double valid_peer=0.0;
        check(cudaMemcpy(&invalid,summary.data,sizeof(double),cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&valid_peer,peer_summary.data,sizeof(double),cudaMemcpyDeviceToHost));
        require(std::isnan(invalid) && std::isfinite(valid_peer),
            "nonfinite block indicator contaminated its peer or disappeared");
    }
}

template<class Eos>
void test_recovered_tabular_temperature(Eos eos, int compositions) {
    Grid grid(1, 1.0, 2.0);
    grid.dim = 1;
    grid.InitializeTopology();
    const int cells = grid.GetTotalSize();
    const double metadata[]{14.,7.,1.4,1.}, fraction[]{1.};
    Buffer<double> device_metadata(4), composition(cells), peer_composition(cells);
    check(cudaMemcpy(device_metadata.data,metadata,sizeof(metadata),cudaMemcpyHostToDevice));
    const SpeciesPODView host_species{metadata,metadata+1,metadata+2,metadata+3,1};
    eos.specs={device_metadata.data,device_metadata.data+1,device_metadata.data+2,device_metadata.data+3,1};
    std::vector<double> fields(7 * cells, 0.0);
    std::fill_n(fields.data()+6*cells,cells,1.0);
    std::fill_n(fields.data(), cells, 1.0);
    std::fill_n(fields.data() + 4 * cells, cells, 100.0);
    Buffer<double> device_fields(fields.size()), thermo(3 * cells), errors(cells), summary(1);
    Buffer<int> eos_status(1);
    Buffer<amr::indicator::Selection> selection(1);
    const amr::indicator::Selection temperature{amr::indicator::Field::Temperature};
    check(cudaMemcpy(selection.data, &temperature, sizeof(temperature), cudaMemcpyHostToDevice));
    check(cudaMemcpy(device_fields.data, fields.data(), fields.size() * sizeof(double), cudaMemcpyHostToDevice));
    arch::cuda::DeviceStateView state{device_fields.data, device_fields.data + cells,
        device_fields.data + 2 * cells, device_fields.data + 3 * cells,
        device_fields.data + 4 * cells, device_fields.data + 5 * cells,
        device_fields.data+6*cells, cells, 1};
    arch::cuda::DeviceIndicatorWorkspace workspace{selection.data, 1, 1.e-12,
        false, true, false, thermo.data, composition.data, errors.data, summary.data, eos_status.data};

    // F=100+2r+rt-1.5t^2, with r=ln(rho), t=ln(T), is represented exactly.
    // At rho=T=1, e=100 and Cv=3. Invalidating the lower
    // endpoint must fail the bounded inverse and remain latched through reduction.
    const int extent = 4 * compositions;
    std::vector<double> table(tabular_eos::FieldCount * extent);
    Buffer<double> device_table(table.size());

    for (const bool fail : {true, false}) {
        for (int ir=0; ir<2; ++ir) for (int it=0; it<2; ++it) {
            const double r=ir*std::log(10.0), t=it*.2*std::log(10.0);
            const double jets[]{100.+2.*r+r*t-1.5*t*t, 2.+t, r-3.*t,
                                0., 1., -3., 0., 0., 0.};
            for (int field=0; field<tabular_eos::FieldCount; ++field)
                std::fill_n(table.data()+field*extent+(ir*2+it)*compositions,
                            compositions,jets[field]);
        }
        if (fail)
            for (int rho = 0; rho < 2; ++rho)
                std::fill_n(table.data() + tabular_eos::Fyy * extent
                    + rho * 2 * compositions, compositions, rho*std::log(10.0));
        Eos host = eos; host.specs=host_species;
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            host.free_energy_fields[field] = table.data() + field * extent;
        bool host_threw = false;
        try { static_cast<void>(host.get_temperature(1.0, 100.0, fraction)); }
        catch (const std::runtime_error&) { host_threw = true; }
        require(host_threw == fail, "Tabular temperature fixture does not exercise Host failure");
        check(cudaMemcpy(device_table.data, table.data(), table.size() * sizeof(double), cudaMemcpyHostToDevice));
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            eos.free_energy_fields[field] = device_table.data + field * extent;
        check(arch::cuda::launch_cuda_refinement_indicators(state,
            arch::cuda::make_device_grid_view(grid), eos, workspace, nullptr));
        std::vector<double> actual_thermo(3 * cells);
        double actual = 0.0;
        int status = -1;
        check(cudaMemcpy(actual_thermo.data(), thermo.data, actual_thermo.size() * sizeof(double), cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&actual, summary.data, sizeof(actual), cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&status, eos_status.data, sizeof(status), cudaMemcpyDeviceToHost));
        for (int cell = 0; cell < cells; ++cell)
            require(fail ? std::isnan(actual_thermo[cells + cell])
                         : std::abs(actual_thermo[cells + cell]-1.0)<1.e-10,
                "Tabular bounded inversion did not preserve success/failure semantics");
        require(status == (fail ? 1 : 0) && (fail ? std::isnan(actual) : actual == 0.0),
            "AMR discarded an intermediate EOS failure or failed to reset the next launch");
    }
    // Mix one failed EOS endpoint with a valid endpoint in both binding orders.
    // This also exercises a reset after the original scalar failure/recovery.
    std::fill_n(table.data()+tabular_eos::Fyy*extent,compositions,0.0);
    Eos host=eos; host.specs=host_species;
    for (int field=0; field<tabular_eos::FieldCount; ++field)
        host.free_energy_fields[field]=table.data()+field*extent;
    bool lower_failed=false;
    try { static_cast<void>(host.get_temperature(1.0,100.0,fraction)); }
    catch (const std::runtime_error&) { lower_failed=true; }
    require(lower_failed && std::isfinite(host.get_temperature(10.0,100.0+std::log(10.0),fraction)),
        "mixed AMR EOS fixture did not isolate two density endpoints");
    check(cudaMemcpy(device_table.data,table.data(),table.size()*sizeof(double),cudaMemcpyHostToDevice));
    auto peer_fields=fields;
    std::fill_n(peer_fields.data(),cells,10.0);
    std::fill_n(peer_fields.data()+4*cells,cells,10.*(100.+std::log(10.0)));
    Buffer<double> peer_state(peer_fields.size()),peer_thermo(3*cells),peer_errors(cells),peer_summary(1);
    Buffer<int> peer_status(1);
    Buffer<arch::cuda::DeviceIndicatorBatchBlock> device_batch(2);
    check(cudaMemcpy(peer_state.data,peer_fields.data(),peer_fields.size()*sizeof(double),cudaMemcpyHostToDevice));
    arch::cuda::DeviceStateView valid_state{peer_state.data,peer_state.data+cells,peer_state.data+2*cells,
        peer_state.data+3*cells,peer_state.data+4*cells,peer_state.data+5*cells,peer_state.data+6*cells,cells,1};
    auto peer_workspace=workspace;
    peer_workspace.composition=peer_composition.data;
    peer_workspace.thermodynamics=peer_thermo.data; peer_workspace.cell_errors=peer_errors.data;
    peer_workspace.block_error=peer_summary.data; peer_workspace.eos_status=peer_status.data;
    std::array<arch::cuda::DeviceIndicatorBatchBlock,2> mixed{{
        {state,arch::cuda::make_device_grid_view(grid),workspace},
        {valid_state,arch::cuda::make_device_grid_view(grid),peer_workspace}}};
    for (int ordering=0; ordering<2; ++ordering) {
        if (ordering) std::swap(mixed[0],mixed[1]);
        check(cudaMemcpy(device_batch.data,mixed.data(),sizeof(mixed),cudaMemcpyHostToDevice));
        check(arch::cuda::launch_cuda_refinement_indicators_batch(mixed,device_batch.data,2,eos,nullptr).error);
        double failed=0.0,valid=-1.0;
        int failed_status=0,valid_status=-1;
        check(cudaMemcpy(&failed,summary.data,sizeof(double),cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&valid,peer_summary.data,sizeof(double),cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&failed_status,eos_status.data,sizeof(int),cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&valid_status,peer_status.data,sizeof(int),cudaMemcpyDeviceToHost));
        require(std::isnan(failed) && valid==0.0 && failed_status==1 && valid_status==0,
            "AMR batch lost or cross-contaminated its EOS failure latch");
    }
    workspace.eos_status = nullptr;
    require(arch::cuda::launch_cuda_refinement_indicators(state,
        arch::cuda::make_device_grid_view(grid), eos, workspace, nullptr) == cudaErrorInvalidValue,
        "Thermodynamic indicators accepted an unbound failure latch");
}

// Native observer fixtures use shared Host closure, with independently integrated
// uniform-density V/W means. No raw J/W momentum is submitted to a point EOS.
constexpr auto native_rz = GridMetrics::GeometrySemantics::AxisymmetricRz;
constexpr int native_species = 2;
constexpr double native_floor = 1.e-12;
constexpr double native_internal = 8.;
constexpr arch::state::Bounds native_bounds{native_floor,1.e-8,100.};

Grid native_indicator_grid(double lower) {
    Grid grid(amr::MAX_NG,lower,lower+2.,-.125,.125,0.,1.);
    grid.dim=2; grid.geometry="cylindrical";
    grid.InitializeTopology(native_rz);
    grid.dyadic_identity.bound=true;
    grid.dyadic_identity.root_lower={lower,-.125};
    grid.dyadic_identity.root_upper={lower+2.,.125};
    grid.dyadic_identity.root_blocks={1,1};
    grid.dyadic_identity.level=0; grid.dyadic_identity.logical={0,0};
    grid.dyadic_identity.periodic_axial=false;
    grid.InitializeTopology(native_rz);
    return grid;
}

void test_native_observer_consumers(double lower,bool curved,bool thermo_on) {
    using amr::indicator::Field;
    const Grid grid=native_indicator_grid(lower);
    const auto geometry=GridMetrics::make_geometry_view(grid,native_rz);
    const auto device_grid=arch::cuda::make_device_grid_view(grid,native_rz);
    const int cells=grid.GetTotalSize();
    constexpr int count=3,capacity=2,planes=1+3+native_species+3;
    const double poison=std::bit_cast<double>(std::uint64_t{0x7ff800000000a57d});
    std::vector<double> fields(count*8*cells,poison);
    std::array<std::vector<double>,count> thermo,velocity;
    std::array<amr::indicator::StateView,count> host;
    IdealGasView eos;
    for (int b=0;b<count;++b) {
        auto* f=fields.data()+b*8*cells;
        for (int j=0;j<grid.GetTotalY();++j)
            for (int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j,0);
                const long double l=grid.GetFacePosL(i),h=grid.GetFacePosR(i);
                const long double volume=(h*h-l*l)/2.L;
                const long double angular=(h*h*h-l*l*l)/3.L;
                const long double inertia=(h*h*h*h-l*l*l*l)/4.L;
                const long double r=grid.GetCellCenterX(i);
                const long double omega=(2.L+b/4.L)*(curved?1.L+r*r/8.L:1.L);
                // Signed negative ghosts have signed V/I and positive W;
                // I/V stays positive and I/W preserves angular parity.
                f[cell]=1.; f[cells+cell]=f[2*cells+cell]=0.;
                f[3*cells+cell]=static_cast<double>(omega*inertia/angular);
                f[4*cells+cell]=static_cast<double>(native_internal+omega*omega*inertia/(2.L*volume));
                f[5*cells+cell]=.25; f[6*cells+cell]=.25; f[7*cells+cell]=.75;
            }
        thermo[b].assign(3*cells,poison); velocity[b].assign(3*cells,poison);
        const auto read=[&](int cell) {
            return FluidVector{f[cell],f[cells+cell],f[2*cells+cell],f[3*cells+cell],f[4*cells+cell]};
        };
        for (int j=0;j<grid.GetTotalY();++j)
            for (int i=0;i<grid.GetTotalX();++i) {
                const int cell=grid.GetIndex(i,j,0);
                const double xi[]{f[6*cells+cell],f[7*cells+cell]};
                const auto closure=RzThermodynamics::make_cell_supported(read,cell,geometry,i,
                    std::clamp(i-1,0,grid.GetTotalX()-3),native_bounds);
                require(closure.valid() && arch::state::validate_eos(closure.effective_mean,
                    xi,native_species,native_bounds,eos)==arch::state::Status::valid,
                    "Native Host mean fixture is inadmissible");
                const auto values=amr::indicator::thermodynamics(closure.effective_mean,xi,eos,true,true,true);
                const auto point=RzThermodynamics::base_point(closure,geometry.GetCellCenterX(i));
                require(arch::state::validate_eos(point,xi,native_species,native_bounds,eos)
                    ==arch::state::Status::valid,"Native Host center fixture is inadmissible");
                thermo[b][cell]=values.pressure; thermo[b][cells+cell]=values.temperature;
                thermo[b][2*cells+cell]=values.gamma1;
                velocity[b][cell]=point.mom_u/point.rho; velocity[b][cells+cell]=point.mom_v/point.rho;
                velocity[b][2*cells+cell]=point.mom_w/point.rho;
                const double r=grid.GetCellCenterX(i);
                const double omega=(2.+b/4.)*(curved?1.+r*r/8.:1.);
                require(std::abs(values.pressure-(eos.global_gamma-1.)*native_internal)<=2.e-13
                    && std::abs(values.temperature-native_internal/IdealGasView::default_specific_heat_cv)<=2.e-13
                    && std::abs(velocity[b][2*cells+cell]-omega*r)<=2.e-13,
                    "Native Host closure violated independent constant P/T or physical swirl identity");
            }
        if (!curved) {
            const amr::indicator::Selection curl{Field::Vorticity},div{Field::Divergence};
            amr::indicator::StateView analytic{};
            analytic.density=f; analytic.cells=cells;
            for (int axis=0;axis<3;++axis) {
                analytic.momentum[axis]=f+(axis+1)*cells;
                analytic.physical_velocity[axis]=velocity[b].data()+axis*cells;
            }
            for (int i=grid.Is()-1;i<=grid.Ie();++i) {
                require(std::abs(analytic.value(curl,geometry,i,grid.Js(),0)-2.*(2.+b/4.))<=2.e-13
                    && std::abs(analytic.value(div,geometry,i,grid.Js(),0))<=2.e-13,
                    "Native rigid-rotation analytic curl/divergence identity changed");
            }
        }
        host[b]={f,{f+cells,f+2*cells,f+3*cells},f+4*cells,f+5*cells,f+6*cells,
            thermo[b].data(),thermo[b].data()+cells,thermo[b].data()+2*cells,cells,
            grid.Is(),grid.Ie(),grid.Js(),grid.Je(),grid.Ks(),grid.Ke(),native_floor,native_species,
            {velocity[b].data(),velocity[b].data()+cells,velocity[b].data()+2*cells}};
    }
    require(grid.GetTotalX()<grid.stride_y,"Native poison fixture contains no pitch padding");
    Buffer<double> state(fields.size()),arena(capacity*planes*cells),summaries(count);
    Buffer<int> statuses(count);
    const std::vector<double> arena_poison(capacity*planes*cells,poison);
    check(cudaMemcpy(arena.data,arena_poison.data(),arena_poison.size()*sizeof(double),cudaMemcpyHostToDevice));
    Buffer<amr::indicator::Selection> selected(count);
    Buffer<arch::cuda::DeviceIndicatorBatchBlock> device_batch(count);
    check(cudaMemcpy(state.data,fields.data(),fields.size()*sizeof(double),cudaMemcpyHostToDevice));
    std::array<arch::cuda::DeviceIndicatorBatchBlock,count> blocks;
    for (int b=0;b<count;++b) {
        auto* f=state.data+b*8*cells; auto* a=arena.data+(b%capacity)*planes*cells;
        blocks[b].state={f,f+cells,f+2*cells,f+3*cells,f+4*cells,f+5*cells,f+6*cells,cells,native_species};
        blocks[b].grid=device_grid;
        blocks[b].workspace={selected.data+b,1,native_floor,thermo_on,thermo_on,thermo_on,
            a,a+3*cells,a+(3+native_species+3)*cells,summaries.data+b,statuses.data+b};
        blocks[b].workspace.bounds=native_bounds;
        for (int axis=0;axis<3;++axis)
            blocks[b].workspace.physical_velocity[axis]=a+(3+native_species+axis)*cells;
    }
    // The middle block borrows its own wave slot but requests no thermodynamics.
    auto& peer=blocks[1].workspace;
    peer.pressure=peer.temperature=peer.gamma1=false;
    peer.thermodynamics=peer.composition=nullptr;
    for (auto& component:peer.physical_velocity) component=nullptr;
    const auto upload_bindings=[&] {
        check(cudaMemcpy(device_batch.data,blocks.data(),sizeof(blocks),cudaMemcpyHostToDevice));
    };
    const auto maximum=[&](int b,amr::indicator::Selection choice) {
        double result=0.;
        for (int j=grid.Js();j<grid.Je();++j) for (int i=grid.Is();i<grid.Ie();++i) {
            const double value=amr::indicator::cell_error(host[b],geometry,&choice,1,i,j,0);
            require(std::isfinite(value),"Native Host indicator reference is nonfinite");
            result=std::max(result,value);
        }
        return result;
    };
    const std::array<Field,8> consumers{Field::Pressure,Field::Temperature,Field::Entropy,
        Field::VelocityX,Field::VelocityY,Field::VelocityZ,Field::Vorticity,Field::Divergence};
    for (const auto field:consumers) {
        if (!thermo_on && (field==Field::Pressure || field==Field::Temperature || field==Field::Entropy)) continue;
        const std::array<amr::indicator::Selection,count> choices{{{field},{Field::Density},{field}}};
        check(cudaMemcpy(selected.data,choices.data(),sizeof(choices),cudaMemcpyHostToDevice));
        std::array<double,count> scalar{},actual{};
        for (int b=0;b<count;++b) {
            check(arch::cuda::launch_cuda_refinement_indicators(blocks[b].state,blocks[b].grid,eos,blocks[b].workspace,nullptr));
            check(cudaMemcpy(&scalar[b],summaries.data+b,sizeof(double),cudaMemcpyDeviceToHost));
            require(std::isfinite(scalar[b]) && std::abs(scalar[b]-maximum(b,choices[b]))<=2.e-13,
                "Native scalar indicator differs from shared Host math");
        }
        check(cudaMemset(statuses.data,0x7f,count*sizeof(int)));
        upload_bindings();
        const auto fused=arch::cuda::launch_cuda_refinement_indicators_batch(blocks,device_batch.data,capacity,eos,nullptr);
        check(fused.error);
        require(fused.kernels_launched==8,"Native physical/thermodynamic wave launch accounting changed");
        check(cudaMemcpy(actual.data(),summaries.data,sizeof(actual),cudaMemcpyDeviceToHost));
        std::array<int,count> flags{};
        check(cudaMemcpy(flags.data(),statuses.data,sizeof(flags),cudaMemcpyDeviceToHost));
        for (int b=0;b<count;++b)
            require(std::bit_cast<std::uint64_t>(actual[b])==std::bit_cast<std::uint64_t>(scalar[b]) && flags[b]==0,
                "Native batch changed scalar result or contaminated private/nonthermo peer status");
        if (field==Field::VelocityZ) {
            const double physical=maximum(0,choices[0]);
            auto raw=host[0]; for (auto& component:raw.physical_velocity) component=nullptr;
            double raw_max=0.;
            for (int j=grid.Js();j<grid.Je();++j) for (int i=grid.Is();i<grid.Ie();++i)
                raw_max=std::max(raw_max,amr::indicator::cell_error(raw,geometry,&choices[0],1,i,j,0));
            require(std::abs(raw_max-physical)>1.e-8,"Native swirl fixture cannot discriminate J/W from physical velocity");
            if (!curved) require(physical<=2.e-13,"Rigid rotation generated physical swirl curvature");
            else require(physical>1.e-6,"Curved physical swirl lost its independent nonzero curvature");
        }
        // Inspect real scratch, including signed-axis ghosts, independently of
        // the final maximum. The last wave owns slot zero when it completes.
        std::vector<double> observed(planes*cells);
        check(cudaMemcpy(observed.data(),arena.data,observed.size()*sizeof(double),cudaMemcpyDeviceToHost));
        for (int j=0;j<grid.GetTotalY();++j) for (int i=0;i<grid.stride_y;++i) {
            const int cell=grid.GetIndex(i,j,0);
            if (i<grid.GetTotalX()) {
                for (int axis=0;axis<3;++axis)
                    require(std::abs(observed[(3+native_species+axis)*cells+cell]-velocity[2][axis*cells+cell])<=2.e-13,
                        "Native physical scratch differs from analytic/shared closure center velocity");
                if (thermo_on) for (int f=0;f<3;++f)
                    require(std::abs(observed[f*cells+cell]-thermo[2][f*cells+cell])<=2.e-13,
                        "Native thermodynamic scratch differs from shared Host closure");
            } else {
                for (int f=0;f<3;++f) {
                    require(std::bit_cast<std::uint64_t>(observed[f*cells+cell])==std::bit_cast<std::uint64_t>(poison)
                        && std::bit_cast<std::uint64_t>(observed[(3+native_species+f)*cells+cell])==std::bit_cast<std::uint64_t>(poison),
                        "Native observer evaluated thermodynamics or physical velocity on pitch padding");
                }
                for (int species=0;species<native_species;++species)
                    require(std::bit_cast<std::uint64_t>(observed[3*cells+cell*native_species+species])
                        ==std::bit_cast<std::uint64_t>(poison),"Native observer consumed padding composition");
            }
        }
        std::vector<double> peer_scratch((planes-1)*cells);
        check(cudaMemcpy(peer_scratch.data(),arena.data+planes*cells,peer_scratch.size()*sizeof(double),cudaMemcpyDeviceToHost));
        for (double value:peer_scratch)
            require(std::bit_cast<std::uint64_t>(value)==std::bit_cast<std::uint64_t>(poison),
                "Native thermodynamic wave wrote into nonthermodynamic peer arena planes");
    }
    // A logical corner ghost is outside every final indicator stencil. Its
    // invalid energy still has to latch failure across the entire observer.
    const std::array<amr::indicator::Selection,count> choices{{{Field::VelocityZ},{Field::Density},{Field::VelocityZ}}};
    check(cudaMemcpy(selected.data,choices.data(),sizeof(choices),cudaMemcpyHostToDevice));
    auto broken=fields;
    broken[4*cells+grid.GetIndex(0,0,0)]=-1.;
    require(grid.Is()>=2 && grid.Js()>=2,"Native failure ghost unexpectedly belongs to final stencil");
    check(cudaMemcpy(state.data,broken.data(),broken.size()*sizeof(double),cudaMemcpyHostToDevice));
    check(arch::cuda::launch_cuda_refinement_indicators(blocks[0].state,blocks[0].grid,eos,blocks[0].workspace,nullptr));
    double scalar_failed=0.; int scalar_flag=0;
    check(cudaMemcpy(&scalar_failed,summaries.data,sizeof(double),cudaMemcpyDeviceToHost));
    check(cudaMemcpy(&scalar_flag,statuses.data,sizeof(int),cudaMemcpyDeviceToHost));
    require(std::isnan(scalar_failed) && scalar_flag==1,"Native scalar lost a logical-ghost failure outside the final stencil");
    for (int ordering=0;ordering<2;++ordering) {
        if (ordering) std::swap(blocks[0],blocks[1]);
        upload_bindings();
        const auto failed=arch::cuda::launch_cuda_refinement_indicators_batch(blocks,device_batch.data,capacity,eos,nullptr);
        check(failed.error);
        std::array<double,count> values{}; std::array<int,count> flags{};
        check(cudaMemcpy(values.data(),summaries.data,sizeof(values),cudaMemcpyDeviceToHost));
        check(cudaMemcpy(flags.data(),statuses.data,sizeof(flags),cudaMemcpyDeviceToHost));
        require(std::isnan(values[0]) && flags[0]==1 && std::isfinite(values[1]) && flags[1]==0
            && std::isfinite(values[2]) && flags[2]==0,"Native logical-ghost failure was lost or crossed a private wave slot");
    }
    std::swap(blocks[0],blocks[1]);
    check(cudaMemcpy(state.data,fields.data(),fields.size()*sizeof(double),cudaMemcpyHostToDevice));
    check(arch::cuda::launch_cuda_refinement_indicators(blocks[0].state,blocks[0].grid,eos,blocks[0].workspace,nullptr));
    double scalar_recovered=0.;
    check(cudaMemcpy(&scalar_recovered,summaries.data,sizeof(double),cudaMemcpyDeviceToHost));
    check(cudaMemcpy(&scalar_flag,statuses.data,sizeof(int),cudaMemcpyDeviceToHost));
    require(scalar_flag==0 && std::isfinite(scalar_recovered)
        && std::abs(scalar_recovered-maximum(0,choices[0]))<=2.e-13,"Native scalar failed recovery on unchanged borrowed workspace");
    upload_bindings();
    check(arch::cuda::launch_cuda_refinement_indicators_batch(blocks,device_batch.data,capacity,eos,nullptr).error);
    std::array<int,count> recovered{};
    check(cudaMemcpy(recovered.data(),statuses.data,sizeof(recovered),cudaMemcpyDeviceToHost));
    require(recovered==std::array<int,count>{0,0,0},"Native observer did not recover after reused-wave failure");
    // Every late malformed binding must reject before the reset kernel and
    // retain original caller-owned sentinels in all scratch/result/latch slots.
    for (int bad=0;bad<10;++bad) {
        auto rejected=blocks;
        auto& w=rejected.back().workspace;
        if (bad==0) w.thermodynamics=nullptr;
        if (bad==1) w.composition=nullptr;
        if (bad>=2 && bad<=4) w.physical_velocity[bad-2]=nullptr;
        if (bad==5) w.eos_status=nullptr;
        if (bad==6) w.bounds.internal_min=-1.;
        if (bad==7) w.bounds.internal_max=w.bounds.internal_min/2.;
        if (bad==8) w.bounds.density=2.*w.density_floor;
        if (bad==9) rejected.back().grid.ng=0;
        std::vector<double> untouched(capacity*planes*cells,42.5);
        const std::array<double,count> sentinel{73.25,74.25,75.25};
        check(cudaMemcpy(arena.data,untouched.data(),untouched.size()*sizeof(double),cudaMemcpyHostToDevice));
        check(cudaMemcpy(summaries.data,sentinel.data(),sizeof(sentinel),cudaMemcpyHostToDevice));
        check(cudaMemset(statuses.data,0x7f,count*sizeof(int)));
        check(cudaMemcpy(device_batch.data,rejected.data(),sizeof(rejected),cudaMemcpyHostToDevice));
        const auto result=arch::cuda::launch_cuda_refinement_indicators_batch(rejected,device_batch.data,capacity,eos,nullptr);
        require(result.error==cudaErrorInvalidValue && result.kernels_launched==0,
            "Native invalid workspace/bounds partially launched a wave");
        require(arch::cuda::launch_cuda_refinement_indicators(rejected.back().state,rejected.back().grid,
            eos,w,nullptr)==cudaErrorInvalidValue,"Native scalar accepted invalid workspace/bounds");
        std::vector<double> scratch(untouched.size()); std::array<double,count> values{}; std::array<int,count> flags{};
        check(cudaMemcpy(scratch.data(),arena.data,scratch.size()*sizeof(double),cudaMemcpyDeviceToHost));
        check(cudaMemcpy(values.data(),summaries.data,sizeof(values),cudaMemcpyDeviceToHost));
        check(cudaMemcpy(flags.data(),statuses.data,sizeof(flags),cudaMemcpyDeviceToHost));
        require(scratch==untouched && values==sentinel && flags==std::array<int,count>{0x7f7f7f7f,0x7f7f7f7f,0x7f7f7f7f},
            "Native cold rejection changed original scratch/result/status sentinels");
    }
    upload_bindings();
    const auto resumed=arch::cuda::launch_cuda_refinement_indicators_batch(blocks,device_batch.data,capacity,eos,nullptr);
    check(resumed.error);
    require(resumed.kernels_launched==8,"Native valid reuse after cold rejection lost a wave");
    std::array<double,count> resumed_values{};
    check(cudaMemcpy(resumed_values.data(),summaries.data,sizeof(resumed_values),cudaMemcpyDeviceToHost));
    check(cudaMemcpy(recovered.data(),statuses.data,sizeof(recovered),cudaMemcpyDeviceToHost));
    for (int b=0;b<count;++b)
        require(recovered[b]==0 && std::isfinite(resumed_values[b])
            && std::abs(resumed_values[b]-maximum(b,choices[b]))<=2.e-13,
            "Native valid reuse after cold rejection retained stale scratch or failure");
    std::vector<double> unchanged(fields.size());
    check(cudaMemcpy(unchanged.data(),state.data,unchanged.size()*sizeof(double),cudaMemcpyDeviceToHost));
    for (std::size_t n=0;n<fields.size();++n)
        require(std::bit_cast<std::uint64_t>(unchanged[n])==std::bit_cast<std::uint64_t>(fields[n]),
            "Native observer changed conserved/species/padding bits");
}

void test_native_indicator_scratch_budget() {
    constexpr std::size_t cells=32768,blocks=1024;
    constexpr int species=2;
    const auto physical=arch::cuda::indicator_wave_capacity(cells,species,false,blocks,true);
    const auto both=arch::cuda::indicator_wave_capacity(cells,species,true,blocks,true);
    constexpr std::size_t expected=(64*1024*1024)/(cells*(1+3+species+3)*sizeof(double));
    require(physical==expected && both==expected,"Native physical planes escaped the original 64MiB wave arena budget");
}

void test_tabular_temperature_failures() {
    Tabular3DEOSView eos3{};
    eos3.n_rho = eos3.n_T = eos3.n_X = 2;
    eos3.log_rho_max = eos3.dlog_rho = 1.0;
    eos3.log_T_max = eos3.dlog_T = .2;
    eos3.X_max = eos3.dX = 1.0;
    eos3.target_species_id = -1;
    test_recovered_tabular_temperature(eos3, 2);
    Tabular4DEOSView eos4{};
    eos4.n_rho = eos4.n_T = eos4.n_A = eos4.n_Z = 2;
    eos4.log_rho_max = eos4.dlog_rho = 1.0;
    eos4.log_T_max = eos4.dlog_T = .2;
    eos4.A_min = 14.0; eos4.A_max = 15.0; eos4.dA = 1.0;
    eos4.Z_min = 7.0; eos4.Z_max = 8.0; eos4.dZ = 1.0;
    test_recovered_tabular_temperature(eos4, 4);
}
}

int main() {
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) return 77;
    if (probe != cudaSuccess) {
        std::cerr << cudaGetErrorString(probe) << '\n';
        return 1;
    }
    try {
        // Actual Grid construction rejects the retired chart before GPU work.
        Grid retired_grid(2, 1.0, 2.0, 0.4, 1.1, 0.2, 0.8);
        retired_grid.dim = 2;
        retired_grid.geometry = "cylindrical";
        bool retired_polar_rejected = false;
        try { retired_grid.InitializeTopology(); }
        catch (const std::invalid_argument&) { retired_polar_rejected = true; }
        require(retired_polar_rejected, "retired cylindrical 2D polar indicator fixture accepted");
        verify_cuda_jeans_resolution();
        require(std::abs(amr::indicator::loehner_error(1.0, 2.0, 4.0) - 1.0 / 3.09) < 1.e-15,
            "frozen Lohner stencil changed");
        require(amr::indicator::refinement_flag(0.8, 1, 0, 2, 0.8, 0.2) == 0,
            "refinement equality boundary changed");
        for (int dimension = 1; dimension <= 3; ++dimension)
            for (const auto* geometry : {"cartesian", "cylindrical", "spherical"}) {
                if (std::string(geometry) == "cylindrical" && dimension == 2) continue;
                run(dimension, geometry);
            }
        test_tabular_temperature_failures();
        test_native_indicator_scratch_budget();
        for (double lower : {0.,1.})
            for (bool curved : {false,true})
                for (bool thermo_on : {false,true})
                    test_native_observer_consumers(lower,curved,thermo_on);
        for (std::size_t count : {1,3,1024,1025}) test_batch_wave(count);
        require(arch::cuda::indicator_wave_capacity(1024,200,true,1024)<1024,
            "thermodynamic indicator scratch cap was ignored");
        require(arch::cuda::indicator_wave_capacity(1<<24,0,false,1024)==1,
            "required oversized single-block scratch was multiplied");
        for (const auto invalid : {std::array<std::size_t,2>{0,1}, {1,0}}) {
            bool rejected=false;
            try { static_cast<void>(arch::cuda::indicator_wave_capacity(invalid[0],0,false,invalid[1])); }
            catch (const std::invalid_argument&) { rejected=true; }
            require(rejected,"empty indicator scratch extent was accepted");
        }
        bool overflow_rejected=false;
        try {
            static_cast<void>(arch::cuda::indicator_wave_capacity(
                std::numeric_limits<std::size_t>::max(),200,true,1024));
        } catch (const std::overflow_error&) { overflow_rejected=true; }
        require(overflow_rejected,"indicator scratch allocation overflow was accepted");
        std::cout << "CUDA_REFINEMENT_INDICATORS_PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
