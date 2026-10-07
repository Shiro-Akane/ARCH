/**
 * @file SNIaCoupled.cpp
 * @brief C/O ignition patch exercising hydro, self gravity, burn and diffusion in 2D/3D.
 *
 * Workflow:
 * 1. Setup checks a supported 2D/3D self-gravity domain, Helmholtz EOS,
 *    active nuclear network and thermal diffusion configuration.
 * 2. Init supplies a smooth, finite C/O density and temperature hotspot.
 * 3. The normal Driver performs hydro, Poisson self-gravity, burning and
 *    diffusion. The case adds no private numerical implementation.
 *
 * This is a centimetre-scale coupled execution example, not a stellar
 * white-dwarf model. Cartesian 2D periodic gravity has translation-invariant
 * source units. Cylindrical 2D uses an axisymmetric full-ring (r,z) domain;
 * its isolated potential and all 3D isolated runs have finite Newtonian mass.
 * Scientific/backend qualification is owned by Core, not this initializer.
 */

#include <cmath>
#include <stdexcept>
#include <vector>

// Stable case API; GlobalDefs also exports the shared CGS constants.
#include <GlobalDefs.h>
#include <UserInterface.h>

class SNIaCoupledProblem
{
    double density_ = 1.0e7;
    double background_temperature_ = 1.0e9;
    double peak_temperature_ = 3.0e9;
    double density_amplitude_ = 0.01;
    double width_ = 0.15;
    double center_x_ = 0.5;
    double center_y_ = 0.5;
    double center_z_ = 0.5;
    int dimension_ = 2;
    bool axisymmetric_rz_ = false;
    std::vector<double> fractions_;

public:
    static arch::config::CaseConfiguration DescribeConfiguration(
        const arch::config::StandardInputResolution& inputs)
    {
        arch::config::CaseConfiguration result;
        result.complete = true;
        result.consumers.needs_network = true;
        result.consumers.needs_temperature_floor = false;
        result.composition = arch::config::DescribeNetworkComposition(inputs);
        if (result.composition->complete)
            result.consumers.needs_composition_floor = !result.composition->keys.empty();
        result.parameters = {
            {"rho0", "float", "g/cm^3"},
            {"temperature0", "float", "K"},
            {"temperature_peak", "float", "K"},
            {"density_amplitude", "float", "1"},
            {"hotspot_width", "float", "cm"},
            {"center_x", "float", "cm"},
            {"center_y", "float", "cm"},
            {"center_z", "float", "cm"}};
        return result;
    }

    /** Validate the four-module contract and load the network composition. */
    void Setup(SimConfig& config, SpeciesManager& species)
    {
        const bool curved=config.grid.geometry=="cylindrical"
            || config.grid.geometry=="spherical";
        if (config.grid.dim<2 || config.grid.dim>3
            || (config.grid.geometry!="cartesian" && !curved)
            || config.physics.gravity.type != "self"
            || (curved && config.physics.gravity.boundary!="isolated")
            || config.physics.eos_type != "helmholtz"
            || !config.physics.burn.use_burn
            || (config.physics.burn.network_name != "aprox13"
                && config.physics.burn.network_name != "aprox19")
            || !config.physics.diffusion.use_diffusion
            || !config.physics.diffusion.use_thermal_diffusion)
            throw std::invalid_argument(
                "SNIaCoupled requires supported 2D/3D self gravity, Helmholtz EOS, "
                "aprox13/19 burning and thermal diffusion");
        dimension_=config.grid.dim;
        axisymmetric_rz_=GridMetrics::resolve_public_chart(config.grid.geometry,dimension_)
            ==GridMetrics::GeometrySemantics::AxisymmetricRz;
        // Global config validation owns the detailed fluid/gravity face contract.
        // This case only owns the physical initial state.

        density_ = config.Get<double>("rho0", density_);
        background_temperature_ = config.Get<double>("temperature0", background_temperature_);
        peak_temperature_ = config.Get<double>("temperature_peak", peak_temperature_);
        density_amplitude_ = config.Get<double>("density_amplitude", density_amplitude_);
        width_ = config.Get<double>("hotspot_width", width_);
        center_x_ = config.Get<double>("center_x", center_x_);
        center_y_ = config.Get<double>("center_y", center_y_);
        center_z_ = config.Get<double>("center_z", center_z_);
        if (!std::isfinite(density_) || !std::isfinite(background_temperature_)
            || !std::isfinite(peak_temperature_) || !std::isfinite(density_amplitude_)
            || !std::isfinite(width_) || !std::isfinite(center_x_) || !std::isfinite(center_y_)
            || !std::isfinite(center_z_)
            || density_ <= 0.0 || background_temperature_ <= 0.0
            || peak_temperature_ < background_temperature_
            || density_amplitude_ < 0.0 || width_ <= 0.0)
            throw std::invalid_argument("SNIaCoupled requires finite positive hotspot inputs");

        // An RZ hotspot is a ring about the z axis, not an off-axis 3D sphere.
        // center_x is its radial centre and center_z its axial centre; center_y
        // must vanish because this chart has no independent azimuthal offset.
        if (axisymmetric_rz_ && center_y_!=0.)
            throw std::invalid_argument("SNIaCoupled RZ ring requires center_y=0");
        ProblemHelper::SetupNetworkAndFractions(config, species, fractions_);
    }

    /** Supply smooth primitive fields; the shared EOS converts T and X to e. */
    void Init(const PointCoords& point, PrimitiveData& state) const
    {
        const double dx = ((axisymmetric_rz_ ? point.r_cy : point.x)-center_x_)/width_;
        const double dy = axisymmetric_rz_ ? 0. : (point.y-center_y_)/width_;
        const double dz = axisymmetric_rz_
            ? (point.z_cy-center_z_)/width_
            : (dimension_==3 ? (point.z-center_z_)/width_ : 0.);
        // Cartesian/3D: q=exp[-|x-x_c|^2/(2 sigma^2)]. RZ ring:
        // q=exp[-((r-r_c)^2+(z-z_c)^2)/(2 sigma^2)], independent of phi.
        const double hotspot = std::exp(-0.5 * (dx * dx + dy * dy + dz * dz));
        // rho = rho_0 (1 + A q), T = T_0 + (T_peak-T_0) q.
        state.rho = density_ * (1.0 + density_amplitude_ * hotspot);
        state.p = 0.0;
        state.SetTemperature(background_temperature_
                             + (peak_temperature_ - background_temperature_) * hotspot);
        state.u = 0.0;
        state.v = 0.0;
        state.w = 0.0;
        state.mass_fractions = fractions_;
    }
};

REGISTER_PROBLEM_CLASS("SNIaCoupled", SNIaCoupledProblem);
