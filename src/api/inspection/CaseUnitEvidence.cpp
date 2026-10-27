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
#include "api/PreviewSession.h"

namespace arch::api {
namespace {
struct ReviewedCase { const char* source; const char* sha256; std::map<std::string, std::string> units; };
// Audited against built-in Setup/Init expressions, NOT inferred from scalar
// observations. Re-review after source edits; never auto-refresh these hashes.
const std::map<std::string, ReviewedCase> reviewed_cases{
    {"Sod", {"simulation/Sod/Sod.cpp", "7867565cdbe801769c0860de8d4a41254aaaa7be354951bcab69dd042a242738", {
        {"x_pos", "cm"},
        {"rho_left", "g/cm^3"},
        {"rho_right", "g/cm^3"},
        {"p_left", "erg/cm^3"},
        {"p_right", "erg/cm^3"},
        {"u_left", "cm/s"},
        {"u_right", "cm/s"},
    }}},
    {"CellularDet", {"simulation/Cellular/Cellular.cpp", "54cb01690f6879ba80d1e9d0179bd0e0416ea1e77e89cfe3a4886a027388a9cc", {
        {"rhoAmbient", "g/cm^3"},
        {"tempAmbient", "K"},
        {"rhoPerturb", "g/cm^3"},
        {"tempPerturb", "K"},
        {"velxPerturb", "cm/s"},
        {"radiusPerturb", "cm"},
        {"noiseAmplitude", "1"},
        {"shock_dir", "1"},
    }}},
    {"Gaussian", {"simulation/GaussianPulse/Gaussian.cpp", "ca23bbbb7aef9332ca2eb9be9e468e97eb8fcaf3154d4c162e458dc6a4ea8f5d", {
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
    {"Sedov", {"simulation/Sedov/Sedov.cpp", "ab92f813d868cf44b26af0585c7b74d774c82cd9e8d8ee583c3551be0ac3a18e", {
        {"center_x", "cm"},
        {"center_y", "cm"},
        {"center_z", "cm"},
        {"deposit_radius", "cm"},
        {"ambient_density", "g/cm^3"},
        {"ambient_pressure", "erg/cm^3"},
        {"explosion_energy", "dimension-dependent-energy"},
    }}},
    {"RT", {"simulation/RTinstability/RT_instab.cpp", "c2aa30d0958d1a7f835cff66be919a0e81037d15f5e61e00522400b01144aacf", {
        {"rho_heavy", "g/cm^3"},
        {"rho_light", "g/cm^3"},
        {"y_int", "cm"},
        {"p_int", "erg/cm^3"},
        {"amplitude", "cm/s"},
    }}},
    {"SmoothAdvection", {"simulation/SmoothAdvection/SmoothAdvection.cpp", "21aaf2c1f2b1dad3ab37ea9aac27b95125b7cb96c0e7c807d29de72ae98edf83", {
        {"rho_mean", "g/cm^3"},
        {"rho_amplitude", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"velocity0", "cm/s"},
        {"mode", "1"},
        {"mode_axis", "1"},
    }}},
    {"GravityBox", {"simulation/GravityBox/GravityBox.cpp", "cdeee8453bf636545a8d3a1e84a7c9e0a4b29752b8838c1a6e7206ae6d839e9c", {
        {"rho0", "g/cm^3"}, {"temperature0", "K"}, {"amplitude", "1"},
        {"temperature_amplitude", "1"}, {"velocity0", "cm/s"}, {"width", "cm"},
        {"center_x", "cm"}, {"center_y", "cm"}, {"center_z", "cm"}, {"gas_cv", "erg/(g*K)"},
        {"hydrostatic_radial", "1"},
    }}},
    {"JeansWave", {"simulation/JeansWave/JeansWave.cpp", "beb898277d86e85b2ed609cba801c72cc8d82c1ba83ac3a209976f0e68c4ac7b", {
        {"rho0", "g/cm^3"}, {"pressure0", "erg/cm^3"},
        {"amplitude", "1"}, {"phase", "rad"}, {"mode", "1"},
        {"standing_wave", "1"},
    }}},
    {"ExternalGravity", {"simulation/ExternalGravity/ExternalGravity.cpp", "512ded6e1a5f447c7b580cf14cb21205942a3f550ff3d43a3bd6b839234d4dc2", {
        {"rho0", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"velocity_x0", "cm/s"},
    }}},
    {"DiffusionMode", {"simulation/DiffusionMode/DiffusionMode.cpp", "07e253d8d7ede7f42fb65531b6298c0c1dd27ad0f9b701ce2f57a48694383e26", {
        {"rho0", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"tracer_mean", "1"},
        {"tracer_amplitude", "1"},
        {"mode", "1"},
        {"mode_axis", "1"},
    }}},
    {"BurnOneZone", {"simulation/BurnOneZone/BurnOneZone.cpp", "52cb9928a8afcd8437d6c3b99149112bd310bbb9cbe47ea7d034bd6e41b503c3", {
        {"rho0", "g/cm^3"},
        {"temperature0", "K"},
    }}},
    {"BurnGradient", {"simulation/BurnGradient/BurnGradient.cpp", "4171da69285643702258f2ae4758fc0519dba7cadda40804f3aefff09b5055c8", {
        {"rho0", "g/cm^3"},
        {"background_temperature", "K"},
        {"peak_temperature", "K"},
        {"center_x", "cm"},
        {"width", "cm"},
    }}},
    {"CooperativeHotspots", {"simulation/CooperativeHotspots/CooperativeHotspots.cpp", "3fe9f8154ecbcf3e3b76b43e3d476efe6c3294cc473fe080826142b72a50a18b", {
        {"ambient_density", "g/cm^3"},
        {"ambient_temperature", "K"},
        {"hotspot_temperature", "K"},
        {"hotspot_center_x", "cm"},
        {"hotspot_center_y", "cm"},
        {"hotspot_radius", "cm"},
        {"hotspot_separation", "cm"},
    }}},
    {"UserBoundary", {"simulation/UserBoundary/case.cpp", "e1e8e2ba03f262bf1b0d77158815cc849f5d92186e24209a063391fb64cd362e", {
        {"user_boundary_heat_flux", "erg/(cm^2 s)"},
    }}},
    {"UserGravity", {"simulation/UserGravity/case.cpp", "8d0254f84d93ffdf62ff42ed677c5d2181bac326a35a9f9f64c02e2b3a285a69", {
        {"user_boundary_heat_flux", "erg/(cm^2 s)"},
    }}},
    {"SNIaCoupled", {"simulation/SNIaCoupled/SNIaCoupled.cpp", "da2e7d0b914ccb55ec450a5f2476390d5b8f78cb6a16544675ad63c45dafdb78", {
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
        {"automaticExpressionInference", false},
#ifdef _WIN32
        {"workerPlatform", "windows"}, {"resourcePolicy", WindowsPreviewResourcePolicy()},
#else
        {"workerPlatform", "linux"},
#endif
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
