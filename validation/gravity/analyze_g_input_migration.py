"""Read-only algebra for maintainer review of retired-G inputs.

No ARCH/FLASH execution, parameter-file writes, budget changes or approval.
The radial similarity applies to the ideal-gas Euler-Poisson equations only;
unchanged absolute numerical controls do not imply discrete equivalence.
"""
import ast
import hashlib
import json
import math
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def params(relative):
    result = {}
    for line in (ROOT / relative).read_text().splitlines():
        raw = line.split("#", 1)[0].strip()
        if not raw:
            continue
        key, value = raw.split("=", 1)
        key = key.strip()
        if key in result:
            raise ValueError("Duplicate analysis input: " + key)
        result[key] = value.strip()
    return result


def analyze():
    constant_path = "src/physics/constant/PhysicalConstants.h"
    match = re.search(r"gravitational_constant\s*=\s*([0-9.eE+-]+)\s*;",
                      (ROOT / constant_path).read_text())
    if not match:
        raise ValueError("Shared G declaration unavailable")
    new_g = float(match.group(1))
    radial_path = "validation/gravity/radial_1d.py"
    tree = ast.parse((ROOT / radial_path).read_text())
    constants = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    constants[target.id] = ast.literal_eval(node.value)
    cls = next(n for n in tree.body if isinstance(n, ast.ClassDef)
               and n.name == "RadialCampaign")
    method = next(n for n in cls.body if isinstance(n, ast.FunctionDef)
                  and n.name == "regrid_cycle")
    call = next(n for n in ast.walk(method) if isinstance(n, ast.Call)
                and isinstance(n.func, ast.Attribute) and n.func.attr == "run")
    controls = {k.arg: constants[k.value.id] if isinstance(k.value, ast.Name)
                else ast.literal_eval(k.value) for k in call.keywords}
    base = params("simulation/GravityBox/GravityBox.par")
    if base["eos_type"] != "ideal" or base["network_name"] != "none":
        raise ValueError("Ideal-gas/no-network similarity assumptions changed")
    baseline = "8fc0dd25eefd2243e8c36f85440bac46994e2e73"
    old_model = subprocess.check_output(
        ["git", "show", baseline + ":simulation/GravityBox/GravityBox.cpp"],
        cwd=ROOT)
    old_par = subprocess.check_output(
        ["git", "show", baseline + ":simulation/GravityBox/GravityBox.par"],
        cwd=ROOT)
    if b"radial?0.:" not in old_model or re.search(rb"^center_x\s*=", old_par, re.M):
        raise ValueError("Historical radial center assumptions changed")
    old_g = controls["gravity_G"]
    rho, length, temperature = controls["rho0"], constants["RADIUS"], controls["temperature0"]
    gamma, cv = float(base["gamma"]), float(base["gas_cv"])
    sound2 = gamma * (gamma - 1) * cv * temperature
    q = new_g / old_g
    b = old_g / new_g
    radial = {
        "oldG": old_g, "newG": new_g, "GFactor": q,
        "historicalCenterEvidence": {
            "baselineCommit": baseline,
            "modelBlobSHA256": hashlib.sha256(old_model).hexdigest(),
            "parameterBlobSHA256": hashlib.sha256(old_par).hexdigest(),
            "historicalRadialDefaultCenterX": 0,
            "currentCommonParameterCenterX": float(base["center_x"]),
            "equivalentProfileNeedsExplicitHistoricalCenter": True,
            "inputNotChanged": True},
        "tmaxIsCeilingNotObservedEndpoint": True,
        "actualHistoricalAndNewEndpointNeedEvidence": True,
        "old": {"rho0": rho, "domainRadius": length, "width": controls["width"],
                "temperature0": temperature, "gamma": gamma, "gas_cv": cv,
                "tmax": controls["tmax"], "max_steps": controls["max_steps"],
                "soundSpeedSquared": sound2,
                "domainGravityStrength": old_g * rho * length**2 / sound2,
                "acousticTime": length / math.sqrt(sound2),
                "gravityTimeConvention": "1/sqrt(G*rho), without geometry prefactor",
                "gravityTime": 1 / math.sqrt(old_g * rho)},
        "candidateDensityOnly_UNAPPROVED": {
            "lengthFactor": 1, "velocityFactor": 1, "timeFactor": 1,
            "densityAndPressureFactor": b, "rho0": b * rho,
            "backgroundPressureOld": (gamma - 1) * rho * cv * temperature,
            "backgroundPressureNew": (gamma - 1) * b * rho * cv * temperature,
            "temperature0": temperature, "tmax": controls["tmax"],
            "domainGravityStrength": new_g * b * rho * length**2 / sound2,
            "gravityTime": 1 / math.sqrt(new_g * b * rho),
            "G_rho_roundoffRelative": (new_g * b * rho) / (old_g * rho) - 1,
            "unchangedDensityFloor": float(base["sml_rho"]),
            "floorOverBackgroundDensityOld": float(base["sml_rho"]) / rho,
            "floorOverBackgroundDensityNew": float(base["sml_rho"]) / (b * rho),
            "discreteEquivalenceProved": False},
        "lengthOnly_NOT_SELECTED": {
            "lengthFactor": 1 / math.sqrt(q), "newRadius": length / math.sqrt(q),
            "newWidth": controls["width"] / math.sqrt(q),
            "timeFactor": 1 / math.sqrt(q), "newTmax": controls["tmax"] / math.sqrt(q),
            "samePhysicalEndpoint": False},
        "temperatureOnly_NOT_SELECTED": {
            "velocityFactor": math.sqrt(q), "newTemperature": q * temperature,
            "timeFactor": 1 / math.sqrt(q), "newTmax": controls["tmax"] / math.sqrt(q),
            "sameEOSPhysicalRegimeConfirmed": False},
        "thresholdsNotChanged": {
            "gaussRelativeErrorLessThan": 1e-7,
            "massAndEnergyRelativeDriftLessThan": 1e-12,
            "topology": ["refine", "coarsen", "no-change"],
            "floorRepairEvents": 0},
    }
    jeans = []
    for relative in ["validation/gravity/flash/arch_jeans_o6plus.par",
                     "validation/gravity/flash/arch_jeans_o6plus_128.par"]:
        values = params(relative)
        rho, pressure = float(values["rho0"]), float(values["pressure0"])
        length = float(values["x1_max"]) - float(values["x1_min"])
        k = 2 * math.pi * int(values["mode"]) / length
        sound_term = float(values["gamma"]) * pressure / rho * k**2
        historic_g, end = float(values["gravity_G"]), float(values["tmax"])
        old_omega2, new_omega2 = (sound_term - 4 * math.pi * g * rho
                                for g in [historic_g, new_g])
        if min(old_omega2, new_omega2) <= 0:
            raise ValueError("Stable standing-wave branch changed")
        old_omega, new_omega = math.sqrt(old_omega2), math.sqrt(new_omega2)
        jeans.append({
            "input": relative, "oldG": historic_g, "newG": new_g,
            "relativeGChange": new_g / historic_g - 1,
            "rho0": rho, "pressure0": pressure, "domainLength": length,
            "mode": int(values["mode"]), "tmax": end,
            "oldOmegaSquared": old_omega2, "newOmegaSquared": new_omega2,
            "oldOmega": old_omega, "newOmega": new_omega,
            "relativeOmegaChange": new_omega / old_omega - 1,
            "phaseChangeAtTmaxRadians": (new_omega - old_omega) * end,
            "frequencySensitivityOnly": True,
            "flashBuiltGConfirmed": False,
            "referenceOrInputsChanged": False,
        })
    sources = [constant_path, radial_path, "simulation/GravityBox/GravityBox.par",
               "simulation/GravityBox/GravityBox.cpp", "simulation/JeansWave/JeansWave.cpp",
               "validation/gravity/flash/compare_jeans_o6plus.py",
               "validation/gravity/flash/arch_jeans_o6plus.par",
               "validation/gravity/flash/arch_jeans_o6plus_128.par"]
    return {
        "status": "UNAPPROVED_ANALYSIS_ONLY",
        "sourceSHA256": {s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest()
                         for s in sources},
        "similarityCondition": "Gnew/Gold * densityFactor * lengthFactor^2 = velocityFactor^2",
        "radial": radial, "jeans": jeans,
        "noARCHorFLASHRun": True, "noScientificInputModified": True,
        "noThresholdModified": True, "oldResultsRemainHistorical": True,
    }


if __name__ == "__main__":
    print(json.dumps(analyze(), ensure_ascii=False, indent=2, allow_nan=False))
