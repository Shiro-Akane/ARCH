#pragma once
#include "core/ArchPortability.h"
namespace Physical::Gravity {
struct GravityPatchView {
    const double* density=nullptr;
    const double* faces[3]{};
    ARCH_INLINE bool enabled() const {return density!=nullptr;}
};
ARCH_INLINE double gravity_momentum(double low,double high,double rho,double dt) {
    return dt*rho*0.5*(low+high);
}
ARCH_INLINE double gravity_flux_work(double low,double high,double flux_low,double flux_high,double dt) {
    return 0.5*dt*(flux_low*low+flux_high*high);
}
}
