#include "core/RuntimeParams.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{
class TemporaryParameterFile
{
public:
    explicit TemporaryParameterFile(const std::string& contents)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("arch_runtime_params_" + std::to_string(stamp) + ".par");
        std::ofstream output(path_);
        if (!output || !(output << contents))
            throw std::runtime_error("Could not create temporary parameter file.");
    }

    ~TemporaryParameterFile()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main()
{
    const TemporaryParameterFile mixed_case(
        "EntropyFix = FaLsE\n"
        "use_burn = TrUe\n"
        "use_nse = FALSE\n"
        "enforce_mass_conservation = tRuE\n"
        "ode_use_numerical_jac = TRUE\n"
        "ode_freeze_jacobian = false\n"
        "use_diffusion = TRUE\n"
        "use_thermal_diff = fAlSe\n"
        "use_viscous_diff = True\n"
        "use_species_diff = FALSE\n"
        "restart = tRuE\n"
        "restart_file = checkpoint.h5\n");

    const SimConfig config = RuntimeParams::Load(mixed_case.path().string());
    require(config.numerics.entropy_fix_coeff == 0.0, "EntropyFix did not parse false.");
    require(config.physics.burn.use_burn, "use_burn did not parse true.");
    require(!config.physics.burn.use_nse, "use_nse did not parse false.");
    require(config.physics.burn.enforce_mass_conservation,
            "enforce_mass_conservation did not parse true.");
    require(config.physics.burn.odeconfig.use_numerical_jacobian,
            "ode_use_numerical_jac did not parse true.");
    require(!config.physics.burn.odeconfig.freeze_jacobian,
            "ode_freeze_jacobian did not parse false.");
    require(config.physics.diffusion.use_diffusion,
            "use_diffusion did not parse true.");
    require(!config.physics.diffusion.use_thermal_diffusion,
            "use_thermal_diff did not parse false.");
    require(config.physics.diffusion.use_viscous_diffusion,
            "use_viscous_diff did not parse true.");
    require(!config.physics.diffusion.use_species_diffusion,
            "use_species_diff did not parse false.");
    require(config.io.restart && config.io.restart_file == "checkpoint.h5",
            "restart did not parse true.");

    const TemporaryParameterFile invalid_numeric("use_burn = 1\n");
    try
    {
        (void)RuntimeParams::Load(invalid_numeric.path().string());
        throw std::runtime_error("Numeric Boolean input was accepted.");
    }
    catch (const std::invalid_argument& error)
    {
        require(std::string(error.what()).find("use_burn") != std::string::npos,
                "Invalid Boolean error omitted the parameter name.");
    }

    return 0;
}
