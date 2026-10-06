#pragma once

#include <string>

#include "core/config/ConfigurationIdentity.h"
#include "core/config/RuntimeParams.h"
#include "core/problem/ProblemRegistry.h"
#include "io/chk/CheckpointCompatibility.h"

#ifndef ARCH_IO_FIXTURE_SOURCE_SHA256
#error "IO fixture compilation must bind its actual source SHA-256"
#endif
#ifndef ARCH_IO_FIXTURE_SOURCE_FILE
#error "IO fixture compilation must bind its actual source file"
#endif

/** Register a named synthetic IO producer; it never masquerades as a physical case. */
inline io::PlotSourceIdentity fixture_plot_identity(
    const SimConfig& controls,const SpeciesManager& species,
    GridMetrics::GeometrySemantics semantics=GridMetrics::GeometrySemantics::Existing)
{
    constexpr const char* case_id="SyntheticIoFixture";
    ProblemRegistry::Get().Register(case_id,[] { return std::unique_ptr<ProblemGenerator>{}; },
        {ARCH_IO_FIXTURE_SOURCE_FILE,ARCH_IO_FIXTURE_SOURCE_SHA256,false,[](const auto&) {
            arch::config::CaseConfiguration declaration;
            declaration.complete=true;
            declaration.consumers.needs_network=false;
            declaration.consumers.needs_temperature_floor=false;
            declaration.consumers.needs_composition_floor=false;
            return declaration;
        }});
    // Parser bytes are an independent setup witness. The final typed fixture
    // controls below are encoded after explicit fixture-owned preparation;
    // raw/effective identities deliberately have different responsibilities.
    const std::string parser_input=R"(
geometry=cartesian
nblockx1=1
nblockx2=0
nblockx3=0
x1_min=0
x1_max=1
x1l_boundary_type=outflow
x1r_boundary_type=outflow
solver=HLLC
reconstruct=muscl
limiter=mc
time_integrator=RK2
cfl=0.4
sml_rho=1e-12
min_eint=1e-10
max_eint=1e21
hll_wave_speed=roe
eos_type=ideal
gamma=1.4
gravity_type=none
use_burn=false
use_diffusion=false
compute_backend=cpu
lrefinemin=0
lrefinemax=0
tmax=0
max_steps=0
out_dir=fixture
base_name=fixture
plt_dt=-1
plt_dstep=-1
chk_dt=-1
chk_dstep=-1
plt_variables=DENS
restart=false
)";
    auto effective=RuntimeParams::LoadText(parser_input,case_id,
        arch::config::ConfigurationPurpose::Evolution);
    effective.grid=controls.grid;effective.numerics=controls.numerics;
    effective.execution=controls.execution;effective.physics=controls.physics;
    effective.amr=controls.amr;effective.io=controls.io;
    const auto request=arch::dispatch::resolve_execution_plan(effective,[]{return 0;},species.count());
    if(!request.ok)throw std::runtime_error(std::string(request.error));
    const auto plan=arch::dispatch::materialize_execution_plan(request.value,
        arch::dispatch::ComputeBackend::Cpu,species.count());
    if(!plan.ok)throw std::runtime_error(std::string(plan.error));
    const auto provenance=io::inspect_checkpoint_provenance(effective,species,plan.value.eos,
        false,"none",false);
    return arch::config::make_plot_source_identity(effective,provenance,plan.value,
        arch::dispatch::ComputeBackend::Cpu,semantics);
}
