/** @file test_poisson_multigrid.cpp
 * Independent analytic and discrete-Fourier references for P2.
 * Acceptance budgets were frozen in docs/development/P2PoissonMultigrid.zh-CN.md.
 */
#include "physics/gravity/UniformGravity.h"
#include "numerics/multigrid/MGTransfer.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace arch;
using elliptic::BoundaryKind;
using elliptic::CartesianMesh;
using multigrid::HostMultigrid;
constexpr double pi = constants::math::pi;
constexpr multigrid::SolveControl control{1e-12, 1e-13, 100};
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "malformed or unsupported input was accepted");
}
CartesianMesh mesh(int dim, int n, double length = 1.) {
    CartesianMesh m;
    m.dimension = dim;
    for (int a = 0; a < dim; ++a) { m.cells[a] = n; m.spacing[a] = length/n; }
    return m;
}
std::array<double,3> coordinate(const CartesianMesh& m, std::array<int,3> p, int face_axis = -1) {
    std::array<double,3> x{};
    for (int a = 0; a < m.dimension; ++a)
        x[a] = m.origin[a] + (p[a] + (a == face_axis ? 0. : 0.5)) * m.spacing[a];
    return x;
}
template<class F> elliptic::BoundaryData dirichlet(const CartesianMesh& m, F&& phi) {
    elliptic::BoundaryData b;
    b.kind = BoundaryKind::Dirichlet;
    for (int a = 0; a < m.dimension; ++a) for (int side = 0; side < 2; ++side) {
        auto f = m;
        f.cells[a] = 1;
        auto& v = b.values[2*a+side];
        v.resize(f.size());
        for (int i = 0; i < f.size(); ++i) {
            auto p = f.position(i);
            p[a] = side * m.cells[a];
            v[i] = phi(coordinate(m,p,a));
        }
    }
    return b;
}
grid::ConstScalarFieldView density_view(const CartesianMesh& m, const std::vector<double>& values) {
    grid::ConstScalarFieldView v;
    v.data = values.data(); v.size = values.size(); v.layout.dimension = m.dimension;
    for (int a = 0; a < 3; ++a) v.layout.extent[a] = v.layout.active_end[a] = m.cells[a];
    v.layout.stride = {1, static_cast<std::size_t>(m.cells[0]),
                         static_cast<std::size_t>(m.cells[0]*m.cells[1])};
    return v;
}
// This norm and residual do not call production operator, lift, or reduction helpers.
double reference_rms(const std::vector<double>& x) {
    long double sum = 0.;
    for (double v : x) sum += static_cast<long double>(v)*v;
    return static_cast<double>(std::sqrt(sum / x.size()));
}
void check_residual(const CartesianMesh& m, const elliptic::BoundaryData& boundary,
                    const std::vector<double>& rhs, const multigrid::SolveResult& result) {
    require(result.report.status == multigrid::SolveStatus::Converged, "MG did not converge");
    require(result.potential.size() == rhs.size(), "missing converged potential");
    require(result.report.residual <= result.report.target, "reported residual exceeds target");
    std::vector<double> r(rhs.size());
    double magnitude = 0., diagonal = 0.;
    for (double v : result.potential) magnitude = std::max(magnitude, std::abs(v));
    for (int a = 0; a < m.dimension; ++a) diagonal += 4./m.spacing[a]/m.spacing[a];
    for (int i = 0; i < m.size(); ++i) {
        const auto p = m.position(i);
        long double au = 0., b = rhs[i] - result.report.removed_rhs_mean;
        const long double u = result.potential[i];
        for (int a = 0; a < m.dimension; ++a) {
            const long double inv_h2 = 1.L / m.spacing[a] / m.spacing[a];
            if (boundary.kind == BoundaryKind::Dirichlet && (p[a] == 0 || p[a] == m.cells[a]-1)) {
                const int side = p[a] == 0 ? 0 : 1;
                auto q = p; q[a] += side == 0 ? 1 : -1;
                au += (4.L*u - (4.L/3.L)*result.potential[m.index(q)]) * inv_h2;
                b += (8.L/3.L)*boundary.values[2*a+side][m.face_index(p,a)] * inv_h2;
            } else {
                auto low = p, high = p;
                low[a] = (p[a]+m.cells[a]-1)%m.cells[a];
                high[a] = (p[a]+1)%m.cells[a];
                au += (2.L*u-result.potential[m.index(low)]-result.potential[m.index(high)])*inv_h2;
            }
        }
        r[i] = static_cast<double>(b-au);
    }
    const double norm = reference_rms(r);
    // Independent long-double arithmetic need not round like the evaluated stencil.
    const double rounding = 64.*std::numeric_limits<double>::epsilon()*diagonal*magnitude;
    require(norm <= result.report.target + rounding, "independent original residual exceeds target and arithmetic bound");
    require(std::abs(norm-result.report.residual) <= rounding, "residual report disagrees with independent evaluation");
}

