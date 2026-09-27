/**
 * @file CaseUnitEvidence.cpp
 * @brief Report case unit provenance separately from execution readiness.
 *
 * Workflow:
 * 1. Match the compiled built-in case source to its reviewed SHA-256 before
 *    making any claim about dimensional units.
 * 2. Attach audited CGS units only to parameters actually read during Setup;
 *    resolve dimension-dependent Sedov explosion energy after grid inspection.
 * 3. Report source identity and evidence status without running the Driver.
 */

#include <map>

#include "api/CaseInspection.h"

namespace arch::api {
namespace {
struct ReviewedCase { const char* source; const char* sha256; std::map<std::string, std::string> units; };
// Audited against built-in Setup/Init expressions, NOT inferred from scalar
// observations. Re-review after source edits; never auto-refresh these hashes.
const std::map<std::string, ReviewedCase> reviewed_cases{
    {"Sod", {"simulation/Sod/Sod.cpp", "b5b74d29684f858a1bf11283faaef211125e009370a89032ba75c5a545248c5b", {
        {"x_pos", "cm"},
        {"rho_left", "g/cm^3"},
        {"rho_right", "g/cm^3"},
        {"p_left", "erg/cm^3"},
        {"p_right", "erg/cm^3"},
        {"u_left", "cm/s"},
        {"u_right", "cm/s"},
    }}},
    {"CellularDet", {"simulation/Cellular/Cellular.cpp", "a6b8f5fb469aa7c73848ef765926fc3d02969be42e07186131079c00a3f5f54d", {
        {"rhoAmbient", "g/cm^3"},
        {"tempAmbient", "K"},
        {"rhoPerturb", "g/cm^3"},
        {"tempPerturb", "K"},
        {"velxPerturb", "cm/s"},
        {"radiusPerturb", "cm"},
        {"noiseAmplitude", "1"},
        {"shock_dir", "1"},
    }}},
    {"Gaussian", {"simulation/GaussianPulse/Gaussian.cpp", "77b37ae234a76a2ffd3f729660af7bc32557df4cb822f34be38e6b8da058c2d3", {
        {"rho0", "g/cm^3"},
        {"p0", "erg/cm^3"},
        {"amp", "1"},
        {"width", "cm"},
        {"xc", "cm"},
        {"yc", "cm"},
        {"zc", "cm"},
        {"pressure_amplitude", "1"},
        {"u_amplitude", "cm/s"},
        {"v_amplitude", "cm/s"},
        {"w_amplitude", "cm/s"},
        {"gas_cv", "erg/(g*K)"},
    }}},
    {"Sedov", {"simulation/Sedov/Sedov.cpp", "ba3da68337dba6ac0ef89674548f157ecb67819fc8d991eb93bd02eff15d0916", {
        {"center_x", "cm"},
        {"center_y", "cm"},
        {"center_z", "cm"},
        {"deposit_radius", "cm"},
        {"ambient_density", "g/cm^3"},
        {"ambient_pressure", "erg/cm^3"},
        {"explosion_energy", "dimension-dependent-energy"},
    }}},
    {"RT", {"simulation/RTinstability/RT_instab.cpp", "cc3d8c4185ed2ba44ab26e1c831ef0ac7ccadacb66af28cd1f0abf3ad7456c1e", {
        {"rho_heavy", "g/cm^3"},
        {"rho_light", "g/cm^3"},
        {"y_int", "cm"},
        {"p_int", "erg/cm^3"},
        {"amplitude", "cm/s"},
    }}},
    {"SmoothAdvection", {"simulation/SmoothAdvection/SmoothAdvection.cpp", "dd9bf2b5d5a16ec24ab16cd7214aca16c447a2867766154731f487a56d24b80c", {
        {"rho_mean", "g/cm^3"},
        {"rho_amplitude", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"velocity0", "cm/s"},
        {"mode", "1"},
    }}},
    {"GravityBox", {"simulation/GravityBox/GravityBox.cpp", "5b7082c6f0778c4632dd7b3d2e55f06fdf8c789ed1fd240039003c582bf93477", {
        {"rho0", "g/cm^3"}, {"temperature0", "K"}, {"amplitude", "1"},
        {"temperature_amplitude", "1"}, {"velocity0", "cm/s"}, {"width", "cm"},
        {"center_x", "cm"}, {"center_y", "cm"}, {"center_z", "cm"}, {"gas_cv", "erg/(g*K)"},
    }}},
    {"JeansWave", {"simulation/JeansWave/JeansWave.cpp", "1d705de8cab39b7b0c42c729245f5175b84e0bd99e94a24c1a576e7e578c387d", {
        {"rho0", "g/cm^3"}, {"pressure0", "erg/cm^3"},
        {"amplitude", "1"}, {"phase", "rad"}, {"mode", "1"},
        {"standing_wave", "1"},
    }}},
    {"ExternalGravity", {"simulation/ExternalGravity/ExternalGravity.cpp", "c2983a892091924f3a5a5abe5152eb091a5eb2d492c38913ca5867ad438899ee", {
        {"rho0", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"velocity_x0", "cm/s"},
    }}},
    {"DiffusionMode", {"simulation/DiffusionMode/DiffusionMode.cpp", "a5a806e03acfeff7d5cca6b8d65230b921e642340692103118eb86b7e1d0d90a", {
        {"rho0", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"tracer_mean", "1"},
        {"tracer_amplitude", "1"},
        {"mode", "1"},
    }}},
    {"BurnOneZone", {"simulation/BurnOneZone/BurnOneZone.cpp", "a5c1c7c81e58fbc5c3f7a4f7139feb8b6bb09e0b160c6d9790ae87a1abadc4af", {
        {"rho0", "g/cm^3"},
        {"temperature0", "K"},
    }}},
    {"BurnGradient", {"simulation/BurnGradient/BurnGradient.cpp", "4b2a1371175ee3c1ec899c504c6fc0287fb819005bafeba6733d9b6224bf63b8", {
        {"rho0", "g/cm^3"},
        {"background_temperature", "K"},
        {"peak_temperature", "K"},
        {"center_x", "cm"},
        {"width", "cm"},
    }}},
    {"CooperativeHotspots", {"simulation/CooperativeHotspots/CooperativeHotspots.cpp", "569b57212f81954d817098ae731e11dfb42bce8b72d3a6c6db2dd7d134375b45", {
        {"ambient_density", "g/cm^3"},
        {"ambient_temperature", "K"},
        {"hotspot_temperature", "K"},
        {"hotspot_center_x", "cm"},
        {"hotspot_center_y", "cm"},
        {"hotspot_radius", "cm"},
        {"hotspot_separation", "cm"},
    }}},
    {"SNIaCoupled", {"simulation/SNIaCoupled/SNIaCoupled.cpp", "291721c4a22fece17f6df95020386e912fc02f333e5d17110f5d10cf28182e15", {
        {"rho0", "g/cm^3"},
        {"temperature0", "K"},
        {"temperature_peak", "K"},
        {"density_amplitude", "1"},
        {"hotspot_width", "cm"},
        {"center_x", "cm"},
        {"center_y", "cm"},
        {"center_z", "cm"},
    }}},
};
}
detail::Json CaseInspectionCapability(const std::string& name, const ProblemRegistration& registration) {
    using detail::Json;
    const auto it = reviewed_cases.find(name);
    const bool stamped = !registration.source_sha256.empty();
    const bool reviewed = it != reviewed_cases.end() && stamped && registration.source_sha256 == it->second.sha256;
    auto keys = Json::array();
    if (reviewed) for (const auto& [key, unit] : it->second.units) keys.push(key);
    return Json::object({{"command", "--inspect-case"}, {"registered", true},
        {"setupReads", true}, {"primitiveSinkProbe", registration.point_initialization},
        {"automaticExpressionInference", false}, {"workerPlatform", "linux"},
        {"processCpuSeconds", contract::case_cpu_seconds}, {"hostWallTimeoutSeconds", contract::case_wall_seconds},
        {"sourceFile", registration.source_file},
        {"compiledSourceSha256", stamped ? Json(registration.source_sha256) : Json()},
        {"reviewedUnitEvidence", reviewed ? "current" : it == reviewed_cases.end() ? "unreviewed-case" : "source-changed-or-unstamped"},
        {"reviewedUnitKeys", keys}, {"coverage", "executed Get and Core composition reads; sampled Init sinks; no unexecuted paths or direct model map reads"}});
}
/** Attach unit claims only when the compiled case source matches audited evidence. */
void AddAuditedCaseUnits(const std::string& name, const ProblemRegistration& registration,
                         int dimension, preview::ParameterReadTrace& reads) {
    const auto it = reviewed_cases.find(name);
    if (it == reviewed_cases.end() || registration.source_sha256 != it->second.sha256) return;
    for (const auto& [key, declared] : it->second.units) {
        if (!reads.reads().contains(key)) continue;
        auto unit = declared;
        if (unit == "dimension-dependent-energy")
            unit = dimension == 1 ? "erg/cm^2" : dimension == 2 ? "erg/cm" : "erg";
        reads.record_unit(key, unit, "reviewed-case-source:" + std::string(it->second.source) + "@" + registration.source_sha256);
    }
}
} // namespace arch::api
