#include "DriverIO.h"
#include "DriverRuntime.h"
#include "DriverControl.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
namespace arch::driver {
void DriverIO::write_plot()
{
    auto& amr_ctrl = runtime.control();
    const auto& config = runtime.configuration();
    const auto& specs = runtime.species();
    runtime.materialize_current_for_host();
    write_plt(amr_ctrl, p_func, t_func, gamma1_func, eos, ctrl.plt_file_index++,
              ctrl.t_current, config, specs);
}
void DriverIO::write_checkpoint(double dt_burn_global, bool resume_after_regrid)
{
    auto& amr_ctrl = runtime.control();
    const auto& config = runtime.configuration();
    const auto& specs = runtime.species();
    if (runtime.backend())
        runtime.materialize_current_for_host();
    write_chk(amr_ctrl, ctrl.chk_file_index++, ctrl.plt_file_index,
              ctrl.step_count, ctrl.t_current, ctrl.dt_old,
              dt_burn_global, resume_after_regrid, config, specs,
              checkpoint_provenance);
}
void DriverIO::write_measurements(std::span<const CudaDiffusionScheduleRecord> cuda_diffusion_schedule)
{
    const auto& config = runtime.configuration();
    const auto* compute_backend = runtime.backend();
    const auto& regrid_measurements = runtime.regrid_records();
    const bool has_diff = config.physics.diffusion.use_diffusion;
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