void transfers() {
    for (int dim = 1; dim <= 3; ++dim) {
        const auto fine = mesh(dim,8), coarse = mesh(dim,4);
        std::vector<double> c(coarse.size(),-2.5), f(fine.size()), restricted(coarse.size());
        for (int i = 0; i < fine.size(); ++i) {
            f[i] = multigrid::prolong_cell(fine,coarse,BoundaryKind::Periodic,c.data(),i);
            require(f[i] == -2.5, "periodic constant prolongation");
        }
        for (int i = 0; i < coarse.size(); ++i)
            require(multigrid::restrict_cell(fine,coarse,f.data(),i) == -2.5, "constant restriction");
        for (int i = 0; i < fine.size(); ++i) f[i] = std::sin(0.37*i)-0.2;
        for (int i = 0; i < coarse.size(); ++i)
            restricted[i] = multigrid::restrict_cell(fine,coarse,f.data(),i);
        require(std::abs(elliptic::mean(f)-elliptic::mean(restricted)) < 1e-14, "restriction volume mean");
        for (int i = 0; i < coarse.size(); ++i) {
            auto x = coordinate(coarse,coarse.position(i));
            c[i] = -1.;
            for (int a = 0; a < dim; ++a) c[i] += (a+1)*x[a];
        }
        for (int i = 0; i < fine.size(); ++i) {
            const auto p = fine.position(i);
            bool interior = true;
            for (int a = 0; a < dim; ++a) interior &= p[a] > 0 && p[a] < fine.cells[a]-1;
            if (!interior) continue;
            auto x = coordinate(fine,p);
            double expected = -1.;
            for (int a = 0; a < dim; ++a) expected += (a+1)*x[a];
            require(std::abs(multigrid::prolong_cell(fine,coarse,BoundaryKind::Dirichlet,c.data(),i)-expected) < 1e-14,
                    "linear signed correction interpolation");
        }
        elliptic::project_mean(c);
        for (int i = 0; i < fine.size(); ++i)
            f[i] = multigrid::prolong_cell(fine,coarse,BoundaryKind::Periodic,c.data(),i);
        require(std::abs(elliptic::mean(f)) < 1e-14, "periodic prolongation zero mode");
    }
}
void polynomial_and_fourier() {
    for (int dim = 1; dim <= 3; ++dim) {
        const auto m = mesh(dim,8);
        for (int degree = 0; degree <= 2; ++degree) {
            const auto phi = [=](auto x) {
                double v = 0.3;
                for (int a = 0; a < dim; ++a) v += degree == 0 ? 0. : (degree == 1 ? x[a] : x[a]*x[a]);
                return v;
            };
            const auto b = dirichlet(m,phi);
            std::vector<double> rhs(m.size(), degree == 2 ? -2.*dim : 0.);
            HostMultigrid solver(m,b.kind);
            auto result = solver.solve(rhs,b,control);
            check_residual(m,b,rhs,result);
            for (int i = 0; i < m.size(); ++i)
                require(std::abs(result.potential[i]-phi(coordinate(m,m.position(i)))) < 1e-9, "polynomial potential");
            for (int a = 0; a < dim; ++a) {
                auto fm = m; ++fm.cells[a];
                for (int i = 0; i < fm.size(); ++i) {
                    const auto p = fm.position(i);
                    const auto x = coordinate(m,p,a);
                    const double expected = degree == 0 ? 0. : (degree == 1 ? 1. : 2.*x[a]);
                    const double gradient = elliptic::face_gradient(m,b.kind,result.potential.data(),a,p,phi(x));
                    require(std::abs(gradient-expected) < 1e-8, "polynomial boundary/interior gradient");
                }
            }
        }
        // Directly derived finite-difference eigenvalue, independent of production A.
        for (int n : {2,8,32}) {
            const auto periodic = mesh(dim,n);
            std::vector<double> rhs(periodic.size()), exact(rhs.size()), initial(rhs.size(),7.);
            const double lambda = 4.*std::pow(std::sin(pi/n)/periodic.spacing[0],2);
            for (int i = 0; i < periodic.size(); ++i) {
                const auto p = periodic.position(i);
                exact[i] = std::sin(2.*pi*(p[0]+0.5)/n);
                rhs[i] = lambda*exact[i];
            }
            HostMultigrid solver(periodic,BoundaryKind::Periodic);
            auto result = solver.solve(rhs,{},control,initial);
            check_residual(periodic,{},rhs,result);
            for (std::size_t i = 0; i < exact.size(); ++i)
                require(std::abs(result.potential[i]-exact[i]) < 1e-9, "discrete Fourier inverse");
            require(std::abs(elliptic::mean(result.potential)) < 1e-14, "potential gauge");
            auto warm = solver.solve(rhs,{},control,result.potential);
            require(warm.report.cycles == 0, "converged warm start changed");
            std::fill(rhs.begin(),rhs.end(),0.);
            auto zero = solver.solve(rhs,{},control,initial);
            require(zero.report.cycles == 0 && elliptic::rms(zero.potential) == 0., "zero RHS constant gauge");
            auto relaxed = solver.solve(rhs,{},control,exact);
            check_residual(periodic,{},rhs,relaxed);
        }
    }
    auto m = mesh(3,8); m.cells = {8,4,2}; m.spacing = {0.125,0.25,0.125};
    std::vector<double> rhs(m.size());
    for (int i = 0; i < m.size(); ++i) rhs[i] = std::sin(2*pi*coordinate(m,m.position(i))[0]);
    HostMultigrid rectangular(m,BoundaryKind::Periodic);
    require(rectangular.level_count() == 1, "rectangular 64-cell bottom");
    check_residual(m,{},rhs,rectangular.solve(rhs,{},control));
}

