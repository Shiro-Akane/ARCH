/**
 * @file test_diffusion_rkl_tabular.cu
 * @brief Tabular-EOS latch coverage split out of test_diffusion_rkl_parity.cu.
 *
 * Build-cost split only: this TU owns the tabular host/device EOS failure latch
 * and its EOS fixtures. The shared helpers, the single failure counter and
 * main() stay in the core TU; nothing here adds a new executable, CTest entry,
 * library or any change to the diffusion/RKL mathematics.
 */
#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "DiffusionRklTestSupport.cuh"
#include "cuda/diffusion/DiffusionBatchKernels.cuh"
#include "cuda/diffusion/DiffusionKernels.cuh"
#include "fixtures/eos/FreeEnergyFixture.h"
#include "numerics/diffusion/DiffFlux.h"
#include "physics/eos/tabular/Tabular3DEOS.h"
#include "physics/eos/tabular/Tabular4DEOS.h"

namespace diffusion_rkl_test {

template<class Eos>
void verify_tabular_diffusion_latch(Eos eos, int compositions)
{
    DeviceFixture fixture;
    DeviceFixture second;
    DeviceStateOwner first_stage(fixture.grid.GetTotalSize(), 2);
    DeviceStateOwner second_stage(second.grid.GetTotalSize(), 2);
    for (int cell = 0; cell < fixture.grid.GetTotalSize(); ++cell) {
        fixture.host_state.rho[cell] = 1.0;
        fixture.host_state.mom_u[cell] = fixture.host_state.mom_v[cell]
            = fixture.host_state.mom_w[cell] = 0.0;
        fixture.host_state.eng[cell] = 100.0;
    }
    fixture.state.upload(fixture.host_state);
    second.state.upload(fixture.host_state);
    first_stage.upload(fixture.host_state);
    second_stage.upload(fixture.host_state);
    std::array<arch::cuda::DeviceDiffusionBatchBlock, 2> bindings{};
    for (int i = 0; i < 2; ++i) {
        auto& f = i == 0 ? fixture : second;
        auto& b = bindings[i];
        b.input = b.state_n = b.previous = b.older = f.state.view;
        b.output = i == 0 ? first_stage.view : second_stage.view;
        b.delta = b.initial_delta = f.output.view;
        b.grid = f.device_grid;
        b.workspace = {f.flux.view, f.candidates.pointer, f.result.pointer, f.status.pointer, {}};
    }
    DeviceArray<arch::cuda::DeviceDiffusionBatchBlock> device_bindings(2);
    device_bindings.upload(bindings.data(), bindings.size());
    const auto batch_plan = arch::scheduler::make_rkl_plan(arch::scheduler::RklMethod::RKL1, 1);
    const int extent = 4 * compositions;
    std::vector<double> table(tabular_eos::FieldCount * extent);
    DeviceArray<double> device_table(table.size());

    const auto config = DiffFlux::make_diffusion_config_view(make_config(true, false, false));
    double previous_dt = 0.0;
    for (const bool invalid : {true, false, false}) {
        // Exact Hermite polynomial F=100+2r+r*t-1.5t^2, r=ln(rho), t=ln(T).
        // At T=1: e=100+r, Cv=3-r, with a unique root in the source domain.
        for (int ir=0; ir<2; ++ir)
            for (int it=0; it<2; ++it) {
                const double r=ir*std::log(10.0), t=it*.2*std::log(10.0);
                const double jets[]{100.+2*r+r*t-1.5*t*t, 2.+t, r-3*t,
                                    0.,1.,-3.,0.,0.,0.};
                for (int field=0; field<tabular_eos::FieldCount; ++field)
                    std::fill_n(table.data()+field*extent+(ir*2+it)*compositions,
                                compositions, jets[field]);
                // Cv=(Fy-Fyy)/T: make only the lower-T endpoint invalid.
                if (invalid && it==0)
                    std::fill_n(table.data()+tabular_eos::Fyy*extent+ir*2*compositions,
                                compositions, r);
            }
        Eos host = eos;
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            host.free_energy_fields[field] = table.data() + field * extent;
        bool host_threw = false;
        try { static_cast<void>(host.get_temperature(1.0, 100.0, nullptr)); }
        catch (const std::runtime_error&) { host_threw = true; }
        if (host_threw != invalid) fail("Diffusion Tabular fixture missed the Host failure boundary");
        device_table.upload(table.data(), table.size());
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            eos.free_energy_fields[field] = device_table.pointer + field * extent;

        const auto operation = arch::cuda::launch_diffusion_operator(
            fixture.state.view, fixture.output.view, eos, fixture.eos.species,
            fixture.device_grid, config, fixture.workspace, nullptr);
        require_cuda(operation.error, "Tabular diffusion operator latch");
        require_cuda(cudaDeviceSynchronize(), "Tabular diffusion operator sync");
        int status = -1;
        fixture.status.download(&status, 1);
        if (status != (invalid ? 1 : 0))
            fail("Diffusion operator concealed an intermediate EOS failure or failed to reset status");
        const auto output = fixture.output.download();
        for (int i = fixture.grid.Is(); i < fixture.grid.Ie(); ++i)
            if (!std::isfinite(output.eng[fixture.grid.GetIndex(i, 0, 0)]))
                fail("Diffusion fixture did not retain finite operator output");

        const auto timestep = arch::cuda::launch_raw_diffusion_dt(
            fixture.state.view, eos, fixture.eos.species, fixture.device_grid,
            config, fixture.workspace, nullptr);
        require_cuda(timestep.error, "Tabular diffusion dt latch");
        require_cuda(cudaDeviceSynchronize(), "Tabular diffusion dt sync");
        fixture.status.download(&status, 1);
        double dt = 0.0;
        fixture.result.download(&dt, 1);
        if (status != (invalid ? 1 : 0) || !std::isfinite(dt))
            fail("Diffusion dt reduction concealed an intermediate EOS failure or failed to reset status");
        const auto batch_dt = arch::cuda::launch_diffusion_dt_batch(bindings, device_bindings.pointer,
            eos, fixture.eos.species, config, nullptr);
        require_cuda(batch_dt.error, "Batched Tabular diffusion dt latch");
        require_cuda(cudaDeviceSynchronize(), "Batched Tabular diffusion dt sync");
        for (auto* f : {&fixture, &second}) {
            f->status.download(&status, 1);
            if (status != (invalid ? 1 : 0)) fail("Batched diffusion dt concealed a block's EOS failure");
        }
        const auto batch_stage = arch::cuda::launch_diffusion_stage_batch(batch_plan, batch_plan.stages[0],
            bindings, device_bindings.pointer, eos, fixture.eos.species, config, 1e-6, nullptr);
        require_cuda(batch_stage.error, "Batched Tabular diffusion stage latch");
        require_cuda(cudaDeviceSynchronize(), "Batched Tabular diffusion stage sync");
        for (auto* f : {&fixture, &second}) {
            f->status.download(&status, 1);
            if (status != (invalid ? 1 : 0)) fail("Batched diffusion stage concealed a block's EOS failure");
        }
        // A late invalid binding must reject the WHOLE launch before clearing
        // even the first block's status, rather than partially updating a wave.
        auto invalid_bindings = bindings;
        invalid_bindings[1].output = invalid_bindings[1].state_n;
        require_cuda(cudaMemset(fixture.status.pointer, 0x7f, sizeof(int)), "poison batch status");
        const auto rejected = arch::cuda::launch_diffusion_stage_batch(batch_plan, batch_plan.stages[0],
            invalid_bindings, device_bindings.pointer, eos, fixture.eos.species, config, 1e-6, nullptr);
        fixture.status.download(&status, 1);
        if (rejected.error != cudaErrorInvalidValue || rejected.kernels_launched != 0 || status != 0x7f7f7f7f)
            fail("Batched diffusion late-alias preflight wrote an earlier block");
        if (!invalid) {
            // An invalid EOS payload is rejected by its status, not a physical
            // timestep reference. Check recovery against the uniform-capacity
            // analytic limit, then bitwise repeatability of two valid runs.
            expect_close("Tabular recovered uniform-capacity dt", dt,
                         fixture.grid.dx1*fixture.grid.dx1/(2.*config.alpha_therm));
            if (previous_dt > 0. && std::bit_cast<std::uint64_t>(dt)
                != std::bit_cast<std::uint64_t>(previous_dt))
                fail("Tabular valid timestep is not repeatable after recovery");
            previous_dt = dt;
        }
    }
    // Only rho=1's lower-T knot is invalid. At rho=10 the original Host
    // evaluator must remain valid, so one failed block cannot poison its peer.
    std::fill_n(table.data() + tabular_eos::Fyy * extent, compositions, 0.0);
    Eos mixed_host = eos;
    for (int field = 0; field < tabular_eos::FieldCount; ++field)
        mixed_host.free_energy_fields[field] = table.data() + field * extent;
    bool lower_failed = false;
    try { static_cast<void>(mixed_host.get_temperature(1.0, 100.0, nullptr)); }
    catch (const std::runtime_error&) { lower_failed = true; }
    if (!lower_failed || !std::isfinite(mixed_host.get_temperature(10.0, 100.0+std::log(10.0), nullptr)))
        fail("Mixed Tabular fixture did not isolate an invalid and valid density endpoint");
    auto valid_peer = fixture.host_state;
    for (int cell = 0; cell < fixture.grid.GetTotalSize(); ++cell) {
        valid_peer.rho[cell] = 10.0;
        valid_peer.eng[cell] = 10.0*(100.0+std::log(10.0));
    }
    second.state.upload(valid_peer);
    device_table.upload(table.data(), table.size());
    for (int ordering = 0; ordering < 2; ++ordering) {
        if (ordering) std::swap(bindings[0], bindings[1]);
        device_bindings.upload(bindings.data(), bindings.size());
        const auto dt_result = arch::cuda::launch_diffusion_dt_batch(bindings, device_bindings.pointer,
            eos, fixture.eos.species, config, nullptr);
        require_cuda(dt_result.error, "Mixed block diffusion dt");
        require_cuda(cudaDeviceSynchronize(), "Mixed block diffusion dt sync");
        int failed_status = -1, valid_status = -1;
        fixture.status.download(&failed_status, 1);
        second.status.download(&valid_status, 1);
        if (failed_status != 1 || valid_status != 0)
            fail("Diffusion dt failure crossed block status ownership");
        const auto stage_result = arch::cuda::launch_diffusion_stage_batch(batch_plan, batch_plan.stages[0],
            bindings, device_bindings.pointer, eos, fixture.eos.species, config, 1e-6, nullptr);
        require_cuda(stage_result.error, "Mixed block diffusion stage");
        require_cuda(cudaDeviceSynchronize(), "Mixed block diffusion stage sync");
        fixture.status.download(&failed_status, 1);
        second.status.download(&valid_status, 1);
        if (failed_status != 1 || valid_status != 0)
            fail("Diffusion stage failure crossed block status ownership");
    }
}

void verify_tabular_diffusion_failures()
{
    BasicTabular3DEOSView<arch::test::FixedTableComposition> eos3{};
    eos3.n_rho = eos3.n_T = eos3.n_X = 2;
    eos3.log_rho_max = eos3.dlog_rho = 1.0;
    eos3.log_T_max = eos3.dlog_T = .2;
    eos3.X_max = eos3.dX = 1.0;
    eos3.target_species_id = -1;
    verify_tabular_diffusion_latch(eos3, 2);
    BasicTabular4DEOSView<arch::test::FixedTableComposition> eos4{};
    eos4.n_rho = eos4.n_T = eos4.n_A = eos4.n_Z = 2;
    eos4.log_rho_max = eos4.dlog_rho = 1.0;
    eos4.log_T_max = eos4.dlog_T = .2;
    eos4.A_min = 14.0; eos4.A_max = 15.0; eos4.dA = 1.0;
    eos4.Z_min = 7.0; eos4.Z_max = 8.0; eos4.dZ = 1.0;
    verify_tabular_diffusion_latch(eos4, 4);
}

} // namespace diffusion_rkl_test
