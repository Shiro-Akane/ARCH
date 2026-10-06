#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "physics/gravity/ExternalGravity.h"
using namespace Physical::Gravity;
static void close(double a,double b) {
    if (std::abs(a-b)>8*std::numeric_limits<double>::epsilon()*std::abs(b))
        throw std::runtime_error("Core exact fixture source mapping mismatch");
}
int main() {
    // Core external_off_axis: r=[1,3], dz=2,rho=2,m_phi(r)=2r,g_phi=-1/40.
    // V,W,J and work all include full azimuth 2*pi.
    const double azimuth=2*std::acos(-1.), dt=1./10000.;
    ExternalRzStageIntegrals source{8*azimuth,(52./3)*azimuth,0,0,
                                   (-13./15)*azimuth,(-13./15)*azimuth};
    FluidVector delta{};
    add_external_rz_stage_integrals(source,dt,delta);
    close(delta.mom_w,-1./200000.);
    close(delta.eng,-13./1200000.);
    // W representative is 60/13, V mean is 13/3; wrong shortcut must differ.
    const double wrong=dt*(60./13)*(-1./40);
    if (std::abs(wrong-delta.eng)<1e-7)
        throw std::runtime_error("W/V counterexample was lost");
    if(delta.rho!=0 || delta.mom_u!=0 || delta.mom_v!=0)
        throw std::runtime_error("unrelated conservative components changed");
    struct UnknownGravity final : IGravityPolicy {
        void add_sources_on_patch(std::vector<FluidVector>&,const FluidState&,
                                  const Grid&,double,void* = nullptr) const override {}
    } unknown;
    if(unknown.source_contract().origin!=GravitySourceOrigin::Unknown)
        throw std::runtime_error("untyped policy default was granted authority");
    ExternalGravity external(0,0,-1./40);
    auto identity=external.source_contract();
    if(identity.origin!=GravitySourceOrigin::ExternalNativeOrthonormal
       || identity.chart!=GridMetrics::GeometrySemantics::Existing)
        throw std::runtime_error("external source identity is not explicit");
    // Declared external identity remains Existing chart: no public RZ release.
    FluidVector legacy{};
    FluidVector state{};state.rho=2;state.mom_w=60./13;
    add_external_gravity_source_cell(state,{0,0,-1./40,true},dt,legacy);
    close(legacy.eng,wrong); // old non-RZ arithmetic retained, not RZ expectation.
    std::cout<<"RZ_STAGE_INTEGRAL_LEAF_PASS physical_case=Core_external_off_axis "
                "producer=NOT_IMPLEMENTED public_gate=UNCHANGED\n";
}
