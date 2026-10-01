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
    {"Sod", {"simulation/Sod/Sod.cpp", "7867565cdbe801769c0860de8d4a41254aaaa7be354951bcab69dd042a242738", {
        {"x_pos", "cm"},
        {"rho_left", "g/cm^3"},
        {"rho_right", "g/cm^3"},
        {"p_left", "erg/cm^3"},
        {"p_right", "erg/cm^3"},
        {"u_left", "cm/s"},
        {"u_right", "cm/s"},
    }}},
    {"CellularDet", {"simulation/Cellular/Cellular.cpp", "afc2ae1c484def0d23e2f18c00da952cd886e1edf1883081bdc944874c4b9a01", {
        {"rhoAmbient", "g/cm^3"},
        {"tempAmbient", "K"},
        {"rhoPerturb", "g/cm^3"},
        {"tempPerturb", "K"},
        {"velxPerturb", "cm/s"},
        {"radiusPerturb", "cm"},
        {"noiseAmplitude", "1"},
        {"shock_dir", "1"},
    }}},
    {"Gaussian", {"simulation/GaussianPulse/Gaussian.cpp", "b853316d7300ccd7fc69c5b5290d95c53055451837185073d0ea54c1a81028ff", {
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
    {"Sedov", {"simulation/Sedov/Sedov.cpp", "4f91941fabb5183233b7b211f1ff61eccaefa1496c4d1e161fe214b2ce3bbae6", {
        {"center_x", "cm"},
        {"center_y", "cm"},
        {"center_z", "cm"},
        {"deposit_radius", "cm"},
        {"ambient_density", "g/cm^3"},
        {"ambient_pressure", "erg/cm^3"},
        {"explosion_energy", "dimension-dependent-energy"},
    }}},
    {"RT", {"simulation/RTinstability/RT_instab.cpp", "d0fcfcc78136d34dea31fff799892ac2c4ddaefcf1f60d5e4be33307b63b515a", {
        {"rho_heavy", "g/cm^3"},
        {"rho_light", "g/cm^3"},
        {"y_int", "cm"},
        {"p_int", "erg/cm^3"},
        {"amplitude", "cm/s"},
    }}},
    {"SmoothAdvection", {"simulation/SmoothAdvection/SmoothAdvection.cpp", "addbe6ea60478e85ec3268740f17307f36c009c36a16b5bd40a9f2cf0c6e2210", {
        {"rho_mean", "g/cm^3"},
        {"rho_amplitude", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"velocity0", "cm/s"},
        {"mode", "1"},
    }}},
    {"GravityBox", {"simulation/GravityBox/GravityBox.cpp", "c594aed5e0a2bb24f09e351a0874e8fe4eac0a39747d0bd05e85c51800971b14", {
        {"rho0", "g/cm^3"}, {"temperature0", "K"}, {"amplitude", "1"},
        {"temperature_amplitude", "1"}, {"velocity0", "cm/s"}, {"width", "cm"},
        {"center_x", "cm"}, {"center_y", "cm"}, {"center_z", "cm"}, {"gas_cv", "erg/(g*K)"},
    }}},
    {"JeansWave", {"simulation/JeansWave/JeansWave.cpp", "5f29312fa5cf0d729ee8be9bf99c89e10c71eabc8c8ef0255cbd730ddc44aa9b", {
        {"rho0", "g/cm^3"}, {"pressure0", "erg/cm^3"},
        {"amplitude", "1"}, {"phase", "rad"}, {"mode", "1"},
        {"standing_wave", "1"},
    }}},
    {"ExternalGravity", {"simulation/ExternalGravity/ExternalGravity.cpp", "198e1be5635176864163fa27ea14dcaa383f10f4173f44fa23b2559e7eae4d71", {
        {"rho0", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"velocity_x0", "cm/s"},
    }}},
    {"DiffusionMode", {"simulation/DiffusionMode/DiffusionMode.cpp", "6f8cff66d7f5c995e6617260182b5997b3d82d5cb4daddb96b2b9811aa791c4b", {
        {"rho0", "g/cm^3"},
        {"pressure0", "erg/cm^3"},
        {"tracer_mean", "1"},
        {"tracer_amplitude", "1"},
        {"mode", "1"},
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
    {"CooperativeHotspots", {"simulation/CooperativeHotspots/CooperativeHotspots.cpp", "dd6b0a29ee35da013ce712892a1c8de73e5c5e5124ff0f2efcbf950a3650698d", {
        {"ambient_density", "g/cm^3"},
        {"ambient_temperature", "K"},
        {"hotspot_temperature", "K"},
        {"hotspot_center_x", "cm"},
        {"hotspot_center_y", "cm"},
        {"hotspot_radius", "cm"},
        {"hotspot_separation", "cm"},
    }}},
    {"SNIaCoupled", {"simulation/SNIaCoupled/SNIaCoupled.cpp", "2fe391554874d95e3802f4f7b5d66565f3afd5cf488da4b2a8c14b4d80e58a6a", {
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
