/**
 * @file ConfigurationIdentity.cpp
 * @brief Exact typed runtime and accepted EOS records for formal Plotfile output.
 *
 * Workflow:
 * 1. Traverse final typed controls and declared model/material values once.
 * 2. Encode each field with a type, length and exact IEEE-754 binary64 bits.
 * 3. Bind resolved policies/geometry, accepted EOS and compiled case/build sources.
 * 4. Return immutable per-run evidence; parser bytes receive their own digest.
 *
 * SHA256(canonical_record) binds interpreted values; SHA256(raw_text) binds input
 * bytes. Neither digest alone certifies the accuracy of a numerical trajectory.
 */

#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/config/ConfigurationIdentity.h"
#include "core/config/ConfigurationInput.h"
#include "core/files/BuildIdentity.h"
#include "core/files/FileFingerprint.h"
#include "driver/dispatch/PolicyDescriptor.h"

namespace arch::config {
namespace {
/** Length-prefix fields so delimiters, aliases and embedded newlines cannot collide. */
class Record {
    std::string text_;
    void append(std::string_view name, char type, std::string_view value) {
        text_ += std::to_string(name.size()) + ":";
        text_.append(name);
        text_ += type;
        text_ += std::to_string(value.size()) + ":";
        text_.append(value);
        text_ += '\n';
        if (text_.size() > 1024*1024)
            throw std::length_error("Effective configuration provenance exceeds 1 MiB");
    }
public:
    explicit Record(std::string_view version, std::string_view key="version") { add(key,std::string(version)); }
    /** Preserve a string's exact interpreted bytes independently of parser spelling. */
    void add(std::string_view name, const std::string& value) {
        if(value.find('\0')!=std::string::npos)
            throw std::invalid_argument("NUL in configuration identity");
        append(name,'s',value);
    }
    /** Preserve every finite double bit, including signed zero, without locale rounding. */
    void add(std::string_view name, double value) {
        if(!std::isfinite(value))throw std::invalid_argument("Nonfinite effective configuration identity");
        constexpr char hex[]="0123456789abcdef";
        const auto bits=std::bit_cast<std::uint64_t>(value);
        std::string bytes(16,'0');
        for(int nibble=0;nibble<16;++nibble)bytes[nibble]=hex[(bits>>(60-4*nibble))&15];
        append(name,'d',bytes);
    }
    /** Stable signed integer encoding; no object bytes or compiler padding. */
    void add(std::string_view name, std::int64_t value) { append(name,'i',std::to_string(value)); }
    void add(std::string_view name, int value) { add(name,std::int64_t(value)); }
    void add(std::string_view name, bool value) { append(name,'b',value?"1":"0"); }
    /** Explicit presence distinguishes an unused absent control from a numeric zero. */
    void absent(std::string_view name) { append(name,'n',{}); }
    /** Preserve ordered selections with a count and independently typed entries. */
    template<class T> void add(std::string_view name, const std::vector<T>& values) {
        add(std::string(name)+".count",std::int64_t(values.size()));
        for(std::size_t i=0;i<values.size();++i)add(std::string(name)+"."+std::to_string(i),values[i]);
    }
    const std::string& text() const { return text_; }
};

/** Encode checked declared values independently of optional material-property provenance.
 * Sparse composition values have their own approved input/model source. They
 * are mass fractions, not species A/Z/gamma/Cv registration properties.
 */
template<class Records>
void declared_values(Record& record, const SimConfig& config,
                     const Records& values, const std::string& prefix,
                     bool material_provenance=true)
{
    for(const auto& [key,input]:values) {
        const std::string field=prefix+key;
        if(!input.resolved){record.absent(field);continue;}
        std::visit([&](const auto& value) {
            using T=std::decay_t<decltype(value)>;
            const auto actual=config.Get<T>(key,T{});
            record.add(field,actual);
            if(material_provenance) {
                if constexpr(std::is_same_v<T,int> || std::is_same_v<T,double>) {
                    const auto material=config.MaterialInput(key);
                    record.add(field+".material",material.value);
                    record.add(field+".material-origin",int(material.origin));
                    record.add(field+".material-owner",material.owner);
                    record.add(field+".material-source",material.source_identity);
                }
            } else {
                // Get uses the existing checked resolved-case storage; its
                // fallback argument never supplies a missing value. Require
                // exact agreement with the immutable resolver witness as well.
                if constexpr(!std::is_same_v<T,double>) {
                    throw std::invalid_argument("Composition identity requires a resolved mass fraction: "+key);
                } else {
                    if(std::bit_cast<std::uint64_t>(actual)!=std::bit_cast<std::uint64_t>(value))
                        throw std::invalid_argument("Composition identity differs from resolved input: "+key);
                    if(input.source==InputValueSource::Input) {
                        if(input.state!=InputState::Present || !input.parsed)
                            throw std::invalid_argument("Composition input lacks its explicit source: "+key);
                        record.add(field+".input-source",std::string("input"));
                        record.add(field+".input-state",std::string("present"));
                        record.absent(field+".source-owner");
                        record.add(field+".source-dependencies",std::vector<std::string>{});
                    } else if(input.source==InputValueSource::CaseDefined) {
                        if(input.state!=InputState::Missing || input.parsed ||
                            std::bit_cast<std::uint64_t>(value)!=std::bit_cast<std::uint64_t>(0.) ||
                            !input.source_evidence || input.source_evidence->owner.empty() ||
                            input.source_evidence->dependencies.empty())
                            throw std::invalid_argument("Composition zero lacks its declared model source: "+key);
                        record.add(field+".input-source",std::string("case-defined"));
                        record.add(field+".input-state",std::string("missing"));
                        record.add(field+".source-owner",input.source_evidence->owner);
                        record.add(field+".source-dependencies",input.source_evidence->dependencies);
                    } else {
                        throw std::invalid_argument("Unsupported composition identity source: "+key);
                    }
                }
            }
        },*input.resolved);
    }
}

/** Use the canonical name of the actual resolved policy, never infer it from a request alias. */
template<class Policies>
std::string policy(typename Policies::id_type id)
{
    const auto name=arch::dispatch::canonical_policy_name<Policies>(id);
    if(name.empty())throw std::invalid_argument("Unknown resolved output policy identity");
    return std::string(name);
}

/** Bind accepted EOS content/interpretation, constituents and applicable model controls.
 * A gamma-only IdealGas deliberately has count zero in every ordered vector;
 * explicit zero counts distinguish that known physical model from missing data.
 */
std::string eos_record(const SimConfig& config,const io::CheckpointProvenance& eos)
{
    Record record("arch-eos-identity-1");
    record.add("type",eos.eos_type);
    record.add("accepted-table-fingerprint",eos.eos_table_sha256);
    record.add("ideal-gamma",eos.ideal_gamma);
    record.add("coulomb-multiplier",config.physics.eos_coulomb_mult);
    record.add("species.names",eos.species_names);
    record.add("species.A",eos.species_A);
    record.add("species.Z",eos.species_Z);
    record.add("species.gamma",eos.species_gamma);
    record.add("species.Cv",eos.species_Cv);
    return record.text();
}
} // namespace

/** Encode the actual final fields, not stale resolved-input metadata or SimConfig padding. */
std::string canonical_configuration_record(const SimConfig& config)
{
    const auto input=config.LoadedInput();
    if(!input || !input->raw_text_available || input->case_id.empty()
       || input->case_source_sha256.empty())
        throw std::runtime_error("Formal Plotfile requires checked case-aware input and compiled case identity");
    Record record("arch-effective-configuration-1");
    record.add("case.id",input->case_id);
    record.add("case.compiled-source",input->case_source_sha256);
    record.add("grid.nblockx1", config.grid.nblockx1);
    record.add("grid.nblockx2", config.grid.nblockx2);
    record.add("grid.nblockx3", config.grid.nblockx3);
    record.add("grid.dim", config.grid.dim);
    record.add("grid.amr_max_blocks", config.grid.amr_max_blocks);
    record.add("grid.x1_min", config.grid.x1_min);
    record.add("grid.x1_max", config.grid.x1_max);
    record.add("grid.x2_min", config.grid.x2_min);
    record.add("grid.x2_max", config.grid.x2_max);
    record.add("grid.x3_min", config.grid.x3_min);
    record.add("grid.x3_max", config.grid.x3_max);
    record.add("grid.geometry", config.grid.geometry);
    record.add("grid.x1l_boundary_type", config.grid.x1l_boundary_type);
    record.add("grid.x1r_boundary_type", config.grid.x1r_boundary_type);
    record.add("grid.x2l_boundary_type", config.grid.x2l_boundary_type);
    record.add("grid.x2r_boundary_type", config.grid.x2r_boundary_type);
    record.add("grid.x3l_boundary_type", config.grid.x3l_boundary_type);
    record.add("grid.x3r_boundary_type", config.grid.x3r_boundary_type);
    record.add("numerics.solver_name", config.numerics.solver_name);
    record.add("numerics.reconstruction", config.numerics.reconstruction);
    record.add("numerics.limiter", config.numerics.limiter);
    record.add("numerics.time_integrator", config.numerics.time_integrator);
    record.add("numerics.dt_init", config.numerics.dt_init);
    record.add("numerics.dt_max", config.numerics.dt_max);
    record.add("numerics.hll_roe_wave_speed", config.numerics.hll_roe_wave_speed);
    record.add("numerics.dt_min", config.numerics.dt_min);
    record.add("numerics.tstep_change_factor", config.numerics.tstep_change_factor);
    record.add("numerics.cfl", config.numerics.cfl);
    record.add("numerics.entropy_fix_coeff", config.numerics.entropy_fix_coeff);
    record.add("numerics.sml_rho", config.numerics.sml_rho);
    record.add("numerics.min_eint", config.numerics.min_eint);
    record.add("numerics.max_eint", config.numerics.max_eint);
    record.add("execution.compute_backend", config.execution.compute_backend);
    record.add("execution.cuda_device", config.execution.cuda_device);
    record.add("physics.eos_type", config.physics.eos_type);
    record.add("physics.eos_table_path", config.physics.eos_table_path);
    record.add("physics.eos_helm_table_path", config.physics.eos_helm_table_path);
    record.add("physics.eos_coulomb_mult", config.physics.eos_coulomb_mult);
    record.add("physics.gamma", config.physics.gamma);
    record.add("physics.gravity.type", config.physics.gravity.type);
    record.add("physics.gravity.g_x", config.physics.gravity.g_x);
    record.add("physics.gravity.g_y", config.physics.gravity.g_y);
    record.add("physics.gravity.g_z", config.physics.gravity.g_z);
    record.add("physics.gravity.boundary", config.physics.gravity.boundary);
    record.add("physics.gravity.relative_tolerance", config.physics.gravity.relative_tolerance);
    record.add("physics.gravity.absolute_tolerance", config.physics.gravity.absolute_tolerance);
    record.add("physics.gravity.max_cycles", config.physics.gravity.max_cycles);
    record.add("physics.burn.use_burn", config.physics.burn.use_burn);
    record.add("physics.burn.network_name", config.physics.burn.network_name);
    record.add("physics.burn.nuclearTempMin", config.physics.burn.nuclearTempMin);
    record.add("physics.burn.nuclearDensMin", config.physics.burn.nuclearDensMin);
    record.add("physics.burn.smallt", config.physics.burn.smallt);
    record.add("physics.burn.smallx", config.physics.burn.smallx);
    record.add("physics.burn.enucDtFactor", config.physics.burn.enucDtFactor);
    record.add("physics.burn.use_nse", config.physics.burn.use_nse);
    record.add("physics.burn.nse_auto", config.physics.burn.nse_auto);
    record.add("physics.burn.nseTempThreshold", config.physics.burn.nseTempThreshold);
    record.add("physics.burn.nseDensThreshold", config.physics.burn.nseDensThreshold);
    record.add("physics.burn.odeconfig.ode_solver", config.physics.burn.odeconfig.ode_solver);
    record.add("physics.burn.odeconfig.linear_solver", config.physics.burn.odeconfig.linear_solver);
    record.add("physics.burn.odeconfig.rtol", config.physics.burn.odeconfig.rtol);
    record.add("physics.burn.odeconfig.atol", config.physics.burn.odeconfig.atol);
    record.add("physics.burn.odeconfig.max_newton_iter", config.physics.burn.odeconfig.max_newton_iter);
    record.add("physics.burn.odeconfig.max_substeps", config.physics.burn.odeconfig.max_substeps);
    record.add("physics.burn.odeconfig.dt_safe_factor", config.physics.burn.odeconfig.dt_safe_factor);
    record.add("physics.burn.odeconfig.dt_fac_max", config.physics.burn.odeconfig.dt_fac_max);
    record.add("physics.burn.odeconfig.dt_fac_min", config.physics.burn.odeconfig.dt_fac_min);
    record.add("physics.burn.odeconfig.initial_dt_frac", config.physics.burn.odeconfig.initial_dt_frac);
    record.add("physics.diffusion.use_diffusion", config.physics.diffusion.use_diffusion);
    record.add("physics.diffusion.integrator", config.physics.diffusion.integrator);
    record.add("physics.diffusion.diff_cfl", config.physics.diffusion.diff_cfl);
    record.add("physics.diffusion.max_stages", config.physics.diffusion.max_stages);
    record.add("physics.diffusion.use_thermal_diffusion", config.physics.diffusion.use_thermal_diffusion);
    record.add("physics.diffusion.use_viscous_diffusion", config.physics.diffusion.use_viscous_diffusion);
    record.add("physics.diffusion.use_species_diffusion", config.physics.diffusion.use_species_diffusion);
    record.add("physics.diffusion.nu_visc", config.physics.diffusion.nu_visc);
    record.add("physics.diffusion.alpha_therm", config.physics.diffusion.alpha_therm);
    record.add("physics.diffusion.D_spec", config.physics.diffusion.D_spec);
    record.add("amr.lrefinemin", config.amr.lrefinemin);
    record.add("amr.lrefinemax", config.amr.lrefinemax);
    record.add("amr.regrid_interval", config.amr.regrid_interval);
    record.add("amr.refine_var", config.amr.refine_var);
    record.add("amr.refine_on_rho", config.amr.refine_on_rho);
    record.add("amr.refine_on_p", config.amr.refine_on_p);
    record.add("amr.refine_on_temp", config.amr.refine_on_temp);
    record.add("amr.refine_on_velx", config.amr.refine_on_velx);
    record.add("amr.refine_on_vely", config.amr.refine_on_vely);
    record.add("amr.refine_on_velz", config.amr.refine_on_velz);
    record.add("amr.refine_on_eng", config.amr.refine_on_eng);
    record.add("amr.refine_on_vorticity", config.amr.refine_on_vorticity);
    record.add("amr.refine_on_div_v", config.amr.refine_on_div_v);
    record.add("amr.refine_on_entropy", config.amr.refine_on_entropy);
    record.add("amr.refine_on_enuc", config.amr.refine_on_enuc);
    record.add("amr.refine_on_jeans", config.amr.refine_on_jeans);
    record.add("amr.jeans_cells", config.amr.jeans_cells);
    record.add("amr.refine_on_species", config.amr.refine_on_species);
    record.add("amr.refine_all_species", config.amr.refine_all_species);
    record.add("amr.refine_species_names", config.amr.refine_species_names);
    record.add("amr.refine_threshold", config.amr.refine_threshold);
    record.add("amr.derefine_threshold", config.amr.derefine_threshold);
    record.add("io.tmax", config.io.tmax);
    record.add("io.max_steps", config.io.max_steps);
    record.add("io.plt_dt", config.io.plt_dt);
    record.add("io.plt_dstep", config.io.plt_dstep);
    record.add("io.chk_dt", config.io.chk_dt);
    record.add("io.chk_dstep", config.io.chk_dstep);
    record.add("io.out_dir", config.io.out_dir);
    record.add("io.base_name", config.io.base_name);
    record.add("io.restart", config.io.restart);
    record.add("io.restart_file", config.io.restart_file);
    record.add("io.plot_species_names", config.io.plot_species_names);
    record.add("io.vars.rho", config.io.vars.rho);
    record.add("io.vars.temp", config.io.vars.temp);
    record.add("io.vars.u", config.io.vars.u);
    record.add("io.vars.v", config.io.vars.v);
    record.add("io.vars.w", config.io.vars.w);
    record.add("io.vars.p", config.io.vars.p);
    record.add("io.vars.eng", config.io.vars.eng);
    record.add("io.vars.species", config.io.vars.species);
    record.add("io.vars.vort", config.io.vars.vort);
    record.add("io.vars.divv", config.io.vars.divv);
    record.add("io.vars.entr", config.io.vars.entr);
    record.add("io.vars.enuc", config.io.vars.enuc);
    record.add("io.vars.jens", config.io.vars.jens);

    declared_values(record,config,input->model.parameters,"declared-case.");
    if(input->model.composition)
        declared_values(record,config,input->model.composition->parameters,"composition.",false);
    declared_values(record,config,input->auxiliary,"auxiliary.");
    return record.text();
}

/** Freeze all production provenance once; unsupported/missing authorities fail explicitly. */
io::PlotSourceIdentity make_plot_source_identity(
    const SimConfig& config,const io::CheckpointProvenance& provenance,
    const arch::dispatch::ResolvedExecutionPlan& plan,
    arch::dispatch::ComputeBackend backend,GridMetrics::GeometrySemantics semantics)
{
    using namespace arch::dispatch;
    if(!provenance.available || provenance.eos_type.empty() || backend==ComputeBackend::Auto)
        throw std::invalid_argument("Output identity requires accepted EOS and actual resolved backend");
    if(semantics!=GridMetrics::GeometrySemantics::Existing &&
       semantics!=GridMetrics::GeometrySemantics::AxisymmetricRz)
        throw std::invalid_argument("Unknown output geometry identity");
    io::PlotSourceIdentity result;
    result.formal=true;
    const auto input=config.LoadedInput();
    result.effective_config_version="arch-effective-configuration-1";
    result.effective_config_record=canonical_configuration_record(config);
    Record runtime("arch-resolved-execution-1","resolved-execution.version");
    result.resolved_backend=backend==ComputeBackend::Cuda?"cuda":"cpu";
    runtime.add("backend",result.resolved_backend);
    runtime.add("geometry-semantics",semantics==GridMetrics::GeometrySemantics::AxisymmetricRz?
        std::string("axisymmetric-rz-2"):std::string("existing-1"));
    runtime.add("flux",policy<FluxPolicies>(plan.flux));
    runtime.add("reconstruction",policy<ReconstructionPolicies>(plan.reconstruction));
    runtime.add("limiter",policy<LimiterPolicies>(plan.limiter));
    runtime.add("time",policy<TimeIntegratorPolicies>(plan.time_integrator));
    runtime.add("eos",policy<EosPolicies>(plan.eos));
    runtime.add("network",policy<NetworkPolicies>(plan.network));
    runtime.add("ode",policy<OdeSolverPolicies>(plan.ode_solver));
    runtime.add("linear",policy<LinearSolverPolicies>(plan.linear_solver));
    runtime.add("diffusion",policy<DiffusionIntegratorPolicies>(plan.diffusion_integrator));
    runtime.add("boundary-identity",provenance.boundary_identity);
    result.effective_config_record+=runtime.text();
    result.effective_config_sha256=arch::core::string_sha256(result.effective_config_record);
    result.case_id=input->case_id;
    result.case_source_sha256=input->case_source_sha256;
    result.raw_config_sha256=arch::core::string_sha256(input->raw_text);
    result.binary_sha256=arch::core::running_executable_sha256();
    const auto& build=arch::core::compiled_build_identity();
    result.build_identity_version=build.version;
    result.build_identity_scope=build.scope;
    result.build_id=build.build_id;
    result.source_manifest_sha256=build.source_manifest_sha256;
    result.build_profile_sha256=build.profile_sha256;
    result.source_manifest_record=build.source_manifest;
    result.build_profile_record=build.profile_record;
    result.source_git_head=build.source_git_head;
    result.source_git_dirty=build.source_git_dirty;
    result.eos_type=provenance.eos_type;
    result.eos_table_sha256=provenance.eos_table_sha256;
    result.ideal_gamma=provenance.ideal_gamma;
    result.species_names=provenance.species_names;
    result.species_A=provenance.species_A;
    result.species_Z=provenance.species_Z;
    result.species_gamma=provenance.species_gamma;
    result.species_Cv=provenance.species_Cv;
    result.unit_system="cgs";
    result.eos_identity_version="arch-eos-identity-1";
    result.eos_identity_record=eos_record(config,provenance);
    result.eos_identity_sha256=arch::core::string_sha256(result.eos_identity_record);
    return result;
}
} // namespace arch::config