void failure_contracts() {
    const auto m = mesh(1,32);
    HostMultigrid solver(m,BoundaryKind::Periodic);
    std::vector<double> rhs(m.size(),1.);
    rejects([&] { solver.solve(rhs,{},control); });
    for (int i = 0; i < m.size(); ++i) rhs[i] = std::cos(2*pi*(i+0.5)/m.size());
    auto failed = solver.solve(rhs,{}, {1e-12,1e-13,1});
    require(failed.report.status == multigrid::SolveStatus::MaxCycles && failed.potential.empty(), "max cycles leaked solution");
    check_residual(m,{},rhs,solver.solve(rhs,{},control));
    rejects([&] { solver.solve(rhs,{}, {1e-12,0.,100}); });
    rejects([&] { solver.solve(rhs,{}, {1.,1e-13,100}); });
    rejects([&] { solver.solve(rhs,{}, {1e-12,1e-13,0}); });
    rejects([&] { solver.solve(std::span(rhs).first(3),{},control); });
    rhs[0] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { solver.solve(rhs,{},control); });
    std::fill(rhs.begin(),rhs.end(),0.);
    std::vector<double> initial(m.size(),std::numeric_limits<double>::max());
    initial[0] = -initial[0];
    failed = solver.solve(rhs,{},control,initial);
    require(failed.report.status == multigrid::SolveStatus::NumericalFailure && failed.potential.empty(), "overflow leaked solution");
    auto bad = m; bad.cells[0] = 7;
    rejects([&] { HostMultigrid x(bad,BoundaryKind::Periodic); });
    bad = m; bad.spacing[0] = 0.;
    rejects([&] { HostMultigrid x(bad,BoundaryKind::Periodic); });
    bad = mesh(2,8); bad.spacing[1] *= 4.;
    rejects([&] { HostMultigrid x(bad,BoundaryKind::Periodic); });
    bad = mesh(3,8); bad.cells[2] = 2;
    rejects([&] { HostMultigrid x(bad,BoundaryKind::Periodic); });
    bad = mesh(3,1<<20);
    rejects([&] { HostMultigrid x(bad,BoundaryKind::Periodic); });
    auto boundary = dirichlet(m,[](auto) { return 0.; });
    rejects([&] { solver.solve(rhs,boundary,control); });
    HostMultigrid nonperiodic(m,BoundaryKind::Dirichlet);
    boundary.values[0].clear();
    rejects([&] { nonperiodic.solve(rhs,boundary,control); });
    auto view = density_view(m,rhs);
    view.memory = grid::FieldMemory::Device;
    rejects([&] { gravity::solve_uniform_gravity(solver,view,{},control); });
    view = density_view(m,rhs); view.layout.stride[0] = std::numeric_limits<std::size_t>::max();
    rejects([&] { gravity::solve_uniform_gravity(solver,view,{},control); });
    view = density_view(m,rhs); rhs[0] = -1.;
    rejects([&] { gravity::solve_uniform_gravity(solver,view,{},control); });
    rhs[0] = std::numeric_limits<double>::infinity();
    rejects([&] { gravity::solve_uniform_gravity(solver,view,{},control); });
    rhs[0] = 0.;
    rejects([&] { gravity::solve_uniform_gravity(solver,view,{},control,0.); });
    for (int i = 0; i < m.size(); ++i) rhs[i] = 1.+0.25*std::cos(2*pi*(i+0.5)/m.size());
    auto gravity_failure = gravity::solve_uniform_gravity(solver,view,{}, {1e-12,1e-20,1});
    require(gravity_failure.report.status == multigrid::SolveStatus::MaxCycles &&
            gravity_failure.potential.empty() && gravity_failure.face_acceleration[0].empty(), "failed gravity published fields");
    boundary = dirichlet(m,[](auto) { return 0.; });
    std::fill(rhs.begin(),rhs.end(),std::numeric_limits<double>::denorm_min());
    gravity_failure = gravity::solve_uniform_gravity(nonperiodic,view,boundary,control);
    require(gravity_failure.report.status == multigrid::SolveStatus::NumericalFailure && gravity_failure.potential.empty(),
            "unrepresentable physical RHS was silently lost");
    require(elliptic::rms(std::vector<double>{1e-200,-1e-200}) == 1e-200, "norm underflow");
    require(elliptic::rms(std::vector<double>{1e200,-1e200}) == 1e200, "norm overflow");
}

