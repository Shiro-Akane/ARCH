/**
 * @file JeansWave.cpp
 * @brief Stable periodic Jeans mode with an optional standing-wave initial state.
 *
 * Workflow:
 * 1. Read the common CGS background, mode and gravity configuration.
 * 2. Check that the selected mode is on the stable oscillatory branch.
 * 3. Initialize cell-averaged density and pressure. The default traveling
 *    mode also initializes velocity; standing_wave=true starts at rest for
 *    comparison with FLASH 4.8's Jeans initial condition.
 */

#include <cmath>
#include <stdexcept>
#include <string>

#include <UserInterface.h>
#include <GlobalDefs.h>

class JeansWaveProblem {
    double rho_ = 1e7, pressure_ = 6e6, amplitude_ = 1e-4;
    double velocity_ = 0.0, phase_ = 0.0, gamma_ = 5.0 / 3.0;
    double x_min_ = 0.0, wave_number_ = 0.0, average_ = 1.0;

public:
    /** Resolve one stable periodic wave without altering the production gravity solver. */
    void Setup(SimConfig& config, SpeciesManager&) {
        if (config.grid.geometry != "cartesian"
            || config.physics.eos_type != "ideal"
            || config.physics.gravity.type != "self")
            throw std::invalid_argument(
                "JeansWave requires Cartesian ideal-gas self gravity");

        phase_ = config.Get<double>("phase", 0.0);
        if (!std::isfinite(phase_))
            throw std::invalid_argument("JeansWave phase must be finite");
        gamma_ = config.physics.gamma;
        rho_ = config.Get<double>("rho0", 1e7);
        pressure_ = config.Get<double>("pressure0", 6e6);
        amplitude_ = config.Get<double>("amplitude", 1e-4);
        const int mode = config.Get<int>("mode", 1);
        // Custom nonnumeric values are stored as strings by RuntimeParams.
        // Read a textual flag here so standing_wave=true is not silently
        // treated as the default traveling-wave initial state.
        const std::string standing_choice =
            config.Get<std::string>("standing_wave", "false");
        if (standing_choice != "true" && standing_choice != "false")
            throw std::invalid_argument("JeansWave standing_wave must be true or false");
        const bool standing_wave = standing_choice == "true";
        if (!(rho_ > 0.0) || !(pressure_ > 0.0)
            || !(amplitude_ > 0.0 && amplitude_ < 0.1) || mode < 1)
            throw std::invalid_argument(
                "JeansWave requires positive background, mode and small amplitude");

        x_min_ = config.grid.x1_min;
        wave_number_ = 2.0 * arch::constants::math::pi * mode
            / (config.grid.x1_max - x_min_);
        const double frequency_squared =
            config.physics.gamma * pressure_ / rho_ * wave_number_ * wave_number_
            - 4.0 * arch::constants::math::pi
                * config.physics.gravity.G_const * rho_;
        if (!(frequency_squared > 0.0))
            throw std::invalid_argument(
                "JeansWave selects the stable oscillatory branch");
        velocity_ = standing_wave ? 0.0
            : std::sqrt(frequency_squared) / wave_number_ * amplitude_;

        // The cell average of cos(k*x) is sinc(k*dx/2) times its centre value.
        const double half_phase = 0.5 * wave_number_
            * ProblemHelper::GetRootCellWidth(config, 1);
        average_ = std::sin(half_phase) / half_phase;
    }

    /** Supply primitive state at a physical cell centre. */
    void Init(const PointCoords& point, PrimitiveData& state) const {
        const double mode = average_
            * std::cos(wave_number_ * (point.x - x_min_) + phase_);
        state.rho = rho_ * (1.0 + amplitude_ * mode);
        state.p = pressure_ * (1.0 + gamma_ * amplitude_ * mode);
        state.u = velocity_ * mode;
        state.v = 0.0;
        state.w = 0.0;
    }
};

REGISTER_PROBLEM_CLASS("JeansWave", JeansWaveProblem);
