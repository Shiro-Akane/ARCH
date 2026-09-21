#include "CaseInspection.h"
#include <map>
namespace arch::api {
namespace {
struct ReviewedCase { const char* source; const char* sha256; std::map<std::string, std::string> units; };
// Audited against built-in Setup/Init expressions, NOT inferred from scalar
// observations. Re-review after source edits; never auto-refresh these hashes.
const std::map<std::string, ReviewedCase> reviewed_cases{
    {"Sod", {"simulation/Sod/Sod.cpp", "11b3e7a5df5e32166e6205cd21a9fc9543f213a7c7643b2249f4a075e008c6e0", {
        {"x_pos", "cm"},
        {"rho_left", "g/cm^3"},
        {"rho_right", "g/cm^3"},
        {"p_left", "erg/cm^3"},
        {"p_right", "erg/cm^3"},
        {"u_left", "cm/s"},
        {"u_right", "cm/s"},
    }}},
    {"CellularDet", {"simulation/Cellular/Cellular.cpp", "4829a5d3cbf6a3d8ee1561d876fc49e5cb158b68a4e9d13533b1ec0b27bf545a", {
        {"rhoAmbient", "g/cm^3"},
        {"tempAmbient", "K"},
        {"rhoPerturb", "g/cm^3"},
        {"tempPerturb", "K"},
        {"velxPerturb", "cm/s"},
        {"radiusPerturb", "cm"},
        {"noiseAmplitude", "1"},
        {"shock_dir", "1"},
    }}},
    {"Gaussian", {"simulation/GaussianPulse/Gaussian.cpp", "3792b070d0fd021d7bc702fa0ed3648377a3bed42350d0fa9eed6c237fe086c8", {
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
    {"Sedov", {"simulation/Sedov/Sedov.cpp", "08f8697c70981d7a6b17a67d596a5beaeda11e0c6268212ac7c0fe27a5df4c1e", {
        {"center_x", "cm"},
        {"center_y", "cm"},
        {"center_z", "cm"},
        {"deposit_radius", "cm"},
        {"ambient_density", "g/cm^3"},
        {"ambient_pressure", "erg/cm^3"},
        {"explosion_energy", "dimension-dependent-energy"},
    }}},
    {"RT", {"simulation/RTinstability/RT_instab.cpp", "386246bbf1f812ac8de2f58b822659085a8a5c516fbc59a0f4c5754fbd10e635", {
        {"rho_heavy", "g/cm^3"},
        {"rho_light", "g/cm^3"},
        {"y_int", "cm"},
        {"p_int", "erg/cm^3"},
        {"amplitude", "cm/s"},
    }}},
    {"SmoothAdvection", {"simulation/SmoothAdvection/SmoothAdvection.cpp", "eb8b7426f8ea46655103c1f6fffd86806b7976c7cbd9b18a0360c9633851458c", {
        {"rho_mean", "g/cm^3"},
        {"rho_amplitude", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"velocity0", "cm/s"},
        {"mode", "1"},
    }}},
    {"ExternalGravity", {"simulation/ExternalGravity/ExternalGravity.cpp", "eca8447af7960109936e572aa6e651dd82d605c64f1268cc055262b13cfd1707", {
        {"rho0", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"velocity_x0", "cm/s"},
    }}},
    {"DiffusionMode", {"simulation/DiffusionMode/DiffusionMode.cpp", "d1fcf7f41162b12c666f7c3cccfcb54ef712c1df48af1fbeb895aaa5da940743", {
        {"rho0", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"tracer_mean", "1"},
        {"tracer_amplitude", "1"},
        {"mode", "1"},
    }}},
    {"BurnOneZone", {"simulation/BurnOneZone/BurnOneZone.cpp", "2bae8ee402133fd5ee13d0037297ebc0869434539701ac4c442caa7a02650bf7", {
        {"rho0", "g/cm^3"},
        {"temperature0", "K"},
    }}},
    {"BurnGradient", {"simulation/BurnGradient/BurnGradient.cpp", "aa9b610cb0a83f8d240a25234cbdc033f958c9cf678e0da918cc6e83d465138f", {
        {"rho0", "g/cm^3"},
        {"background_temperature", "K"},
        {"peak_temperature", "K"},
        {"center_x", "cm"},
        {"width", "cm"},
    }}},
    {"CooperativeHotspots", {"simulation/CooperativeHotspots/CooperativeHotspots.cpp", "08c74fa780f566ae04017ed304725ea06015805189e248540e6705a0b6857c4f", {
        {"ambient_density", "g/cm^3"},
        {"ambient_temperature", "K"},
        {"hotspot_temperature", "K"},
        {"hotspot_center_x", "cm"},
        {"hotspot_center_y", "cm"},
        {"hotspot_radius", "cm"},
        {"hotspot_separation", "cm"},
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
