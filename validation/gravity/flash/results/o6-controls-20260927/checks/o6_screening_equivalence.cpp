#include <algorithm>
#include <cmath>
#include <iostream>
#include "physics/network/timmes_common/ScreeningTimmes.h"
#include "o6_screening_reference.h"
int main() {
    double worst=0; int checks=0;
    constexpr double pairs[][4]={{2,4,2,4},{6,12,6,12},{6,12,8,16},{8,16,2,4},{13,27,1,1},{26,52,2,4},{27,55,1,1}};
    for(double t:{1e6,1e7,1e8,1e9,1e10}) for(double rho:{1e-6,1.,1e4,1e8,1e12})
      for(double z:{2.,6.,14.,26.}) for(const auto& p:pairs) {
        const auto temperature=timmes::Dual<1>::variable(t,0);
        const auto state=timmes::make_screen5_state(temperature,rho,timmes::Dual<1>(z),timmes::Dual<1>(2*z),timmes::Dual<1>(z*z));
        const auto prior=screen5_reference::make_screen5_state(temperature,rho,timmes::Dual<1>(z),timmes::Dual<1>(2*z),timmes::Dual<1>(z*z));
        const auto a=timmes::screen5(state,p[0],p[1],p[2],p[3]);
        const auto b=screen5_reference::screen5(prior,p[0],p[1],p[2],p[3]);
        for(const auto values:{std::array<double,2>{a.value,b.value},std::array<double,2>{a.deriv[0],b.deriv[0]}}) {
          if(!std::isfinite(values[0])||!std::isfinite(values[1]))return 2;
          worst=std::max(worst,std::abs(values[0]-values[1])/std::max(std::abs(values[1]),1e-100)); ++checks;
        }
      }
    std::cout<<"{\"comparisons\":"<<checks<<",\"max_relative_difference\":"<<worst<<"}\n";
    return worst>1e-13;
}
