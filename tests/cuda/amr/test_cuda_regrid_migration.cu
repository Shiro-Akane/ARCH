/**
 * @file test_cuda_regrid_migration.cu
 * @brief Compare production GPU regrid transfers with CPU results.
 *
 * Ordinary coordinate/dimension cases retain their existing windows. Native
 * RZ uses true bound parent/child geometry, the accepted Host transfer owner,
 * independent constant-density ring integrals and whole-domain V/W budgets.
 * These provisional transfers do not qualify completed target EOS/Runtime.
 */
#include "cuda/amr/RegridMigration.h"
#include "fixtures/amr/regrid_migration_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void check(cudaError_t result)
{
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class T> struct Buffer {
    T* pointer = nullptr;
    explicit Buffer(std::size_t count) { if (count) check(cudaMalloc(&pointer, count * sizeof(T))); }
    ~Buffer() { static_cast<void>(cudaFree(pointer)); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

struct DeviceBlock {
    Buffer<double> state;
    Buffer<double> volume;
    arch::cuda::DeviceRegridBlock binding{};
    std::vector<double> packed;
    explicit DeviceBlock(const amr::Block& host)
        : state(static_cast<std::size_t>(host.grid.GetTotalSize())
                * (6 + host.fluid_state.GetNumSpecies())),
          volume(host.grid.GetTotalSize())
    {
        const int cells = host.grid.GetTotalSize();
        const int species = host.fluid_state.GetNumSpecies();
        packed.resize(static_cast<std::size_t>(cells) * (6 + species));
        const auto fields = amr::test::regrid_fields(host.fluid_state);
        for (int field = 0; field < 6; ++field)
            std::copy_n(fields[field], cells, packed.data() + field * cells);
        std::copy(host.fluid_state.mass_fractions.begin(), host.fluid_state.mass_fractions.end(),
                  packed.data() + 6 * cells);
        check(cudaMemcpy(state.pointer, packed.data(), packed.size() * sizeof(double), cudaMemcpyHostToDevice));
        binding.state = {state.pointer, state.pointer + cells, state.pointer + 2 * cells,
                         state.pointer + 3 * cells, state.pointer + 4 * cells,
                         state.pointer + 5 * cells, state.pointer + 6 * cells, cells, species};
        binding.grid = arch::cuda::make_device_grid_view(host.grid);
        std::vector<double> volumes(cells, 0.0);
        for (int k = host.grid.Ks(); k < host.grid.Ke(); ++k)
            for (int j = host.grid.Js(); j < host.grid.Je(); ++j)
                for (int i = host.grid.Is(); i < host.grid.Ie(); ++i)
                    volumes[host.grid.GetIndex(i, j, k)] = GridMetrics::CellVolume(
                        GridMetrics::make_geometry_view(host.grid,
                            host.grid.dyadic_identity.bound
                                ? GridMetrics::GeometrySemantics::AxisymmetricRz
                                : GridMetrics::GeometrySemantics::Existing), i, j, k);
        check(cudaMemcpy(volume.pointer, volumes.data(), volumes.size() * sizeof(double), cudaMemcpyHostToDevice));
        binding.grid.cell_volume = volume.pointer;
    }
    void compare(const amr::Block& expected, bool native = false)
    {
        check(cudaMemcpy(packed.data(), state.pointer, packed.size() * sizeof(double), cudaMemcpyDeviceToHost));
        const int cells = expected.grid.GetTotalSize();
        const auto fields = amr::test::regrid_fields(expected.fluid_state);
        for (int cell = 0; cell < cells; ++cell) {
            for (int field = 0; field < 6 + expected.fluid_state.GetNumSpecies(); ++field) {
                const double actual = packed[field * cells + cell];
                const double reference = field < 6 ? fields[field][cell]
                    : expected.fluid_state.X(field - 6, cell);
                const double allowance = native ? 3.0e-13 * std::abs(reference)
                    : 3.0e-14 * std::max(1.0, std::abs(reference));
                require(std::isfinite(actual)
                    && std::abs(actual - reference) <= allowance,
                    "device regrid migration differs from Host Block authority");
                if (field >= 6) require(actual >= 0.0, "device migration produced negative species");
            }
        }
    }
};

/** Build the actual computational RZ chart, never the retired (r,phi) fixture. */
amr::Block native_block(int level, int child, int species)
{
    Grid root(amr::MAX_NG, 1.0, 2.0, 0.4, 1.0, 0.0, 1.0, 1, 1, 0);
    root.dim = 2;
    root.geometry = "cylindrical";
    root.InitializeTopology(GridMetrics::GeometrySemantics::AxisymmetricRz);
    amr::Block block{};
    block.level = level;
    block.logical_x1 = level ? child & 1 : 0;
    block.logical_x2 = level ? (child >> 1) & 1 : 0;
    block.InitGeometry(root, root.dx1, root.dx2, root.dx3,
                       GridMetrics::GeometrySemantics::AxisymmetricRz);
    block.fluid_state.Preallocate(block.grid.GetTotalSize());
    block.fluid_state.InitSpecies(species);
    return block;
}

/** Independent constant-rho ring means; long-double integration uses input
 * faces, not shared reconstruction/inertia helpers. pi and dz cancel here.
 * I/V=rho*(b^4-a^4)/(2*(b^2-a^2)); I/W=3*rho*(b^4-a^4)/(4*(b^3-a^3)).
 */
FluidVector ring_state(const amr::Block& block, int i, int j,
                       double density, bool nonuniform)
{
    const auto geometry = GridMetrics::make_geometry_view(
        block.grid, GridMetrics::GeometrySemantics::AxisymmetricRz);
    const long double a = geometry.GetFacePosL(i), b = geometry.GetFacePosR(i);
    const long double x = (a + b) / 2 - 1;
    const long double y = geometry.GetCellCenterY(j) - 0.4;
    const long double rho = density * (nonuniform ? 1 + 0.04L*x + 0.02L*y : 1);
    const long double ur = 0.3L + (nonuniform ? 0.02L*x : 0);
    const long double uz = -0.2L + (nonuniform ? 0.01L*y : 0);
    const long double omega = 0.5L + (nonuniform ? -0.01L*x : 0);
    const long double internal = 0.5L + (nonuniform ? 0.02L*y : 0);
    const long double r2 = (b*b*b*b-a*a*a*a)/(2*(b*b-a*a));
    const long double inertia_over_W = 3*rho*(b*b*b*b-a*a*a*a)
        /(4*(b*b*b-a*a*a));
    return {static_cast<double>(rho), static_cast<double>(rho*ur),
            static_cast<double>(rho*uz), static_cast<double>(omega*inertia_over_W),
            static_cast<double>(rho*(internal + (ur*ur+uz*uz)/2 + omega*omega*r2/2))};
}

/** Seed logical source/ghost cells; storage padding retains its initial bits. */
void seed_native(amr::Block& block, double density, bool nonuniform)
{
    auto& state = block.fluid_state;
    for (int j=0; j<block.grid.GetTotalY(); ++j)
        for (int i=0; i<block.grid.GetTotalX(); ++i) {
            const int cell = block.grid.GetIndex(i,j,0);
            state.set(cell, ring_state(block,i,j,density,nonuniform));
            state.enuc_rate[cell] = -1.0;
            const int species = state.GetNumSpecies();
            if (species == 4) {
                state.X(0,cell)=0.25; state.X(1,cell)=0.75-1.0e-13;
                state.X(2,cell)=1.0e-13; state.X(3,cell)=0.0;
            } else if (species > 0) {
                for (int sp=0; sp<species; ++sp)
                    state.X(sp,cell)=1.0/species;
            }
        }
}

/** Independent whole-domain integrals: V for four fluid means, W for q. */
std::array<long double,5> native_integrals(const amr::Block& block,
                                         const std::vector<double>* packed = nullptr)
{
    std::array<long double,5> result{};
    const auto geometry = GridMetrics::make_geometry_view(
        block.grid,GridMetrics::GeometrySemantics::AxisymmetricRz);
    const auto fields = amr::test::regrid_fields(block.fluid_state);
    for(int j=block.grid.Js(); j<block.grid.Je(); ++j)
        for(int i=block.grid.Is(); i<block.grid.Ie(); ++i) {
            const long double a=geometry.GetFacePosL(i),b=geometry.GetFacePosR(i);
            const long double dz=geometry.GetAxialFacePosR(j)-geometry.GetAxialFacePosL(j);
            // Common pi is omitted from every integral; no ratio normalization.
            const long double volume=(b*b-a*a)*dz;
            const long double angular=2*(b*b*b-a*a*a)*dz/3;
            const int cell=block.grid.GetIndex(i,j,0);
            for(int field=0; field<5; ++field)
                result[field] += (field==3 ? angular : volume)
                    * (packed ? (*packed)[field*block.grid.GetTotalSize()+cell]
                              : fields[field][cell]);
        }
    return result;
}

/** Device positive/negative identity cases retain every strict shared field. */
__global__ void native_identity_checks(GridMetrics::DyadicGridIdentity identity,
                                      int* result)
{
    if(blockIdx.x || threadIdx.x) return;
    auto same=identity;
    ++same.level;
    ++same.logical[0];
    same.level=identity.level;
    same.logical=identity.logical;
    result[0]=GridMetrics::equal_identity(identity,same);
    int index=1;
    for(int axis=0;axis<2;++axis) {
        auto altered=identity;
        ++altered.root_blocks[axis];
        result[index++]=!GridMetrics::equal_identity(identity,altered);
        altered=identity;
        ++altered.logical[axis];
        result[index++]=!GridMetrics::equal_identity(identity,altered);
        altered=identity;
        altered.root_lower[axis]=nextafter(altered.root_lower[axis],
                                         altered.root_upper[axis]);
        result[index++]=!GridMetrics::equal_identity(identity,altered);
        altered=identity;
        altered.root_upper[axis]=nextafter(altered.root_upper[axis],
                                         altered.root_lower[axis]);
        result[index++]=!GridMetrics::equal_identity(identity,altered);
    }
    auto altered=identity;
    ++altered.level;
    result[index++]=!GridMetrics::equal_identity(identity,altered);
    altered=identity;altered.periodic_axial=!altered.periodic_axial;
    result[index++]=!GridMetrics::equal_identity(identity,altered);
    altered=identity;altered.bound=!altered.bound;
    result[index++]=!GridMetrics::equal_identity(identity,altered);
}

/** Exercise real provisional native transfers, identities and sticky failure. */
void run_native(int species, double density, bool nonuniform)
{
    constexpr auto native = GridMetrics::GeometrySemantics::AxisymmetricRz;
    const arch::state::Bounds bounds{1.0e-30,1.0e-10,1.0e22};
    auto parent = native_block(0,0,species);
    seed_native(parent,density,nonuniform);
    DeviceBlock source(parent);
    Buffer<int> identity_result(12);
    native_identity_checks<<<1,1>>>(source.binding.grid.dyadic_identity,identity_result.pointer);
    check(cudaGetLastError());
    std::array<int,12> identity_host{};
    check(cudaMemcpy(identity_host.data(),identity_result.pointer,
        identity_host.size()*sizeof(int),cudaMemcpyDeviceToHost));
    for(int result:identity_host)
        require(result==1,"native exact identity accepted/rejected the wrong field");
    const auto source_bits = source.packed;
    std::vector<amr::Block> children;
    std::vector<std::unique_ptr<DeviceBlock>> devices;
    children.reserve(4);
    arch::cuda::DeviceRegridChildren bindings{};
    const auto width = static_cast<std::size_t>(source.binding.grid.active_cell_count())
        * amr::regrid_math::prolongation_workspace_per_species * species;
    Buffer<double> workspace(width);
    Buffer<int> status(1);
    check(cudaMemset(status.pointer,0,sizeof(int)));
    for(int child=0; child<4; ++child) {
        children.push_back(native_block(1,child,species));
        devices.push_back(std::make_unique<DeviceBlock>(children.back()));
        children.back().InterpolateFromCoarse(parent,child,2,bounds.density,
            bounds.internal_min,native,bounds.internal_max);
        bindings.blocks[child]=devices.back()->binding;
        check(arch::cuda::launch_cuda_regrid_prolongation(source.binding,
            bindings.blocks[child],child,bounds,workspace.pointer,width,status.pointer,nullptr));
    }
    check(cudaDeviceSynchronize());
    int outcome=-1;
    check(cudaMemcpy(&outcome,status.pointer,sizeof(int),cudaMemcpyDeviceToHost));
    if(outcome!=0)
        throw std::runtime_error(std::string("native prolongation failed: ")
            + amr::regrid_math::status_message(static_cast<amr::regrid_math::Status>(outcome)));
    source.compare(parent,true);
    require(std::memcmp(source.packed.data(),source_bits.data(),
        source_bits.size()*sizeof(double))==0,"native accepted source bits changed");
    std::array<long double,5> fine_integral{};
    for(int child=0; child<4; ++child) {
        devices[child]->compare(children[child],true);
        // The constant-rho solid rotation has nonuniform q=Omega*I/W.
        // Check actual device interiors against independent analytic ring means.
        if(!nonuniform) {
            const auto& block=children[child];
            for(int j=block.grid.Js();j<block.grid.Je();++j)
                for(int i=block.grid.Is();i<block.grid.Ie();++i) {
                    auto exact=ring_state(block,i,j,density,false);
                    const double values[]{exact.rho,exact.mom_u,exact.mom_v,exact.mom_w,exact.eng};
                    const int cell=block.grid.GetIndex(i,j,0);
                    for(int field=0;field<5;++field)
                        require(std::abs(devices[child]->packed[field*block.grid.GetTotalSize()+cell]
                            -values[field])<=3.0e-13*std::abs(values[field]),
                            "native device prolongation violates independent ring means");
                }
        }
        const auto sum=native_integrals(children[child],&devices[child]->packed);
        for(int field=0;field<5;++field) fine_integral[field]+=sum[field];
    }
    const auto parent_integral=native_integrals(parent);
    for(int field=0;field<5;++field)
        require(std::abs(fine_integral[field]-parent_integral[field])
            <=3.0e-13L*std::abs(parent_integral[field]),"native V/W family conservation failed");
    auto coarse=native_block(0,0,species);
    DeviceBlock destination(coarse);
    const amr::Block* pointers[]{&children[0],&children[1],&children[2],&children[3]};
    coarse.AverageToCoarse(pointers,2,bounds.density,bounds.internal_min,native);
    check(arch::cuda::launch_cuda_regrid_restriction(bindings,destination.binding,
        bounds,workspace.pointer,width,status.pointer,nullptr));
    check(cudaDeviceSynchronize());
    check(cudaMemcpy(&outcome,status.pointer,sizeof(int),cudaMemcpyDeviceToHost));
    require(outcome==0,"native restriction failed");
    destination.compare(coarse,true);
    const auto coarse_integral=native_integrals(coarse,&destination.packed);
    for(int field=0;field<5;++field)
        require(std::abs(coarse_integral[field]-parent_integral[field])
            <=3.0e-13L*std::abs(parent_integral[field]),"native restricted V/W conservation failed");

    auto invalid=source.binding;
    invalid.grid.semantics=GridMetrics::GeometrySemantics::Existing;
    require(arch::cuda::launch_cuda_regrid_prolongation(invalid,bindings.blocks[0],0,
        bounds,workspace.pointer,width,status.pointer,nullptr)==cudaErrorInvalidValue,
        "native chart relabelling was accepted");
    require(arch::cuda::launch_cuda_regrid_prolongation(source.binding,bindings.blocks[1],0,
        bounds,workspace.pointer,width,status.pointer,nullptr)==cudaErrorInvalidValue,
        "wrong native parent/child identity was accepted");
    auto wrong_children=bindings;
    std::swap(wrong_children.blocks[0],wrong_children.blocks[1]);
    require(arch::cuda::validate_cuda_regrid_restriction(wrong_children,destination.binding,
        bounds,workspace.pointer,width,status.pointer)==cudaErrorInvalidValue,
        "wrong native restriction child order was accepted");
    if(species>0)
        require(arch::cuda::launch_cuda_regrid_prolongation(source.binding,bindings.blocks[0],0,
            bounds,workspace.pointer,0,status.pointer,nullptr)==cudaErrorInvalidValue,
            "insufficient native scratch was accepted");
    auto bad=parent;
    std::fill(bad.fluid_state.rho.begin(),bad.fluid_state.rho.end(),-1.0);
    DeviceBlock bad_source(bad);
    check(cudaMemset(status.pointer,0,sizeof(int)));
    check(arch::cuda::launch_cuda_regrid_prolongation(bad_source.binding,bindings.blocks[0],0,
        bounds,workspace.pointer,width,status.pointer,nullptr));
    check(arch::cuda::launch_cuda_regrid_prolongation(source.binding,bindings.blocks[1],1,
        bounds,workspace.pointer,width,status.pointer,nullptr));
    check(cudaDeviceSynchronize());
    check(cudaMemcpy(&outcome,status.pointer,sizeof(int),cudaMemcpyDeviceToHost));
    require(outcome==static_cast<int>(amr::regrid_math::Status::ParentFluid),
        "native first failure did not survive a later successful family");
    bad_source.compare(bad,true);
    for(int child=0;child<4;++child) devices[child]->compare(children[child],true);
    destination.compare(coarse,true);
    std::cout<<"CUDA_NATIVE_REGRID_MIGRATION_PASS species="<<species
             <<" rho="<<density<<" nonuniform="<<nonuniform<<'\n';
}

void run(int dimension, int species, const std::string& geometry)
{
    auto parent = amr::test::regrid_block(dimension, 0, 0, species, geometry);
    amr::test::seed_regrid_parent(parent);
    DeviceBlock source(parent);
    const int count = 1 << dimension;
    std::vector<amr::Block> children;
    std::vector<std::unique_ptr<DeviceBlock>> device_children;
    children.reserve(count);
    arch::cuda::DeviceRegridChildren bindings{};
    const auto max_work = static_cast<std::size_t>(source.binding.grid.active_cell_count())
        * amr::regrid_math::prolongation_workspace_per_species * species;
    Buffer<double> workspace(max_work);
    Buffer<int> status(1);
    check(cudaMemset(status.pointer, 0, sizeof(int)));
    for (int child = 0; child < count; ++child) {
        children.push_back(amr::test::regrid_block(dimension, 1, child, species, geometry));
        device_children.push_back(std::make_unique<DeviceBlock>(children.back()));
        children.back().InterpolateFromCoarse(parent, child, dimension, 1.0e-10, 1.0e-10);
        bindings.blocks[child] = device_children.back()->binding;
        check(arch::cuda::launch_cuda_regrid_prolongation(source.binding,
            bindings.blocks[child], child, {1.0e-10, 1.0e-10, 1.0e22},
            workspace.pointer, max_work, status.pointer, nullptr));
    }
    check(cudaDeviceSynchronize());
    int result = -1;
    check(cudaMemcpy(&result, status.pointer, sizeof(int), cudaMemcpyDeviceToHost));
    require(result == 0, "device prolongation rejected valid family");
    source.compare(parent); // Old active sources must remain untouched.
    for (int child = 0; child < count; ++child) device_children[child]->compare(children[child]);
    auto coarse = amr::test::regrid_block(dimension, 0, 0, species, geometry);
    DeviceBlock destination(coarse);
    const amr::Block* pointers[8]{};
    for (int child = 0; child < count; ++child) pointers[child] = &children[child];
    coarse.AverageToCoarse(pointers, dimension, 1.0e-10, 1.0e-10);
    check(arch::cuda::launch_cuda_regrid_restriction(bindings, destination.binding,
        {1.0e-10, 1.0e-10, 1.0e22}, workspace.pointer, max_work, status.pointer, nullptr));
    check(cudaDeviceSynchronize());
    check(cudaMemcpy(&result, status.pointer, sizeof(int), cudaMemcpyDeviceToHost));
    require(result == 0, "device restriction rejected valid family");
    destination.compare(coarse);
    for (int child = 0; child < count; ++child) device_children[child]->compare(children[child]);

    // Malformed public device views must fail before launching/indexing.
    auto malformed = source.binding;
    malformed.grid.stride_y = malformed.grid.total_x - 1;
    require(arch::cuda::launch_cuda_regrid_prolongation(malformed,
        bindings.blocks[0], 0, {1.0e-10, 1.0e-10, 1.0e22},
        workspace.pointer, max_work, status.pointer, nullptr) == cudaErrorInvalidValue,
        "invalid row stride was accepted by migration launcher");
    malformed = destination.binding;
    malformed.grid.stride_z = 0;
    require(arch::cuda::launch_cuda_regrid_restriction(bindings, malformed,
        {1.0e-10, 1.0e-10, 1.0e22}, workspace.pointer, max_work, status.pointer, nullptr)
        == cudaErrorInvalidValue, "invalid plane stride was accepted by migration launcher");
    destination.compare(coarse);

    // Invalid source density is reported without mutating the active source.
    auto bad = parent;
    std::fill(bad.fluid_state.rho.begin(), bad.fluid_state.rho.end(), -1.0);
    DeviceBlock bad_source(bad);
    check(cudaMemset(status.pointer, 0, sizeof(int)));
    check(arch::cuda::launch_cuda_regrid_prolongation(bad_source.binding,
        bindings.blocks[0], 0, {1.0e-10, 1.0e-10, 1.0e22},
        workspace.pointer, max_work, status.pointer, nullptr));
    check(cudaDeviceSynchronize());
    check(cudaMemcpy(&result, status.pointer, sizeof(int), cudaMemcpyDeviceToHost));
    require(result == static_cast<int>(amr::regrid_math::Status::ParentFluid),
            "device migration failed to propagate shared fluid guard");
    bad_source.compare(bad);
    device_children[0]->compare(children[0]);
    std::cout << "CUDA_REGRID_MIGRATION_PASS dim=" << dimension
              << " species=" << species << " geometry=" << geometry << '\n';
}

} // namespace

int main()
{
    int count = 0;
    const auto probe = cudaGetDeviceCount(&count);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && count == 0)) return 77;
    try {
        check(probe);
        for (const char* geometry : {"cartesian", "cylindrical", "spherical"})
            for (int dimension = 1; dimension <= 3; ++dimension)
                if (!(dimension==2 && std::string(geometry)=="cylindrical"))
                    run(dimension, 4, geometry);
        run(1, 0, "cartesian");
        run(2, 21, "cartesian");
        run(2, 41, "cartesian");
        run_native(4,2.0,false);
        run_native(4,2.0,true);
        run_native(4,1.0e-20,false);
        run_native(0,2.0,false);
        run_native(41,2.0,false);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
