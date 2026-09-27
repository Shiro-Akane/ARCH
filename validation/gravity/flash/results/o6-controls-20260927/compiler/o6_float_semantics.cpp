#include <cmath>
#include <cstdlib>
#include <cstdio>
__attribute__((noinline)) double compensated(const double* a) {
    double s=0.,c=0.;
    for(int i=0;i<3;++i) {
        double t=s+a[i];
        if(std::abs(s)>=std::abs(a[i])) c+=(s-t)+a[i];
        else c+=(a[i]-t)+s;
        s=t;
    }
    return s+c;
}
int main(int argc,char**argv) {
    if(argc!=6)return 2;
    double a[]{std::strtod(argv[1],nullptr),std::strtod(argv[2],nullptr),std::strtod(argv[3],nullptr)};
    double bad=std::strtod(argv[4],nullptr), tiny=std::strtod(argv[5],nullptr);
    std::printf("sum=%.17g finite_nan=%d half_tiny=%.17g\n",compensated(a),int(std::isfinite(bad)),tiny*.5);
}
