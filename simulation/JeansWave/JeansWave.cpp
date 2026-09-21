/** Stable linear Jeans plane wave in CGS; small finite amplitude also measures
 * nonlinear truncation. Boundary, force and evolution use production owners.
 */
#include <UserInterface.h>
#include <GlobalDefs.h>
#include "physics/constant/PhysicalConstants.h"
#include <cmath>
#include <stdexcept>
class JeansWaveProblem {
    double rho_=1e7, pressure_=6e6, amplitude_=1e-4, velocity_=0.;
    double phase_=0.;
    double gamma_=5./3.;
    double x_min_=0., wave_number_=0., average_=1.;
public:
    void Setup(SimConfig& config, SpeciesManager&) {
        if(config.grid.geometry!="cartesian" || config.physics.eos_type!="ideal" || config.physics.gravity.type!="self")
            throw std::invalid_argument("JeansWave requires Cartesian ideal-gas self gravity");
        phase_=config.Get<double>("phase",0.);
        if(!std::isfinite(phase_)) throw std::invalid_argument("JeansWave phase must be finite");
        gamma_=config.physics.gamma;
        rho_=config.Get<double>("rho0",1e7); pressure_=config.Get<double>("pressure0",6e6);
        amplitude_=config.Get<double>("amplitude",1e-4);
        const int mode=config.Get<int>("mode",1);
        if (!(rho_>0.) || !(pressure_>0.) || !(amplitude_>0. && amplitude_<0.1) || mode<1)
            throw std::invalid_argument("JeansWave requires positive background, mode and small amplitude");
        x_min_=config.grid.x1_min;
        wave_number_=2.*arch::constants::math::pi*mode/(config.grid.x1_max-x_min_);
        const double frequency_squared=config.physics.gamma*pressure_/rho_*wave_number_*wave_number_
            -4.*arch::constants::math::pi*config.physics.gravity.G_const*rho_;
        if (!(frequency_squared>0.)) throw std::invalid_argument("JeansWave selects the stable oscillatory branch");
        velocity_=std::sqrt(frequency_squared)/wave_number_*amplitude_;
        const double phase=0.5*wave_number_*ProblemHelper::GetRootCellWidth(config,1);
        average_=std::sin(phase)/phase;
    }
    void Init(const PointCoords& point,PrimitiveData& state) const {
        const double mode=average_*std::cos(wave_number_*(point.x-x_min_)+phase_);
        state.rho=rho_*(1.+amplitude_*mode);
        state.p=pressure_*(1.+gamma_*amplitude_*mode);
        state.u=velocity_*mode;state.v=0.;state.w=0.;
    }
};
REGISTER_PROBLEM_CLASS("JeansWave",JeansWaveProblem);
