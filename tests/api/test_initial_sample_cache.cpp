#include "api/InitialSampleCache.h"
#include <cmath>
#include <stdexcept>
using arch::api::InitialSampleCache;
void require(bool value) { if(!value) throw std::runtime_error("exact initial state cache failed"); }
int main() {
    InitialSampleCache cache;
    PrimitiveData p{}; p.rho=1; p.p=1; p.mass_fractions={.3,.7};
    int calls=0;
    const auto run=[&] { return cache.evaluate(p,[&] { ++calls; return InitialSampleCache::Row{double(calls)}; }); };
    require(run()[0]==1 && run()[0]==1 && calls==1);
    for (double* field : {&p.rho,&p.p,&p.temperature,&p.u,&p.v,&p.w,&p.mass_fractions[0],&p.mass_fractions[1]}) {
        const auto before=calls;
        *field=std::nextafter(*field,100.0);
        run(); require(calls==before+1); // Even a one-ULP input change is distinct.
    }
    auto before=calls;
    p.has_temperature=true; run(); require(calls==before+1);
    p.u=0.; run(); before=calls;
    p.u=-0.; run(); require(calls==before+1);
    before=calls;
    p.mass_fractions.push_back(0); run(); require(calls==before+1);
    p.p=42;
    bool failed=false;
    try { cache.evaluate(p,[]()->InitialSampleCache::Row { throw std::runtime_error("conversion failure"); }); }
    catch(...) { failed=true; }
    require(failed); before=calls; run(); require(calls==before+1);
    for (int i=0;i<2000;++i) { p.p=1000+i; run(); }
    p.p=4000; run(); before=calls; run(); require(calls==before+1); // Full cache falls back to full conversion.
    InitialSampleCache fresh;
    before=calls; fresh.evaluate(p,[&] { ++calls; return InitialSampleCache::Row{}; });
    require(calls==before+1); // No result crosses requests.
}
