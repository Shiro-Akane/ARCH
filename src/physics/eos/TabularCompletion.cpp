/** @file TabularCompletion.cpp
 *  @brief Component-aware table assembly through existing Timmes math.
 */
#include "TabularCompletion.h"
#include "HelmEos.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace tabular_eos {

std::vector<double> uniform_axis(int count, double minimum, double maximum)
{
    if (count < 2 || !std::isfinite(minimum) || !std::isfinite(maximum)
        || !(maximum > minimum))
        throw std::runtime_error("Invalid component-completion axis");
    std::vector<double> values(count);
    const double spacing = (maximum-minimum)/(count-1);
    for (int i=0; i<count; ++i) values[i]=minimum+i*spacing;
    values.back()=maximum;
    return values;
}

double require_uniform_axis(const std::vector<double>& values,const char* name)
{
    if (values.size()<5) throw std::runtime_error(std::string(name)+" needs at least five nodes");
    const double spacing=(values.back()-values.front())/(values.size()-1);
    if (!(spacing>0.0) || !std::isfinite(spacing))
        throw std::runtime_error(std::string(name)+" is not a valid increasing axis");
    for (std::size_t i=0;i<values.size();++i) {
        const double expected=values.front()+i*spacing;
        if (!std::isfinite(values[i]) || std::abs(values[i]-expected)>
            128*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(expected)))
            throw std::runtime_error(std::string(name)+" must be uniform for the free-energy derivative contract");
    }
    return spacing;
}

void complete_free_energy(std::vector<double>& potential,
    std::vector<double>& validity, const std::vector<double>& log_density,
    const std::vector<double>& log_temperature,
    const std::vector<double>& electron_fraction,
    const TabularComponents& present, const std::string& helm_path,
    double baryon_mass_g,std::vector<double>* density_derivative,
    std::vector<double>* temperature_derivative)
{
    const auto nr=log_density.size(), nt=log_temperature.size(), nc=electron_fraction.size();
    if (nr<5 || nt<5 || nc==0 || potential.size()!=nr*nt*nc)
        throw std::runtime_error("Component completion requires a complete free-energy grid");
    if (validity.empty()) validity.assign(potential.size(),1.0);
    if (validity.size()!=potential.size())
        throw std::runtime_error("Component completion validity shape mismatch");
    for (const auto* seed : {density_derivative,temperature_derivative})
        if (seed && seed->size()!=potential.size())
            throw std::runtime_error("Component completion derivative shape mismatch");
    if (!present.needs_completion()) return;
    if (baryon_mass_g<0.0 || !std::isfinite(baryon_mass_g))
        throw std::runtime_error("Invalid source baryon mass convention");
    const double mass_scale=baryon_mass_g>0.0
        ? 1.0/(baryon_mass_g*HelmEosHostView::avo) : 1.0;
    if (!(mass_scale>0.0) || !std::isfinite(mass_scale))
        throw std::runtime_error("Unrepresentable electron density conversion");

    std::unique_ptr<HelmEos> electron_owner;
    if (!present.electrons_positrons) {
        if (helm_path.empty())
            throw std::runtime_error("Missing electron/positron component requires eos_helm_table_path");
        electron_owner=std::make_unique<HelmEos>(helm_path,nullptr);
    }
    const HelmEosHostView component=electron_owner ? electron_owner->get_view() : HelmEosHostView{};
    std::size_t excluded=0;
    for (std::size_t r=0; r<nr; ++r) for (std::size_t t=0; t<nt; ++t) {
        const double rho=std::pow(10.0,log_density[r]);
        const double temperature=std::pow(10.0,log_temperature[t]);
        for (std::size_t c=0; c<nc; ++c) {
            const auto index=(r*nt+t)*nc+c;
            if (validity[index]!=1.0) continue;
            arch::math::CompensatedSum sum;
            sum.add(potential[index]);
            HelmEosHostView::ComponentThermodynamics part{};
            bool ok=std::isfinite(potential[index]);
            if (!present.electrons_positrons) {
                ok=ok && component.electron_positron_component(
                    rho*mass_scale,temperature,electron_fraction[c],part);
                if (ok) {
                    sum.add(part.free_energy*mass_scale);
                    if (density_derivative) (*density_derivative)[index]+=part.pressure/rho;
                    if (temperature_derivative)
                        (*temperature_derivative)[index]+=-temperature*part.entropy*mass_scale;
                }
            }
            if (!present.photons) {
                ok=ok && component.photon_component(rho,temperature,part);
                if (ok) {
                    sum.add(part.free_energy);
                    if (density_derivative) (*density_derivative)[index]+=part.pressure/rho;
                    if (temperature_derivative)
                        (*temperature_derivative)[index]+=-temperature*part.entropy;
                }
            }
            if (!ok || !std::isfinite(sum.value())) {
                validity[index]=0.0;
                // Derivatives are computed on finite storage, but their full
                // dependency mask excludes this placeholder and its stencil.
                potential[index]=0.0;
                if (density_derivative) (*density_derivative)[index]=0.0;
                if (temperature_derivative) (*temperature_derivative)[index]=0.0;
                ++excluded;
            } else potential[index]=sum.value();
        }
    }
    std::cout << "[Tabular EOS] Completing missing components: electrons/positrons="
              << (!present.electrons_positrons) << ", photons=" << (!present.photons)
              << "; " << excluded << " source nodes outside component support. "
              << "Affected derivative stencils are excluded, never extrapolated.\n";
}