void small_density_contrast() {
    const auto m = mesh(1,32);
    constexpr double G = constants::gravity::cgs::gravitational_constant;
    constexpr double contrast = 1e-10;
    for (double rho : {1.,1e-100}) {
        std::vector<double> density(m.size());
        for (int i = 0; i < m.size(); ++i)
            density[i] = rho*(1.+contrast*std::cos(2*pi*(i+0.5)/m.size()+0.17));
        HostMultigrid solver(m,BoundaryKind::Periodic);
        const double amplitude = 4*pi*G*rho*contrast;
        const auto r = gravity::solve_uniform_gravity(solver,density_view(m,density),{},
                                                     {1e-12,amplitude*1e-13,100});
        require(r.report.status == multigrid::SolveStatus::Converged, "small density contrast lost periodic compatibility");
        for (int i = 0; i < m.size(); ++i)
            require(std::abs(r.potential[i]*(4*pi*pi)/amplitude+std::cos(2*pi*(i+0.5)/m.size()+0.17)) < 0.01,
                    "small density contrast changed analytic solution");
    }
}

struct Analytic {
    int dim;
    bool periodic;
    double phi(std::array<double,3> x) const {
        double value = 0., cross = 0.1;
        for (int a = 0; a < dim; ++a) {
            value += (a+1)*(periodic ? std::cos(2*pi*x[a]) : x[a]*x[a]*x[a]+std::sin(pi*x[a])+0.4*x[a]+0.2);
            cross *= 1.+x[a];
        }
        return value + (periodic ? 0. : cross);
    }
    double rhs(std::array<double,3> x) const {
        double value = 0.;
        for (int a = 0; a < dim; ++a)
            value += (a+1)*(periodic ? 4*pi*pi*std::cos(2*pi*x[a]) : pi*pi*std::sin(pi*x[a])-6*x[a]);
        return value;
    }
    double gradient(std::array<double,3> x, int axis) const {
        double cross = 0.1;
        for (int a = 0; a < dim; ++a) if (a != axis) cross *= 1.+x[a];
        return (axis+1)*(periodic ? -2*pi*std::sin(2*pi*x[axis]) : 3*x[axis]*x[axis]+pi*std::cos(pi*x[axis])+0.4)
            + (periodic ? 0. : cross);
    }
};
void convergence() {
    std::cout << "boundary,dimension,n,cycles,residual,target,phi_rms,cell_force_rms,face_force_rms,boundary_force_rms,phi_order,cell_order,face_order,boundary_order\n";
    for (bool periodic : {true,false}) for (int dim = 1; dim <= 3; ++dim) {
        const Analytic exact{dim,periodic};
        std::array<double,4> previous{};
        for (int n : {16,32,64}) {
            const auto m = mesh(dim,n);
            auto b = periodic ? elliptic::BoundaryData{} : dirichlet(m,[&](auto x) { return exact.phi(x); });
            std::vector<double> rhs(m.size()), error_phi, error_cell, error_face, error_boundary;
            for (int i = 0; i < m.size(); ++i) rhs[i] = exact.rhs(coordinate(m,m.position(i)));
            HostMultigrid solver(m,b.kind);
            auto result = solver.solve(rhs,b,control);
            check_residual(m,b,rhs,result);
            for (int i = 0; i < m.size(); ++i)
                error_phi.push_back(result.potential[i]-exact.phi(coordinate(m,m.position(i))));
            for (int a = 0; a < dim; ++a) {
                auto fm = m; ++fm.cells[a];
                std::vector<double> gradient(fm.size());
                for (int i = 0; i < fm.size(); ++i) {
                    const auto p = fm.position(i);
                    const auto x = coordinate(m,p,a);
                    gradient[i] = elliptic::face_gradient(m,b.kind,result.potential.data(),a,p,exact.phi(x));
                    error_face.push_back(gradient[i]-exact.gradient(x,a));
                    if (p[a] == 0 || p[a] == m.cells[a]) error_boundary.push_back(error_face.back());
                }
                for (int i = 0; i < m.size(); ++i) {
                    const auto p = m.position(i); auto high = p; ++high[a];
                    error_cell.push_back(0.5*(gradient[fm.index(p)]+gradient[fm.index(high)])-exact.gradient(coordinate(m,p),a));
                }
            }
            // Periodic cosine has exactly zero normal derivative on domain faces:
            // its endpoint roundoff has no meaningful refinement order.
            const std::array<double,4> errors{reference_rms(error_phi),reference_rms(error_cell),
                                              reference_rms(error_face),reference_rms(error_boundary)};
            std::array<double,4> order{};
            for (int a = 0; a < (periodic ? 3 : 4); ++a) if (previous[a] != 0.) {
                order[a] = std::log2(previous[a]/errors[a]);
                require(order[a] >= 1.8, "potential/force order below frozen 1.8 budget");
            }
            previous = errors;
            std::cout << (periodic ? "periodic" : "dirichlet") << ',' << dim << ',' << n << ',' << result.report.cycles
                << ',' << result.report.residual << ',' << result.report.target;
            for (double x : errors) std::cout << ',' << x;
            for (double x : order) std::cout << ',' << x;
            std::cout << '\n';
        }
    }
}
void physical_gravity() {
    constexpr double G = constants::gravity::cgs::gravitational_constant;
    std::vector<double> normalized_reference;
    std::cout << "rho0,length,cycles,relative_phi_error,relative_force_error,removed_density_mean\n";
    for (double rho0 : {1.,1e-30,1e-100}) for (double length : {1.,1e6}) {
        const auto m = mesh(1,32,length);
        const double k = 2.*pi/length, rhs_amplitude = 4.*pi*G*0.25*rho0;
        const double phi_amplitude = rhs_amplitude/(k*k), force_amplitude = rhs_amplitude/k;
        // Padded borrowed storage: invalid ghosts are deliberately never sampled.
        std::vector<double> density(m.size()+4,std::numeric_limits<double>::quiet_NaN());
        for (int i = 0; i < m.size(); ++i) density[i+2] = rho0*(1.+0.25*std::cos(k*coordinate(m,m.position(i))[0]));
        auto view = density_view(m,density);
        view.layout.extent[0] += 4; view.layout.active_begin[0] = 2; view.layout.active_end[0] += 2;
        HostMultigrid solver(m,BoundaryKind::Periodic);
        auto result = gravity::solve_uniform_gravity(solver,view,{}, {1e-12,rhs_amplitude*1e-13,100});
        require(result.report.status == multigrid::SolveStatus::Converged, "CGS gravity failed");
        require(std::abs(result.removed_density_mean/rho0-1.) < 1e-14, "physical source mean");
        std::vector<double> errors_phi, errors_g, normalized;
        for (int i = 0; i < m.size(); ++i) {
            const double phase = k*coordinate(m,m.position(i))[0];
            normalized.push_back(result.potential[i]/phi_amplitude);
            errors_phi.push_back(normalized.back()+std::cos(phase));
            errors_g.push_back(result.cell_acceleration[0][i]/force_amplitude+std::sin(phase));
        }
        for (int i = 0; i <= m.size(); ++i)
            errors_g.push_back(result.face_acceleration[0][i]/force_amplitude+std::sin(2*pi*i/m.size()));
        const double ep = reference_rms(errors_phi)*std::sqrt(2.), eg = reference_rms(errors_g)*std::sqrt(2.);
        require(ep < 0.01 && eg < 0.01, "32-cell analytic CGS potential/force exceeded 1 percent");
        if (normalized_reference.empty()) normalized_reference = normalized;
        for (std::size_t i = 0; i < normalized.size(); ++i)
            require(std::abs(normalized[i]-normalized_reference[i]) < 1e-10, "CGS density/length scaling changed normalized result");
        std::cout << rho0 << ',' << length << ',' << result.report.cycles << ',' << ep << ',' << eg << ',' << result.removed_density_mean << '\n';
    }
    // Constant physical density in a prescribed quadratic potential; this is not isolated gravity.
    for (int dim = 1; dim <= 3; ++dim) {
        const auto m = mesh(dim,8);
        const double rho = 0.7, coefficient = 2.*pi*G*rho/dim;
        auto phi = [=](auto x) { double v = 0.; for (int a = 0; a < dim; ++a) v += coefficient*x[a]*x[a]; return v; };
        auto b = dirichlet(m,phi);
        std::vector<double> density(m.size(),rho);
        HostMultigrid solver(m,b.kind);
        auto r = gravity::solve_uniform_gravity(solver,density_view(m,density),b,{1e-12,4*pi*G*rho*1e-13,100});
        require(r.report.status == multigrid::SolveStatus::Converged && r.removed_density_mean == 0., "constant density Dirichlet solve");
        for (int i = 0; i < m.size(); ++i) {
            auto x = coordinate(m,m.position(i));
            require(std::abs((r.potential[i]-phi(x))/coefficient) < 1e-9, "constant density potential sign/scale");
            for (int a = 0; a < dim; ++a)
                require(std::abs(r.cell_acceleration[a][i]/coefficient+2.*x[a]) < 1e-8, "constant density gravity sign/scale");
        }
    }
}
}
int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(17);
        const std::string mode = argc == 2 ? argv[1] : "all";
        require(mode == "all" || mode == "contract" || mode == "analytic", "unknown test mode");
        if (mode != "analytic") { transfers(); polynomial_and_fourier(); failure_contracts(); small_density_contrast(); }
        if (mode != "contract") { convergence(); physical_gravity(); }
        std::cout << "P2 " << mode << " validation passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
