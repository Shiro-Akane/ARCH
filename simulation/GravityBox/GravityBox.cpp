/** CGS gas box: periodic modes or an isolated Gaussian cloud.
 *
 * Workflow:
 * 1. DescribeConfiguration(): require physical profile centres for the chart.
 * 2. Setup(): validate the profile and load composition through common owners.
 * 3. Init(): evaluate the ordinary Cartesian-point Gaussian or periodic mode.
 * A cylindrical meridian is embedded by Grid as (r,0,z), so its isolated
 * profile retains all three Cartesian coordinates with centre_y fixed to zero.
 * Geometry, gravity operators, EOS conversion and AMR retain their own owners.
 */
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <GlobalDefs.h>
#include <UserInterface.h>
class GravityBoxProblem {
    // Construction seeds/audited example scales: rho in g/cm^3, T in K and
    // perturbations dimensionless. Setup reads the explicitly required input;
    // these values do not authorize a missing-key fallback.
    double rho_=1e7,temperature_=1e7,amplitude_=1e-3,temperature_amplitude_=0.;
    double velocity_=0.,width_=0.;
    double lower_[3]{},length_[3]{},center_[3]{};
    bool isolated_=false,radial_=false,hydrostatic_radial_=false;
    int dimension_=1,profile_dimension_=1;double hydrostatic_drop_=0.;
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
            {"amplitude", "float", "1"},
            {"temperature_amplitude", "float", "1"},
            {"velocity0", "float", "cm/s"},
            {"width", "float", "cm"},
            {"center_x", "float", "cm"},
            {"center_y", "float", "cm"},
            {"center_z", "float", "cm"},
            {"gas_cv", "float", "erg/(g*K)"},
            {"hydrostatic_radial", "string", "1", "verification", {true, {}}, {"true", "false"}}};
        for (auto& parameter : result.parameters) {
            if (parameter.key == "gas_cv")
                parameter.requirement = result.composition->complete
                    ? arch::config::ConditionResult{result.composition->keys.size() < 1, {}}
                    : arch::config::ConditionResult{std::nullopt, {"network_name"}};
            if (parameter.key == "center_y")
                parameter.requirement = arch::config::input_detail::condition(
                    arch::config::InputCondition::Axis2, inputs, result.consumers);
            if (parameter.key == "center_z") {
                // x2 of a cylindrical meridian is physical z, even though the
                // third computational direction is inactive. No fallback centre.
                using namespace arch::config::input_detail;
                parameter.requirement = combine(
                    condition(arch::config::InputCondition::Axis3, inputs, result.consumers),
                    combine(choice(inputs,"geometry",{"cylindrical"}),
                        condition(arch::config::InputCondition::Axis2, inputs, result.consumers),true),
                    false);
            }
        }
        return result;
    }

    void Setup(SimConfig& config,SpeciesManager& species) {
        const bool radial=config.grid.dim==1 &&
            (config.grid.geometry=="spherical" || config.grid.geometry=="cylindrical");
        const bool curved=config.grid.geometry=="spherical" || config.grid.geometry=="cylindrical";
        if(config.grid.geometry!="cartesian" && !curved)
            throw std::invalid_argument("GravityBox requires Cartesian, cylindrical or spherical geometry");
        isolated_=config.physics.gravity.boundary=="isolated";dimension_=config.grid.dim;
        const bool meridian=config.grid.geometry=="cylindrical" && dimension_==2;
        // Reuse the 3D physical-point profile; Grid supplies (r,0,z) in 2D.
        profile_dimension_=meridian?3:dimension_;
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
        for(int a=0;a<profile_dimension_;++a)
            center_[a]=config.Get<double>(keys[a],radial?0.:
                curved?(a==0?lower_[0]+0.5*length_[0]:0.)
                      :lower_[a]+0.5*length_[a]);
        if(meridian && center_[1]!=0.)
            throw std::invalid_argument("GravityBox cylindrical meridian requires center_y=0; center_x is radial and center_z is axial");
        width_=config.Get<double>("width",0.08*length_[0]);
        for(double v:{rho_,temperature_,amplitude_,temperature_amplitude_,velocity_,width_,center_[0],center_[1],center_[2]})
            if(!std::isfinite(v))throw std::invalid_argument("GravityBox parameters must be finite");
        if(rho_<=0.||temperature_<=0.||width_<=0.||amplitude_<0.||(!isolated_&&amplitude_>=1.)||
            std::abs(temperature_amplitude_)>=1.)throw std::invalid_argument("Invalid GravityBox density, temperature or profile amplitude");
        if(hydrostatic_radial_ && (!radial_ || !isolated_ || lower_[0]!=0. ||
            amplitude_!=0. || velocity_!=0. || temperature_amplitude_!=0.))
            throw std::invalid_argument("Radial hydrostatic reference requires an origin-centered uniform resting gas");
        // network_name is the already resolved standard input; Setup does not override it.
        ProblemHelper::SetupNetworkAndFractions(config,species,fractions_);
        if(species.count()==0){const double cv=config.Get<double>("gas_cv",1.2471693927e8);
            if(!std::isfinite(cv)||cv<=0.)throw std::invalid_argument("GravityBox gas_cv must be finite and positive");
            species.add_species("gas",config.MaterialConstant(1., "gas.A"),config.MaterialConstant(0., "gas.Z"),config.MaterialInput("gamma"),config.MaterialInput("gas_cv"));fractions_={1.};
            if(hydrostatic_radial_) {
                const double dimension=config.grid.geometry=="spherical"?3.:2.;
                // dP/dr=-rho*4*pi*G*rho*r/d and
                // P=(gamma-1)*rho*cv*T for the single ideal-gas species.
                hydrostatic_drop_=2.*arch::constants::math::pi*arch::constants::gravity::cgs::gravitational_constant*rho_/
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
        if(isolated_){
            const double coords[]{p.x,p.y,p.z};double r2=0.;
            for(int a=0;a<profile_dimension_;++a){const double q=(coords[a]-center_[a])/width_;r2+=q*q;}
            // width_ is the Gaussian standard deviation: exp(-|x-xc|^2/(2 width_^2)).
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