namespace {
std::vector<double> derivative_mask(const std::vector<double>& input,
    int nr,int nt,int nc,bool density)
{
    std::vector<double> output(input.size(),1.0);
    const int count=density ? nr : nt;
    for (int r=0;r<nr;++r) for (int t=0;t<nt;++t) for (int c=0;c<nc;++c) {
        const int position=density ? r : t;
        const int first=derivative_stencil_begin(position,count);
        const std::size_t target=(static_cast<std::size_t>(r)*nt+t)*nc+c;
        for (int k=first;k<first+5;++k) {
            const std::size_t source=(static_cast<std::size_t>(density?k:r)*nt+(density?t:k))*nc+c;
            if (input[source]!=1.0) output[target]=0.0;
        }
    }
    return output;
}
} // namespace

std::vector<double> free_energy_derivative_validity(
    const std::vector<double>& input,int nr,int nt,int nc)
{
    if (nr<5 || nt<5 || nc<1 || input.size()!=static_cast<std::size_t>(nr)*nt*nc)
        throw std::runtime_error("Invalid free-energy validity grid");
    // Fxxyy has the union of every lower-order field's dependencies. Include
    // the center even where the centered derivative coefficient is zero.
    auto result=derivative_mask(input,nr,nt,nc,true);
    result=derivative_mask(result,nr,nt,nc,true);
    result=derivative_mask(result,nr,nt,nc,false);
    return derivative_mask(result,nr,nt,nc,false);
}

double positive_energy_reference(
    const std::array<std::vector<double>, FieldCount>& fields,
    const std::vector<double>& validity)
{
    double minimum=std::numeric_limits<double>::infinity();
    for (std::size_t i=0;i<validity.size();++i) if (validity[i]==1.0) {
        const double energy=fields[F][i]-fields[Fy][i];
        if (!std::isfinite(energy)) throw std::runtime_error("Nonfinite completed free-energy field");
        minimum=std::min(minimum,energy);
    }
    if (!std::isfinite(minimum))
        throw std::runtime_error("No valid component-completed thermodynamic stencil remains");
    const double shift=minimum>0.0 ? 0.0 : std::max(1.0,-2.0*minimum);
    if (!std::isfinite(shift)) throw std::runtime_error("Unrepresentable constant EOS energy reference");
    return shift;
}
} // namespace tabular_eos
