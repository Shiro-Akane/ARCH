/**
 * @file DriverIO.cpp
 * @brief Schedule plots, checkpoints and diagnostics from the current published state.
 *
 * Workflow:
 * 1. Receive a resolved configuration, stage request and current state identity.
 * 2. Schedule plots, checkpoints and diagnostics from the current published state.
 * 3. Hand completed state and diagnostics to the next scheduled stage.
 */

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <vector>

#include "driver/io/DriverIO.h"

#include "amr/AMRControl.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/state/StateAdmissibility.h"
#include "physics/species/Species.h"

namespace arch::driver {
namespace {
/** Reject invalid conserved, composition or EOS state before serializing any plot. */
void validate_output_state(DriverRuntime& runtime, PressureFunc pressure,
                          TemperatureFunc temperature, Gamma1Func gamma1, const void* eos)
{
    const auto& limits=runtime.configuration().numerics;
    const int species=runtime.species().count();
    std::vector<double> fractions(species);
    auto& control=runtime.control();
    for (int id : control.tree->GetActiveBlocks()) {
        const auto& block=control.pool->GetBlock(id);
        const auto& grid=block.grid;
        const auto& state=block.fluid_state;
        for (int k=grid.Ks(); k<grid.Ke(); ++k)
            for (int j=grid.Js(); j<grid.Je(); ++j)
                for (int i=grid.Is(); i<grid.Ie(); ++i) {
                    const int cell=grid.GetIndex(i,j,k);
                    try {
                        state.get_species_to_buffer(cell,fractions.data());
                        const auto fluid=state.get(cell);
                        if (arch::state::validate(fluid,fractions.data(),species,1,
                            limits.sml_rho,limits.min_eint,limits.max_eint)!=arch::state::Status::valid)
                            throw std::runtime_error("Invalid conserved output state");
                        io::require_output_thermodynamics(fluid,fractions.data(),pressure,temperature,gamma1,eos);
                    } catch (const std::exception& error) {
                        throw std::runtime_error("Output block="+std::to_string(id)+" cell="+
                            std::to_string(cell)+": "+error.what());
                    }
                }
    }
}
}
/** Materialize the accepted state and write a validated plot with gravity fields. */
void DriverIO::write_plot(std::span<const io::PlotScalarField> extra_fields)
{
    const auto start = Clock::now();
    auto& amr_ctrl = runtime.control();
    const auto& config = runtime.configuration();
    const auto& specs = runtime.species();
    runtime.materialize_current_for_host();
    validate_output_state(runtime,p_func,t_func,gamma1_func,eos);
    write_plt(amr_ctrl, p_func, t_func, gamma1_func, eos, ctrl.plt_file_index++,
              ctrl.t_current, config, specs, extra_fields);
    output_seconds_ += std::chrono::duration<double>(Clock::now()-start).count();
    ++output_calls_;
}
/** Write restart state and provenance at a completed step boundary. */
void DriverIO::write_checkpoint(double dt_burn_global, bool resume_after_regrid)
{
    const auto start = Clock::now();
    auto& amr_ctrl = runtime.control();
    const auto& config = runtime.configuration();
    const auto& specs = runtime.species();
    if (runtime.backend())
        runtime.materialize_current_for_host();
    validate_output_state(runtime,p_func,t_func,gamma1_func,eos);
    write_chk(amr_ctrl, ctrl.chk_file_index++, ctrl.plt_file_index,
              ctrl.step_count, ctrl.t_current, ctrl.dt_old,
              dt_burn_global, resume_after_regrid, config, specs,
              checkpoint_provenance, ctrl.repairs);
    output_seconds_ += std::chrono::duration<double>(Clock::now()-start).count();
    ++output_calls_;
}
/** Persist run timing and CUDA diffusion scheduling diagnostics. */
void DriverIO::write_measurements(std::span<const CudaDiffusionScheduleRecord> cuda_diffusion_schedule,
                                  const CpuStageTimings& cpu_stages)
{
    const auto& config = runtime.configuration();
    const auto* compute_backend = runtime.backend();
    const auto& regrid_measurements = runtime.regrid_records();
    const bool has_diff = config.physics.diffusion.use_diffusion;
    {
        std::ofstream timing(config.io.out_dir + "/run_timings.tsv");
        timing << "driver_seconds\toutput_seconds\toutput_calls\n" << std::setprecision(17)
               << std::chrono::duration<double>(Clock::now()-started_).count() << '\t'
               << output_seconds_ << '\t' << output_calls_ << '\n';
        if (!timing) throw std::runtime_error("cannot write run timings");
    }
    // Stage clocks are wall intervals around synchronous CPU calls. They do
    // not include setup, output or miscellaneous Driver work; the existing
    // driver_seconds column remains the complete Driver elapsed interval.
    if (!compute_backend) {
        static constexpr std::array<const char*, CpuStageTimings::size> names{
            "regrid", "gravity", "timestep", "burn_first", "diffusion",
            "hydro", "burn_second"};
        std::ofstream timing(config.io.out_dir + "/cpu_stage_timings.tsv");
        timing << "stage\twall_seconds\tcalls\n" << std::setprecision(17);
        for (std::size_t i = 0; i < names.size(); ++i)
            timing << names[i] << '\t' << cpu_stages.seconds[i]
                   << '\t' << cpu_stages.calls[i] << '\n';
        if (!timing) throw std::runtime_error("cannot write CPU stage timings");
    }
    {
        std::ofstream report(config.io.out_dir + "/state_repairs.txt");
        if (!report) throw std::runtime_error("cannot write state repair diagnostics");
        report << std::setprecision(17) << "revision=P1.5-v1 units=CGS\n";
        const char* names[]{"events","affected_volume","mass_signed","mass_absolute",
            "momentum_x","momentum_y","momentum_z","energy_signed","energy_absolute","local_cell"};
        for (int i=0;i<state::RepairView::fixed_size;++i) report << names[i] << "=" << ctrl.repairs.values[i] << "\n";
        for (int i=0;i<ctrl.repairs.species();++i) {
            report << "species_" << i << "_signed=" << ctrl.repairs.values[10+2*i] << "\n";
            report << "species_" << i << "_absolute=" << ctrl.repairs.values[11+2*i] << "\n";
        }
        report << "block_uid=" << ctrl.repairs.block_uid << "\nstage=" << ctrl.repairs.stage
               << "\ntime=" << ctrl.repairs.time << "\nposition=" << ctrl.repairs.position[0] << ","
               << ctrl.repairs.position[1] << "," << ctrl.repairs.position[2] << "\n";
        std::cout << "[State] floor repairs=" << ctrl.repairs.values[0]
                  << " delta_mass=" << ctrl.repairs.values[2]
                  << " delta_energy=" << ctrl.repairs.values[7] << std::endl;
    }
    if (!regrid_measurements.empty()) {
        const std::filesystem::path directory = config.Get<std::string>("log_dir", config.io.out_dir);
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / (config.io.base_name + "_regrid.tsv"));
        if (!output) throw std::runtime_error("cannot write regrid measurements");
        output << "macro_step\tphysical_time\tbackend\told_blocks\tnew_blocks\ttopology_changed"
                  "\twall_seconds\tbytes_h2d\tbytes_d2h\tkernel_count\tstream_sync_count\n"
               << std::setprecision(17);
        for (const auto& record : regrid_measurements) {
            output << record.step << '\t' << record.time << '\t'
                   << (compute_backend ? "cuda" : "cpu") << '\t' << record.old_blocks << '\t'
                   << record.new_blocks << '\t' << record.changed << '\t' << record.elapsed_seconds << '\t'
                   << record.operations.bytes_h2d << '\t' << record.operations.bytes_d2h << '\t'
                   << record.operations.kernel_count << '\t' << record.operations.stream_sync_count << '\n';
        }
        if (!output) throw std::runtime_error("failed writing regrid measurements");
    }

