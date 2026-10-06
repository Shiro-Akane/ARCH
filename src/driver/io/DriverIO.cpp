/**
 * @file DriverIO.cpp
 * @brief Schedule plots, checkpoints and diagnostics from the current published state.
 *
 * Workflow:
 * 1. Receive a resolved configuration, stage request and current state identity.
 * 2. Complete actual native RZ ghosts through the Runtime materialization owner.
 * 3. Validate an ephemeral shared thermodynamic mean with the bound EOS, while
 *    retaining the original V/W-averaged conserved state for persistence.
 * 4. Publish plots/checkpoints only after serialization and close succeed.
 * 5. Hand completed state and diagnostics to the next scheduled stage.
 */

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include "driver/io/DriverIO.h"
#include "core/files/RunIdentity.h"

#include "amr/AMRControl.h"
#include "driver/runtime/DriverRuntime.h"
#include "driver/schedule/DriverControl.h"
#include "numerics/state/RzNativeClosure.h"
#include "numerics/state/StateAdmissibility.h"
#include "physics/species/Species.h"

namespace arch::driver {
namespace {
/** Finish diagnostic streams explicitly; destructors cannot propagate buffered failures. */
void close_diagnostic(std::ofstream& output, const char* description)
{
    output.flush();
    if (!output)
        throw std::runtime_error(std::string("cannot flush ") + description);
    output.close();
    if (!output)
        throw std::runtime_error(std::string("cannot close ") + description);
}
/** Reject invalid native closure, composition or EOS state before persistence.
 * RZ uses the actual density stencil to recover J^2/(2*I_*V), rather than
 * treating m_phi=J/W as an ordinary point momentum. The transient effective
 * mean is only an EOS input; this traversal never changes stored native U.
 * Runtime materialization must have completed the actual Current ghosts.
 */
void validate_output_state(DriverRuntime& runtime, PressureFunc pressure,
                          TemperatureFunc temperature, Gamma1Func gamma1, const void* eos)
{
    const auto& limits=runtime.configuration().numerics;
    const int species=runtime.species().count();
    const auto semantics=runtime.geometry_semantics();
    const arch::state::Bounds bounds{limits.sml_rho,limits.min_eint,limits.max_eint};
    std::vector<double> fractions(species);
    auto& control=runtime.control();
    for (int id : control.tree->GetActiveBlocks()) {
        const auto& block=control.pool->GetBlock(id);
        const auto& grid=block.grid;
        const auto& state=block.fluid_state;
        const auto geometry=GridMetrics::make_geometry_view(grid,semantics);
        const auto read=[&state](int index) { return state.get(index); };
        for (int k=grid.Ks(); k<grid.Ke(); ++k)
            for (int j=grid.Js(); j<grid.Je(); ++j)
                for (int i=grid.Is(); i<grid.Ie(); ++i) {
                    const int cell=grid.GetIndex(i,j,k);
                    try {
                        state.get_species_to_buffer(cell,fractions.data());
                        FluidVector eos_mean=read(cell);
                        if (semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
                            const auto closure=RzThermodynamics::make_cell(read,cell,geometry,i,bounds);
                            if (!closure.valid())
                                throw std::runtime_error("Invalid native RZ output closure");
                            eos_mean=closure.effective_mean;
                        }
                        if (arch::state::validate(eos_mean,fractions.data(),species,1,
                            limits.sml_rho,limits.min_eint,limits.max_eint)!=arch::state::Status::valid)
                            throw std::runtime_error("Invalid conserved output state");
                        io::require_output_thermodynamics(eos_mean,fractions.data(),pressure,temperature,gamma1,eos);
                    } catch (const std::exception& error) {
                        throw std::runtime_error("Output block="+std::to_string(id)+" cell="+
                            std::to_string(cell)+": "+error.what());
                    }
                }
    }
}
}
/** Complete actual ghosts/materialization and publish a validated native plot. */
void DriverIO::write_plot(std::span<const io::PlotScalarField> extra_fields)
{
    const auto start = Clock::now();
    auto& amr_ctrl = runtime.control();
    const auto& config = runtime.configuration();
    const auto& specs = runtime.species();
    if (amr_ctrl.tree->GetActiveBlocks().empty())
        throw std::invalid_argument("Cannot publish Plotfile without active leaf blocks.");
    runtime.materialize_current_for_host();
    validate_output_state(runtime,p_func,t_func,gamma1_func,eos);
    if (plot_run_id_.empty()) plot_run_id_ = arch::core::new_run_identity();
    write_plt(amr_ctrl, p_func, t_func, gamma1_func, eos, ctrl.plt_file_index,
              ctrl.t_current, config, specs, extra_fields, &checkpoint_provenance,
              plot_run_id_, runtime.geometry_semantics(), &plot_source_identity);
    // A failed write/close/publication must not consume the next output identity.
    ++ctrl.plt_file_index;
    output_seconds_ += std::chrono::duration<double>(Clock::now()-start).count();
    ++output_calls_;
}
/** Persist original native restart means after closure/EOS validation.
 * Materialization already performs ensure_fluid_ghosts first, so native RZ
 * takes that single owned path; an extra call would resample user BC twice.
 */
void DriverIO::write_checkpoint(double dt_burn_global, bool resume_after_regrid)
{
    const auto start = Clock::now();
    auto& amr_ctrl = runtime.control();
    const auto& config = runtime.configuration();
    const auto& specs = runtime.species();
    if (runtime.backend()
        || runtime.geometry_semantics()==GridMetrics::GeometrySemantics::AxisymmetricRz)
        runtime.materialize_current_for_host();
    validate_output_state(runtime,p_func,t_func,gamma1_func,eos);
    const auto semantics = runtime.geometry_semantics();
    io::CheckpointGeometryIdentity geometry_identity{1, "existing"};
    if (semantics == GridMetrics::GeometrySemantics::AxisymmetricRz)
        geometry_identity = io::current_rz_checkpoint_geometry();
    else if (semantics != GridMetrics::GeometrySemantics::Existing)
        throw std::runtime_error("Unsupported runtime checkpoint geometry profile");
    write_chk(amr_ctrl, ctrl.chk_file_index, ctrl.plt_file_index,
              ctrl.step_count, ctrl.t_current, ctrl.dt_old,
              dt_burn_global, resume_after_regrid, config, specs,
              checkpoint_provenance, ctrl.repairs, geometry_identity);
    // Reserve the identity until the serializer reports success.
    ++ctrl.chk_file_index;
    output_seconds_ += std::chrono::duration<double>(Clock::now()-start).count();
    ++output_calls_;
}
/** Persist run timing and CUDA diffusion scheduling diagnostics. */
void DriverIO::write_measurements(std::span<const CudaDiffusionScheduleRecord> cuda_diffusion_schedule,
                                  const CpuStageTimings& cpu_stages)
{
    const auto& config = runtime.configuration();
    const auto& hydro_budget=runtime.hydro_boundary_budget();
    const auto& diffusion_budget=runtime.diffusion_boundary_budget();
    if (!hydro_budget.empty()) {
        std::ofstream fluxes(config.io.out_dir+"/boundary_fluxes.tsv");
        if (!fluxes) throw std::runtime_error("Cannot write boundary flux diagnostics");
        fluxes << "# scope=since-process-start; positive=outgoing; momentum=native; "
                  "energy=fluid-total; gravity-work=separate; units=CGS\n";
        const auto& observer=runtime.boundary_observer_operations();
        fluxes << "# observer_bytes_h2d=" << observer.bytes_h2d << "; observer_bytes_d2h=" << observer.bytes_d2h
            << "; observer_kernels=" << observer.kernel_count << "; observer_synchronizations=" << observer.stream_sync_count << '\n';
        fluxes << "time\toperator\tmass\tmomentum_x1\tmomentum_x2\tmomentum_x3\tenergy";
        for (const auto& species:runtime.species().species_list)
            fluxes << "\tspecies_" << species.name;
        fluxes << "\theat\n" << std::setprecision(17);
        for (int kind=0;kind<3;++kind) {
            fluxes << ctrl.t_current << '\t' << (kind==0 ? "hydro" : kind==1 ? "diffusion" : "total");
            for (std::size_t k=0;k<hydro_budget.size();++k)
                fluxes << '\t' << (kind==0 ? hydro_budget[k] : kind==1 ? diffusion_budget[k]
                    : hydro_budget[k]+diffusion_budget[k]);
            fluxes << '\n';
        }
    }
    const auto* compute_backend = runtime.backend();
    const auto& regrid_measurements = runtime.regrid_records();
    const bool has_diff = config.physics.diffusion.use_diffusion;
    {
        std::ofstream timing(config.io.out_dir + "/run_timings.tsv");
        timing << "driver_seconds\toutput_seconds\toutput_calls\n" << std::setprecision(17)
               << std::chrono::duration<double>(Clock::now()-started_).count() << '\t'
               << output_seconds_ << '\t' << output_calls_ << '\n';
        if (!timing) throw std::runtime_error("cannot write run timings");
        close_diagnostic(timing, "run timings");
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
        close_diagnostic(timing, "CPU stage timings");
    }
    {
        const bool rz = runtime.geometry_semantics()
            == GridMetrics::GeometrySemantics::AxisymmetricRz;
        const auto expected_repairs=rz ? state::RepairSemantics::RzVolumeAngular
            : state::RepairSemantics::ExistingVolume;
        if(ctrl.repairs.semantics!=expected_repairs)
            throw std::runtime_error("Repair report measure identity differs from runtime chart");
        std::ofstream report(config.io.out_dir + "/state_repairs.txt");
        if (!report) throw std::runtime_error("cannot write state repair diagnostics");
        report << std::setprecision(17) << "revision=P1.5-v1 units=CGS\n";
        // RZ slot 6 is J=W*m_phi; other conserved deltas use V.
        if(rz) report << "repair_semantics=" << state::repair_semantics_name(ctrl.repairs.semantics) << "\n";
        if (rz)
            report << "geometry_semantics_revision=2\ngeometry_chart=axisymmetric-rz\n"
                   << "momentum_basis=local-orthonormal-r-z-phi\n"
                   << "measure_unit=cm^3\nmeasure_normalization=full_rotation\n"
                   << "mass_unit=g\nmomentum_unit=g*cm/s\nenergy_unit=erg\n"
                   << "angular_momentum_unit=g*cm^2/s\nstate_semantics=rz-m-phi-j-over-w-v1\n";
        const char* names[]{"events","affected_volume","mass_signed","mass_absolute",
            rz ? "momentum_r" : "momentum_x",
            rz ? "momentum_z" : "momentum_y",
            rz ? "angular_momentum_signed" : "momentum_z",
            "energy_signed","energy_absolute","local_cell"};
        for (int i=0;i<state::RepairView::fixed_size;++i) report << names[i] << "=" << ctrl.repairs.values[i] << "\n";
        for (int i=0;i<ctrl.repairs.species();++i) {
            report << "species_" << i << "_signed=" << ctrl.repairs.values[10+2*i] << "\n";
            report << "species_" << i << "_absolute=" << ctrl.repairs.values[11+2*i] << "\n";
        }
        report << "block_uid=" << ctrl.repairs.block_uid << "\nstage=" << ctrl.repairs.stage
               << "\ntime=" << ctrl.repairs.time << "\nposition=" << ctrl.repairs.position[0] << ","
               << ctrl.repairs.position[1] << "," << ctrl.repairs.position[2] << "\n";
        // Buffered text failures must propagate before announcing diagnostics.
        close_diagnostic(report, "state repair diagnostics");
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
        close_diagnostic(output, "regrid measurements");
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
        close_diagnostic(trace_output, "CUDA backend trace");

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
            close_diagnostic(schedule_output, "CUDA diffusion schedule");
        }
    }

}
} // namespace arch::driver
