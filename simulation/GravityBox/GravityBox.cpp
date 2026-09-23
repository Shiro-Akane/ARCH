/** CGS gas box: periodic modes or an isolated Gaussian cloud. Physical
 * operators, composition/EOS conversion and AMR use the production interfaces.
 */
#include <UserInterface.h>
#include <GlobalDefs.h>
#include <cmath>
#include <stdexcept>
class GravityBoxProblem {
    double rho_=1e7,temperature_=1e7,amplitude_=1e-3,temperature_amplitude_=0.;
    double velocity_=0.,width_=0.;
    double lower_[3]{},length_[3]{},center_[3]{};
    bool isolated_=false,radial_=false,hydrostatic_radial_=false;
    int dimension_=1;double hydrostatic_drop_=0.;
    std::vector<double> fractions_;
public:
    void Setup(SimConfig& config,SpeciesManager& species) {
        const bool radial=config.grid.dim==1 &&
            (config.grid.geometry=="spherical" || config.grid.geometry=="cylindrical");
        const bool curved=config.grid.geometry=="spherical" || config.grid.geometry=="cylindrical";
        if(config.grid.geometry!="cartesian" && !curved)
            throw std::invalid_argument("GravityBox requires Cartesian, cylindrical or spherical geometry");
        isolated_=config.physics.gravity.boundary=="isolated";dimension_=config.grid.dim;
        radial_=radial;
        const auto hydrostatic=config.Get<std::string>("hydrostatic_radial","false");
        if(hydrostatic!="true" && hydrostatic!="false")
            throw std::invalid_argument("hydrostatic_radial expects true or false");
        hydrostatic_radial_=hydrostatic=="true";
        if(isolated_&&dimension_!=3&&!curved)
            throw std::invalid_argument("Isolated Cartesian GravityBox requires 3D");
        rho_=config.Get<double>("rho0",1e7);temperature_=config.Get<double>("temperature0",1e7);
        amplitude_=config.Get<double>("amplitude",1e-3);
        temperature_amplitude_=config.Get<double>("temperature_amplitude",0.);
        velocity_=config.Get<double>("velocity0",0.);
        lower_[0]=config.grid.x1_min;lower_[1]=config.grid.x2_min;lower_[2]=config.grid.x3_min;
        length_[0]=config.grid.x1_max-lower_[0];length_[1]=config.grid.x2_max-lower_[1];length_[2]=config.grid.x3_max-lower_[2];
        const char* keys[]{"center_x","center_y","center_z"};
        for(int a=0;a<dimension_;++a)
            center_[a]=config.Get<double>(keys[a],radial?0.:
                curved?(a==0?lower_[0]+0.5*length_[0]:0.)
                      :lower_[a]+0.5*length_[a]);
        width_=config.Get<double>("width",0.08*length_[0]);
        for(double v:{rho_,temperature_,amplitude_,temperature_amplitude_,velocity_,width_,center_[0],center_[1],center_[2]})
            if(!std::isfinite(v))throw std::invalid_argument("GravityBox parameters must be finite");
        if(rho_<=0.||temperature_<=0.||width_<=0.||amplitude_<0.||(!isolated_&&amplitude_>=1.)||
            std::abs(temperature_amplitude_)>=1.)throw std::invalid_argument("Invalid GravityBox density, temperature or profile amplitude");
        if(hydrostatic_radial_ && (!radial_ || !isolated_ || lower_[0]!=0. ||
            amplitude_!=0. || velocity_!=0. || temperature_amplitude_!=0.))
            throw std::invalid_argument("Radial hydrostatic reference requires an origin-centered uniform resting gas");
        config.physics.burn.network_name=config.Get<std::string>("network_name",config.physics.burn.use_burn?"aprox13":"none");
        ProblemHelper::SetupNetworkAndFractions(config,species,fractions_);
        if(species.count()==0){const double cv=config.Get<double>("gas_cv",1.2471693927e8);
            if(!std::isfinite(cv)||cv<=0.)throw std::invalid_argument("GravityBox gas_cv must be finite and positive");
            species.add_species("gas",1.,0.,config.physics.gamma,cv);fractions_={1.};
            if(hydrostatic_radial_) {
                const double dimension=config.grid.geometry=="spherical"?3.:2.;
                // dP/dr=-rho*4*pi*G*rho*r/d and
                // P=(gamma-1)*rho*cv*T for the single ideal-gas species.
                hydrostatic_drop_=2.*arch::constants::math::pi*config.physics.gravity.G_const*rho_/
                    (dimension*(config.physics.gamma-1.)*cv);
                const double outer_ghost=length_[0]*
                    (1.+4./(config.grid.nblockx1*amr::BLOCK_NX));
                if(!std::isfinite(hydrostatic_drop_) ||
                    temperature_<=hydrostatic_drop_*outer_ghost*outer_ghost)
                    throw std::invalid_argument("Radial hydrostatic temperature becomes nonpositive in the outer ghost region");
            }
        } else if(hydrostatic_radial_)
            throw std::invalid_argument("Radial hydrostatic reference requires the single ideal-gas species");
    }
    void Init(const PointCoords& p,PrimitiveData& state) const {
        double mode;
        if(isolated_){const double coords[]{p.x,p.y,p.z};double r2=0.;
            for(int a=0;a<dimension_;++a){const double q=(coords[a]-center_[a])/width_;r2+=q*q;}
            mode=std::exp(-0.5*r2);
        }else mode=std::cos(2.*arch::constants::math::pi*(p.x-lower_[0])/length_[0]);
        state.rho=rho_*(1.+amplitude_*mode);state.p=0.;
        state.SetTemperature(hydrostatic_radial_ ? temperature_-hydrostatic_drop_*p.x*p.x
            : temperature_*(1.+temperature_amplitude_*mode));
        state.u=velocity_*std::sin(2.*arch::constants::math::pi*(p.x-lower_[0])/length_[0]);state.v=0.;state.w=0.;
        state.mass_fractions=fractions_;
    }
};
REGISTER_PROBLEM_CLASS("GravityBox",GravityBoxProblem);