    if (compute_backend) {
        const std::filesystem::path directory = config.Get<std::string>(
            "log_dir", config.io.out_dir);
        std::filesystem::create_directories(directory);
        const std::filesystem::path trace_path = directory
            / (config.io.base_name + "_backend_trace.tsv");
        std::ofstream trace_output(trace_path, std::ios::trunc);
        if (!trace_output)
            throw std::runtime_error("cannot write CUDA backend trace");
        trace_output
            << "macro_step\toperation\tuid\tepoch\tstorage_generation\tslot"
               "\tinterior_residency\tinterior_version\tinterior_pending"
               "\tinterior_token\tinterior_token_state\tghost_residency"
               "\tghost_version\tghost_source_version\tghost_pending"
               "\tghost_token\tghost_token_state\tbytes_h2d\tbytes_d2h"
               "\tkernel_count\tstream_sync_count\n";
        for (const auto& record : compute_backend->trace_snapshot()) {
            const auto& interior = record.coherence.interior;
            const auto& ghost = record.coherence.ghost;
            trace_output
                << record.macro_step << '\t'
                << static_cast<unsigned int>(record.operation) << '\t'
                << record.block.uid.value << '\t'
                << record.block.epoch.value << '\t'
                << record.storage.value << '\t'
                << static_cast<unsigned int>(record.slot) << '\t'
                << static_cast<unsigned int>(interior.residency) << '\t'
                << interior.version.value << '\t'
                << static_cast<unsigned int>(interior.pending_transfer) << '\t'
                << interior.completion.value << '\t'
                << static_cast<unsigned int>(interior.completion.state) << '\t'
                << static_cast<unsigned int>(ghost.residency) << '\t'
                << ghost.version.value << '\t'
                << record.coherence.ghost_source_version.value << '\t'
                << static_cast<unsigned int>(ghost.pending_transfer) << '\t'
                << ghost.completion.value << '\t'
                << static_cast<unsigned int>(ghost.completion.state) << '\t'
                << record.bytes_h2d << '\t' << record.bytes_d2h << '\t'
                << record.kernel_count << '\t'
                << record.stream_sync_count << '\n';
        }
        if (!trace_output)
            throw std::runtime_error("failed writing CUDA backend trace");

        if (has_diff) {
            const std::filesystem::path schedule_path = directory
                / (config.io.base_name + "_diffusion_schedule.tsv");
            std::ofstream schedule_output(schedule_path, std::ios::trunc);
            if (!schedule_output)
                throw std::runtime_error(
                    "cannot write CUDA diffusion schedule");
            schedule_output
                << "macro_step\tcache_generation\torder\tstages"
                   "\tnegative_gamma_stages\tcaptures_initial_operator"
                   "\tdiffusion_dt\tdt_forward_euler\n"
                << std::setprecision(17);
            for (const auto& record : cuda_diffusion_schedule) {
                schedule_output
                    << record.macro_step << '\t'
                    << record.cache_generation << '\t'
                    << record.order << '\t'
                    << record.stages << '\t'
                    << record.negative_gamma_stages << '\t'
                    << (record.captures_initial_operator ? 1 : 0) << '\t'
                    << record.diffusion_dt << '\t'
                    << record.dt_forward_euler << '\n';
            }
            if (!schedule_output || cuda_diffusion_schedule.empty())
                throw std::runtime_error(
                    "failed writing CUDA diffusion schedule");
        }
    }

}
} // namespace arch::driver
